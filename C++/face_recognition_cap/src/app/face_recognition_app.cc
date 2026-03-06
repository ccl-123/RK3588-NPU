/**
 * @file face_recognition_app.cc
 * @brief 人脸识别应用主类实现(支持回调)
 * @author CL
 * @date 2025-11-20
 */

#include "app/face_recognition_app.h"
#include "core/yolov8_face.h"
#include "core/facenet.h"
#include "core/postprocess.h"
#include "hardware/camera_util.h"
#include "database/database_manager.h"
#include "service/attendance_service.h"
#include <spdlog/spdlog.h>
#include <array>
#include <sys/time.h>
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

FaceRecognitionApp::FaceRecognitionApp()
    : preprocess_thread_(nullptr)
    , postprocess_thread_(nullptr)
    , recognition_thread_(nullptr)
    , perf_monitor_(10)
    , recognition_callback_(nullptr)
    , frame_callback_(nullptr)
    , registration_callback_(nullptr)
    , attendance_service_(nullptr)
    , scale_w_(0)
    , scale_h_(0)
    , resize_w_(0)
    , resize_h_(0)
    , padding_(0)
    , initialized_(false)
    , running_(false)
    , camera_initialized_(false)
    , camera_error_("")
    , models_loaded_(false)
{
}

FaceRecognitionApp::~FaceRecognitionApp() {
    cleanup();
}

int FaceRecognitionApp::initialize(const AppConfig& config) {
    if (initialized_) {
        spdlog::error("App already initialized");
        return -1;
    }

    config_ = config;

    // 1. 初始化模型
    spdlog::info("Initializing models...");
    if (model_manager_.init_face_detector(config_.retinaface_model_path.c_str()) != 0) {
        spdlog::error("Failed to initialize YOLOv8-face model");
        return -1;
    }

    if (model_manager_.init_facenet(config_.facenet_model_path.c_str()) != 0) {
        spdlog::error("Failed to initialize FaceNet model");
        return -1;
    }
    // 初始化性能监控（先配置上报周期，再绑定 NPU 上下文避免数据被覆盖）
    perf_monitor_ = PerformanceMonitor(config_.perf_report_interval);
    // 设置 NPU 内存查询上下文
    perf_monitor_.set_npu_contexts(
        *model_manager_.get_face_detector_ctx(),
        *model_manager_.get_facenet_ctx());

    // 2. 加载特征库
    spdlog::info("Loading feature library...");
    int feature_count = 0;

    if (config_.use_database) {
        // 从数据库加载
        auto* db_manager = &db::DatabaseManager::instance();
        if (!db_manager->initialize(config_.database_path)) {
            spdlog::error("Failed to initialize database");
            return -1;
        }

        feature_count = feature_library_.load_from_database(db_manager, FACENET_FEATURE_DIM);
    } else {
        // 从文件系统加载
        feature_count = feature_library_.load_from_directory(config_.feature_lib_path, FACENET_FEATURE_DIM);
    }

    if (feature_count < 0) {
        spdlog::error("Failed to load feature library");
        return -1;
    }

    if (feature_count == 0) {
        spdlog::warn("No features loaded, system will work but cannot recognize anyone");
    } else {
        spdlog::info("Loaded {} features", feature_count);
    }

    // 3. 初始化摄像头（失败不影响应用启动）
    spdlog::info("Initializing camera...");
    if (init_camera() != 0) {
        spdlog::warn("Camera initialization failed: {}", camera_error_);
        spdlog::warn("Application will start without camera, you can configure it in settings");
        camera_initialized_ = false;
        // 不返回失败，继续初始化其他组件
    } else {
        camera_initialized_ = true;
        camera_error_.clear();
        spdlog::info("Camera initialized successfully");
    }

    // 4. 计算缩放参数
    int detector_width, detector_height, detector_channel;
    model_manager_.get_face_detector_size(detector_width, detector_height, detector_channel);

    if (config_.camera_width > config_.camera_height) {
        scale_w_ = (float)detector_width / config_.camera_width;
        scale_h_ = scale_w_;
        resize_w_ = detector_width;
        resize_h_ = (int)(resize_w_ * config_.camera_height / config_.camera_width);
        padding_ = resize_w_ - resize_h_;
    } else {
        scale_h_ = (float)detector_height / config_.camera_height;
        scale_w_ = scale_h_;
        resize_h_ = detector_height;
        resize_w_ = (int)(resize_h_ * config_.camera_width / config_.camera_height);
        padding_ = resize_h_ - resize_w_;
    }

    // 5. 初始化人脸对齐目标点 (针对w600k_mbf.rknn 112x112 输入)
    // 注意：必须先创建 Mat，再复制数据，避免使用局部变量指针
    dst_landmark_ = cv::Mat(5, 2, CV_32FC1);
    float dst_landmark_data[5][2] = {
        {38.2946f, 51.6963f},
        {73.5318f, 51.6963f},
        {56.0252f, 71.7366f},
        {41.5493f, 92.3655f},
        {70.7299f, 92.3655f}
    };
    memcpy(dst_landmark_.data, dst_landmark_data, 2 * 5 * sizeof(float));

    // 6. 创建并启动线程（流水线架构）- 使用智能指针
    spdlog::info("Starting pipeline threads...");

    // 线程1: 采集 + RGA预处理（只有在摄像头初始化成功时才创建和启动）
    if (camera_initialized_) {
        preprocess_thread_ = std::make_unique<PreprocessingThread>(
            resize_w_, resize_h_,
            config_.camera_width, config_.camera_height,
            &perf_monitor_,
            config_.camera_type);
        preprocess_thread_->start();
        spdlog::info("Preprocessing thread started");
    } else {
        spdlog::warn("Preprocessing thread not started (camera not initialized)");
    }

    // 线程3: 识别 + 渲染
    recognition_thread_ = std::make_unique<RecognitionThread>(
        &model_manager_, &feature_library_,
        dst_landmark_, config_.facenet_threshold,
        &perf_monitor_);

    // 线程2.5: YOLO后处理
    postprocess_thread_ = std::make_unique<PostprocessThread>(
        &model_manager_, recognition_thread_.get(), &perf_monitor_,
        config_.box_conf_threshold, config_.nms_threshold);

    postprocess_thread_->start();
    recognition_thread_->start();

    initialized_ = true;
    models_loaded_ = true;
    spdlog::info("App initialized successfully");
    spdlog::info("Post process config: box_conf_threshold = {:.2f}, nms_threshold = {:.2f}",
                 config_.box_conf_threshold, config_.nms_threshold);

    return 0;
}

double FaceRecognitionApp::get_camera_fps() const {
    return ::get_camera_fps();  // 调用 camera_util.h 中的全局函数
}

/**
 * @brief 内部初始化摄像头函数
 * @return 0 成功, -1 失败
 * @note 仅支持 USB 摄像头。如果初始化失败，会将错误信息保存到 camera_error_。
 */
int FaceRecognitionApp::init_camera() {
    int ret = 0;
    camera_error_.clear();

    if (config_.camera_type == "usb") {
        ret = load_usb_camera(config_.device_number,
                               config_.camera_width, config_.camera_height);
        if (ret == EXIT_SUCCESS) {
            start_usb_capture_thread(); // 启动异步采集线程
            spdlog::info("USB camera async mode enabled");
        } else {
            camera_error_ = "Failed to open USB camera /dev/video" + config_.device_number +
                           ". Please check device connection or select correct device in settings.";
        }
    } else {
        camera_error_ = "Unsupported camera type: " + config_.camera_type;
        spdlog::error("{}", camera_error_);
        return -1;
    }

    return (ret == EXIT_SUCCESS) ? 0 : -1;
}

int FaceRecognitionApp::run() {
    if (!initialized_) {
        spdlog::error("App not initialized");
        return -1;
    }

    // 检查摄像头状态
    if (!camera_initialized_) {
        spdlog::error("Cannot run: camera not initialized");
        spdlog::error("Camera error: {}", camera_error_);
        return -1;
    }

    if (!preprocess_thread_) {
        spdlog::error("Cannot run: preprocessing thread not created");
        return -1;
    }

    // 检查模型是否已加载
    if (!models_loaded_) {
        spdlog::error("Cannot run: models not loaded. Call reload_models() first.");
        return -1;
    }

    running_.store(true, std::memory_order_release);
    spdlog::info("Starting pipeline mode...");
    spdlog::info("  Thread 1: Camera + RGA preprocess");
    spdlog::info("  Thread 2: YOLO detection (main loop)");
    spdlog::info("  Thread 3: FaceNet + Match + Render");

    struct timeval t_start, t_detect_end;
    int detector_width, detector_height, detector_channel;
    model_manager_.get_face_detector_size(detector_width, detector_height, detector_channel);

    while (running_.load(std::memory_order_acquire)) {
        // 1. 从预处理线程获取结果（采集+RGA已在线程1完成）
        PreprocessTask task;
        if (!preprocess_thread_->get_result(task)) {
            continue;
        }

        gettimeofday(&t_start, NULL);

        // 2. 人脸检测（仅NPU推理）
        std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM> yolo_outputs;
        YoloRunTimings yolo_timing;
        int ret = yolov8_face_run(
            model_manager_.get_face_detector_ctx(),
            task.processed_img,
            detector_width,
            detector_height,
            detector_channel,
            task.processed_img.cols,
            task.processed_img.rows,
            model_manager_.get_face_detector_io_num(),
            model_manager_.get_face_detector_inputs(),
            model_manager_.get_face_detector_outputs(),
            model_manager_.get_face_detector_output_attrs(),
            yolo_outputs,
            &yolo_timing
        );
        
        gettimeofday(&t_detect_end, NULL);
        float detect_time = (get_us(t_detect_end) - get_us(t_start)) / 1000.0;

        if (ret < 0) {
            spdlog::warn("YOLO inference failed, ret={}", ret);
            continue;
        }
        
        // 3. 性能统计
        perf_monitor_.update_fps(1000.0 / detect_time);
        perf_monitor_.record_detection_time(detect_time);
        perf_monitor_.record_detection_inputs_time(yolo_timing.inputs_set_ms);
        perf_monitor_.record_detection_run_time(yolo_timing.run_ms);
        perf_monitor_.record_detection_outputs_time(yolo_timing.outputs_get_ms);
        perf_monitor_.record_detection_copy_time(yolo_timing.copy_ms);
        perf_monitor_.record_alignment_time(recognition_thread_->get_avg_align_time());
        perf_monitor_.record_recognition_time(recognition_thread_->get_avg_facenet_time());
        perf_monitor_.record_matching_time(recognition_thread_->get_avg_match_time());

        // 4. 提交到后处理线程
        PostprocessTask pp_task;
        pp_task.orig_img = std::move(task.orig_img);
        pp_task.yolo_outputs = std::move(yolo_outputs);
        // 将模型坐标映射回原始相机坐标所需的尺度（考虑resize后再padding）
        pp_task.img_width = static_cast<int>(
            (static_cast<float>(detector_width) * config_.camera_width) / resize_w_);
        pp_task.img_height = static_cast<int>(
            (static_cast<float>(detector_height) * config_.camera_height) / resize_h_);
        pp_task.timestamp = task.timestamp;
        pp_task.current_fps = perf_monitor_.get_smoothed_fps();
        pp_task.frame_time = detect_time;
        postprocess_thread_->submit_task(std::move(pp_task));

        // 5. 打印性能报告
        if (perf_monitor_.should_print_report()) {
            perf_monitor_.print_report();
        }
    }

    return 0;
}

void FaceRecognitionApp::detect_faces(const cv::Mat& img, detect_result_group_t& result_group) {
    cv::Mat padded_img = img.clone();
    int detector_width, detector_height, detector_channel;
    model_manager_.get_face_detector_size(detector_width, detector_height, detector_channel);

    int img_width = static_cast<int>(
        (static_cast<float>(detector_width) * config_.camera_width) / resize_w_);
    int img_height = static_cast<int>(
        (static_cast<float>(detector_height) * config_.camera_height) / resize_h_);

    // 添加padding到正方形
    if (img.cols > img.rows) {
        int pad = img.cols - img.rows;
        cv::copyMakeBorder(padded_img, padded_img, 0, pad, 0, 0,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    } else if (img.rows > img.cols) {
        int pad = img.rows - img.cols;
        cv::copyMakeBorder(padded_img, padded_img, 0, 0, 0, pad,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    }

    std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM> yolo_outputs;
    int ret = yolov8_face_run(
        model_manager_.get_face_detector_ctx(),
        padded_img,
        detector_width,
        detector_height,
        detector_channel,
        img_width,
        img_height,
        model_manager_.get_face_detector_io_num(),
        model_manager_.get_face_detector_inputs(),
        model_manager_.get_face_detector_outputs(),
        model_manager_.get_face_detector_output_attrs(),
        yolo_outputs
    );

    if (ret != 0) {
        memset(&result_group, 0, sizeof(detect_result_group_t));
        return;
    }

    yolov8_face_postprocess(
        yolo_outputs,
        model_manager_.get_face_detector_output_attrs(),
        YOLOV8_FACE_OUTPUT_NUM,
        detector_height,
        detector_width,
        img_width,
        img_height,
        config_.box_conf_threshold,
        config_.nms_threshold,
        &result_group
    );
}

void FaceRecognitionApp::stop() {
    running_.store(false, std::memory_order_release);
    // 唤醒阻塞在 get_result() 的主循环，使其检查 running_ 后退出
    if (preprocess_thread_) {
        preprocess_thread_->wake_consumer();
    }
}

void FaceRecognitionApp::cleanup() {
    if (!initialized_) {
        return;
    }

    spdlog::info("Cleaning up...");

    // 停止线程 - 智能指针自动管理内存
    if (preprocess_thread_) {
        preprocess_thread_->stop();
        preprocess_thread_.reset();
    }

    if (postprocess_thread_) {
        postprocess_thread_->stop();
        postprocess_thread_.reset();
    }

    if (recognition_thread_) {
        recognition_thread_->stop();
        recognition_thread_.reset();
    }

    // 关闭摄像头（只有在摄像头已初始化时才关闭）
    if (camera_initialized_) {
        if (config_.camera_type == "usb") {
            close_usb_camera();
        }
        camera_initialized_ = false;
    }

    // 释放模型
    model_manager_.release();

    // 清空特征库
    feature_library_.clear();

    initialized_ = false;
    models_loaded_ = false;
    spdlog::info("Cleanup complete");
}

// ==================== NPU 资源管理接口 ====================
// 
// RK3588 NPU 资源共享说明：
//   - RKNN (人脸检测/识别) 和 RKLLM (语言模型) 共用同一个 NPU
//   - 两者不能同时高效运行，必须完全互斥使用
//   - 进入智能看板页面时：释放 RKNN → 加载 RKLLM
//   - 回到实时识别页面时：释放 RKLLM → 加载 RKNN
//
// 关键点：
//   1. 必须先停止所有使用 NPU 的工作线程
//   2. 然后调用 rknn_destroy / rkllm_destroy 释放模型
//   3. 等待 NPU 驱动完全释放资源（约 500ms）
//   4. 才能加载另一个模型
// =========================================================

bool FaceRecognitionApp::release_models() {
    if (!initialized_) {
        spdlog::warn("Cannot release models: app not initialized");
        return false;
    }

    if (running_.load(std::memory_order_acquire)) {
        spdlog::error("Cannot release models while app is running. Please stop first.");
        return false;
    }

    if (!models_loaded_) {
        spdlog::info("Models already released");
        return true;
    }

    spdlog::info("Releasing RKNN models to free NPU resources for LLM...");

    // 【关键】先停止所有 NPU 工作线程
    // 原因：这些线程持有 model_manager_ 引用，会阻止 NPU 资源完全释放
    // 如果不停止这些线程，RKLLM 推理会因资源抢占而极慢（5分钟 vs 5秒）
    if (postprocess_thread_) {
        postprocess_thread_->stop();
        postprocess_thread_.reset();
        spdlog::info("Postprocess thread stopped");
    }

    if (recognition_thread_) {
        recognition_thread_->stop();
        recognition_thread_.reset();
        spdlog::info("Recognition thread stopped");
    }

    // 释放 RKNN 模型（调用 rknn_destroy）
    model_manager_.release();
    models_loaded_ = false;

    // 【关键】等待 NPU 驱动完全释放资源
    // 原因：rknn_destroy 是异步的，如果立即加载 RKLLM 可能导致资源冲突崩溃
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    spdlog::info("RKNN models released, NPU resources are now available for LLM");
    return true;
}

bool FaceRecognitionApp::reload_models() {
    if (!initialized_) {
        spdlog::warn("Cannot reload models: app not initialized");
        return false;
    }

    if (running_.load(std::memory_order_acquire)) {
        spdlog::error("Cannot reload models while app is running.");
        return false;
    }

    if (models_loaded_) {
        spdlog::info("Models already loaded");
        return true;
    }

    spdlog::info("Reloading RKNN models...");

    // 重新初始化人脸检测模型
    if (model_manager_.init_face_detector(config_.retinaface_model_path.c_str()) != 0) {
        spdlog::error("Failed to reload YOLOv8-face model");
        return false;
    }

    // 重新初始化 FaceNet 模型
    if (model_manager_.init_facenet(config_.facenet_model_path.c_str()) != 0) {
        spdlog::error("Failed to reload FaceNet model");
        return false;
    }

    // 更新性能监控的 NPU 上下文（用于统计 NPU 内存使用）
    perf_monitor_.set_npu_contexts(
        *model_manager_.get_face_detector_ctx(),
        *model_manager_.get_facenet_ctx());

    // 【关键】重新创建并启动工作线程
    // 这些线程在 release_models() 时被销毁，需要重新创建
    if (!recognition_thread_) {
        recognition_thread_ = std::make_unique<RecognitionThread>(
            &model_manager_, &feature_library_,
            dst_landmark_, config_.facenet_threshold,
            &perf_monitor_);
        recognition_thread_->start();
        spdlog::info("Recognition thread recreated");
        if (recognition_callback_) {
            set_recognition_callback(recognition_callback_);
        }
        if (frame_callback_) {
            set_frame_callback(frame_callback_);
        }
        if (registration_callback_) {
            set_registration_callback(registration_callback_);
        }
    }

    if (!postprocess_thread_) {
        postprocess_thread_ = std::make_unique<PostprocessThread>(
            &model_manager_, recognition_thread_.get(), &perf_monitor_,
            config_.box_conf_threshold, config_.nms_threshold);
        postprocess_thread_->start();
        spdlog::info("Postprocess thread recreated");
    }

    models_loaded_ = true;
    spdlog::info("RKNN models reloaded successfully");
    return true;
}

/**
 * @brief 重新初始化摄像头 (支持热切换)
 * @param device_number 新的摄像头设备编号 (例如 "1" 对应 /dev/video1)
 * @return true 初始化成功, false 失败
 * @warning 此函数非线程安全，必须在应用停止运行 (running_ == false) 时调用。
 *          如果在运行状态下调用，会直接返回失败。
 * @details 
 * 1. 停止并销毁预处理线程
 * 2. 关闭当前打开的摄像头
 * 3. 尝试初始化新摄像头
 * 4. 重新创建预处理线程 (但不自动启动应用主循环)
 */
bool FaceRecognitionApp::reinitialize_camera(const std::string& device_number) {
    if (!initialized_) {
        spdlog::error("Cannot reinitialize camera: app not initialized");
        return false;
    }

    if (running_.load(std::memory_order_acquire)) {
        spdlog::error("Cannot reinitialize camera while app is running. Please stop the app first.");
        return false;
    }

    spdlog::info("Reinitializing camera with device: /dev/video{}", device_number);

    // 1. 停止预处理线程（stop() 内部会 join()，确保线程完全退出）
    if (preprocess_thread_) {
        preprocess_thread_->stop();
        preprocess_thread_.reset();
    }

    // 2. 关闭旧摄像头
    if (camera_initialized_) {
        if (config_.camera_type == "usb") {
            close_usb_camera();
        }
        camera_initialized_ = false;
    }

    // 3. 更新设备号
    config_.device_number = device_number;

    // 4. 尝试初始化新摄像头
    if (init_camera() != 0) {
        spdlog::error("Failed to reinitialize camera: {}", camera_error_);
        camera_initialized_ = false;
        return false;
    }

    camera_initialized_ = true;
    camera_error_.clear();
    spdlog::info("Camera reinitialized successfully");

    // 5. 创建并启动新的预处理线程
    preprocess_thread_ = std::make_unique<PreprocessingThread>(
        resize_w_, resize_h_,
        config_.camera_width, config_.camera_height,
        &perf_monitor_,
        config_.camera_type);
    preprocess_thread_->start();

    return true;
}

bool FaceRecognitionApp::pause_camera() {
    if (!initialized_) {
        spdlog::warn("Cannot pause camera: app not initialized");
        return false;
    }

    if (running_.load(std::memory_order_acquire)) {
        spdlog::error("Cannot pause camera while app is running. Please stop the app first.");
        return false;
    }

    if (!camera_initialized_ && !preprocess_thread_) {
        spdlog::info("Camera already paused");
        return true;
    }

    if (preprocess_thread_) {
        preprocess_thread_->stop();
        preprocess_thread_.reset();
        spdlog::info("Preprocessing thread stopped");
    }

    if (camera_initialized_) {
        if (config_.camera_type == "usb") {
            close_usb_camera();
        }
        camera_initialized_ = false;
        spdlog::info("Camera paused");
    }

    return true;
}

bool FaceRecognitionApp::resume_camera() {
    if (!initialized_) {
        spdlog::warn("Cannot resume camera: app not initialized");
        return false;
    }

    if (running_.load(std::memory_order_acquire)) {
        spdlog::error("Cannot resume camera while app is running. Please stop the app first.");
        return false;
    }

    if (camera_initialized_) {
        if (!preprocess_thread_) {
            preprocess_thread_ = std::make_unique<PreprocessingThread>(
                resize_w_, resize_h_,
                config_.camera_width, config_.camera_height,
                &perf_monitor_,
                config_.camera_type);
            preprocess_thread_->start();
            spdlog::info("Preprocessing thread restarted");
        }
        return true;
    }

    spdlog::info("Resuming camera...");
    if (init_camera() != 0) {
        spdlog::warn("Camera resume failed: {}", camera_error_);
        camera_initialized_ = false;
        return false;
    }

    camera_initialized_ = true;
    camera_error_.clear();
    spdlog::info("Camera resumed successfully");

    preprocess_thread_ = std::make_unique<PreprocessingThread>(
        resize_w_, resize_h_,
        config_.camera_width, config_.camera_height,
        &perf_monitor_,
        config_.camera_type);
    preprocess_thread_->start();
    spdlog::info("Preprocessing thread restarted");

    return true;
}

void FaceRecognitionApp::set_recognition_callback(RecognitionCallback callback) {
    recognition_callback_ = callback;
    
    // 转发到识别线程（多线程模式）
    if (recognition_thread_ && callback) {
        recognition_thread_->set_recognition_callback(
            [callback](const RecognitionResultData& data) {
                // 转换 RecognitionResultData 到 RecognitionResult
                RecognitionResult result;
                result.user_id = data.user_id;
                result.user_name = data.user_name;
                result.similarity = data.similarity;
                result.face_image = data.face_image;
                result.face_box = data.face_box;
                result.timestamp = data.timestamp;
                callback(result);
            }
        );
    }
}

void FaceRecognitionApp::set_frame_callback(FrameCallback callback) {
    frame_callback_ = callback;
    
    // 转发到识别线程（多线程模式）
    if (recognition_thread_ && callback) {
        recognition_thread_->set_frame_callback(
            [callback](const cv::Mat& frame, const std::vector<RecognitionResultData>& data_results) {
                // 转换 RecognitionResultData 到 RecognitionResult
                std::vector<RecognitionResult> results;
                for (const auto& data : data_results) {
                    RecognitionResult result;
                    result.user_id = data.user_id;
                    result.user_name = data.user_name;
                    result.similarity = data.similarity;
                    // GUI 渲染/显示不需要 face_image，避免跨线程回调额外持有图像引用导致内存/CPU 开销增大
                    result.face_box = data.face_box;
                    result.timestamp = data.timestamp;
                    results.push_back(result);
                }
                callback(frame, results);
            }
        );
    }
}

void FaceRecognitionApp::set_registration_callback(RegistrationCallback callback) {
    registration_callback_ = std::move(callback);
    if (recognition_thread_) {
        recognition_thread_->set_registration_callback(registration_callback_);
    }
}

void FaceRecognitionApp::set_recognition_mode(RecognitionMode mode) {
    if (recognition_thread_) {
        recognition_thread_->set_mode(mode);
    }
}

bool FaceRecognitionApp::get_current_frame(cv::Mat& frame) {
    if (!initialized_) {
        return false;
    }

    // 检查摄像头是否已初始化
    if (!camera_initialized_) {
        return false;
    }

    // 从摄像头读取一帧（使用全局函数）
    cv::Mat orig_img;
    bool ret = false;
    if (config_.camera_type == "usb") {
        ret = read_usb_frame(&orig_img, &current_frame_sequence_cursor_);
    } else {
        // MIPI support removed
        return false;
    }

    if (!ret || orig_img.empty()) {
        return false;
    }

    // 翻转图像
    cv::flip(orig_img, frame, 1);

    return true;
}

// ==================== GUI 人脸注册接口（Public） ====================
// 以下函数专为 GUI 人脸注册功能设计，返回友好的数据结构
// 不用于实时识别线程

int FaceRecognitionApp::detect_faces(const cv::Mat& frame,
                                     std::vector<cv::Rect>& face_boxes,
                                     std::vector<std::vector<cv::Point2f>>& landmarks) {
    // 输入：原始帧（如 1280x720）
    // 输出：人脸框和关键点（坐标已转换回原始帧坐标系）
    // 用途：GUI 人脸注册时的人脸检测

    if (!initialized_) {
        return 0;
    }

    face_boxes.clear();
    landmarks.clear();

    // 调整图像大小用于检测
    cv::Mat resized_img;
    cv::resize(frame, resized_img, cv::Size(resize_w_, resize_h_));

    // 添加 padding 使其成为正方形（与旧版本一致）
    cv::Mat padded_img = resized_img.clone();
    if (resize_w_ >= resize_h_) {
        cv::copyMakeBorder(padded_img, padded_img, 0, padding_, 0, 0,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    } else {
        cv::copyMakeBorder(padded_img, padded_img, 0, 0, 0, padding_,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    }
    // 使用 padding 后的实际尺寸做坐标还原，避免重复缩放导致框偏移
    int img_width = padded_img.cols;
    int img_height = padded_img.rows;

    // YOLOv8-face 检测
    detect_result_group_t detect_result_group;
    memset(&detect_result_group, 0, sizeof(detect_result_group_t));

    int detector_width, detector_height, detector_channel;
    model_manager_.get_face_detector_size(detector_width, detector_height, detector_channel);

    std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM> yolo_outputs;
    int ret = yolov8_face_run(
        model_manager_.get_face_detector_ctx(),
        padded_img,
        detector_width,
        detector_height,
        detector_channel,
        img_width,
        img_height,
        model_manager_.get_face_detector_io_num(),
        model_manager_.get_face_detector_inputs(),
        model_manager_.get_face_detector_outputs(),
        model_manager_.get_face_detector_output_attrs(),
        yolo_outputs
    );

    if (ret != 0) {
        return 0;
    }

    yolov8_face_postprocess(
        yolo_outputs,
        model_manager_.get_face_detector_output_attrs(),
        YOLOV8_FACE_OUTPUT_NUM,
        detector_height,
        detector_width,
        img_width,
        img_height,
        config_.box_conf_threshold,
        config_.nms_threshold,
        &detect_result_group
    );

    // 转换结果（从 padded 图像坐标转换回原始帧坐标）
    float scale_w = static_cast<float>(frame.cols) / resize_w_;
    float scale_h = static_cast<float>(frame.rows) / resize_h_;

    for (int i = 0; i < detect_result_group.count; i++) {
        detect_result_t* det_result = &(detect_result_group.results[i]);

        // 人脸框
        int x1 = static_cast<int>(det_result->box.left * scale_w);
        int y1 = static_cast<int>(det_result->box.top * scale_h);
        int x2 = static_cast<int>(det_result->box.right * scale_w);
        int y2 = static_cast<int>(det_result->box.bottom * scale_h);

        face_boxes.push_back(cv::Rect(x1, y1, x2 - x1, y2 - y1));

        // 关键点
        std::vector<cv::Point2f> face_landmarks;
        KEY_POINT* kp = &(det_result->point);
        face_landmarks.push_back(cv::Point2f(kp->point_1_x * scale_w, kp->point_1_y * scale_h));
        face_landmarks.push_back(cv::Point2f(kp->point_2_x * scale_w, kp->point_2_y * scale_h));
        face_landmarks.push_back(cv::Point2f(kp->point_3_x * scale_w, kp->point_3_y * scale_h));
        face_landmarks.push_back(cv::Point2f(kp->point_4_x * scale_w, kp->point_4_y * scale_h));
        face_landmarks.push_back(cv::Point2f(kp->point_5_x * scale_w, kp->point_5_y * scale_h));

        landmarks.push_back(face_landmarks);
    }

    return detect_result_group.count;
}

bool FaceRecognitionApp::extract_face_feature(const cv::Mat& aligned_face,
                                              std::vector<float>& feature) {
    // 输入：对齐后的人脸图像（112x112，RGB 格式）
    // 输出：特征向量（512 维）
    // 用途：GUI 人脸注册时的特征提取

    if (!initialized_) {
        return false;
    }

    // 检查输入图像大小
    if (aligned_face.cols != 112 || aligned_face.rows != 112) {
        spdlog::error("Aligned face must be 112x112, got {}x{}",
                     aligned_face.cols, aligned_face.rows);
        return false;
    }

    // FaceNet 推理
    float* feature_ptr = nullptr;
    int ret = facenet_inference(
        model_manager_.get_facenet_ctx(),
        aligned_face,
        model_manager_.get_facenet_io_num(),
        model_manager_.get_facenet_inputs(),
        model_manager_.get_facenet_outputs(),
        &feature_ptr
    );

    if (ret != 0 || feature_ptr == nullptr) {
        spdlog::error("FaceNet inference failed");
        return false;
    }

    // 复制特征
    feature.assign(feature_ptr, feature_ptr + 512);

    // 释放资源
    facenet_output_release(
        model_manager_.get_facenet_ctx(),
        model_manager_.get_facenet_io_num(),
        model_manager_.get_facenet_outputs()
    );

    return true;
}

bool FaceRecognitionApp::extract_feature_from_frame(const cv::Mat& frame,
                                                   std::vector<float>& feature,
                                                   cv::Rect* face_box) {
    // 输入：原始帧（如 1280x720）
    // 输出：特征向量（512 维）+ 可选的人脸框
    // 用途：GUI 人脸注册的一站式接口（检测 + 对齐 + 提取）
    // 注意：使用与实时识别相同的对齐方法（similarTransform + warpPerspective）
    //       确保注册特征与实时识别特征一致

    if (!initialized_) {
        return false;
    }

    // 检测人脸
    std::vector<cv::Rect> face_boxes;
    std::vector<std::vector<cv::Point2f>> landmarks;

    int face_count = detect_faces(frame, face_boxes, landmarks);

    if (face_count == 0) {
        spdlog::warn("No face detected in frame");
        return false;
    }

    if (face_count > 1) {
        spdlog::warn("Multiple faces detected ({}), using the first one", face_count);
    }

    // 使用第一个人脸
    if (face_box != nullptr) {
        *face_box = face_boxes[0];
    }

    // 人脸对齐（使用与实时识别相同的方法）
    std::vector<cv::Point2f>& src_landmark = landmarks[0];

    // 将 vector<Point2f> 转换为 Mat（5x2）
    float landmark_data[5][2];
    for (int i = 0; i < 5; i++) {
        landmark_data[i][0] = src_landmark[i].x;
        landmark_data[i][1] = src_landmark[i].y;
    }
    cv::Mat src(5, 2, CV_32FC1, landmark_data);
    memcpy(src.data, landmark_data, 2 * 5 * sizeof(float));

    // 使用与实时识别相同的 similarTransform + warpPerspective
    cv::Mat M = similarTransform(src, dst_landmark_);
    cv::Mat aligned_face;

    int facenet_width, facenet_height, facenet_channel;
    model_manager_.get_facenet_size(facenet_width, facenet_height, facenet_channel);

    cv::warpPerspective(frame, aligned_face, M, cv::Size(facenet_width, facenet_height));

    // 转换为 RGB（FaceNet 模型需要 RGB 输入）
    cv::cvtColor(aligned_face, aligned_face, cv::COLOR_BGR2RGB);

    // 提取特征
    return extract_face_feature(aligned_face, feature);
}

bool FaceRecognitionApp::process_single_frame(cv::Mat& frame, std::vector<RecognitionResult>& results) {
    // 用途：GUI 单帧处理接口（用于人脸注册预览等）
    // 注意：使用实时识别的核心函数（private 版本），确保与实时识别一致
    // 原因：实时识别使用 similarTransform + warpPerspective 对齐，经过充分测试
    //       GUI 注册接口使用 estimateAffinePartial2D + warpAffine 对齐
    //       为了保证识别准确性，这里使用与实时识别相同的对齐方法

    if (!initialized_) {
        return false;
    }

    results.clear();

    // 1. 获取当前帧
    cv::Mat orig_img;
    if (!get_current_frame(orig_img)) {
        return false;
    }

    // 2. 预处理
    cv::Mat resized_img;
    cv::resize(orig_img, resized_img, cv::Size(resize_w_, resize_h_));

    // 3. 人脸检测（使用实时识别的 private 版本）
    //    注意：不使用 public 版本的 detect_faces，因为对齐方法不同
    detect_result_group_t detect_result_group;
    detect_faces(resized_img, detect_result_group);

    // 4. 人脸识别和绘制
    frame = orig_img.clone();

    int facenet_width, facenet_height, facenet_channel;
    model_manager_.get_facenet_size(facenet_width, facenet_height, facenet_channel);

    for (int i = 0; i < detect_result_group.count; i++) {
        detect_result_t* det_result = &(detect_result_group.results[i]);

        // 人脸对齐（使用旧版本的方法）
        float landmark[5][2] = {
            {(float)det_result->point.point_1_x, (float)det_result->point.point_1_y},
            {(float)det_result->point.point_2_x, (float)det_result->point.point_2_y},
            {(float)det_result->point.point_3_x, (float)det_result->point.point_3_y},
            {(float)det_result->point.point_4_x, (float)det_result->point.point_4_y},
            {(float)det_result->point.point_5_x, (float)det_result->point.point_5_y}
        };

        cv::Mat src(5, 2, CV_32FC1, landmark);
        memcpy(src.data, landmark, 2 * 5 * sizeof(float));

        cv::Mat M = similarTransform(src, dst_landmark_);
        cv::Mat aligned_face;
        cv::warpPerspective(orig_img, aligned_face, M, cv::Size(facenet_width, facenet_height));

        // 转换为 RGB（FaceNet 模型需要 RGB 输入）
        cv::cvtColor(aligned_face, aligned_face, cv::COLOR_BGR2RGB);

        // 特征提取
        float* feature_ptr = nullptr;
        int ret = facenet_inference(
            model_manager_.get_facenet_ctx(),
            aligned_face,
            model_manager_.get_facenet_io_num(),
            model_manager_.get_facenet_inputs(),
            model_manager_.get_facenet_outputs(),
            &feature_ptr
        );

        if (ret != 0 || feature_ptr == nullptr) {
            spdlog::error("FaceNet inference failed for face {}", i);
            continue;
        }

        // 复制特征到 vector
        std::vector<float> feature(feature_ptr, feature_ptr + 512);

        // 特征匹配
        int matched_user_id = -1;
        std::string matched_name = "Unknown";
        float max_similarity = 0.0f;

        feature_library_.match_feature_with_id(feature.data(), config_.facenet_threshold,
                                              matched_user_id, matched_name, max_similarity);

        // 调试日志：显示匹配结果（包括低于阈值的）
        if (max_similarity > 0.0f) {
            spdlog::debug("Face detected - Best match: {} (ID: {}, similarity: {:.3f}, threshold: {:.2f})",
                         matched_name, matched_user_id, max_similarity, config_.facenet_threshold);
        }

        // 获取人脸框坐标
        int x1 = det_result->box.left;
        int y1 = det_result->box.top;
        int x2 = det_result->box.right;
        int y2 = det_result->box.bottom;

        // 创建识别结果
        RecognitionResult result;
        result.user_id = matched_user_id;
        result.user_name = matched_name;
        result.similarity = max_similarity;
        result.face_box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
        result.face_image = aligned_face.clone();
        result.timestamp = std::chrono::system_clock::now();
        results.push_back(result);

        // 绘制人脸框和识别结果
        cv::Scalar color = (max_similarity >= config_.facenet_threshold) ?
                          cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);

        cv::rectangle(frame, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 注意：如果是 GUI 模式，名称文字由 Qt 层的 VideoDisplayWidget 使用 QPainter 绘制
        // OpenCV 的 Hershey 字体不支持中文，会显示为问号
        // 命令行模式下仍使用 cv::putText（仅英文名或 ID 可正常显示）
        if (!frame_callback_) {
            char text[256];
            if (max_similarity >= config_.facenet_threshold) {
                snprintf(text, sizeof(text), "%s (%.2f)", matched_name.c_str(), max_similarity);
            } else {
                snprintf(text, sizeof(text), "Unknown (%.2f)", max_similarity);
            }
            cv::putText(frame, text, cv::Point(x1, y1 - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
        }

        // 释放 FaceNet 输出资源
        facenet_output_release(
            model_manager_.get_facenet_ctx(),
            model_manager_.get_facenet_io_num(),
            model_manager_.get_facenet_outputs()
        );

        // 触发回调
        if (recognition_callback_ && max_similarity >= config_.facenet_threshold) {
            recognition_callback_(result);
        }
    }

    return true;
}

void FaceRecognitionApp::set_attendance_service(service::AttendanceService* service) {
    attendance_service_ = service;
}

void FaceRecognitionApp::set_recognition_threshold(float threshold) {
    config_.facenet_threshold = threshold;
    
    // 同步到识别线程（多线程模式）
    if (recognition_thread_) {
        recognition_thread_->set_threshold(threshold);
    }
    
    spdlog::info("Recognition threshold updated to: {:.2f}", threshold);
}

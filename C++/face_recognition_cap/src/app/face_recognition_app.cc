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

FaceRecognitionApp::FaceRecognitionApp()
    : preprocess_thread_(nullptr)
    , recognition_thread_(nullptr)
    , perf_monitor_(10)
    , scale_w_(0)
    , scale_h_(0)
    , resize_w_(0)
    , resize_h_(0)
    , padding_(0)
    , initialized_(false)
    , running_(false)
    , recognition_callback_(nullptr)
    , frame_callback_(nullptr)
    , attendance_service_(nullptr)
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

    // 3. 初始化摄像头
    spdlog::info("Initializing camera...");
    if (init_camera() != 0) {
        spdlog::error("Failed to initialize camera");
        return -1;
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

    // 5. 初始化人脸对齐目标点
    // 注意：必须先创建 Mat，再复制数据，避免使用局部变量指针
    dst_landmark_ = cv::Mat(5, 2, CV_32FC1);
    float dst_landmark_data[5][2] = {
        {54.7065, 73.8519},
        {105.0454, 73.5734},
        {80.036, 102.4808},
        {59.3561, 131.9507},
        {89.6141, 131.7201}
    };
    memcpy(dst_landmark_.data, dst_landmark_data, 2 * 5 * sizeof(float));

    // 6. 创建并启动线程（流水线架构）- 使用智能指针
    spdlog::info("Starting pipeline threads...");

    // 线程1: 采集 + RGA预处理
    preprocess_thread_ = std::make_unique<PreprocessingThread>(
        resize_w_, resize_h_,
        config_.camera_width, config_.camera_height,
        &perf_monitor_,
        config_.camera_type, config_.use_async_usb);

    // 线程3: 识别 + 渲染
    recognition_thread_ = std::make_unique<RecognitionThread>(
        &model_manager_, &feature_library_,
        dst_landmark_, config_.facenet_threshold);

    // 线程2.5: YOLO后处理
    postprocess_thread_ = std::make_unique<PostprocessThread>(
        &model_manager_, recognition_thread_.get(), &perf_monitor_,
        config_.box_conf_threshold, config_.nms_threshold);

    preprocess_thread_->start();
    postprocess_thread_->start();
    recognition_thread_->start();

    // 7. 初始化性能监控
    perf_monitor_ = PerformanceMonitor(config_.perf_report_interval);

    initialized_ = true;
    spdlog::info("App initialized successfully");
    spdlog::info("Post process config: box_conf_threshold = {:.2f}, nms_threshold = {:.2f}",
                 config_.box_conf_threshold, config_.nms_threshold);

    return 0;
}

int FaceRecognitionApp::init_camera() {
    int ret = 0;

    if (config_.camera_type == "usb") {
        if (config_.use_async_usb) {
            ret = load_usb_camera_async(config_.device_number,
                                       config_.camera_width, config_.camera_height);
            if (ret == EXIT_SUCCESS) {
                start_usb_capture_thread();
                spdlog::info("USB camera async mode enabled");
            }
        } else {
            ret = load_usb_camera(config_.device_number,
                                 config_.camera_width, config_.camera_height);
        }
    } else if (config_.camera_type == "mipi") {
        ret = load_mipi_camera(config_.device_number,
                              config_.camera_width, config_.camera_height);
    } else {
        spdlog::error("Unsupported camera type: {}", config_.camera_type);
        return -1;
    }

    return (ret == EXIT_SUCCESS) ? 0 : -1;
}

int FaceRecognitionApp::run() {
    if (!initialized_) {
        spdlog::error("App not initialized");
        return -1;
    }

    running_ = true;
    spdlog::info("Starting pipeline mode...");
    spdlog::info("  Thread 1: Camera + RGA preprocess");
    spdlog::info("  Thread 2: YOLO detection (main loop)");
    spdlog::info("  Thread 3: FaceNet + Match + Render");

    struct timeval t_start, t_detect_end;
    int detector_width, detector_height, detector_channel;
    model_manager_.get_face_detector_size(detector_width, detector_height, detector_channel);

    while (running_) {
        // 1. 从预处理线程获取结果（采集+RGA已在线程1完成）
        PreprocessTask task;
        if (!preprocess_thread_->get_result(task)) {
            continue;
        }

        gettimeofday(&t_start, NULL);

        // 2. 人脸检测（仅NPU推理）
        std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM> yolo_outputs;
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
            yolo_outputs
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
    running_ = false;
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

    // 关闭摄像头
    if (config_.camera_type == "usb") {
        if (config_.use_async_usb) {
            close_usb_camera_async();
        } else {
            close_usb_camera();
        }
    } else if (config_.camera_type == "mipi") {
        close_mipi_camera();
    }

    // 释放模型
    model_manager_.release();

    // 清空特征库
    feature_library_.clear();

    initialized_ = false;
    spdlog::info("Cleanup complete");
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
                    result.face_image = data.face_image;
                    result.face_box = data.face_box;
                    result.timestamp = data.timestamp;
                    results.push_back(result);
                }
                callback(frame, results);
            }
        );
    }
}

bool FaceRecognitionApp::get_current_frame(cv::Mat& frame) {
    if (!initialized_) {
        return false;
    }

    // 从摄像头读取一帧（使用全局函数）
    cv::Mat orig_img;
    if (config_.camera_type == "usb") {
        if (config_.use_async_usb) {
            read_usb_frame_async(&orig_img);
        } else {
            read_usb_frame(&orig_img);
        }
    } else {
        read_mipi_frame(&orig_img);
    }

    if (orig_img.empty()) {
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
    int img_width = config_.camera_width;
    int img_height = config_.camera_height;

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

        // 显示姓名和置信度（在检测框上方）
        char text[256];
        if (max_similarity >= config_.facenet_threshold) {
            snprintf(text, sizeof(text), "%s (%.2f)", matched_name.c_str(), max_similarity);
        } else {
            snprintf(text, sizeof(text), "Unknown (%.2f)", max_similarity);
        }

        // 显示文本在检测框上方
        cv::putText(frame, text, cv::Point(x1, y1 - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

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

/**
 * @file face_recognition_app.cc
 * @brief 人脸识别应用主类实现(支持回调)
 * @author CL
 * @date 2025-11-20
 */

#include "app/face_recognition_app.h"
#include "core/retinaface.h"
#include "core/facenet.h"
#include "core/postprocess.h"
#include "hardware/camera_util.h"
#include "database/database_manager.h"
#include <spdlog/spdlog.h>
#include <sys/time.h>
#include <iostream>
#include <cstring>

FaceRecognitionApp::FaceRecognitionApp()
    : preprocess_thread_(nullptr)
    , render_thread_(nullptr)
    , perf_monitor_(10)
    , scale_w_(0)
    , scale_h_(0)
    , resize_w_(0)
    , resize_h_(0)
    , padding_(0)
    , initialized_(false)
    , running_(false)
    , recognition_callback_(nullptr)
{
}

FaceRecognitionApp::~FaceRecognitionApp() {
    cleanup();
}

int FaceRecognitionApp::initialize(const AppConfig& config) {
    if (initialized_) {
        std::cerr << "App already initialized" << std::endl;
        return -1;
    }

    config_ = config;

    // 1. 初始化模型
    std::cout << "Initializing models..." << std::endl;
    if (model_manager_.init_retinaface(config_.retinaface_model_path.c_str()) != 0) {
        std::cerr << "Failed to initialize RetinaFace model" << std::endl;
        return -1;
    }

    if (model_manager_.init_facenet(config_.facenet_model_path.c_str()) != 0) {
        std::cerr << "Failed to initialize FaceNet model" << std::endl;
        return -1;
    }

    // 2. 加载特征库
    std::cout << "Loading feature library..." << std::endl;
    int feature_count = 0;

    if (config_.use_database) {
        // 从数据库加载
        auto db_manager = db::DatabaseManager::instance();
        if (!db_manager->initialize(config_.database_path)) {
            std::cerr << "Failed to initialize database" << std::endl;
            return -1;
        }

        feature_count = feature_library_.load_from_database(db_manager, FACENET_FEATURE_DIM);
    } else {
        // 从文件系统加载
        feature_count = feature_library_.load_from_directory(config_.feature_lib_path, FACENET_FEATURE_DIM);
    }

    if (feature_count < 0) {
        std::cerr << "Failed to load feature library" << std::endl;
        return -1;
    }

    if (feature_count == 0) {
        std::cout << "Warning: No features loaded, system will work but cannot recognize anyone" << std::endl;
    } else {
        std::cout << "Loaded " << feature_count << " features" << std::endl;
    }

    // 3. 初始化摄像头
    std::cout << "Initializing camera..." << std::endl;
    if (init_camera() != 0) {
        std::cerr << "Failed to initialize camera" << std::endl;
        return -1;
    }

    // 4. 计算缩放参数
    int retinaface_width, retinaface_height, retinaface_channel;
    model_manager_.get_retinaface_size(retinaface_width, retinaface_height, retinaface_channel);

    if (config_.camera_width > config_.camera_height) {
        scale_w_ = (float)retinaface_width / config_.camera_width;
        scale_h_ = scale_w_;
        resize_w_ = retinaface_width;
        resize_h_ = (int)(resize_w_ * config_.camera_height / config_.camera_width);
        padding_ = resize_w_ - resize_h_;
    } else {
        scale_h_ = (float)retinaface_height / config_.camera_height;
        scale_w_ = scale_h_;
        resize_h_ = retinaface_height;
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

    // 6. 创建并启动线程
    std::cout << "Starting threads..." << std::endl;
    preprocess_thread_ = new PreprocessingThread(resize_w_, resize_h_, 
                                                 config_.camera_width, config_.camera_height);
    render_thread_ = new RenderingThread("Image Window");
    
    preprocess_thread_->start();
    render_thread_->start();

    // 7. 初始化性能监控
    perf_monitor_ = PerformanceMonitor(config_.perf_report_interval);

    initialized_ = true;
    std::cout << "App initialized successfully" << std::endl;
    std::cout << "post process config: box_conf_threshold = " << config_.box_conf_threshold 
              << ", nms_threshold = " << config_.nms_threshold << std::endl;

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
                std::cout << "USB camera async mode enabled" << std::endl;
            }
        } else {
            ret = load_usb_camera(config_.device_number, 
                                 config_.camera_width, config_.camera_height);
        }
    } else if (config_.camera_type == "mipi") {
        ret = load_mipi_camera(config_.device_number, 
                              config_.camera_width, config_.camera_height);
    } else {
        std::cerr << "Unsupported camera type: " << config_.camera_type << std::endl;
        return -1;
    }

    return (ret == EXIT_SUCCESS) ? 0 : -1;
}

int FaceRecognitionApp::run() {
    if (!initialized_) {
        std::cerr << "App not initialized" << std::endl;
        return -1;
    }

    running_ = true;
    std::cout << "Starting main loop..." << std::endl;

    cv::Mat orig_img;
    struct timeval start_time, stop_time;
    struct timeval t1, t2, t3, t4, t5;

    while (running_) {
        gettimeofday(&start_time, NULL);

        // 1. 摄像头读取
        gettimeofday(&t1, NULL);
        if (config_.camera_type == "usb") {
            if (config_.use_async_usb) {
                read_usb_frame_async(&orig_img);
            } else {
                read_usb_frame(&orig_img);
            }
        } else if (config_.camera_type == "mipi") {
            read_mipi_frame(&orig_img);
        }
        gettimeofday(&t2, NULL);

        // 2. 提交预处理任务
        gettimeofday(&t3, NULL);
        preprocess_thread_->submit_task(orig_img, t3);

        // 3. 获取预处理结果
        PreprocessTask processed_task;
        if (!preprocess_thread_->get_result(processed_task)) {
            continue;  // 没有预处理好的图像，跳过本帧
        }

        cv::Mat img = processed_task.processed_img;
        orig_img = processed_task.orig_img;  // 使用翻转后的原图
        gettimeofday(&t4, NULL);

        // 4. 人脸检测
        detect_result_group_t detect_result_group;
        detect_faces(img, detect_result_group);
        gettimeofday(&t5, NULL);

        // 5. 人脸识别和匹配
        cv::Mat render_img = orig_img.clone();
        recognize_and_match(orig_img, detect_result_group, render_img);

        // 6. 性能统计
        gettimeofday(&stop_time, NULL);
        float current_frame_time = (get_us(stop_time) - get_us(start_time)) / 1000.0;
        float current_fps = 1000.0 / current_frame_time;

        perf_monitor_.update_fps(current_fps);
        perf_monitor_.record_camera_time((get_us(t2) - get_us(t1)) / 1000);
        perf_monitor_.record_preprocess_time((get_us(t4) - get_us(t3)) / 1000);
        perf_monitor_.record_detection_time((get_us(t5) - get_us(t4)) / 1000);

        // 7. 渲染
        char fps_text[64];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.1f (%.1f ms)",
                 perf_monitor_.get_smoothed_fps(), current_frame_time);

        cv::putText(render_img, fps_text, cv::Point(10, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        cv::putText(render_img, "3-Thread Optimized", cv::Point(10, 60),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

        struct timeval t_render_start, t_render_end;
        gettimeofday(&t_render_start, NULL);
        render_thread_->submit_task(render_img, fps_text);
        gettimeofday(&t_render_end, NULL);
        perf_monitor_.record_render_time((get_us(t_render_end) - get_us(t_render_start)) / 1000);

        // 8. 打印性能报告
        if (perf_monitor_.should_print_report()) {
            perf_monitor_.print_report();
        }
    }

    return 0;
}

// ==================== 实时识别核心函数（Private） ====================
// 以下函数用于实时识别线程，使用 similarTransform + warpPerspective 对齐
// 经过充分测试，稳定可靠，不要轻易修改

void FaceRecognitionApp::detect_faces(const cv::Mat& img, detect_result_group_t& result_group) {
    // 输入：已缩放到 resize_w_ x resize_h_ 的图像
    // 输出：RKNN 原始格式的检测结果
    cv::Mat padded_img = img.clone();
    int img_width, img_height;

    // 添加padding
    if (config_.camera_width > config_.camera_height) {
        cv::copyMakeBorder(padded_img, padded_img, 0, padding_, 0, 0,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        img_width = config_.camera_width;
        img_height = config_.camera_width;
    } else {
        cv::copyMakeBorder(padded_img, padded_img, 0, 0, 0, padding_,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        img_width = config_.camera_height;
        img_height = config_.camera_height;
    }

    // 获取模型参数
    int retinaface_width, retinaface_height, retinaface_channel;
    model_manager_.get_retinaface_size(retinaface_width, retinaface_height, retinaface_channel);

    // 执行推理
    retinaface_inference(
        model_manager_.get_retinaface_ctx(),
        padded_img,
        retinaface_width,
        retinaface_height,
        retinaface_channel,
        config_.box_conf_threshold,
        config_.nms_threshold,
        img_width,
        img_height,
        model_manager_.get_retinaface_io_num(),
        model_manager_.get_retinaface_inputs(),
        model_manager_.get_retinaface_outputs(),
        model_manager_.get_retinaface_out_scales(),
        model_manager_.get_retinaface_out_zps(),
        &result_group
    );
}

void FaceRecognitionApp::recognize_and_match(const cv::Mat& orig_img,
                                             const detect_result_group_t& result_group,
                                             cv::Mat& render_img) {
    // 输入：原始图像（未缩放）+ 检测结果
    // 输出：绘制了人脸框和识别结果的图像
    // 功能：人脸对齐（similarTransform + warpPerspective）、特征提取、匹配、绘制
    struct timeval t_align_start, t_align_end;
    struct timeval t_facenet_start, t_facenet_end;
    struct timeval t_match_start, t_match_end;
    float total_align_time = 0;
    float total_facenet_time = 0;
    float total_match_time = 0;

    int facenet_width, facenet_height, facenet_channel;
    model_manager_.get_facenet_size(facenet_width, facenet_height, facenet_channel);

    for (int i = 0; i < result_group.count; i++) {
        // 人脸对齐
        gettimeofday(&t_align_start, NULL);

        float landmark[5][2] = {
            {(float)result_group.results[i].point.point_1_x, (float)result_group.results[i].point.point_1_y},
            {(float)result_group.results[i].point.point_2_x, (float)result_group.results[i].point.point_2_y},
            {(float)result_group.results[i].point.point_3_x, (float)result_group.results[i].point.point_3_y},
            {(float)result_group.results[i].point.point_4_x, (float)result_group.results[i].point.point_4_y},
            {(float)result_group.results[i].point.point_5_x, (float)result_group.results[i].point.point_5_y}
        };

        cv::Mat src(5, 2, CV_32FC1, landmark);
        memcpy(src.data, landmark, 2 * 5 * sizeof(float));

        cv::Mat M = similarTransform(src, dst_landmark_);
        cv::Mat warp;
        cv::warpPerspective(orig_img, warp, M, cv::Size(facenet_width, facenet_height));
        cv::cvtColor(warp, warp, cv::COLOR_BGR2RGB);

        gettimeofday(&t_align_end, NULL);

        // FaceNet 特征提取
        gettimeofday(&t_facenet_start, NULL);
        float* facenet_result = nullptr;
        facenet_inference(
            model_manager_.get_facenet_ctx(),
            warp,
            model_manager_.get_facenet_io_num(),
            model_manager_.get_facenet_inputs(),
            model_manager_.get_facenet_outputs(),
            &facenet_result
        );
        gettimeofday(&t_facenet_end, NULL);

        // 特征匹配
        gettimeofday(&t_match_start, NULL);
        std::string name;
        float max_score;
        int user_id = 0;
        feature_library_.match_feature_with_id(facenet_result, config_.facenet_threshold,
                                               user_id, name, max_score);
        gettimeofday(&t_match_end, NULL);

        // 触发回调(新增)
        if (recognition_callback_ && name != "stranger") {
            RecognitionResult result;
            result.user_id = user_id;
            result.user_name = name;
            result.similarity = max_score;
            result.timestamp = std::chrono::system_clock::now();

            // 提取人脸图像
            int x1 = result_group.results[i].box.left;
            int y1 = result_group.results[i].box.top;
            int x2 = result_group.results[i].box.right;
            int y2 = result_group.results[i].box.bottom;

            // 确保坐标在图像范围内
            x1 = std::max(0, x1);
            y1 = std::max(0, y1);
            x2 = std::min(orig_img.cols, x2);
            y2 = std::min(orig_img.rows, y2);

            if (x2 > x1 && y2 > y1) {
                result.face_image = orig_img(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
                result.face_box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
            }

            recognition_callback_(result);
        }

        // 释放输出
        facenet_output_release(
            model_manager_.get_facenet_ctx(),
            model_manager_.get_facenet_io_num(),
            model_manager_.get_facenet_outputs()
        );

        // 绘制结果
        int x1 = result_group.results[i].box.left;
        int y1 = result_group.results[i].box.top;
        int x2 = result_group.results[i].box.right;
        int y2 = result_group.results[i].box.bottom;

        cv::rectangle(render_img, cv::Point(x1, y1), cv::Point(x2, y2),
                     cv::Scalar(255, 0, 0, 255), 2);

        // 显示姓名和置信度
        char label[128];
        snprintf(label, sizeof(label), "%s (%.2f)", name.c_str(), max_score);
        cv::putText(render_img, label, cv::Point(x1, y1 - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);

        // 累计时间
        total_align_time += (get_us(t_align_end) - get_us(t_align_start)) / 1000;
        total_facenet_time += (get_us(t_facenet_end) - get_us(t_facenet_start)) / 1000;
        total_match_time += (get_us(t_match_end) - get_us(t_match_start)) / 1000;
    }

    // 记录性能数据
    perf_monitor_.record_alignment_time(total_align_time);
    perf_monitor_.record_recognition_time(total_facenet_time);
    perf_monitor_.record_matching_time(total_match_time);
}

void FaceRecognitionApp::stop() {
    running_ = false;
}

void FaceRecognitionApp::cleanup() {
    if (!initialized_) {
        return;
    }

    std::cout << "Cleaning up..." << std::endl;

    // 停止线程
    if (preprocess_thread_) {
        preprocess_thread_->stop();
        delete preprocess_thread_;
        preprocess_thread_ = nullptr;
    }

    if (render_thread_) {
        render_thread_->stop();
        delete render_thread_;
        render_thread_ = nullptr;
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
    std::cout << "Cleanup complete" << std::endl;
}

void FaceRecognitionApp::set_recognition_callback(RecognitionCallback callback) {
    recognition_callback_ = callback;
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

    // 添加 padding 使其成为正方形（与旧版本 detect_faces 一致）
    cv::Mat padded_img = resized_img.clone();
    int img_width, img_height;

    if (config_.camera_width > config_.camera_height) {
        // 1280x720 -> 640x360，需要在底部添加 padding 到 640x640
        cv::copyMakeBorder(padded_img, padded_img, 0, padding_, 0, 0,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        img_width = resize_w_;
        img_height = resize_w_;  // 640x640
    } else {
        // 竖屏模式，在右侧添加 padding
        cv::copyMakeBorder(padded_img, padded_img, 0, 0, 0, padding_,
                          cv::BorderTypes::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        img_width = resize_h_;
        img_height = resize_h_;
    }

    // RetinaFace 检测
    detect_result_group_t detect_result_group;
    memset(&detect_result_group, 0, sizeof(detect_result_group_t));

    int retinaface_width, retinaface_height, retinaface_channel;
    model_manager_.get_retinaface_size(retinaface_width, retinaface_height, retinaface_channel);

    retinaface_inference(
        model_manager_.get_retinaface_ctx(),
        padded_img,
        retinaface_width,
        retinaface_height,
        retinaface_channel,
        config_.box_conf_threshold,
        config_.nms_threshold,
        img_width,
        img_height,
        model_manager_.get_retinaface_io_num(),
        model_manager_.get_retinaface_inputs(),
        model_manager_.get_retinaface_outputs(),
        model_manager_.get_retinaface_out_scales(),
        model_manager_.get_retinaface_out_zps(),
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

        char text[128];
        if (max_similarity >= config_.facenet_threshold) {
            snprintf(text, sizeof(text), "%s (%.2f)", matched_name.c_str(), max_similarity);
        } else {
            snprintf(text, sizeof(text), "Unknown (%.2f)", max_similarity);
        }

        cv::putText(frame, text, cv::Point(x1, y1 - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);

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

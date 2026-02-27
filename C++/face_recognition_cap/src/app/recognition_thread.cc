/**
 * @file recognition_thread.cc
 * @brief 识别线程实现 - 人脸对齐、特征提取、匹配、渲染
 * @author CL
 * @date 2025-12-04
 */

#include "app/recognition_thread.h"
#include "core/facenet.h"
#include <cstring>
#include <spdlog/spdlog.h>

RecognitionThread::RecognitionThread(ModelManager* model_manager,
                                     FeatureLibrary* feature_library,
                                     const cv::Mat& dst_landmark,
                                     float facenet_threshold,
                                     PerformanceMonitor* perf_monitor)
    : running_(false)
    , model_manager_(model_manager)
    , feature_library_(feature_library)
    , dst_landmark_(dst_landmark.clone())
    , facenet_threshold_(facenet_threshold)
    , perf_monitor_(perf_monitor)
    , recognition_callback_(nullptr)
    , frame_callback_(nullptr)
    , registration_callback_(nullptr)
    , mode_(RecognitionMode::Recognition)
    , avg_align_time_(0)
    , avg_facenet_time_(0)
    , avg_match_time_(0)
    , stat_faces_detected_(0)
    , stat_faces_recognized_(0)
{
}

RecognitionThread::~RecognitionThread() {
    stop();
}

void RecognitionThread::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&RecognitionThread::thread_func, this);
    }
}

void RecognitionThread::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_all();  // 唤醒可能在等待的线程
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

bool RecognitionThread::submit_task(const RecognitionTask& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // 丢弃旧帧，只保留最新
        while (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }

        queue_.push(task);
    }
    cv_.notify_one();  // 通知等待的线程
    return true;
}

void RecognitionThread::set_recognition_callback(RecognitionCallbackFunc callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    recognition_callback_ = std::move(callback);
}

void RecognitionThread::set_frame_callback(FrameCallbackFunc callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    frame_callback_ = std::move(callback);
}

void RecognitionThread::set_registration_callback(RegistrationCallbackFunc callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    registration_callback_ = std::move(callback);
}

void RecognitionThread::set_mode(RecognitionMode mode) {
    mode_.store(mode);
}

void RecognitionThread::set_threshold(float threshold) {
    facenet_threshold_ = threshold;
}

void RecognitionThread::get_recognition_stats(int& faces_detected, int& faces_recognized) {
    faces_detected = stat_faces_detected_.exchange(0);
    faces_recognized = stat_faces_recognized_.exchange(0);
}

void RecognitionThread::thread_func() {
    while (running_) {
        RecognitionTask task;

        // 使用条件变量等待任务，避免忙等待浪费 CPU
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if (!running_) {
                break;
            }

            task = queue_.front();
            queue_.pop();
        }

        process_task(task);
    }
}

void RecognitionThread::process_task(RecognitionTask& task) {
    struct timeval t_align_start, t_align_end;
    struct timeval t_facenet_start, t_facenet_end;
    struct timeval t_match_start, t_match_end;
    struct timeval t_render_start, t_render_end;
    
    float total_align_time = 0;
    float total_facenet_time = 0;
    float total_match_time = 0;
    
    auto get_us = [](struct timeval t) -> double {
        return t.tv_sec * 1000000.0 + t.tv_usec;
    };
    
    int facenet_width, facenet_height, facenet_channel;
    model_manager_->get_facenet_size(facenet_width, facenet_height, facenet_channel);
    
    RecognitionMode mode = mode_.load();
    std::vector<RecognitionResultData> recognition_results;
    std::vector<RegistrationSample> registration_samples;
    cv::Mat render_img = task.orig_img.clone();
    float threshold = facenet_threshold_;
    int recognized_count = 0;

    RecognitionCallbackFunc recognition_callback;
    FrameCallbackFunc frame_callback;
    RegistrationCallbackFunc registration_callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        recognition_callback = recognition_callback_;
        frame_callback = frame_callback_;
        registration_callback = registration_callback_;
    }
    
    for (int i = 0; i < task.detect_result.count; i++) {
        // 1. 人脸对齐
        gettimeofday(&t_align_start, NULL);
        
        float landmark[5][2] = {
            {(float)task.detect_result.results[i].point.point_1_x, (float)task.detect_result.results[i].point.point_1_y},
            {(float)task.detect_result.results[i].point.point_2_x, (float)task.detect_result.results[i].point.point_2_y},
            {(float)task.detect_result.results[i].point.point_3_x, (float)task.detect_result.results[i].point.point_3_y},
            {(float)task.detect_result.results[i].point.point_4_x, (float)task.detect_result.results[i].point.point_4_y},
            {(float)task.detect_result.results[i].point.point_5_x, (float)task.detect_result.results[i].point.point_5_y}
        };
        
        cv::Mat src(5, 2, CV_32FC1, landmark);
        memcpy(src.data, landmark, 2 * 5 * sizeof(float));
        
        cv::Mat M = similarTransform(src, dst_landmark_);
        cv::Mat warp;
        cv::warpPerspective(task.orig_img, warp, M, cv::Size(facenet_width, facenet_height));
        cv::cvtColor(warp, warp, cv::COLOR_BGR2RGB);
        
        gettimeofday(&t_align_end, NULL);
        
        // 2. FaceNet 特征提取
        gettimeofday(&t_facenet_start, NULL);
        float* facenet_result = nullptr;
        int facenet_ret = facenet_inference(
            model_manager_->get_facenet_ctx(),
            warp,
            model_manager_->get_facenet_io_num(),
            model_manager_->get_facenet_inputs(),
            model_manager_->get_facenet_outputs(),
            &facenet_result
        );
        gettimeofday(&t_facenet_end, NULL);
        const bool facenet_ok = (facenet_ret == 0 && facenet_result != nullptr);
        if (!facenet_ok) {
            spdlog::warn("FaceNet inference failed, skip face index {}", i);
        }
        
        std::vector<float> feature;
        if (registration_callback && facenet_ok) {
            feature.resize(FACENET_FEATURE_DIM);
            memcpy(feature.data(), facenet_result, FACENET_FEATURE_DIM * sizeof(float));
        }

        // 3. 特征匹配（仅识别模式）
        std::string name = "stranger";
        float max_score = 0.0f;
        int user_id = 0;
        bool is_recognized = false;
        if (mode == RecognitionMode::Recognition && facenet_ok) {
            gettimeofday(&t_match_start, NULL);
            bool match_found = feature_library_->match_feature_with_id(facenet_result, threshold,
                                                                       user_id, name, max_score);
            gettimeofday(&t_match_end, NULL);

            // 判断是否识别成功
            is_recognized = match_found && (name != "stranger") && (max_score >= threshold);
        }
        
        // 获取人脸框
        int x1 = task.detect_result.results[i].box.left;
        int y1 = task.detect_result.results[i].box.top;
        int x2 = task.detect_result.results[i].box.right;
        int y2 = task.detect_result.results[i].box.bottom;

        // 组装注册预览数据（每张人脸）
        if (registration_callback) {
            RegistrationSample sample;
            sample.face_box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
            sample.landmarks = {
                cv::Point2f(task.detect_result.results[i].point.point_1_x, task.detect_result.results[i].point.point_1_y),
                cv::Point2f(task.detect_result.results[i].point.point_2_x, task.detect_result.results[i].point.point_2_y),
                cv::Point2f(task.detect_result.results[i].point.point_3_x, task.detect_result.results[i].point.point_3_y),
                cv::Point2f(task.detect_result.results[i].point.point_4_x, task.detect_result.results[i].point.point_4_y),
                cv::Point2f(task.detect_result.results[i].point.point_5_x, task.detect_result.results[i].point.point_5_y)
            };
            sample.feature = std::move(feature);
            sample.score = task.detect_result.results[i].prop;
            registration_samples.push_back(std::move(sample));
        }
        
        // 创建识别结果
        RecognitionResultData result;
        result.user_id = user_id;
        result.user_name = name;
        result.similarity = max_score;
        result.face_box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
        result.face_image = warp.clone();
        result.timestamp = std::chrono::system_clock::now();
        recognition_results.push_back(result);
        
        // 修复：只有识别成功时才触发回调（避免陌生人误触发）
        if (mode == RecognitionMode::Recognition && recognition_callback && is_recognized) {
            recognition_callback(result);
        }
        
        // 释放输出
        if (facenet_ok) {
            facenet_output_release(
                model_manager_->get_facenet_ctx(),
                model_manager_->get_facenet_io_num(),
                model_manager_->get_facenet_outputs()
            );
        }
        if (mode == RecognitionMode::Recognition && is_recognized) {
            recognized_count++;
        }
        cv::Scalar color = (mode == RecognitionMode::Recognition)
            ? (is_recognized ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255))
            : cv::Scalar(0, 255, 0);
        
        cv::rectangle(render_img, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);

        // 注意：名称文字由 Qt 层的 VideoDisplayWidget 使用 QPainter 绘制
        // OpenCV 的 Hershey 字体不支持中文，会显示为问号
        // 因此这里只绘制人脸框，不绘制文字
        
        // 累计时间
        total_align_time += (get_us(t_align_end) - get_us(t_align_start)) / 1000;
        total_facenet_time += (get_us(t_facenet_end) - get_us(t_facenet_start)) / 1000;
        if (mode == RecognitionMode::Recognition) {
            total_match_time += (get_us(t_match_end) - get_us(t_match_start)) / 1000;
        }
    }
    
    // 更新性能统计（滑动平均）
    avg_align_time_ = avg_align_time_ * 0.9f + total_align_time * 0.1f;
    avg_facenet_time_ = avg_facenet_time_ * 0.9f + total_facenet_time * 0.1f;
    avg_match_time_ = avg_match_time_ * 0.9f + total_match_time * 0.1f;
    
    // 更新检测精度统计
    stat_faces_detected_ += task.detect_result.count;
    stat_faces_recognized_ += recognized_count;
    
    // 输出 / 渲染
    gettimeofday(&t_render_start, NULL);
    if (frame_callback) {
        // GUI 模式：通过回调返回帧
        frame_callback(render_img, recognition_results);
    } else {
        // 命令行模式：直接渲染
        char fps_text[64];
        snprintf(fps_text, sizeof(fps_text), "FPS: %.1f (%.1f ms)",
                 task.current_fps, task.frame_time);
        cv::putText(render_img, fps_text, cv::Point(10, 30),
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        cv::imshow("Face Recognition", render_img);
        cv::waitKey(1);
    }
    gettimeofday(&t_render_end, NULL);

    if (registration_callback) {
        registration_callback(task.orig_img, registration_samples);
    }

    if (perf_monitor_) {
        auto get_us = [](struct timeval t) -> double {
            return t.tv_sec * 1000000.0 + t.tv_usec;
        };
        double render_ms = (get_us(t_render_end) - get_us(t_render_start)) / 1000.0;
        perf_monitor_->record_render_time(render_ms);
    }
}

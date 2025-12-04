/**
 * @file recognition_thread.cc
 * @brief 识别线程实现 - 人脸对齐、特征提取、匹配、渲染
 * @author CL
 * @date 2025-12-04
 */

#include "app/recognition_thread.h"
#include "core/facenet.h"
#include <cstring>

RecognitionThread::RecognitionThread(ModelManager* model_manager,
                                     FeatureLibrary* feature_library,
                                     const cv::Mat& dst_landmark,
                                     float facenet_threshold)
    : running_(false)
    , model_manager_(model_manager)
    , feature_library_(feature_library)
    , dst_landmark_(dst_landmark.clone())
    , facenet_threshold_(facenet_threshold)
    , recognition_callback_(nullptr)
    , frame_callback_(nullptr)
    , avg_align_time_(0)
    , avg_facenet_time_(0)
    , avg_match_time_(0)
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
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

bool RecognitionThread::submit_task(const RecognitionTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 丢弃旧帧，只保留最新
    while (queue_.size() >= MAX_QUEUE_SIZE) {
        queue_.pop();
    }
    
    queue_.push(task);
    return true;
}

void RecognitionThread::set_recognition_callback(RecognitionCallbackFunc callback) {
    recognition_callback_ = callback;
}

void RecognitionThread::set_frame_callback(FrameCallbackFunc callback) {
    frame_callback_ = callback;
}

void RecognitionThread::set_threshold(float threshold) {
    facenet_threshold_ = threshold;
}

void RecognitionThread::thread_func() {
    while (running_) {
        RecognitionTask task;
        
        // 尝试获取任务（非阻塞轮询）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty()) {
                continue;
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
    
    float total_align_time = 0;
    float total_facenet_time = 0;
    float total_match_time = 0;
    
    auto get_us = [](struct timeval t) -> double {
        return t.tv_sec * 1000000.0 + t.tv_usec;
    };
    
    int facenet_width, facenet_height, facenet_channel;
    model_manager_->get_facenet_size(facenet_width, facenet_height, facenet_channel);
    
    std::vector<RecognitionResultData> recognition_results;
    cv::Mat render_img = task.orig_img.clone();
    float threshold = facenet_threshold_;
    
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
        facenet_inference(
            model_manager_->get_facenet_ctx(),
            warp,
            model_manager_->get_facenet_io_num(),
            model_manager_->get_facenet_inputs(),
            model_manager_->get_facenet_outputs(),
            &facenet_result
        );
        gettimeofday(&t_facenet_end, NULL);
        
        // 3. 特征匹配
        gettimeofday(&t_match_start, NULL);
        std::string name;
        float max_score;
        int user_id = 0;
        feature_library_->match_feature_with_id(facenet_result, threshold,
                                                user_id, name, max_score);
        gettimeofday(&t_match_end, NULL);
        
        // 获取人脸框
        int x1 = task.detect_result.results[i].box.left;
        int y1 = task.detect_result.results[i].box.top;
        int x2 = task.detect_result.results[i].box.right;
        int y2 = task.detect_result.results[i].box.bottom;
        
        // 创建识别结果
        RecognitionResultData result;
        result.user_id = user_id;
        result.user_name = name;
        result.similarity = max_score;
        result.face_box = cv::Rect(x1, y1, x2 - x1, y2 - y1);
        result.face_image = warp.clone();
        result.timestamp = std::chrono::system_clock::now();
        recognition_results.push_back(result);
        
        // 调用识别回调
        if (recognition_callback_) {
            recognition_callback_(result);
        }
        
        // 释放输出
        facenet_output_release(
            model_manager_->get_facenet_ctx(),
            model_manager_->get_facenet_io_num(),
            model_manager_->get_facenet_outputs()
        );
        
        // 绘制结果（与之前的 recognize_and_match 保持一致）
        cv::Scalar color = (name != "stranger" && max_score >= threshold) ?
                          cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
        
        cv::rectangle(render_img, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);
        
        char label[256];
        snprintf(label, sizeof(label), "%s (%.2f)", name.c_str(), max_score);
        cv::putText(render_img, label, cv::Point(x1, y1 - 10),
                   cv::FONT_HERSHEY_SIMPLEX, 0.7, color, 2);
        
        // 累计时间
        total_align_time += (get_us(t_align_end) - get_us(t_align_start)) / 1000;
        total_facenet_time += (get_us(t_facenet_end) - get_us(t_facenet_start)) / 1000;
        total_match_time += (get_us(t_match_end) - get_us(t_match_start)) / 1000;
    }
    
    // 更新性能统计（滑动平均）
    avg_align_time_ = avg_align_time_ * 0.9f + total_align_time * 0.1f;
    avg_facenet_time_ = avg_facenet_time_ * 0.9f + total_facenet_time * 0.1f;
    avg_match_time_ = avg_match_time_ * 0.9f + total_match_time * 0.1f;
    
    // 输出
    if (frame_callback_) {
        // GUI 模式：通过回调返回帧
        frame_callback_(render_img, recognition_results);
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
}

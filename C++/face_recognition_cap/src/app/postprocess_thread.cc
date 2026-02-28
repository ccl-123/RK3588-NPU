/**
 * @file postprocess_thread.cc
 * @brief YOLO 后处理线程实现
 */

#include "app/postprocess_thread.h"
#include <utility>
#include <cstring>
#include <chrono>

PostprocessThread::PostprocessThread(ModelManager* model_manager,
                                     RecognitionThread* recognition_thread,
                                     PerformanceMonitor* perf_monitor,
                                     float box_conf_threshold,
                                     float nms_threshold)
    : running_(false)
    , model_manager_(model_manager)
    , recognition_thread_(recognition_thread)
    , perf_monitor_(perf_monitor)
    , box_conf_threshold_(box_conf_threshold)
    , nms_threshold_(nms_threshold) {
}

PostprocessThread::~PostprocessThread() {
    stop();
}

void PostprocessThread::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&PostprocessThread::thread_func, this);
    }
}

void PostprocessThread::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

bool PostprocessThread::submit_task(PostprocessTask&& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    cv_.notify_one();
    return true;
}

size_t PostprocessThread::queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void PostprocessThread::thread_func() {
    while (running_) {
        PostprocessTask task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return !queue_.empty() || !running_;
            });
            if (!running_ && queue_.empty()) {
                break;
            }
            task = std::move(queue_.front());
            queue_.pop();
        }

        // 后处理
        auto t0 = std::chrono::steady_clock::now();
        detect_result_group_t detect_result;
        memset(&detect_result, 0, sizeof(detect_result_group_t));

        int model_w, model_h, model_c;
        model_manager_->get_face_detector_size(model_w, model_h, model_c);

        yolov8_face_postprocess(
            task.yolo_outputs,
            model_manager_->get_face_detector_output_attrs(),
            YOLOV8_FACE_OUTPUT_NUM,
            model_h,
            model_w,
            task.img_width,
            task.img_height,
            box_conf_threshold_.load(),
            nms_threshold_.load(),
            &detect_result
        );
        auto t1 = std::chrono::steady_clock::now();
        if (perf_monitor_) {
            double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
            perf_monitor_->record_postprocess_time(ms);
        }

        // 组装识别任务
        RecognitionTask rec_task;
        rec_task.orig_img = std::move(task.orig_img);
        rec_task.detect_result = detect_result;
        rec_task.timestamp = task.timestamp;
        rec_task.current_fps = task.current_fps;
        rec_task.frame_time = task.frame_time;

        recognition_thread_->submit_task(rec_task);
    }
}



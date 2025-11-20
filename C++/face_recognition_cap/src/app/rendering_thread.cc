/**
 * @file rendering_thread.cc
 * @brief 渲染线程实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/rendering_thread.h"

RenderingThread::RenderingThread(const std::string& window_name)
    : running_(false)
    , window_name_(window_name)
{
}

RenderingThread::~RenderingThread() {
    stop();
}

void RenderingThread::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&RenderingThread::thread_func, this);
    }
}

void RenderingThread::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

bool RenderingThread::submit_task(const cv::Mat& img, const std::string& fps_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (queue_.size() >= MAX_QUEUE_SIZE) {
        return false;  // 队列已满,丢弃旧帧
    }

    RenderTask task;
    task.img = img.clone();
    task.fps_text = fps_text;
    
    queue_.push(task);
    cv_.notify_one();
    
    return true;
}

size_t RenderingThread::queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return queue_.size();
}

void RenderingThread::thread_func() {
    while (running_) {
        RenderTask task;

        // 从队列获取任务
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty()) break;
            if (queue_.empty()) continue;

            task = queue_.front();
            queue_.pop();
        }

        // 执行渲染
        cv::imshow(window_name_, task.img);
        cv::waitKey(1);
    }
}


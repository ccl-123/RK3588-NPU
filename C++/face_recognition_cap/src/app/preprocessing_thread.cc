/**
 * @file preprocessing_thread.cc
 * @brief 预处理线程实现
 * @author CL
 * @date 2025-11-20
 */

#include "app/preprocessing_thread.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"

PreprocessingThread::PreprocessingThread(int resize_w, int resize_h, int img_width, int img_height)
    : running_(false)
    , resize_w_(resize_w)
    , resize_h_(resize_h)
    , img_width_(img_width)
    , img_height_(img_height)
    , flipped_buffer_(img_height, img_width, CV_8UC3)
{
}

PreprocessingThread::~PreprocessingThread() {
    stop();
}

void PreprocessingThread::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&PreprocessingThread::thread_func, this);
    }
}

void PreprocessingThread::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

bool PreprocessingThread::submit_task(const cv::Mat& orig_img, struct timeval timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (input_queue_.size() >= MAX_QUEUE_SIZE) {
        return false;  // 队列已满
    }

    PreprocessTask task;
    task.orig_img = orig_img.clone();
    task.timestamp = timestamp;
    
    input_queue_.push(task);
    cv_.notify_one();
    
    return true;
}

bool PreprocessingThread::get_result(PreprocessTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (output_queue_.empty()) {
        return false;
    }

    task = output_queue_.front();
    output_queue_.pop();
    
    return true;
}

size_t PreprocessingThread::input_queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return input_queue_.size();
}

size_t PreprocessingThread::output_queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return output_queue_.size();
}

void PreprocessingThread::thread_func() {
    while (running_) {
        PreprocessTask task;

        // 从输入队列获取任务
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return !input_queue_.empty() || !running_; });

            if (!running_ && input_queue_.empty()) break;
            if (input_queue_.empty()) continue;

            task = input_queue_.front();
            input_queue_.pop();
        }

        // 执行RGA处理
        process_with_rga(task);

        // 放入输出队列
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (output_queue_.size() < MAX_QUEUE_SIZE) {
                output_queue_.push(task);
            }
        }
    }
}

void PreprocessingThread::process_with_rga(PreprocessTask& task) {
    // 分配处理后的图像缓冲区
    task.processed_img = cv::Mat(resize_h_, resize_w_, CV_8UC3);

    // 第一步: RGA翻转
    rga_buffer_t flip_src = wrapbuffer_virtualaddr(task.orig_img.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    rga_buffer_t flip_dst = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);

    IM_STATUS flip_status = imflip(flip_src, flip_dst, IM_HAL_TRANSFORM_FLIP_H);
    if (flip_status != IM_STATUS_SUCCESS) {
        // RGA失败,降级到OpenCV
        cv::flip(task.orig_img, flipped_buffer_, 1);
    }

    // 第二步: RGA缩放
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(task.processed_img.data, resize_w_, resize_h_, RK_FORMAT_BGR_888);

    im_rect src_rect = {0, 0, img_width_, img_height_};
    im_rect dst_rect = {0, 0, resize_w_, resize_h_};
    im_rect pat_rect = {0, 0, 0, 0};
    rga_buffer_t pat_buf = {};

    IM_STATUS resize_status = improcess(src_buf, dst_buf, pat_buf, src_rect, dst_rect, pat_rect, 0);
    if (resize_status != IM_STATUS_SUCCESS) {
        // RGA失败,降级到OpenCV
        cv::resize(flipped_buffer_, task.processed_img, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
    }

    // 更新原图为翻转后的图像
    task.orig_img = flipped_buffer_.clone();
}


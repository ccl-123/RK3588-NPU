/**
 * @file preprocessing_thread.cc
 * @brief 采集预处理线程实现 - 摄像头采集 + RGA硬件加速预处理
 * @author CL
 * @date 2025-11-20
 */

#include "app/preprocessing_thread.h"
#include "hardware/camera_util.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"

PreprocessingThread::PreprocessingThread(int resize_w, int resize_h, 
                                         int img_width, int img_height,
                                         const std::string& camera_type,
                                         bool use_async_usb)
    : running_(false)
    , resize_w_(resize_w)
    , resize_h_(resize_h)
    , img_width_(img_width)
    , img_height_(img_height)
    , camera_type_(camera_type)
    , use_async_usb_(use_async_usb)
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
        if (thread_.joinable()) {
            thread_.join();
        }
    }
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

size_t PreprocessingThread::output_queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return output_queue_.size();
}

void PreprocessingThread::thread_func() {
    cv::Mat frame;
    
    while (running_) {
        // 1. 从摄像头读取一帧
        if (!read_frame(frame)) {
            continue;
        }
        
        // 2. 创建预处理任务
        PreprocessTask task;
        task.orig_img = frame;
        gettimeofday(&task.timestamp, NULL);
        
        // 3. 执行 RGA 预处理（翻转 + 缩放）
        process_with_rga(task);

        // 4. 放入输出队列（丢弃旧帧，只保留最新）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (output_queue_.size() >= MAX_QUEUE_SIZE) {
                output_queue_.pop();
            }
                output_queue_.push(task);
            }
        }
    }

bool PreprocessingThread::read_frame(cv::Mat& frame) {
    if (camera_type_ == "usb") {
        if (use_async_usb_) {
            read_usb_frame_async(&frame);
        } else {
            read_usb_frame(&frame);
        }
    } else if (camera_type_ == "mipi") {
        read_mipi_frame(&frame);
    } else {
        return false;
    }
    
    return !frame.empty();
}

void PreprocessingThread::process_with_rga(PreprocessTask& task) {
    // 分配处理后的图像缓冲区
    task.processed_img = cv::Mat(resize_h_, resize_w_, CV_8UC3);

    // 检查是否启用RGA硬件加速
    if (!Config::Performance::USE_RGA) {
        // 完全使用OpenCV，避免RGA库的Valgrind警告
        cv::flip(task.orig_img, flipped_buffer_, 1);
        cv::resize(flipped_buffer_, task.processed_img, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
        task.orig_img = flipped_buffer_.clone();
        return;
    }

    // 第一步: RGA翻转
    rga_buffer_t flip_src = wrapbuffer_virtualaddr(task.orig_img.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    rga_buffer_t flip_dst = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);

    IM_STATUS flip_status = imflip(flip_src, flip_dst, IM_HAL_TRANSFORM_FLIP_H);
    if (flip_status != IM_STATUS_SUCCESS) {
        // RGA失败，降级到OpenCV
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
        // RGA失败，降级到OpenCV
        cv::resize(flipped_buffer_, task.processed_img, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
    }

    // 更新原图为翻转后的图像
    task.orig_img = flipped_buffer_.clone();
}

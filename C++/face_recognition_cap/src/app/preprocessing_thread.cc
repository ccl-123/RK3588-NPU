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
#include <algorithm>
#include <chrono>

PreprocessingThread::PreprocessingThread(int resize_w, int resize_h, 
                                         int img_width, int img_height,
                                         PerformanceMonitor* perf_monitor,
                                         const std::string& camera_type)
    : running_(false)
    , resize_w_(resize_w)
    , resize_h_(resize_h)
    , img_width_(img_width)
    , img_height_(img_height)
    , camera_type_(camera_type)
    , perf_monitor_(perf_monitor)
    , flipped_buffer_(img_height, img_width, CV_8UC3)
    , resized_buffer_(resize_h, resize_w, CV_8UC3)
{
    // 计算 padding 目标尺寸与边界（输出为正方形，适配模型输入）
    target_w_ = std::max(resize_w_, resize_h_);
    target_h_ = target_w_;
    pad_top_ = 0;
    pad_left_ = 0;
    if (resize_w_ >= resize_h_) {
        pad_bottom_ = target_h_ - resize_h_;
        pad_right_ = 0;
    } else {
        pad_bottom_ = 0;
        pad_right_ = target_w_ - resize_w_;
    }
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
        auto t0 = std::chrono::steady_clock::now();
        // 1. 从摄像头读取一帧
        if (!read_frame(frame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
        auto t1 = std::chrono::steady_clock::now();
        if (perf_monitor_) {
            double ms = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;
            perf_monitor_->record_preprocess_time(ms);
        }
    }
    }

/**
 * @brief 从摄像头读取原始帧
 * @param[out] frame 输出的原始图像
 * @return true 读取成功, false 失败
 */
bool PreprocessingThread::read_frame(cv::Mat& frame) {
    bool ret = false;
    if (camera_type_ == "usb") {
        ret = read_usb_frame(&frame);
    } else {
        return false;
    }
    
    return ret && !frame.empty();
}

void PreprocessingThread::process_with_rga(PreprocessTask& task) {
    // 目标方形缓冲区（匹配模型输入尺寸）
    task.processed_img = cv::Mat(target_h_, target_w_, CV_8UC3);

    // 检查是否启用RGA硬件加速
    if (!Config::Performance::USE_RGA) {
        // 完全使用OpenCV，避免RGA库的Valgrind警告
        cv::flip(task.orig_img, flipped_buffer_, 1);
        cv::resize(flipped_buffer_, resized_buffer_, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
        // padding 到方形
        cv::copyMakeBorder(resized_buffer_, task.processed_img,
                           pad_top_, pad_bottom_, pad_left_, pad_right_,
                           cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
        task.orig_img = flipped_buffer_.clone();  // 保留翻转后的原图用于渲染
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

    // 第二步: RGA缩放到非方形 resized_buffer_
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(resized_buffer_.data, resize_w_, resize_h_, RK_FORMAT_BGR_888);

    im_rect src_rect = {0, 0, img_width_, img_height_};
    im_rect dst_rect = {0, 0, resize_w_, resize_h_};
    im_rect pat_rect = {0, 0, 0, 0};
    rga_buffer_t pat_buf = {};

    IM_STATUS resize_status = improcess(src_buf, dst_buf, pat_buf, src_rect, dst_rect, pat_rect, 0);
    if (resize_status != IM_STATUS_SUCCESS) {
        // RGA失败，降级到OpenCV
        cv::resize(flipped_buffer_, resized_buffer_, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
    }

    // 第三步: padding 到方形模型输入
    cv::copyMakeBorder(resized_buffer_, task.processed_img,
                       pad_top_, pad_bottom_, pad_left_, pad_right_,
                       cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

    // 更新原图为翻转后的图像
    task.orig_img = flipped_buffer_.clone();
}

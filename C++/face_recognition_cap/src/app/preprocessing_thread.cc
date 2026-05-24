/**
 * @file preprocessing_thread.cc
 * @brief 采集预处理线程实现 - 合并采集与预处理
 * @author CL
 * @date 2026-05-25
 */

#include "app/preprocessing_thread.h"
#include "hardware/camera_util.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"
#include <spdlog/spdlog.h>
#include <algorithm>

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
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool PreprocessingThread::get_result(PreprocessTask& task) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_output_.wait(lock, [this] {
        return !output_queue_.empty() || wakeup_ || camera_failed_.load(std::memory_order_acquire);
    });

    if (wakeup_ || camera_failed_.load(std::memory_order_acquire)) {
        wakeup_ = false;
        return false;  // 被停止/故障信号唤醒，用于退出
    }

    if (output_queue_.empty()) {
        return false;
    }

    task = output_queue_.front();
    output_queue_.pop();
    return true;
}

void PreprocessingThread::wake_consumer() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wakeup_ = true;
    }
    cv_output_.notify_all();
}

size_t PreprocessingThread::output_queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return output_queue_.size();
}

std::string PreprocessingThread::get_camera_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return camera_error_;
}

void PreprocessingThread::thread_func() {
    spdlog::info("PreprocessingThread: integrated capture and preprocess loop started");

    while (running_) {
        void* raw_pkt_data = nullptr;
        uint32_t raw_pkt_size = 0;
        uint32_t raw_buf_index = 0;

        // 1. 底层同步抓取 V4L2 原始 MJPEG 数据包 (非阻塞/极轻量)
        if (!read_usb_raw_packet(&raw_pkt_data, &raw_pkt_size, &raw_buf_index)) {
            if (!running_) {
                break;
            }
            if (has_usb_camera_error()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    camera_error_ = get_usb_camera_error();
                    wakeup_ = true;
                }
                camera_failed_.store(true, std::memory_order_release);
                running_ = false;
                cv_output_.notify_all();
                spdlog::error("Camera error detected in preprocessing loop: {}", camera_error_);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        // 开始精确记录 CPU 解码耗时
        auto t_dec_start = std::chrono::steady_clock::now();

        // 2. CPU 解码 MJPEG 数据包到 BGR cv::Mat
        cv::Mat raw_data(1, raw_pkt_size, CV_8UC1, raw_pkt_data);
        cv::Mat decoded_frame;
        bool decode_ok = false;
        try {
            decoded_frame = cv::imdecode(raw_data, cv::IMREAD_COLOR);
            if (!decoded_frame.empty()) {
                decode_ok = true;
            }
        } catch (const cv::Exception& e) {
            spdlog::warn("OpenCV imdecode failed for raw frame size={}: {}", raw_pkt_size, e.what());
        }

        // 无论解码成功还是失败，均立刻将底层的硬件映射缓冲区释放给内核 (QBUF)，防止队列饥饿
        release_usb_raw_packet(raw_buf_index);

        if (!decode_ok) {
            spdlog::warn("Decoded frame is empty, skipping frame.");
            continue;
        }

        auto t_dec_end = std::chrono::steady_clock::now();
        double decode_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_dec_end - t_dec_start).count() / 1000.0;

        // 开始精确记录 RGA 预处理耗时
        auto t_rga_start = std::chrono::steady_clock::now();

        // 3. 封装预处理任务
        PreprocessTask task;
        task.orig_img = std::move(decoded_frame);
        gettimeofday(&task.timestamp, NULL);

        // 4. 执行 RGA 硬件加速预处理（翻转 + 缩放）
        process_with_rga(task);

        // 5. 放入输出队列（如果堆积，丢弃旧帧，只保留最新）
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while (output_queue_.size() >= MAX_QUEUE_SIZE) {
                output_queue_.pop();
            }
            output_queue_.push(std::move(task));
        }
        cv_output_.notify_one();

        auto t_rga_end = std::chrono::steady_clock::now();
        double rga_ms = std::chrono::duration_cast<std::chrono::microseconds>(t_rga_end - t_rga_start).count() / 1000.0;

        if (perf_monitor_) {
            perf_monitor_->record_decode_time(decode_ms);
            perf_monitor_->record_preprocess_time(rga_ms);
        }
    }
}

/**
 * @brief 使用 RGA 硬件加速进行图像镜像翻转、非等比缩放以及 Letterbox 填充
 */
void PreprocessingThread::process_with_rga(PreprocessTask& task) {
    // 检查是否启用RGA硬件加速
    if (!Config::Performance::USE_RGA) {
        // 目标方形预填充（完全使用OpenCV，避免RGA库的Valgrind警告）
        task.processed_img = cv::Mat(target_h_, target_w_, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::flip(task.orig_img, flipped_buffer_, 1);
        cv::Mat resized_part = task.processed_img(cv::Rect(pad_left_, pad_top_, resize_w_, resize_h_));
        cv::resize(flipped_buffer_, resized_part, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
        task.orig_img = flipped_buffer_.clone();  // 保留翻转后的原图用于渲染
        return;
    }

    // 启用 RGA 加速下，分配内存但不进行 CPU 填充
    task.processed_img = cv::Mat(target_h_, target_w_, CV_8UC3);

    // 第一步: RGA翻转 (BGR888 水平镜像)
    rga_buffer_t flip_src = wrapbuffer_virtualaddr(task.orig_img.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    rga_buffer_t flip_dst = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);

    IM_STATUS flip_status = imflip(flip_src, flip_dst, IM_HAL_TRANSFORM_FLIP_H);
    if (flip_status != IM_STATUS_SUCCESS) {
        cv::flip(task.orig_img, flipped_buffer_, 1);
    }

    // 第二步: RGA 硬件刷黑底 (0x00000000 = 黑色)
    rga_buffer_t dst_buf = wrapbuffer_virtualaddr(task.processed_img.data, target_w_, target_h_, RK_FORMAT_BGR_888);
    im_rect whole_rect = {0, 0, target_w_, target_h_};
    imfill(dst_buf, whole_rect, 0x00000000);

    // 第三步: RGA缩放到 task.processed_img 的正方形中心 Letterbox 区域 (BGR888 缩放)
    rga_buffer_t src_buf = wrapbuffer_virtualaddr(flipped_buffer_.data, img_width_, img_height_, RK_FORMAT_BGR_888);
    im_rect src_rect = {0, 0, img_width_, img_height_};
    im_rect dst_rect = {pad_left_, pad_top_, resize_w_, resize_h_};
    im_rect pat_rect = {0, 0, 0, 0};
    rga_buffer_t pat_buf = {};

    IM_STATUS resize_status = improcess(src_buf, dst_buf, pat_buf, src_rect, dst_rect, pat_rect, 0);
    if (resize_status != IM_STATUS_SUCCESS) {
        // 降级使用 OpenCV 局部 resize (需手动清零防止旧随机内存污染)
        task.processed_img.setTo(cv::Scalar(0, 0, 0));
        cv::Mat resized_part = task.processed_img(cv::Rect(pad_left_, pad_top_, resize_w_, resize_h_));
        cv::resize(flipped_buffer_, resized_part, cv::Size(resize_w_, resize_h_), 0, 0, cv::INTER_LINEAR);
    }

    // 更新原图为翻转后的图像，供渲染使用
    task.orig_img = flipped_buffer_.clone();
}

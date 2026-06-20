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
{
    // 计算 padding 目标尺寸与边界（输出为正方形，适配模型输入）
    target_w_ = std::max(resize_w_, resize_h_);
    target_h_ = target_w_;
    pad_top_ = 0;
    pad_left_ = 0;

    // USB MJPEG 需要 MPP 硬解；MIPI/OV13855 已由 ISP 输出 NV12 DMA-BUF。
    if (camera_type_ == "usb" && init_mpp() != 0) {
        spdlog::error("PreprocessingThread: failed to initialize MPP hardware decoder!");
    }
}

PreprocessingThread::~PreprocessingThread() {
    stop();
    deinit_mpp();
}

int PreprocessingThread::init_mpp() {
    if (mpp_initialized_) return 0;

    MPP_RET ret = mpp_create(&mpp_ctx_, &mpp_api_);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: mpp_create failed: ret={}", static_cast<int>(ret));
        return -1;
    }

    ret = mpp_init(mpp_ctx_, MPP_CTX_DEC, MPP_VIDEO_CodingMJPEG);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: mpp_init CodingMJPEG failed: ret={}", static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }

    MppDecCfg cfg = nullptr;
    ret = mpp_dec_cfg_init(&cfg);
    if (ret == MPP_OK) {
        ret = mpp_api_->control(mpp_ctx_, MPP_DEC_GET_CFG, cfg);
    }
    if (ret == MPP_OK) {
        // V4L2 supplies complete JPEG images, but retain the official decoder configuration.
        ret = mpp_dec_cfg_set_u32(cfg, "base:split_parse", 1);
    }
    if (ret == MPP_OK) {
        ret = mpp_api_->control(mpp_ctx_, MPP_DEC_SET_CFG, cfg);
    }
    if (cfg) {
        mpp_dec_cfg_deinit(cfg);
    }
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: failed to configure MPP decoder: ret={}", static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }

    // JPEG task decoding requires an application-provided output frame. Force
    // NV12 so the RGA and CPU fallback paths consume one defined layout.
    MppFrameFormat output_format = MPP_FMT_YUV420SP;
    ret = mpp_api_->control(mpp_ctx_, MPP_DEC_SET_OUTPUT_FORMAT, &output_format);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: failed to set MJPEG output format to NV12: ret={}",
                      static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }

    ret = mpp_frame_init(&mpp_output_frame_);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: failed to allocate MPP JPEG output frame: ret={}",
                      static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }

    ret = mpp_buffer_group_get_internal(&mpp_frm_grp_, MPP_BUFFER_TYPE_DRM);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: failed to allocate DRM buffer group: ret={}",
                      static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }

    const RK_U32 hor_stride = (static_cast<RK_U32>(img_width_) + 15U) & ~15U;
    const RK_U32 ver_stride = (static_cast<RK_U32>(img_height_) + 15U) & ~15U;
    // Follow Rockchip mpi_dec_test: leave headroom for JPEG output formats.
    const RK_U32 buffer_size = hor_stride * ver_stride * 4U;
    ret = mpp_buffer_get(mpp_frm_grp_, &mpp_output_buffer_, buffer_size);
    if (ret != MPP_OK) {
        spdlog::error("PreprocessingThread: failed to allocate DRM JPEG output buffer: ret={}",
                      static_cast<int>(ret));
        deinit_mpp();
        return -1;
    }
    mpp_frame_set_buffer(mpp_output_frame_, mpp_output_buffer_);

    mpp_initialized_ = true;
    spdlog::info("PreprocessingThread: MPP MJPEG task decoder initialized with DRM NV12 output.");
    return 0;
}

void PreprocessingThread::deinit_mpp() {
    if (mpp_output_frame_) {
        mpp_frame_deinit(&mpp_output_frame_);
        mpp_output_frame_ = nullptr;
    }
    if (mpp_ctx_) {
        mpp_destroy(mpp_ctx_);
        mpp_ctx_ = nullptr;
        mpp_api_ = nullptr;
    }
    if (mpp_output_buffer_) {
        mpp_buffer_put(mpp_output_buffer_);
        mpp_output_buffer_ = nullptr;
    }
    if (mpp_input_buffer_) {
        mpp_buffer_put(mpp_input_buffer_);
        mpp_input_buffer_ = nullptr;
        mpp_input_capacity_ = 0;
    }
    if (mpp_frm_grp_) {
        mpp_buffer_group_put(mpp_frm_grp_);
        mpp_frm_grp_ = nullptr;
    }
    mpp_initialized_ = false;
    spdlog::info("PreprocessingThread: MPP hardware decoder deinitialized.");
}

bool PreprocessingThread::decode_mjpeg_packet(void* packet_data, uint32_t packet_size,
                                              double& input_copy_ms) {
    MppPacket packet = nullptr;
    MppTask input_task = nullptr;
    MppTask output_task = nullptr;
    MPP_RET ret = MPP_OK;
    input_copy_ms = 0.0;

    if (packet_data == nullptr || packet_size == 0) {
        spdlog::warn("PreprocessingThread: empty MJPEG packet.");
        return false;
    }

    if (packet_size > mpp_input_capacity_) {
        if (mpp_input_buffer_) {
            mpp_buffer_put(mpp_input_buffer_);
            mpp_input_buffer_ = nullptr;
            mpp_input_capacity_ = 0;
        }

        mpp_input_capacity_ = (static_cast<size_t>(packet_size) + 4095U) & ~static_cast<size_t>(4095U);
        ret = mpp_buffer_get(mpp_frm_grp_, &mpp_input_buffer_, mpp_input_capacity_);
        if (ret != MPP_OK) {
            mpp_input_capacity_ = 0;
            spdlog::warn("PreprocessingThread: failed to allocate MPP JPEG input buffer: ret={}",
                         static_cast<int>(ret));
            return false;
        }
    }

    auto t_copy_start = std::chrono::steady_clock::now();
    ret = mpp_buffer_write(mpp_input_buffer_, 0, packet_data, packet_size);
    auto t_copy_end = std::chrono::steady_clock::now();
    input_copy_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        t_copy_end - t_copy_start).count() / 1000.0;

    if (ret == MPP_OK) {
        ret = mpp_packet_init_with_buffer(&packet, mpp_input_buffer_);
    }
    if (ret != MPP_OK) {
        spdlog::warn("PreprocessingThread: failed to prepare MPP JPEG input packet: ret={}",
                     static_cast<int>(ret));
        return false;
    }
    mpp_packet_set_pos(packet, mpp_buffer_get_ptr(mpp_input_buffer_));
    mpp_packet_set_size(packet, packet_size);
    mpp_packet_set_length(packet, packet_size);

    ret = mpp_api_->poll(mpp_ctx_, MPP_PORT_INPUT, MPP_POLL_BLOCK);
    if (ret == MPP_OK) {
        ret = mpp_api_->dequeue(mpp_ctx_, MPP_PORT_INPUT, &input_task);
    }
    if (ret == MPP_OK && input_task) {
        ret = mpp_task_meta_set_packet(input_task, KEY_INPUT_PACKET, packet);
    }
    if (ret == MPP_OK) {
        ret = mpp_task_meta_set_frame(input_task, KEY_OUTPUT_FRAME, mpp_output_frame_);
    }
    if (ret == MPP_OK) {
        ret = mpp_api_->enqueue(mpp_ctx_, MPP_PORT_INPUT, input_task);
    }
    if (ret != MPP_OK) {
        spdlog::warn("PreprocessingThread: failed to submit MPP MJPEG task: ret={}",
                     static_cast<int>(ret));
        mpp_packet_deinit(&packet);
        return false;
    }

    ret = mpp_api_->poll(mpp_ctx_, MPP_PORT_OUTPUT, MPP_POLL_BLOCK);
    if (ret == MPP_OK) {
        ret = mpp_api_->dequeue(mpp_ctx_, MPP_PORT_OUTPUT, &output_task);
    }

    MppFrame output_frame = nullptr;
    if (ret == MPP_OK && output_task) {
        ret = mpp_task_meta_get_frame(output_task, KEY_OUTPUT_FRAME, &output_frame);
    }
    if (output_task) {
        MPP_RET enqueue_ret = mpp_api_->enqueue(mpp_ctx_, MPP_PORT_OUTPUT, output_task);
        if (ret == MPP_OK) {
            ret = enqueue_ret;
        }
    }

    // Reclaim the submitted packet after MPP finishes with the reusable input buffer.
    MppPacket returned_packet = nullptr;
    MppTask returned_input_task = nullptr;
    MPP_RET input_ret = mpp_api_->dequeue(mpp_ctx_, MPP_PORT_INPUT, &returned_input_task);
    if (input_ret == MPP_OK && returned_input_task) {
        input_ret = mpp_task_meta_get_packet(returned_input_task, KEY_INPUT_PACKET, &returned_packet);
        if (returned_packet) {
            mpp_packet_deinit(&returned_packet);
        }
        MPP_RET enqueue_ret = mpp_api_->enqueue(mpp_ctx_, MPP_PORT_INPUT, returned_input_task);
        if (input_ret == MPP_OK) {
            input_ret = enqueue_ret;
        }
    }

    if (ret != MPP_OK || input_ret != MPP_OK || output_frame == nullptr) {
        spdlog::warn("PreprocessingThread: MPP MJPEG task decode failed: output_ret={}, input_ret={}",
                     static_cast<int>(ret), static_cast<int>(input_ret));
        return false;
    }

    return true;
}

void PreprocessingThread::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&PreprocessingThread::thread_func, this);
    }
}

void PreprocessingThread::stop() {
    running_ = false;
    cv_npu_input_.notify_all();
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

bool PreprocessingThread::get_latest_frame(cv::Mat& frame) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (output_queue_.empty()) {
        return false;
    }
    // 拷贝最新的原始帧，供人脸注册/UI 快照使用
    frame = output_queue_.back().orig_img.clone();
    return true;
}

void PreprocessingThread::wake_consumer() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wakeup_ = true;
    }
    cv_output_.notify_all();
}

void PreprocessingThread::begin_inference_pipeline() {
    std::lock_guard<std::mutex> input_lock(npu_mem_mutex_);
    std::lock_guard<std::mutex> output_lock(mutex_);
    while (!output_queue_.empty()) {
        output_queue_.pop();
    }
    inference_pipeline_active_ = true;
    npu_input_pending_ = false;
}

void PreprocessingThread::end_inference_pipeline() {
    {
        std::lock_guard<std::mutex> lock(npu_mem_mutex_);
        inference_pipeline_active_ = false;
        npu_input_pending_ = false;
    }
    cv_npu_input_.notify_all();
}

void PreprocessingThread::complete_npu_inference() {
    {
        std::lock_guard<std::mutex> lock(npu_mem_mutex_);
        npu_input_pending_ = false;
    }
    cv_npu_input_.notify_one();
}

size_t PreprocessingThread::output_queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return output_queue_.size();
}

std::string PreprocessingThread::get_camera_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return camera_error_;
}

bool PreprocessingThread::process_nv12_frame(int dma_fd,
                                             void* virtual_addr,
                                             int frame_width,
                                             int frame_height,
                                             int horizontal_stride,
                                             int vertical_stride,
                                             PreprocessTask& task,
                                             PerformanceMonitor::PreprocessTimings& timings) {
    bool preprocess_ok = false;

    auto t_wait_start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(npu_mem_mutex_);
    cv_npu_input_.wait(lock, [this] {
        return !inference_pipeline_active_ || !npu_input_pending_ || !running_;
    });
    auto t_wait_end = std::chrono::steady_clock::now();
    timings.npu_input_wait_ms = std::chrono::duration_cast<std::chrono::microseconds>(
        t_wait_end - t_wait_start).count() / 1000.0;
    if (!running_) {
        return false;
    }

    if (!inference_pipeline_active_) {
        // ========== 优化：推理流水线未激活（纯预览模式），无需写入 NPU 零拷贝内存 ==========
        if (Config::Performance::USE_RGA && dma_fd >= 0) {
            rga_buffer_t src_buf = wrapbuffer_fd(dma_fd, frame_width, frame_height,
                                                 RK_FORMAT_YCbCr_420_SP,
                                                 horizontal_stride, vertical_stride);
            task.orig_img = cv::Mat(frame_height, frame_width, CV_8UC3);
            rga_buffer_t dst_orig = wrapbuffer_virtualaddr(task.orig_img.data,
                                                           frame_width, frame_height,
                                                           RK_FORMAT_BGR_888);
            im_rect src_rect = {0, 0, frame_width, frame_height};
            rga_buffer_t pat_buf = {};
            im_rect pat_rect = {0, 0, 0, 0};
            
            auto t_preview_start = std::chrono::steady_clock::now();
            IM_STATUS preview_status = improcess(src_buf, dst_orig, pat_buf,
                                                 src_rect, src_rect, pat_rect,
                                                 IM_HAL_TRANSFORM_FLIP_H);
            auto t_preview_end = std::chrono::steady_clock::now();
            timings.preview_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                t_preview_end - t_preview_start).count() / 1000.0;
            preprocess_ok = (preview_status == IM_STATUS_SUCCESS);
        }

        if (!preprocess_ok && virtual_addr) {
            auto t_fallback_start = std::chrono::steady_clock::now();
            cv::Mat yuv_frame(vertical_stride + vertical_stride / 2,
                              horizontal_stride,
                              CV_8UC1,
                              virtual_addr);
            cv::Mat decoded_with_stride;
            cv::cvtColor(yuv_frame, decoded_with_stride, cv::COLOR_YUV2BGR_NV12);
            cv::Mat decoded_frame = decoded_with_stride(
                cv::Rect(0, 0, frame_width, frame_height));
            task.orig_img = cv::Mat(frame_height, frame_width, CV_8UC3);
            cv::flip(decoded_frame, task.orig_img, 1);
            
            auto t_fallback_end = std::chrono::steady_clock::now();
            timings.cpu_fallback_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                t_fallback_end - t_fallback_start).count() / 1000.0;
            timings.used_cpu_fallback = true;
            preprocess_ok = true;
        }
    } else {
        // ========== 推理流水线激活时（进行人脸识别）：必须将 resized 图像写入 NPU 零拷贝内存 ==========
        if (Config::Performance::USE_RGA && npu_input_mem_ && dma_fd >= 0) {
            rga_buffer_t src_buf = wrapbuffer_fd(dma_fd, frame_width, frame_height,
                                                 RK_FORMAT_YCbCr_420_SP,
                                                 horizontal_stride, vertical_stride);
            rga_buffer_t dst_buf = wrapbuffer_fd(npu_input_mem_->fd, target_w_, target_h_,
                                                 RK_FORMAT_BGR_888);

            auto t_input_start = std::chrono::steady_clock::now();
            im_rect whole_rect = {0, 0, target_w_, target_h_};
            imfill(dst_buf, whole_rect, 0x00000000);

            im_rect src_rect = {0, 0, frame_width, frame_height};
            im_rect dst_rect = {pad_left_, pad_top_, resize_w_, resize_h_};
            im_rect pat_rect = {0, 0, 0, 0};
            rga_buffer_t pat_buf = {};

            IM_STATUS resize_status = improcess(src_buf, dst_buf, pat_buf,
                                                src_rect, dst_rect, pat_rect,
                                                IM_HAL_TRANSFORM_FLIP_H);
            auto t_input_end = std::chrono::steady_clock::now();
            timings.rga_input_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                t_input_end - t_input_start).count() / 1000.0;
            if (resize_status != IM_STATUS_SUCCESS) {
                spdlog::error("PreprocessingThread: RGA fd-to-NPU zero-copy failed: STATUS={}",
                              static_cast<int>(resize_status));
            } else {
                task.orig_img = cv::Mat(frame_height, frame_width, CV_8UC3);
                rga_buffer_t dst_orig = wrapbuffer_virtualaddr(task.orig_img.data,
                                                               frame_width, frame_height,
                                                               RK_FORMAT_BGR_888);
                auto t_preview_start = std::chrono::steady_clock::now();
                IM_STATUS preview_status = improcess(src_buf, dst_orig, pat_buf,
                                                     src_rect, src_rect, pat_rect,
                                                     IM_HAL_TRANSFORM_FLIP_H);
                auto t_preview_end = std::chrono::steady_clock::now();
                timings.preview_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                    t_preview_end - t_preview_start).count() / 1000.0;
                preprocess_ok = preview_status == IM_STATUS_SUCCESS;
                if (!preprocess_ok) {
                    spdlog::error("PreprocessingThread: RGA preview conversion failed: STATUS={}",
                                  static_cast<int>(preview_status));
                }
            }
        }

        if (!preprocess_ok && npu_input_mem_ && virtual_addr) {
            auto t_fallback_start = std::chrono::steady_clock::now();
            cv::Mat yuv_frame(vertical_stride + vertical_stride / 2,
                              horizontal_stride,
                              CV_8UC1,
                              virtual_addr);
            cv::Mat decoded_with_stride;
            cv::cvtColor(yuv_frame, decoded_with_stride, cv::COLOR_YUV2BGR_NV12);
            cv::Mat decoded_frame = decoded_with_stride(
                cv::Rect(0, 0, frame_width, frame_height));

            task.orig_img = cv::Mat(frame_height, frame_width, CV_8UC3);
            cv::flip(decoded_frame, task.orig_img, 1);

            cv::Mat npu_input(target_h_, target_w_, CV_8UC3, npu_input_mem_->virt_addr);
            npu_input.setTo(cv::Scalar(0, 0, 0));
            cv::Mat resized_part = npu_input(cv::Rect(pad_left_, pad_top_, resize_w_, resize_h_));
            cv::resize(task.orig_img, resized_part, cv::Size(resize_w_, resize_h_),
                       0, 0, cv::INTER_LINEAR);
            auto t_fallback_end = std::chrono::steady_clock::now();
            timings.cpu_fallback_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                t_fallback_end - t_fallback_start).count() / 1000.0;
            timings.used_cpu_fallback = true;
            preprocess_ok = true;
        }
    }

    if (!preprocess_ok) {
        spdlog::error("PreprocessingThread: NPU input memory is not registered or NV12 preprocessing failed.");
    } else {
        npu_input_pending_ = inference_pipeline_active_;
        std::lock_guard<std::mutex> output_lock(mutex_);
        while (output_queue_.size() >= MAX_QUEUE_SIZE) {
            output_queue_.pop();
        }
        output_queue_.push(std::move(task));
    }

    return preprocess_ok;
}

void PreprocessingThread::thread_func() {
    spdlog::info("PreprocessingThread: integrated {} capture and RGA preprocess loop started",
                 camera_type_);

    while (running_) {
        CameraFrame camera_frame;

        if (!read_camera_frame(&camera_frame)) {
            if (!running_) {
                break;
            }
            if (has_camera_error()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    camera_error_ = get_camera_error();
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

        PerformanceMonitor::PreprocessTimings timings;
        PreprocessTask task;
        gettimeofday(&task.timestamp, NULL);
        bool preprocess_ok = false;

        if (camera_frame.is_usb_mjpeg_packet()) {
            timings.mjpeg_bytes = camera_frame.raw_size;

            auto t_dec_start = std::chrono::steady_clock::now();
            if (!mpp_initialized_ ||
                !decode_mjpeg_packet(camera_frame.raw_data,
                                     camera_frame.raw_size,
                                     timings.mpp_input_copy_ms)) {
                release_camera_frame(camera_frame);
                continue;
            }

            MppFrame frame = mpp_output_frame_;
            if (mpp_frame_get_errinfo(frame)) {
                spdlog::warn("PreprocessingThread: MPP decoded frame has error!");
                release_camera_frame(camera_frame);
                continue;
            }

            MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
            int decoded_fd = mpp_buffer_get_fd(mpp_buf);
            int decoded_width = mpp_frame_get_width(frame);
            int decoded_height = mpp_frame_get_height(frame);
            int hor_stride = mpp_frame_get_hor_stride(frame);
            int ver_stride = mpp_frame_get_ver_stride(frame);
            void* decoded_ptr = mpp_buffer_get_ptr(mpp_buf);

            auto t_dec_end = std::chrono::steady_clock::now();
            double decode_total_ms = std::chrono::duration_cast<std::chrono::microseconds>(
                t_dec_end - t_dec_start).count() / 1000.0;
            timings.mpp_decode_ms = std::max(0.0, decode_total_ms - timings.mpp_input_copy_ms);

            preprocess_ok = process_nv12_frame(decoded_fd,
                                               decoded_ptr,
                                               decoded_width,
                                               decoded_height,
                                               hor_stride,
                                               ver_stride,
                                               task,
                                               timings);
        } else if (camera_frame.is_mipi_nv12_dma()) {
            const MipiDmaFrame& mipi = camera_frame.mipi;
            preprocess_ok = process_nv12_frame(mipi.dma_fd,
                                               mipi.mapped_data,
                                               mipi.width,
                                               mipi.height,
                                               mipi.horizontal_stride,
                                               mipi.vertical_stride,
                                               task,
                                               timings);
        } else {
            spdlog::warn("PreprocessingThread: unsupported camera frame type.");
        }

        // RGA has completed by this point; the V4L2 buffer can return to hardware.
        release_camera_frame(camera_frame);

        if (!running_) {
            break;
        }

        if (!preprocess_ok) {
            continue;
        }
        if (perf_monitor_) {
            perf_monitor_->record_preprocess_timings(timings);
        }
        cv_output_.notify_one();
    }
}

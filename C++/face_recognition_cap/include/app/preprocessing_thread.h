/**
 * @file preprocessing_thread.h
 * @brief 采集预处理线程 - MJPEG采集 + MPP硬解 + NPU输入准备
 * @author CL
 * @date 2025-11-20
 * 
 * 多线程优化架构：
 * - 线程1(本类): V4L2采集 + MPP硬解 + RGA/CPU降级写入NPU输入
 * - 线程2(主线程): YOLO零拷贝检测 → 识别队列
 * - 线程3: 对齐 + FaceNet + 匹配 + 渲染
 */

#ifndef _PREPROCESSING_THREAD_H_
#define _PREPROCESSING_THREAD_H_

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <sys/time.h>
#include <string>
#include "config/config.h"
#include "app/performance_monitor.h"
#include "rk_mpi.h"
#include "mpp_frame.h"
#include "rknn_api.h"

/*-------------------------------------------
    预处理任务结构
-------------------------------------------*/
struct PreprocessTask {
    cv::Mat orig_img;           // 原始图像（翻转后）
    struct timeval timestamp;   // 时间戳
};

/*-------------------------------------------
    采集预处理线程类
    职责: MJPEG采集、MPP硬解、图像转换及写入绑定的NPU输入内存
-------------------------------------------*/
class PreprocessingThread {
public:
    /**
     * @brief 构造函数
     * @param resize_w 缩放目标宽度
     * @param resize_h 缩放目标高度
     * @param img_width 摄像头图像宽度
     * @param img_height 摄像头图像高度
     * @param camera_type 摄像头类型 "usb" 或 "mipi"
     */
    PreprocessingThread(int resize_w, int resize_h, int img_width, int img_height,
                                         PerformanceMonitor* perf_monitor,
                                         const std::string& camera_type);
    ~PreprocessingThread();

    // 启动/停止线程
    void start();
    void stop();

    // 获取处理结果（非阻塞）
    bool get_result(PreprocessTask& task);

    // 获取最新一帧用于 UI 快照/人脸注册
    bool get_latest_frame(cv::Mat& frame);

    // 获取队列状态
    bool is_running() const { return running_; }
    size_t output_queue_size() const;
    bool has_camera_failed() const { return camera_failed_.load(std::memory_order_acquire); }
    std::string get_camera_error() const;

    // 唤醒阻塞在 get_result() 的消费者（用于外部停止信号）
    void wake_consumer();

    // 注册 NPU Zero-Copy 输入内存与互斥锁
    void register_npu_input_mem(rknn_tensor_mem* input_mem) {
        npu_input_mem_ = input_mem;
    }
    void begin_inference_pipeline();
    void end_inference_pipeline();
    void complete_npu_inference();
    std::mutex& get_npu_mem_mutex() { return npu_mem_mutex_; }

private:
    // 线程函数（采集 + 预处理循环）
    void thread_func();

private:
    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_;

    // 输出队列
    mutable std::mutex mutex_;
    std::condition_variable cv_output_;
    bool wakeup_ = false;            // wake_consumer() 一次性唤醒标志
    std::queue<PreprocessTask> output_queue_;
    std::string camera_error_;

    // 配置参数
    int resize_w_;
    int resize_h_;
    int img_width_;
    int img_height_;
    std::string camera_type_;
    PerformanceMonitor* perf_monitor_;

    std::atomic<bool> camera_failed_{false};

    // padding 目标尺寸与边界
    int target_w_;
    int target_h_;
    int pad_top_;
    int pad_left_;

    // MPP 硬件解码器及状态
    MppCtx mpp_ctx_ = nullptr;
    MppApi* mpp_api_ = nullptr;
    MppBufferGroup mpp_frm_grp_ = nullptr;
    MppBuffer mpp_input_buffer_ = nullptr;
    size_t mpp_input_capacity_ = 0;
    MppBuffer mpp_output_buffer_ = nullptr;
    MppFrame mpp_output_frame_ = nullptr;
    bool mpp_initialized_ = false;

    int init_mpp();
    void deinit_mpp();
    bool decode_mjpeg_packet(void* packet_data, uint32_t packet_size, double& input_copy_ms);
    bool process_nv12_frame(int dma_fd,
                            void* virtual_addr,
                            int frame_width,
                            int frame_height,
                            int horizontal_stride,
                            int vertical_stride,
                            PreprocessTask& task,
                            PerformanceMonitor::PreprocessTimings& timings);

    // NPU 零拷贝输入内存和互斥锁
    rknn_tensor_mem* npu_input_mem_ = nullptr;
    std::mutex npu_mem_mutex_;
    std::condition_variable cv_npu_input_;
    bool inference_pipeline_active_ = false;
    bool npu_input_pending_ = false;

    // 队列大小限制（只保留最新帧）
    static const int MAX_QUEUE_SIZE = Config::Performance::QUEUE_MAX_SIZE;
};

#endif // _PREPROCESSING_THREAD_H_

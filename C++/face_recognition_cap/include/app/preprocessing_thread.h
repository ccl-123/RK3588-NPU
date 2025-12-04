/**
 * @file preprocessing_thread.h
 * @brief 采集预处理线程 - 摄像头采集 + RGA硬件加速预处理
 * @author CL
 * @date 2025-11-20
 * 
 * 多线程优化架构：
 * - 线程1(本类): 采集 + RGA 预处理 → 检测队列
 * - 线程2(主线程): YOLO 检测 → 识别队列
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
#include <sys/time.h>
#include <string>
#include "config/config.h"

/*-------------------------------------------
    预处理任务结构
-------------------------------------------*/
struct PreprocessTask {
    cv::Mat orig_img;           // 原始图像（翻转后）
    cv::Mat processed_img;      // 处理后的图像（缩放后）
    struct timeval timestamp;   // 时间戳
};

/*-------------------------------------------
    采集预处理线程类
    职责: 摄像头采集 + RGA硬件加速的图像翻转和缩放
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
     * @param use_async_usb 是否使用异步USB读取
     */
    PreprocessingThread(int resize_w, int resize_h, int img_width, int img_height,
                        const std::string& camera_type = "usb", bool use_async_usb = true);
    ~PreprocessingThread();

    // 启动/停止线程
    void start();
    void stop();

    // 获取处理结果（非阻塞）
    bool get_result(PreprocessTask& task);

    // 获取队列状态
    bool is_running() const { return running_; }
    size_t output_queue_size() const;

private:
    // 线程函数（采集 + 预处理循环）
    void thread_func();

    // 从摄像头读取一帧
    bool read_frame(cv::Mat& frame);

    // RGA处理
    void process_with_rga(PreprocessTask& task);

private:
    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_;

    // 输出队列
    mutable std::mutex mutex_;
    std::queue<PreprocessTask> output_queue_;

    // 配置参数
    int resize_w_;
    int resize_h_;
    int img_width_;
    int img_height_;
    std::string camera_type_;
    bool use_async_usb_;

    // 静态缓冲区（避免重复分配）
    cv::Mat flipped_buffer_;

    // 队列大小限制（只保留最新帧）
    static const int MAX_QUEUE_SIZE = Config::Performance::QUEUE_MAX_SIZE;
};

#endif // _PREPROCESSING_THREAD_H_

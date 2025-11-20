/**
 * @file preprocessing_thread.h
 * @brief 预处理线程 - 使用RGA硬件加速进行图像预处理
 * @author Augment Agent
 * @date 2025-11-20
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

/*-------------------------------------------
    预处理任务结构
-------------------------------------------*/
struct PreprocessTask {
    cv::Mat orig_img;           // 原始图像
    cv::Mat processed_img;      // 处理后的图像
    struct timeval timestamp;   // 时间戳
};

/*-------------------------------------------
    预处理线程类
    职责: RGA硬件加速的图像翻转和缩放
-------------------------------------------*/
class PreprocessingThread {
public:
    PreprocessingThread(int resize_w, int resize_h, int img_width, int img_height);
    ~PreprocessingThread();

    // 启动/停止线程
    void start();
    void stop();

    // 提交预处理任务
    bool submit_task(const cv::Mat& orig_img, struct timeval timestamp);

    // 获取处理结果
    bool get_result(PreprocessTask& task);

    // 获取队列状态
    bool is_running() const { return running_; }
    size_t input_queue_size() const;
    size_t output_queue_size() const;

private:
    // 线程函数
    void thread_func();

    // RGA处理
    void process_with_rga(PreprocessTask& task);

private:
    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_;

    // 任务队列
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<PreprocessTask> input_queue_;
    std::queue<PreprocessTask> output_queue_;

    // 配置参数
    int resize_w_;
    int resize_h_;
    int img_width_;
    int img_height_;

    // 静态缓冲区 (避免重复分配)
    cv::Mat flipped_buffer_;

    // 队列大小限制
    static const int MAX_QUEUE_SIZE = 2;
};

#endif // _PREPROCESSING_THREAD_H_


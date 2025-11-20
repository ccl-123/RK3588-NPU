/**
 * @file rendering_thread.h
 * @brief 渲染线程 - 异步显示图像，避免阻塞主线程
 * @author CL
 * @date 2025-11-20
 */

#ifndef _RENDERING_THREAD_H_
#define _RENDERING_THREAD_H_

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <string>

/*-------------------------------------------
    渲染任务结构
-------------------------------------------*/
struct RenderTask {
    cv::Mat img;            // 要显示的图像
    std::string fps_text;   // FPS文本 (可选)
};

/*-------------------------------------------
    渲染线程类
    职责: 异步显示图像,避免阻塞主线程
-------------------------------------------*/
class RenderingThread {
public:
    RenderingThread(const std::string& window_name = "Image Window");
    ~RenderingThread();

    // 启动/停止线程
    void start();
    void stop();

    // 提交渲染任务
    bool submit_task(const cv::Mat& img, const std::string& fps_text = "");

    // 获取队列状态
    bool is_running() const { return running_; }
    size_t queue_size() const;

private:
    // 线程函数
    void thread_func();

private:
    // 线程控制
    std::thread thread_;
    std::atomic<bool> running_;

    // 任务队列
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<RenderTask> queue_;

    // 窗口名称
    std::string window_name_;

    // 队列大小限制
    static const int MAX_QUEUE_SIZE = 2;
};

#endif // _RENDERING_THREAD_H_


/**
 * @file postprocess_thread.h
 * @brief YOLO 后处理线程 - 负责 DFL/NMS 并将结果推送到识别线程
 */

#ifndef _POSTPROCESS_THREAD_H_
#define _POSTPROCESS_THREAD_H_

#include <array>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <sys/time.h>
#include <opencv2/opencv.hpp>

#include "config/config.h"
#include "core/yolov8_face.h"
#include "core/postprocess.h"
#include "app/model_manager.h"
#include "app/recognition_thread.h"
#include "app/performance_monitor.h"

/*-------------------------------------------
    后处理任务结构
-------------------------------------------*/
struct PostprocessTask {
    cv::Mat orig_img;                                             // 翻转后的原图
    std::array<std::vector<uint8_t>, YOLOV8_FACE_OUTPUT_NUM> yolo_outputs; // YOLO原始输出拷贝
    int img_width;                                                // padding 后的宽
    int img_height;                                               // padding 后的高
    struct timeval timestamp;                                     // 时间戳
    float current_fps;                                            // 当前FPS
    float frame_time;                                             // 检测耗时
};

class PostprocessThread {
public:
    PostprocessThread(ModelManager* model_manager,
                      RecognitionThread* recognition_thread,
                      PerformanceMonitor* perf_monitor,
                      float box_conf_threshold,
                      float nms_threshold);
    ~PostprocessThread();

    void start();
    void stop();

    // 提交任务（丢弃旧帧，只保留最新）
    bool submit_task(PostprocessTask&& task);
    size_t queue_size() const;

private:
    void thread_func();

private:
    std::thread thread_;
    std::atomic<bool> running_;

    mutable std::mutex mutex_;
    std::queue<PostprocessTask> queue_;

    ModelManager* model_manager_;
    RecognitionThread* recognition_thread_;
    PerformanceMonitor* perf_monitor_;
    std::atomic<float> box_conf_threshold_;
    std::atomic<float> nms_threshold_;

    // 队列限长，防止堆积
    static const int MAX_QUEUE_SIZE = 2;
};

#endif // _POSTPROCESS_THREAD_H_


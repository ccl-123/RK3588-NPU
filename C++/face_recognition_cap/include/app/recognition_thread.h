/**
 * @file recognition_thread.h
 * @brief 识别线程 - 人脸对齐、特征提取、匹配、渲染
 * @author CL
 * @date 2025-12-04
 * 
 * 多线程优化架构：
 * - 线程1: 采集 + RGA 预处理 → 检测队列
 * - 线程2(主线程): YOLO 检测 → 识别队列
 * - 线程3(本类): 对齐 + FaceNet + 匹配 + 渲染
 */

#ifndef _RECOGNITION_THREAD_H_
#define _RECOGNITION_THREAD_H_

#include <opencv2/opencv.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <functional>
#include <chrono>
#include <sys/time.h>
#include <array>

#include "config/config.h"
#include "core/postprocess.h"
#include "app/model_manager.h"
#include "app/feature_library.h"
#include "app/performance_monitor.h"

/*-------------------------------------------
    识别结果结构
-------------------------------------------*/
struct RecognitionResultData {
    int user_id;
    std::string user_name;
    float similarity;
    cv::Mat face_image;
    cv::Rect face_box;
    std::chrono::system_clock::time_point timestamp;
    
    RecognitionResultData() : user_id(0), similarity(0.0f) {}
};

/*-------------------------------------------
    识别任务结构
-------------------------------------------*/
struct RecognitionTask {
    cv::Mat orig_img;                    // 原始图像（翻转后）
    detect_result_group_t detect_result; // YOLO 检测结果
    struct timeval timestamp;            // 时间戳
    float current_fps;                   // 当前 FPS（用于显示）
    float frame_time;                    // 帧耗时（用于显示）
};

/*-------------------------------------------
    注册预览数据结构
-------------------------------------------*/
struct RegistrationSample {
    cv::Rect face_box;
    std::array<cv::Point2f, 5> landmarks;
    std::vector<float> feature;
    float score;
};

/*-------------------------------------------
    运行模式
-------------------------------------------*/
enum class RecognitionMode {
    Recognition,
    Registration
};

/*-------------------------------------------
    回调函数类型
-------------------------------------------*/
using RecognitionCallbackFunc = std::function<void(const RecognitionResultData&)>;
using FrameCallbackFunc = std::function<void(const cv::Mat&, const std::vector<RecognitionResultData>&)>;
using RegistrationCallbackFunc = std::function<void(const cv::Mat&, const std::vector<RegistrationSample>&)>;

/*-------------------------------------------
    识别线程类
-------------------------------------------*/
class RecognitionThread {
public:
    RecognitionThread(ModelManager* model_manager,
                      FeatureLibrary* feature_library,
                      const cv::Mat& dst_landmark,
                      float facenet_threshold,
                      PerformanceMonitor* perf_monitor = nullptr);
    ~RecognitionThread();

    void start();
    void stop();

    // 提交识别任务（丢弃旧帧）
    bool submit_task(const RecognitionTask& task);

    // 设置回调
    void set_recognition_callback(RecognitionCallbackFunc callback);
    void set_frame_callback(FrameCallbackFunc callback);
    void set_registration_callback(RegistrationCallbackFunc callback);
    void set_mode(RecognitionMode mode);
    void set_threshold(float threshold);

    // 获取性能数据
    float get_avg_align_time() const { return avg_align_time_; }
    float get_avg_facenet_time() const { return avg_facenet_time_; }
    float get_avg_match_time() const { return avg_match_time_; }
    
    // 获取检测精度统计（调用后重置）
    void get_recognition_stats(int& faces_detected, int& faces_recognized);

    bool is_running() const { return running_; }

private:
    void thread_func();
    void process_task(RecognitionTask& task);

private:
    std::thread thread_;
    std::atomic<bool> running_;

    std::mutex mutex_;
    std::condition_variable cv_;           // 条件变量，避免忙等待
    std::queue<RecognitionTask> queue_;
    std::mutex callback_mutex_;            // 回调读写互斥，避免跨线程竞态

    ModelManager* model_manager_;
    FeatureLibrary* feature_library_;
    cv::Mat dst_landmark_;
    std::atomic<float> facenet_threshold_;
    PerformanceMonitor* perf_monitor_;

    RecognitionCallbackFunc recognition_callback_;
    FrameCallbackFunc frame_callback_;
    RegistrationCallbackFunc registration_callback_;
    std::atomic<RecognitionMode> mode_;

    std::atomic<float> avg_align_time_;
    std::atomic<float> avg_facenet_time_;
    std::atomic<float> avg_match_time_;

    // 检测精度统计
    std::atomic<int> stat_faces_detected_;
    std::atomic<int> stat_faces_recognized_;

    static const int MAX_QUEUE_SIZE = Config::Performance::QUEUE_MAX_SIZE;
};

#endif // _RECOGNITION_THREAD_H_

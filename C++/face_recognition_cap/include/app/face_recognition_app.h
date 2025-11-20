/**
 * @file face_recognition_app.h
 * @brief 人脸识别应用主类 - 封装整个应用的流程控制(支持回调)
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _FACE_RECOGNITION_APP_H_
#define _FACE_RECOGNITION_APP_H_

#include <string>
#include <functional>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "core/postprocess.h"
#include "app/model_manager.h"
#include "app/feature_library.h"
#include "app/preprocessing_thread.h"
#include "app/rendering_thread.h"
#include "app/performance_monitor.h"

/**
 * @brief 识别结果结构(新增)
 */
struct RecognitionResult {
    int user_id;                                        // 用户ID
    std::string user_name;                              // 用户姓名
    float similarity;                                   // 相似度
    cv::Mat face_image;                                 // 人脸图像
    cv::Rect face_box;                                  // 人脸框
    std::chrono::system_clock::time_point timestamp;    // 时间戳

    RecognitionResult() : user_id(0), similarity(0.0f) {}
};

/**
 * @brief 应用配置结构
 */
struct AppConfig {
    std::string retinaface_model_path;  // RetinaFace 模型路径
    std::string facenet_model_path;     // FaceNet 模型路径
    std::string camera_type;            // 摄像头类型: "usb" 或 "mipi"
    std::string device_number;          // 设备编号
    std::string feature_lib_path;       // 特征库路径
    std::string database_path;          // 数据库路径(新增)
    bool use_database;                  // 是否使用数据库(新增)
    int camera_width;                   // 摄像头宽度
    int camera_height;                  // 摄像头高度
    float box_conf_threshold;           // 人脸检测置信度阈值
    float nms_threshold;                // NMS 阈值
    float facenet_threshold;            // 人脸识别阈值
    bool use_async_usb;                 // 是否使用异步USB读取
    int perf_report_interval;           // 性能报告间隔(帧数)

    AppConfig()
        : camera_width(1280)
        , camera_height(720)
        , box_conf_threshold(0.7f)
        , nms_threshold(0.6f)
        , facenet_threshold(0.5f)
        , use_async_usb(true)
        , perf_report_interval(10)
        , feature_lib_path("./data/face_feature_lib/")
        , database_path("./data/database/face_recognition.db")
        , use_database(false)
    {}
};

/**
 * @brief 识别结果回调函数类型(新增)
 */
using RecognitionCallback = std::function<void(const RecognitionResult&)>;

/**
 * @brief 人脸识别应用主类
 *
 * 职责:
 * - 初始化所有模块 (模型、摄像头、线程等)
 * - 控制主循环流程
 * - 协调各模块协同工作
 * - 资源清理和释放
 * - 支持识别结果回调(新增)
 */
class FaceRecognitionApp {
public:
    FaceRecognitionApp();
    ~FaceRecognitionApp();

    /**
     * @brief 初始化应用
     * @param config 应用配置
     * @return 0 成功, -1 失败
     */
    int initialize(const AppConfig& config);

    /**
     * @brief 运行主循环
     * @return 0 正常退出, -1 错误退出
     */
    int run();

    /**
     * @brief 停止应用
     */
    void stop();

    /**
     * @brief 设置识别结果回调函数(新增)
     * @param callback 回调函数
     */
    void set_recognition_callback(RecognitionCallback callback);

    /**
     * @brief 获取特征库引用(新增)
     */
    FeatureLibrary& get_feature_library() { return feature_library_; }

private:
    /**
     * @brief 初始化摄像头
     */
    int init_camera();

    /**
     * @brief 处理单帧图像
     */
    void process_frame();

    /**
     * @brief 人脸检测
     */
    void detect_faces(const cv::Mat& img, detect_result_group_t& result_group);

    /**
     * @brief 人脸识别和匹配
     */
    void recognize_and_match(const cv::Mat& orig_img, 
                            const detect_result_group_t& result_group,
                            cv::Mat& render_img);

    /**
     * @brief 清理资源
     */
    void cleanup();

    /**
     * @brief 时间转换工具
     */
    static double get_us(struct timeval t) { 
        return (t.tv_sec * 1000000 + t.tv_usec); 
    }

private:
    // 配置
    AppConfig config_;

    // 模块实例
    ModelManager model_manager_;
    FeatureLibrary feature_library_;
    PreprocessingThread* preprocess_thread_;
    RenderingThread* render_thread_;
    PerformanceMonitor perf_monitor_;

    // 回调函数(新增)
    RecognitionCallback recognition_callback_;

    // 人脸对齐目标点
    cv::Mat dst_landmark_;

    // 缩放参数
    float scale_w_;
    float scale_h_;
    int resize_w_;
    int resize_h_;
    int padding_;

    // 运行状态
    bool initialized_;
    bool running_;
};

#endif // _FACE_RECOGNITION_APP_H_


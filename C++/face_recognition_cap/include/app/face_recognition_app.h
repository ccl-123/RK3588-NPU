/**
 * @file face_recognition_app.h
 * @brief 人脸识别应用主类 - 封装整个应用的流程控制(支持回调)
 * @author CL
 * @date 2025-11-20
 */

#ifndef _FACE_RECOGNITION_APP_H_
#define _FACE_RECOGNITION_APP_H_

#include <string>
#include <functional>
#include <chrono>
#include <memory>
#include <opencv2/opencv.hpp>
#include "config/config.h"
#include "core/postprocess.h"
#include "app/model_manager.h"
#include "app/feature_library.h"
#include "app/preprocessing_thread.h"
#include "app/recognition_thread.h"
#include "app/performance_monitor.h"
#include "app/postprocess_thread.h"

// 前向声明
namespace service {
    class AttendanceService;
}

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
    std::string retinaface_model_path;  // 人脸检测模型路径 (YOLOv8-face，字段名保留兼容)
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
        : camera_width(Config::Camera::WIDTH)
        , camera_height(Config::Camera::HEIGHT)
        , box_conf_threshold(Config::Detection::BOX_CONF_THRESHOLD)
        , nms_threshold(Config::Detection::NMS_THRESHOLD)
        , facenet_threshold(Config::Default::RECOGNITION_THRESHOLD)  // UI 可配置
        , use_async_usb(Config::Camera::USE_ASYNC_USB)
        , perf_report_interval(Config::Performance::REPORT_INTERVAL)
        , feature_lib_path(Config::Path::FEATURE_LIB)
        , database_path(Config::Path::DATABASE)
        , use_database(false)
    {}
};

/**
 * @brief 识别结果回调函数类型(新增)
 */
using RecognitionCallback = std::function<void(const RecognitionResult&)>;

/**
 * @brief 帧回调函数类型(新增 - 用于GUI)
 * @param frame 渲染好的帧（包含人脸框、识别结果等）
 * @param results 识别结果列表
 */
using FrameCallback = std::function<void(const cv::Mat& frame, const std::vector<RecognitionResult>& results)>;

/**
 * @brief 人脸识别应用主类
 *
 * 流水线架构：
 * - 线程1: 采集 + RGA预处理
 * - 线程2: YOLO检测 (主线程)
 * - 线程3: 对齐 + FaceNet + 匹配 + 渲染
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
     * @brief 设置帧回调函数(新增 - 用于GUI)
     * @param callback 帧回调函数
     */
    void set_frame_callback(FrameCallback callback);

    /**
     * @brief 设置考勤服务(新增)
     * @param service 考勤服务指针（类型安全）
     */
    void set_attendance_service(service::AttendanceService* service);

    /**
     * @brief 获取特征库引用(新增)
     */
    FeatureLibrary& get_feature_library() { return feature_library_; }

    /**
     * @brief 动态更新识别阈值（用于设置页面）
     * @param threshold 新的识别阈值（0.0 - 1.0）
     */
    void set_recognition_threshold(float threshold);

    /**
     * @brief 获取当前识别阈值
     * @return 当前阈值
     */
    float get_recognition_threshold() const {
        return config_.facenet_threshold;
    }

    /**
     * @brief GUI 模式：获取当前帧（不阻塞）
     * @param frame 输出帧
     * @return true 成功获取, false 失败
     */
    bool get_current_frame(cv::Mat& frame);

    /**
     * @brief GUI 模式：处理单帧并返回结果
     * @param frame 输出处理后的帧
     * @param results 输出识别结果列表
     * @return true 成功处理, false 失败
     */
    bool process_single_frame(cv::Mat& frame, std::vector<RecognitionResult>& results);

    /**
     * @brief 检查是否正在运行
     */
    bool is_running() const { return running_; }

    // ==================== GUI 人脸注册接口 ====================

    /**
     * @brief 从原始帧中检测人脸（GUI 注册专用）
     * @param frame 输入帧（原始分辨率，如 1280x720）
     * @param face_boxes 输出人脸框列表（相对于原始帧的坐标）
     * @param landmarks 输出关键点列表（每个人脸 5 个关键点，相对于原始帧的坐标）
     * @return 检测到的人脸数量
     *
     * @note 此函数会自动处理图像缩放和 padding，返回的坐标已转换回原始帧坐标系
     * @note 用于 GUI 人脸注册时的人脸检测，不用于实时识别
     */
    int detect_faces(const cv::Mat& frame,
                    std::vector<cv::Rect>& face_boxes,
                    std::vector<std::vector<cv::Point2f>>& landmarks);

    /**
     * @brief 从对齐后的人脸图像提取特征（GUI 注册专用）
     * @param aligned_face 对齐后的人脸图像（112x112，RGB 格式）
     * @param feature 输出特征向量（512 维）
     * @return true 成功, false 失败
     *
     * @note 输入图像必须已经对齐到 112x112 并转换为 RGB 格式
     * @note 用于 GUI 人脸注册时的特征提取，不用于实时识别
     */
    bool extract_face_feature(const cv::Mat& aligned_face, std::vector<float>& feature);

    /**
     * @brief 从原始帧中提取人脸特征（GUI 注册专用，包含检测、对齐、提取）
     * @param frame 输入帧（原始分辨率，如 1280x720）
     * @param feature 输出特征向量（512 维）
     * @param face_box 输出人脸框（可选，相对于原始帧的坐标）
     * @return true 成功（检测到一个人脸）, false 失败
     *
     * @note 此函数是一站式接口，自动完成检测、对齐、特征提取
     * @note ⚠️ 使用与实时识别相同的对齐方法（similarTransform + warpPerspective）
     * @note ⚠️ 使用与实时识别相同的目标关键点（dst_landmark_）
     * @note ⚠️ 确保注册特征与实时识别特征一致，避免注册后无法识别的问题
     * @note 如果检测到多个人脸，只处理第一个
     * @note 用于 GUI 人脸注册的便捷接口，不用于实时识别
     */
    bool extract_feature_from_frame(const cv::Mat& frame,
                                   std::vector<float>& feature,
                                   cv::Rect* face_box = nullptr);

private:
    /**
     * @brief 初始化摄像头
     */
    int init_camera();

    /**
     * @brief 人脸检测（主线程调用）
     */
    void detect_faces(const cv::Mat& img, detect_result_group_t& result_group);

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
    std::unique_ptr<PreprocessingThread> preprocess_thread_;   // 智能指针管理
    std::unique_ptr<PostprocessThread> postprocess_thread_;    // YOLO后处理线程
    std::unique_ptr<RecognitionThread> recognition_thread_;     // 智能指针管理
    PerformanceMonitor perf_monitor_;

    // 回调函数(新增)
    RecognitionCallback recognition_callback_;
    FrameCallback frame_callback_;  // 帧回调函数(新增 - 用于GUI)

    // 考勤服务指针（类型安全）
    service::AttendanceService* attendance_service_;

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


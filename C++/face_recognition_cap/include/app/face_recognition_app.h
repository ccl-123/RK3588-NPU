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
    // 注意：以下接口专为 GUI 人脸注册功能设计，返回友好的数据结构
    // 实时识别线程使用 private 版本的 detect_faces() 和 recognize_and_match()

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
     * @brief 处理单帧图像（实时识别线程使用）
     */
    void process_frame();

    // ==================== 实时识别核心函数 ====================
    // 注意：以下函数用于实时识别线程，经过充分测试，稳定可靠
    // 使用 similarTransform + warpPerspective 进行人脸对齐
    // GUI 人脸注册使用 public 版本的接口

    /**
     * @brief 人脸检测（实时识别专用）
     * @param img 输入图像（已缩放到 resize_w_ x resize_h_）
     * @param result_group 输出检测结果（RKNN 原始格式）
     *
     * @note 此函数用于实时识别线程，输入图像已经过预处理
     * @note 使用 similarTransform + warpPerspective 进行人脸对齐
     * @note 不要在 GUI 注册功能中使用此函数
     */
    void detect_faces(const cv::Mat& img, detect_result_group_t& result_group);

    /**
     * @brief 人脸识别和匹配（实时识别专用）
     * @param orig_img 原始图像（未缩放）
     * @param result_group 检测结果
     * @param render_img 输出渲染图像（绘制人脸框和识别结果）
     *
     * @note 此函数用于实时识别线程，包含人脸对齐、特征提取、匹配、绘制
     * @note 使用 similarTransform + warpPerspective 进行人脸对齐
     * @note 不要在 GUI 注册功能中使用此函数
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


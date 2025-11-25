# API 文档

本文档详细介绍各模块的接口和使用方法。

## 目录

- [App 层 API](#app-层-api)
  - [FaceRecognitionApp](#facerecognitionapp)
  - [ModelManager](#modelmanager)
  - [FeatureLibrary](#featurelibrary)
  - [PreprocessingThread](#preprocessingthread)
  - [RenderingThread](#renderingthread)
  - [PerformanceMonitor](#performancemonitor)
- [Core 层 API](#core-层-api)
- [Hardware 层 API](#hardware-层-api)

---

## App 层 API

### FaceRecognitionApp

**文件**: `include/app/face_recognition_app.h`

人脸识别应用主类，封装整个应用的流程控制。

#### 配置结构

```cpp
struct AppConfig {
    std::string retinaface_model_path;  // RetinaFace 模型路径
    std::string facenet_model_path;     // FaceNet 模型路径
    std::string camera_type;            // 摄像头类型: "usb" 或 "mipi"
    std::string device_number;          // 设备编号
    std::string feature_lib_path;       // 特征库路径
    int camera_width;                   // 摄像头宽度 (默认 1280)
    int camera_height;                  // 摄像头高度 (默认 720)
    float box_conf_threshold;           // 人脸检测置信度阈值 (默认 0.7)
    float nms_threshold;                // NMS 阈值 (默认 0.6)
    float facenet_threshold;            // 人脸识别阈值 (默认 0.5)
    bool use_async_usb;                 // 是否使用异步USB读取 (默认 true)
    int perf_report_interval;           // 性能报告间隔帧数 (默认 10)
};
```

#### 主要方法

```cpp
class FaceRecognitionApp {
public:
    FaceRecognitionApp();
    ~FaceRecognitionApp();

    // 初始化应用
    int initialize(const AppConfig& config);

    // 运行主循环
    int run();

    // 停止应用
    void stop();
};
```

#### 使用示例

```cpp
#include "app/face_recognition_app.h"

int main(int argc, char** argv) {
    // 配置参数
    AppConfig config;
    config.retinaface_model_path = "data/model/retinaface.rknn";
    config.facenet_model_path = "data/model/w600k_mbf.rknn";
    config.camera_type = "usb";
    config.device_number = "21";

    // 创建应用
    FaceRecognitionApp app;

    // 初始化
    if (app.initialize(config) != 0) {
        return -1;
    }

    // 运行
    return app.run();
}
```

---

### ModelManager

**文件**: `include/app/model_manager.h`

模型管理器，负责 RKNN 模型的加载、配置和释放。

#### 主要方法

```cpp
class ModelManager {
public:
    ModelManager();
    ~ModelManager();

    // 初始化 RetinaFace 模型
    int init_retinaface(const char* model_path);

    // 初始化 FaceNet 模型
    int init_facenet(const char* model_path);

    // 获取模型上下文
    rknn_context* get_retinaface_ctx();
    rknn_context* get_facenet_ctx();

    // 获取模型尺寸
    void get_retinaface_size(int& width, int& height, int& channel) const;
    void get_facenet_size(int& width, int& height, int& channel) const;

    // 获取输入输出配置
    rknn_input* get_retinaface_inputs();
    rknn_output* get_retinaface_outputs();
    rknn_input* get_facenet_inputs();
    rknn_output* get_facenet_outputs();

    // 释放资源
    void release();
};
```

#### 使用示例

```cpp
ModelManager model_manager;

// 初始化模型
model_manager.init_retinaface("retinaface.rknn");
model_manager.init_facenet("facenet.rknn");

// 获取模型信息
int width, height, channel;
model_manager.get_retinaface_size(width, height, channel);

// 使用完毕后释放
model_manager.release();
```

---

### FeatureLibrary

**文件**: `include/app/feature_library.h`

人脸特征库管理器，负责特征库的加载和匹配。

#### 主要方法

```cpp
class FeatureLibrary {
public:
    FeatureLibrary();
    ~FeatureLibrary();

    // 从目录加载特征库
    int load_from_directory(const std::string& lib_path, int feature_dim = 512);

    // 匹配人脸特征
    bool match_feature(const float* feature, float threshold,
                      std::string& matched_name, float& max_score);

    // 获取特征库大小
    size_t size() const;
    bool empty() const;

    // 清空特征库
    void clear();

    // 获取所有人名列表
    const std::vector<std::string>& get_names() const;
};
```

#### 使用示例

```cpp
FeatureLibrary feature_lib;

// 加载特征库
int count = feature_lib.load_from_directory("./data/face_feature_lib/", 512);
std::cout << "Loaded " << count << " features" << std::endl;

// 匹配特征
float* query_feature = ...; // 待匹配的特征向量
std::string name;
float score;

if (feature_lib.match_feature(query_feature, 0.5, name, score)) {
    std::cout << "Matched: " << name << " (score: " << score << ")" << std::endl;
} else {
    std::cout << "No match found" << std::endl;
}
```

---

### PreprocessingThread

**文件**: `include/app/preprocessing_thread.h`

预处理线程，使用 RGA 硬件加速进行图像翻转和缩放。

#### 数据结构

```cpp
struct PreprocessTask {
    cv::Mat orig_img;        // 原始图像（翻转后）
    cv::Mat processed_img;   // 预处理后的图像
    struct timeval timestamp;
};
```

#### 主要方法

```cpp
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
    bool is_running() const;
    size_t input_queue_size() const;
    size_t output_queue_size() const;
};
```

#### 使用示例

```cpp
// 创建预处理线程
PreprocessingThread preprocess_thread(640, 640, 1280, 720);
preprocess_thread.start();

// 提交任务
cv::Mat frame = ...;
struct timeval timestamp;
gettimeofday(&timestamp, NULL);
preprocess_thread.submit_task(frame, timestamp);

// 获取结果
PreprocessTask result;
if (preprocess_thread.get_result(result)) {
    cv::Mat processed = result.processed_img;
    // 使用处理后的图像
}

// 停止线程
preprocess_thread.stop();
```

---

### RenderingThread

**文件**: `include/app/rendering_thread.h`

渲染线程，异步显示图像，避免阻塞主线程。

#### 主要方法

```cpp
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
    bool is_running() const;
    size_t queue_size() const;
};
```

#### 使用示例

```cpp
// 创建渲染线程
RenderingThread render_thread("Face Recognition");
render_thread.start();

// 提交渲染任务
cv::Mat display_img = ...;
render_thread.submit_task(display_img, "FPS: 60.0");

// 停止线程
render_thread.stop();
```

---

### PerformanceMonitor

**文件**: `include/app/performance_monitor.h`

性能监控器，统计和显示各阶段耗时。

#### 主要方法

```cpp
class PerformanceMonitor {
public:
    PerformanceMonitor(int report_interval = 10);
    ~PerformanceMonitor();

    // 记录各阶段耗时
    void record_camera_time(double ms);
    void record_preprocess_time(double ms);
    void record_detection_time(double ms);
    void record_alignment_time(double ms);
    void record_recognition_time(double ms);
    void record_matching_time(double ms);
    void record_render_time(double ms);

    // 更新 FPS
    void update_fps(double current_fps);
    double get_smoothed_fps() const;

    // 检查是否需要打印报告
    bool should_print_report();

    // 打印性能报告
    void print_report();

    // 重置统计
    void reset();
};
```

#### 使用示例

```cpp
PerformanceMonitor perf_monitor(10);  // 每10帧打印一次

// 记录各阶段耗时
perf_monitor.record_camera_time(5.2);
perf_monitor.record_detection_time(12.5);
perf_monitor.update_fps(60.0);

// 检查并打印报告
if (perf_monitor.should_print_report()) {
    perf_monitor.print_report();
}
```

---

## Core 层 API

### RetinaFace

**文件**: `include/core/retinaface.h`

人脸检测模型接口。

```cpp
// 创建 RetinaFace 模型
int create_retinaface(char* model_name, rknn_context* ctx,
                     int& width, int& height, int& channel,
                     std::vector<float>& out_scales,
                     std::vector<int32_t>& out_zps,
                     rknn_input_output_num& io_num,
                     unsigned char* model_data);

// 执行推理
int retinaface_inference(rknn_context* ctx, cv::Mat img,
                        int width, int height, int channel,
                        float box_conf_threshold, float nms_threshold,
                        int img_width, int img_height,
                        rknn_input_output_num io_num,
                        rknn_input* inputs, rknn_output* outputs,
                        std::vector<float> out_scales,
                        std::vector<int32_t> out_zps,
                        detect_result_group_t* detect_result_group);

// 释放模型
void release_retinaface(rknn_context* ctx, unsigned char* model_data);
```

### FaceNet

**文件**: `include/core/facenet.h`

人脸特征提取模型接口。

```cpp
// 创建 FaceNet 模型
int create_facenet(char* model_name, rknn_context* ctx,
                  int& width, int& height, int& channel,
                  rknn_input_output_num& io_num,
                  unsigned char* model_data);

// 执行推理
int facenet_inference(rknn_context* ctx, cv::Mat img,
                     rknn_input_output_num io_num,
                     rknn_input* inputs, rknn_output* outputs,
                     float** result);

// 释放输出
int facenet_output_release(rknn_context* ctx,
                          rknn_input_output_num io_num,
                          rknn_output* outputs);

// 释放模型
void release_facenet(rknn_context* ctx, unsigned char* model_data);
```

### PostProcess

**文件**: `include/core/postprocess.h`

后处理工具函数。

```cpp
// 人脸对齐变换
cv::Mat similarTransform(cv::Mat src, cv::Mat dst);

// L2 归一化
void l2_normalize(float* input);

// 余弦相似度
float cos_similarity(float* input1, float* input2);

// 欧氏距离
float compare_eu_distance(float* input1, float* input2);
```

---

## Hardware 层 API

### Camera Util

**文件**: `include/hardware/camera_util.h`

摄像头控制接口。

#### USB 摄像头（同步）

```cpp
// 加载 USB 摄像头
int load_usb_camera(std::string device, int camera_width, int camera_height);

// 读取帧
void read_usb_frame(cv::Mat* orig_img);

// 关闭摄像头
void close_usb_camera();
```

#### USB 摄像头（异步）

```cpp
// 加载 USB 摄像头（异步模式）
int load_usb_camera_async(std::string device, int camera_width, int camera_height);

// 启动采集线程
void start_usb_capture_thread();

// 停止采集线程
void stop_usb_capture_thread();

// 读取帧（异步）
void read_usb_frame_async(cv::Mat* orig_img);

// 关闭摄像头（异步）
void close_usb_camera_async();
```

#### MIPI 摄像头

```cpp
// 加载 MIPI 摄像头
int load_mipi_camera(std::string device, int camera_width, int camera_height);

// 读取帧
void read_mipi_frame(cv::Mat* orig_img);

// 关闭摄像头
void close_mipi_camera();
```

---

## 数据结构

### 检测结果

```cpp
typedef struct _BOX_RECT {
    int left;
    int right;
    int top;
    int bottom;
} BOX_RECT;

typedef struct _KEY_POINT {
    int point_1_x, point_1_y;  // 左眼
    int point_2_x, point_2_y;  // 右眼
    int point_3_x, point_3_y;  // 鼻子
    int point_4_x, point_4_y;  // 左嘴角
    int point_5_x, point_5_y;  // 右嘴角
} KEY_POINT;

typedef struct __detect_result_t {
    char name[OBJ_NAME_MAX_SIZE];
    BOX_RECT box;
    KEY_POINT point;
    float prop;  // 置信度
} detect_result_t;

typedef struct _detect_result_group_t {
    int id;
    int count;
    detect_result_t results[OBJ_NUMB_MAX_SIZE];
} detect_result_group_t;
```

---

## 常量定义

```cpp
#define NMS_THRESH 0.6           // NMS 阈值
#define BOX_THRESH 0.7           // 人脸检测置信度阈值
#define FACENET_THRESH 0.5       // 人脸识别阈值
#define FACENET_FEATURE_DIM 512  // 特征向量维度
```


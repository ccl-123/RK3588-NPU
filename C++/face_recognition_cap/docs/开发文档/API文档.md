# API 文档

本文档基于 C++17 源代码详细介绍系统各模块的接口和使用方法。

## 目录

- [App 层 API](#app-层-api)
  - [FaceRecognitionApp](#facerecognitionapp)
  - [ModelManager](#modelmanager)
  - [FeatureLibrary](#featurelibrary)
  - [PreprocessingThread](#preprocessingthread)
  - [PostprocessThread](#postprocessthread)
  - [RecognitionThread](#recognitionthread)
  - [PerformanceMonitor](#performancemonitor)
- [Core 层 API](#core-层-api)
- [Service 层 API](#service-层-api)
- [Hardware 层 API](#hardware-层-api)

---

## App 层 API

### FaceRecognitionApp

**文件**: `include/app/face_recognition_app.h`

应用控制中枢，负责多线程流水线的编排与生命周期管理。

#### 配置结构 `AppConfig`

```cpp
struct AppConfig {
    std::string retinaface_model_path;  // YOLOv8-face 模型路径
    std::string facenet_model_path;     // FaceNet 模型路径
    std::string camera_type;            // "usb" (默认)
    std::string device_number;          // /dev/videoX 的索引
    std::string feature_lib_path;       // 文件系统模式路径
    std::string database_path;          // SQLite 数据库路径
    bool use_database;                  // 是否启用数据库模式
    int camera_width;                   // 采集宽度
    int camera_height;                  // 采集高度
    float box_conf_threshold;           // 检测置信度
    float nms_threshold;                // 非极大值抑制阈值
    float facenet_threshold;            // 人脸识别相似度阈值
    bool use_async_usb;                 // 始终为 true (mmap 零拷贝)
    int perf_report_interval;           // 性能报告帧数间隔
};
```

#### 主要接口

```cpp
// 初始化流水线与模型
int initialize(const AppConfig& config);

// 启动检测主循环 (线程2)
int run();

// 设置识别成功回调 (用于业务处理)
void set_recognition_callback(RecognitionCallback callback);

// 设置帧回调 (用于 GUI 渲染)
void set_frame_callback(FrameCallback callback);

// 动态重置摄像头 (支持热切换)
bool reinitialize_camera(const std::string& device_number);

// GUI 注册接口：一站式获取特征
bool extract_feature_from_frame(const cv::Mat& frame, std::vector<float>& feature, cv::Rect* face_box = nullptr);
```

---

### ModelManager

**文件**: `include/app/model_manager.h`

管理 RKNN 句柄与内存，支持 NPU 卸载。

- `init_face_detector(path)`: 加载 YOLOv8-face。
- `init_facenet(path)`: 加载 MobileFaceNet。
- `get_face_detector_ctx()`: 获取 NPU 上下文。

---

### FeatureLibrary

**文件**: `include/app/feature_library.h`

基于 `std::shared_mutex` 实现的高并发特征库。

- `load_from_database(db_manager)`: 从数据库同步特征。
- `match_feature_with_id(...)`: 返回 `user_id` 的匹配接口。

---

### 流水线线程类

#### 1. PreprocessingThread (线程1)
**职责**: 持续采集 USB 视频流 (mmap 零拷贝)，使用 RGA 硬件进行镜像和正方形填充。

#### 2. PostprocessThread (线程2.5)
**职责**: 在 CPU 上执行 YOLOv8 输出张量的解码、DFL 与 NMS，将检测框推送到识别线程。

#### 3. RecognitionThread (线程3)
**职责**: 人脸对齐 (similarTransform)、FaceNet NPU 推理、特征比对、以及通过回调通知渲染。

---

## Core 层 API

### YOLOv8-face
- `yolov8_face_run(...)`: 执行 NPU 推理。
- `yolov8_face_postprocess(...)`: DFL/NMS 算法实现。

### FaceNet
- `facenet_inference(...)`: 提取 512 维归一化特征。

---

## Service 层 API

### AttendanceService
**文件**: `include/service/attendance_service.h`

- `auto_determine_check_type(...)`: 根据工作时间配置自动判断签到/签退。
- `record_attendance(...)`: 记录带状态 (正常/迟到/早退) 的考勤。
- `set_work_schedule(...)`: 配置弹性上下班时间规则。

### UserService
**文件**: `include/service/user_service.h`

- `register_user(...)`: 注册人员信息。
- `add_face_feature(...)`: 绑定人脸特征向量。

### AiAnalysisService
**文件**: `gui/include/services/ai_analysis_service.h`

- `requestAnalysis(...)`: 发送考勤数据至 LLM，支持多维度（今日/近7日/近30日）分析。
- `cancelAnalysis()`: 取消正在进行的推理请求。
- `signals`: 提供 `analysisResultReady` (增量流式输出)、`analysisFinished` 等信号。

---

## Hardware 层 API

### Camera Util
**文件**: `include/hardware/camera_util.h`

- `read_usb_frame(cv::Mat *img)`: **零拷贝接口**。利用 `shared_ptr` 引用计数浅拷贝，直接访问 mmap 映射的内存。
- `start_usb_capture_thread()`: 启动独立采集线程。

---

## 常量与数据结构

- `FACENET_FEATURE_DIM`: 512
- `RecognitionResult`: 包含 `user_id`, `user_name`, `similarity`, `face_image`, `timestamp`。
# RK3588 NPU 人脸识别系统 — 优化分析报告

> 生成日期: 2026-05-29
> 分支: yolov8-face-dev
> 代码规模: ~23,649 行 C++ (src/ + gui/src/)

---

## 目录

- [P0 — 严重问题（需立即修复）](#p0--严重问题需立即修复)
  - [1. FeatureLibrary 无锁访问](#1-featurelibrary-无锁访问)
  - [2. initialized_/camera_initialized_ 非原子变量](#2-initialized_camera_initialized_-非原子变量)
  - [3. ReactAgent::running_ 数据竞争（已修复）](#3-reactagentrunning_-数据竞争已修复)
  - [4. NPU 资源互斥缺失](#4-npu-资源互斥缺失)
  - [5. device_build.sh 未指定 Release 模式](#5-device_buildsh-未指定-release-模式)
- [P1 — 高优先级（显著性能影响）](#p1--高优先级显著性能影响)
  - [6. DFL 解码缓存未命中](#6-dfl-解码缓存未命中)
  - [7. FaceNet 无零拷贝路径](#7-facenet-无零拷贝路径)
  - [8. NMS 算法效率](#8-nms-算法效率)
  - [9. GUI 线程数据库查询阻塞](#9-gui-线程数据库查询阻塞)
  - [10. NPU 互斥锁持有时间过长](#10-npu-互斥锁持有时间过长)
  - [11. cos_similarity 冗余计算](#11-cos_similarity-冗余计算)
  - [12. YOLO 输出缓冲区拷贝抵消零拷贝收益](#12-yolo-输出缓冲区拷贝抵消零拷贝收益)
  - [13. FaceNet 输出指针生命周期不安全](#13-facenet-输出指针生命周期不安全)
  - [14. l2_normalize 除零风险](#14-l2_normalize-除零风险)
  - [15. similarTransform 逻辑 bug](#15-similartransform-逻辑-bug)
  - [16. 快排实现风险](#16-快排实现风险)
  - [17. softmax 重复调用](#17-softmax-重复调用)
- [P2 — 中优先级（代码质量/可维护性）](#p2--中优先级代码质量可维护性)
  - [18. 缺少 NEON SIMD 优化](#18-缺少-neon-simd-优化)
  - [19. 代码重复](#19-代码重复)
  - [20. cv::Mat 按值传递](#20-cvmat-按值传递)
  - [21. 正则表达式重复编译](#21-正则表达式重复编译)
  - [22. 数据库 PreparedStatement 每列加锁](#22-database-preparedstatement-每列加锁)
  - [23. 硬编码配置](#23-硬编码配置)
  - [24. 构建系统问题汇总](#24-构建系统问题汇总)
  - [25. RecognitionTask 拷贝而非移动](#25-recognitiontask-拷贝而非移动)
  - [26. PerformanceMonitor 7次锁获取](#26-performancemonitor-7次锁获取)
  - [27. 预览 RGA 在 NPU 锁内执行](#27-预览-rga-在-npu-锁内执行)
  - [28. FaceNet 串行推理](#28-facenet-串行推理)
  - [29. SSE 超时重置漏洞](#29-sse-超时重置漏洞)
  - [30. Agent 循环检测不完善](#30-agent-循环检测不完善)
  - [31. get_names/get_user_ids 返回无锁引用](#31-get_namesget_user_ids-返回无锁引用)
  - [32. 统计量非原子复合读写](#32-统计量非原子复合读写)
  - [33. N+1 查询模式](#33-n1-查询模式)
  - [34. SELECT * 通配符](#34-select--通配符)
  - [35. SQLITE_TRANSIENT 不必要拷贝](#35-sqlite_transient-不必要拷贝)
  - [36. batch_insert 重复 prepare](#36-batch_insert-重复-prepare)
  - [37. find_all_active 无 reserve](#37-find_all_active-无-reserve)
- [P3 — 低优先级（改进建议）](#p3--低优先级改进建议)
  - [38. V4L2 摄像头无自动重连](#38-v4l2-摄像头无自动重连)
  - [39. close_usb_camera 未排空缓冲区](#39-close_usb_camera-未排空缓冲区)
  - [40. 摄像头全局状态阻止多实例](#40-摄像头全局状态阻止多实例)
  - [41. FPS 计数器数据竞争](#41-fps-计数器数据竞争)
  - [42. 设备路径未校验](#42-设备路径未校验)
  - [43. LocalLLMThread 单例生命周期风险](#43-localllmthread-单例生命周期风险)
  - [44. MPP 解码可能阻塞 stop()](#44-mpp-解码可能阻塞-stop)
  - [45. GUI 组件无上限](#45-gui-组件无上限)
  - [46. 静默丢帧无统计](#46-静默丢帧无统计)
  - [47. 队列大小未差异化](#47-队列大小未差异化)
  - [48. localtime_r 不可移植](#48-localtime_r-不可移植)
  - [49. 头文件不自包含](#49-头文件不自包含)
  - [50. C 风格结构体定义](#50-c-风格结构体定义)
- [收益预估](#收益预估)
- [建议优化顺序](#建议优化顺序)

---

## P0 — 严重问题（需立即修复）

### 1. FeatureLibrary 无锁访问

**文件:** `include/app/feature_library.h:98-118`

**当前状态（2026-06-04）:** 已修复。`size()`、`empty()` 已加 `shared_lock`，`get_names()` / `get_user_ids()` 已改为加锁后返回拷贝。

```cpp
size_t size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lib_feature_.size();
}
std::vector<std::string> get_names() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return lib_face_name_;
}
```

**风险:** GUI 线程调用这些方法时，识别线程可能正在修改 vector，导致**未定义行为/崩溃**。

**修复:** 所有读操作必须获取 `shared_mutex` 的共享锁，返回值应为拷贝而非引用。

```cpp
size_t size() const {
    std::shared_lock lock(mutex_);
    return lib_feature_.size();
}
std::vector<std::string> get_names() const {
    std::shared_lock lock(mutex_);
    return lib_face_name_;  // 返回拷贝
}
```

---

### 2. initialized_/camera_initialized_ 非原子变量

**文件:** `include/app/face_recognition_app.h:374-383`

**当前状态（2026-06-04）:** 已修复。两个状态已改为 `std::atomic<bool>`。

```cpp
std::atomic<bool> initialized_;
std::atomic<bool> camera_initialized_;
std::atomic<bool> models_loaded_;  // ✅ 原子
std::atomic<bool> running_;        // ✅ 原子
```

**风险:** GUI 线程读取时，主线程可能正在写入，属于**数据竞争**（UB）。

**修复:** 改为 `std::atomic<bool>`，与 `models_loaded_` 和 `running_` 保持一致。

---

### 3. ReactAgent::running_ 数据竞争（已修复）

**文件:** `include/agent/react_agent.h:148`、`src/agent/react_agent.cc:25,192`

```cpp
// 当前代码：running_ 已经是 atomic
std::atomic<bool> running_{false};

// run() 中读取（Agent 工作线程）
while (running_ && iteration < max_iterations) { ... }

// stop() 中写入（可能从 GUI 线程调用）
void ReactAgent::stop() { running_ = false; }
```

**当前状态:** 该项在当前版本已修复。`running_` 已定义为 `std::atomic<bool>`，因此不再是非原子 bool 数据竞争。

**建议:** 从 P0 修复清单中移除，仅保留为历史记录。

---

### 4. NPU 资源互斥缺失

**文件:** `src/app/face_recognition_app.cc:471-598`、`src/app/local_llm_thread.cc`

**问题:** RKNN（人脸识别）和 RKLLM（语言模型）共享同一 NPU 硬件。当前 GUI 层已有 `QFutureWatcher`、`rknn_switching_`、`releaseModelAsync()` 等流程控制，但缺少跨 RKNN/RKLLM 的**集中资源仲裁器**。资源状态分散在 GUI、`FaceRecognitionApp` 和 `LocalLLMThread` 中，仍依赖 `release_models()` → `initModel()` / `releaseModelAsync()` → `reload_models()` 的调用顺序。

**风险:** 如果状态机遗漏边界条件、并发切页或异步回调乱序，两个子系统仍可能同时初始化/使用 NPU。`500ms` sleep 等待 `rknn_destroy` 完成也是脆弱的硬编码等待。

**修复:** 实现 `NpuResourceManager`，用状态机管理 NPU 访问权：

```cpp
class NpuResourceManager {
    std::mutex mutex_;
    enum class State { IDLE, VISION_ACTIVE, LLM_ACTIVE };
    State state_ = State::IDLE;
public:
    bool request_vision();   // 获取视觉模式，若 LLM 活跃则返回 false
    bool request_llm();      // 获取 LLM 模式，若视觉活跃则返回 false
    void release();          // 释放，通知等待方
};
```

---

### 5. device_build.sh 未指定 Release 模式

**文件:** `device_build.sh:64`

**当前状态（2026-06-04）:** 暂不修改脚本。当前使用交叉编译流程，`cross_build.sh` 已显式传入 `-DCMAKE_BUILD_TYPE=Release`；`device_build.sh` 保持原状。

```bash
cmake ../.. -DTARGET_NAME=face_recognition_cap
```

**影响:** 性能比 Release 模式慢 **3-10 倍**。这是板端构建脚本，直接影响部署性能。

**修复:**

```bash
cmake ../.. -DTARGET_NAME=face_recognition_cap -DCMAKE_BUILD_TYPE=Release
```

---

## P1 — 高优先级（显著性能影响）

### 6. DFL 解码缓存未命中

**文件:** `src/core/postprocess.cc:169-172`

```cpp
// ❌ 跨步内存访问，每次跳 6400 字节（80×80 特征图）
for (int i = 0; i < kInputLocLen; ++i) {
    loc[i] = deqnt_affine_to_f32(input[i * grid_h * grid_w + offset], zp, scale);
}
```

**影响:** 这是后处理最热的循环。NCHW 布局下每次迭代跨步 `grid_h * grid_w` 字节，远超缓存行大小，每次都缓存未命中。

**修复方案:**

| 方案 | 描述 | 复杂度 |
|------|------|--------|
| A | 将 NCHW 数据转置为 NHWC 后再处理 | 中 |
| B | 使用 ARM NEON gather 指令 | 中 |
| C | 按 channel tile 处理并使用 `__builtin_prefetch` | 低 |

---

### 7. FaceNet 无零拷贝路径

**文件:** `src/core/facenet.cc`（整个文件）

**问题:** YOLOv8 有 `yolov8_face_init_zero_copy` / `yolov8_face_run_zero_copy`，但 FaceNet **完全没有零拷贝路径**。每次推理都通过 `rknn_inputs_set`（拷贝入）和 `rknn_outputs_get`（拷贝出）。

**影响:** YOLOv8 每帧推理 1 次，但 FaceNet 每帧推理 N 次（N = 检测到的人脸数，通常 1-10）。每张人脸 2 次额外内存拷贝（输入 ~37KB + 输出 2KB）。

**修复:** 参照 `yolov8_face.cc` 的零拷贝实现，为 FaceNet 添加：
- `facenet_init_zero_copy()` — 创建 dma-buf 输入/输出内存
- `facenet_run_zero_copy()` — 使用 `rknn_set_io_mem` + `rknn_run`

---

### 8. NMS 算法效率

**文件:** `src/core/postprocess.cc:83-114`

**当前状态（2026-06-04）:** 已部分修复。外层候选框坐标已移出内层循环，避免重复加载和重复计算；整体排序/NMS 算法仍可继续优化。

```cpp
// ❌ 内层循环每次重新从 vector 加载 box 坐标
for (int j = i + 1; j < validCount; ++j) {
    if (order[j] == -1) continue;
    float x1 = outputLocations[order[j] * 5 + 0];  // 每次从 vector 读取
    float y1 = outputLocations[order[j] * 5 + 1];
    // ... IoU 计算
}
```

**问题:**
1. O(n²) 且内层循环不跳过已抑制的 `-1` 项
2. box 坐标在内层循环重复加载

**修复:**

```cpp
// 外层循环加载当前 box
float bx1 = outputLocations[order[i] * 5 + 0];
float by1 = outputLocations[order[i] * 5 + 1];
float bx2 = outputLocations[order[i] * 5 + 2];
float by2 = outputLocations[order[i] * 5 + 3];

for (int j = i + 1; j < validCount; ++j) {
    if (order[j] == -1) continue;
    // 使用局部变量 bx1..by2 与 order[j] 的坐标计算 IoU
}
```

---

### 9. GUI 线程数据库查询阻塞

**文件:** `gui/src/gui/main_window.cc:1225-1227`

```cpp
// ❌ 每帧每张人脸都在 GUI 线程执行数据库查询
attendance_service_->is_duplicate_check(user_id);
attendance_service_->auto_determine_check_type(user_id);
```

**影响:** 30 FPS + 5 张人脸 = **每秒 150 次数据库查询**在 GUI 线程执行，导致 UI 卡顿。

同类问题还有：
- `update_status()` 每 10 秒在 GUI 线程查询统计 (main_window.cc:1369)
- `refreshData()` 在 GUI 线程执行 30 天范围的多表查询 (dashboard_page.cc:1512-1822)
- `load_today_attendance()` N+1 查询模式 (main_window.cc:699)

**修复:**
- 方案 A: 使用内存缓存（300 秒内的考勤记录），消除热路径数据库查询
- 方案 B: 将查询移到 `QConcurrent::run` 后台线程，结果通过信号返回 GUI 线程

---

### 10. NPU 互斥锁持有时间过长

**文件:** `src/app/face_recognition_app.cc:287-307`

**当前状态（2026-06-04）:** 已修复。YOLO 输出拷贝已移出 NPU 输入锁，推理完成后再在锁外拷贝输出缓冲区。

```cpp
{
    std::lock_guard<std::mutex> lock(preprocess_thread_->get_npu_mem_mutex());
    yolov8_face_run_zero_copy(...);  // 推理 — 必须持锁
}
memcpy(output_buffers, outputs);      // 锁外拷贝
```

**影响:** 输出拷贝（~1MB）在锁内执行，阻塞预处理线程开始下一帧的 RGA 操作。

**修复:** 将输出拷贝移到锁外：

```cpp
{
    std::lock_guard<std::mutex> lock(npu_mem_mutex);
    yolov8_face_run_zero_copy(...);
}
// 锁外拷贝输出，预处理线程可以立即开始下一帧
for (uint32_t i = 0; i < io_num.n_output; ++i) {
    output_buffers[i].resize(outputs[i].size);
    memcpy(output_buffers[i].data(), outputs[i].buf, outputs[i].size);
}
preprocess_thread_->complete_npu_inference();
```

---

### 11. cos_similarity 冗余计算

**文件:** `src/core/postprocess.cc:530-538`

```cpp
float cos_similarity(float* input1, float* input2) {
    float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i)
        sum += input1[i] * input2[i];
    float tmp1 = eu_distance(input1);  // ❌ 输入已 L2 归一化，结果恒为 1.0
    float tmp2 = eu_distance(input2);  // ❌ 同上
    return sum / (tmp1 * tmp2);
}
```

**影响:** 每次匹配浪费 2 × 512 = 1024 次乘加运算。

**修复:** 既然 `facenet_inference` 内部已调用 `l2_normalize`，直接返回点积：

```cpp
float cos_similarity(float* input1, float* input2) {
    float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i)
        sum += input1[i] * input2[i];
    return sum;  // 已归一化，点积 = 余弦相似度
}
```

---

### 12. YOLO 输出缓冲区拷贝抵消零拷贝收益

**文件:** `src/core/yolov8_face.cc:291-297`

```cpp
// yolov8_face_run() 中，推理完成后立即拷贝所有输出
for (uint32_t i = 0; i < io_num.n_output; ++i) {
    output_buffers[i].resize(outputs[i].size);
    memcpy(output_buffers[i].data(), outputs[i].buf, outputs[i].size);
}
```

**影响:** 每帧约 1.05MB memcpy（3 个特征图 ~546KB + 关键点 ~504KB），抵消了零拷贝输入的收益。

**修复:** 实现零拷贝后处理函数 `yolov8_face_postprocess_zero_copy()`，直接读取 NPU 输出内存中的数据，避免拷贝到 `output_buffers`。

---

### 13. FaceNet 输出指针生命周期不安全

**文件:** `src/core/facenet.cc:275`

```cpp
*result = (float*)outputs[0].buf;  // 返回 RKNN 内部缓冲区指针
```

**风险:** 调用方必须手动调用 `facenet_output_release()` 释放。若任何提前返回路径跳过释放，将导致 NPU 输出缓冲区泄漏。

**修复:** 在 `facenet_inference` 内部将 512 个 float 拷贝到调用方提供的缓冲区（2KB 拷贝开销可忽略），消除手动释放需求：

```cpp
int facenet_inference(rknn_context *ctx, const cv::Mat& img,
                      float* out_feature,  // 调用方提供 512-float 缓冲区
                      rknn_input* inputs, rknn_output* outputs, ...);
```

---

### 14. l2_normalize 除零风险

**文件:** `src/core/postprocess.cc:511-520`

**当前状态（2026-06-04）:** 已修复。归一化前已增加零范数保护。

```cpp
void l2_normalize(float* input) {
    float sum = 0;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i)
        sum += input[i] * input[i];
    sum = sqrt(sum);
    if (sum < 1e-10f) return;
    for (int i = 0; i < FACENET_FEATURE_DIM; ++i)
        input[i] = input[i] / sum;
}
```

**修复:**

```cpp
if (sum < 1e-10f) return;  // 零向量不做归一化
```

---

### 15. similarTransform 逻辑 bug

**文件:** `src/core/postprocess.cc:446-497`

| 行号 | 问题 |
|------|------|
| 464-465 | `rank == 0` 分支断言恒真后落入后续代码，返回 `-I * scale`，几乎确定是错误结果 |
| 470-471 | `d.at<float>(dim-1,0) = -1` 连续赋值两次，第二次为死代码 |
| 482 | `-U.t() * twp` 的符号处理与其他分支不一致 |
| 489-495 | 所有分支都执行 `T = -T * scale`，但 `rank==0` 分支的 `T` 是单位矩阵 |

**影响:** 人脸对齐使用 `similarTransform` 计算仿射变换矩阵。逻辑 bug 可能导致极少数情况下人脸对齐错误。

**修复:** 参考 OpenCV 的 `estimateAffinePartial2D` 或 `estimateRigidTransform` 重写此函数。

---

### 16. 快排实现风险

**文件:** `src/core/postprocess.cc:117-144`

- 递归实现，无尾调用优化，病态输入（全部相同分数）可能栈溢出
- **原地修改 `objProbs`**，调用方可能未意识到输入被修改

**修复:** 使用 `std::sort` + 自定义比较器（内省排序，更快更安全）：

```cpp
std::vector<int> indices(validCount);
std::iota(indices.begin(), indices.end(), 0);
std::sort(indices.begin(), indices.end(), [&](int a, int b) {
    return objProbs[a] > objProbs[b];
});
```

---

### 17. softmax 重复调用

**文件:** `src/core/postprocess.cc:64-80`、175-177

每个候选锚点调用 4 次 `softmax`，每次 3 遍扫描 16 个 float（找最大值、求 exp 和、归一化）。对 5×5 DFL 情况 = 12 遍扫描 64 个 float。

**修复:** 融合为单遍 `softmax`，或使用查表法加速 `exp` 计算。

---

## P2 — 中优先级（代码质量/可维护性）

### 18. 缺少 NEON SIMD 优化

**文件:** `src/core/postprocess.cc` 多个函数

以下函数操作连续的 512 维 float 数组，完美适合 ARM NEON 优化：

| 函数 | 操作 | 预期加速 |
|------|------|----------|
| `l2_normalize` | 512 次乘加 + 除法 | 3-4x |
| `compare_eu_distance` | 512 次乘加 | 3-4x |
| `cos_similarity` | 512 次乘加 | 3-4x |
| `deqnt_affine_to_f32` 内循环 | 64 次反量化 | 2-3x |
| `softmax` | 16 次 exp 运算 | 2x |

**示例（l2_normalize NEON 版）:**

```cpp
#include <arm_neon.h>
void l2_normalize_neon(float* input) {
    float32x4_t sum_vec = vdupq_n_f32(0.0f);
    for (int i = 0; i < FACENET_FEATURE_DIM; i += 4) {
        float32x4_t v = vld1q_f32(input + i);
        sum_vec = vfmaq_f32(sum_vec, v, v);
    }
    float sum = vaddvq_f32(sum_vec);
    if (sum < 1e-10f) return;
    float inv_norm = 1.0f / sqrtf(sum);
    float32x4_t norm_vec = vdupq_n_f32(inv_norm);
    for (int i = 0; i < FACENET_FEATURE_DIM; i += 4) {
        float32x4_t v = vld1q_f32(input + i);
        vst1q_f32(input + i, vmulq_f32(v, norm_vec));
    }
}
```

---

### 19. 代码重复

**文件:** `src/core/yolov8_face.cc` 和 `src/core/facenet.cc`

以下函数完全重复：

| 函数 | 行号（yolov8） | 行号（facenet） |
|------|----------------|-----------------|
| `load_model` | 39-68 | 51-80 |
| `load_data` | 70-102 | 82-114 |
| `dump_tensor_attr` | 31-37 | 42-49 |

**修复:** 提取到共享的 `rknn_util.cc/h`。

---

### 20. cv::Mat 按值传递

**文件:** `include/core/facenet.h:10`

**当前状态（2026-06-04）:** 已修复。`facenet_inference` 的图像参数已改为 `const cv::Mat&`。

```cpp
int facenet_inference(rknn_context *ctx, const cv::Mat& img, ...);
```

**影响:** 虽然 `cv::Mat` 使用引用计数不会深拷贝像素数据，但每次调用仍有原子引用计数增减开销，且语义上应为只读。

**修复:** 改为 `const cv::Mat& img`。

---

### 21. 正则表达式重复编译

**文件:** `src/agent/react_agent.cc:207-303`

**当前状态（2026-06-04）:** 已部分修复。`parseStepType()` 中固定标签正则已改为 `static const QRegularExpression`；`extractContent()` 的动态 tag 正则仍按调用构造。

```cpp
// ❌ 每次调用都重新编译正则
QRegularExpression re_answer("<answer>([\\s\\S]*?)</answer>");
QRegularExpression re_tool("<tool_call>...");
QRegularExpression re_thought("<thought>...");
```

**影响:** 每次 ReAct 迭代调用 1-2 次，正则编译非免费操作。

**修复:** 改为 `static const QRegularExpression`，编译一次：

```cpp
static const QRegularExpression re_answer("<answer>([\\s\\S]*?)</answer>");
```

---

### 22. 数据库 PreparedStatement 每列加锁

**文件:** `src/database/database_manager.cc:100-125`

每行读取 11 列 = 11 次 mutex 获取。查询 100 条考勤记录 = **1100 次锁操作**。

**修复:** 将 mutex 提升到 `step + 所有 column read` 的外层，每行只加锁 1 次。

---

### 23. 硬编码配置

**文件:** `include/config/config.h`、`cross_build.sh`

| 问题 | 位置 | 修复建议 |
|------|------|----------|
| LLM 模型路径含拼写错误 "projtect" | config.h:128 | 使用环境变量或配置文件 |
| API 端点为编译时常量 | config.h:117-122 | 改为运行时可配置（复用 ConfigManager 模式） |
| News API 源硬编码 | config.h:99-112 | 改为配置文件 |
| 设备 IP 硬编码 | cross_build.sh:175-176 | 使用命令行参数或环境变量 |
| SSH 密码明文提交 | cross_build.sh:178-179 | 改用 SSH 密钥认证 |

---

### 24. 构建系统问题汇总

**文件:** `CMakeLists.txt`、`cross_build.sh`、`device_build.sh`

| 问题 | 位置 | 修复 |
|------|------|------|
| 缺少 `-mcpu=cortex-a76` | CMakeLists.txt | 添加 RK3588 专用编译标志 |
| 缺少 LTO | CMakeLists.txt | 启用 `CMAKE_INTERPROCEDURAL_OPTIMIZATION` |
| 链接器标志放在编译器标志中 | CMakeLists.txt:34-35 | 移到 `CMAKE_EXE_LINKER_FLAGS` |
| 全局 `include_directories()` | CMakeLists.txt:129-141 | 改为 `target_include_directories()` |
| `device_build.sh` 使用 `-j2` | device_build.sh:66 | 改为 `$(nproc)` |
| `cross_build.sh` 覆盖 CXX_FLAGS | cross_build.sh:112 | 用 `add_compile_definitions()` 代替 |
| 部署不存在的 `face_recognition_cap` 二进制 | cross_build.sh:212 | 移除或更正 |
| LIB_ARCH 检测重复 | CMakeLists.txt:54-58,74-78 | 合并为一处 |

---

### 25. RecognitionTask 拷贝而非移动

**文件:** `src/app/recognition_thread.cc:57-69`

**当前状态（2026-06-04）:** 已修复。`submit_task` 已改为接收 `RecognitionTask&&` 并移动入队，`PostprocessThread` 提交时使用 `std::move`。

```cpp
bool RecognitionThread::submit_task(const RecognitionTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(task);  // ❌ 拷贝，应为移动
}
```

`RecognitionTask` 包含 `cv::Mat orig_img`，拷贝触发引用计数操作或深拷贝。

**修复:** 改签名为 `submit_task(RecognitionTask&& task)` 并使用 `queue_.push(std::move(task))`。对比 `PostprocessThread::submit_task(PostprocessTask&& task)` 已正确使用移动语义。

---

### 26. PerformanceMonitor 7 次锁获取

**文件:** `src/app/face_recognition_app.cc:318-324`

```cpp
perf_monitor_.update_fps(...);              // 1 次锁
perf_monitor_.record_detection_time(...);   // 2 次锁
perf_monitor_.record_detection_run_time(...); // 3 次锁
perf_monitor_.record_detection_copy_time(...); // 4 次锁
perf_monitor_.record_alignment_time(...);   // 5 次锁
perf_monitor_.record_recognition_time(...); // 6 次锁
perf_monitor_.record_matching_time(...);    // 7 次锁
```

**影响:** 每帧热路径上 7 次 mutex 获取。

**修复:** 提供批量记录 API：

```cpp
struct FrameMetrics {
    float fps, detect_time, yolo_run, yolo_copy, align, recognition, matching;
};
void record_frame_metrics(const FrameMetrics& m);  // 单次锁
```

---

### 27. 预览 RGA 在 NPU 锁内执行

**文件:** `src/app/preprocessing_thread.cc:436-473`

在 `npu_mem_mutex_` 锁内执行两次 RGA 操作：

1. `improcess` 写入 NPU 输入内存（必须持锁）
2. `improcess` 生成预览 `task.orig_img`（**不需要持锁**，写入本地 `cv::Mat`）

**修复:** 将预览 RGA 移到锁外，减少锁持有时间。

---

### 28. FaceNet 串行推理

**文件:** `src/app/recognition_thread.cc:157-278`

多张人脸时，FaceNet 推理串行执行：`warpPerspective` → `cvtColor` → `facenet_inference` → 特征匹配。5 张人脸 = 5 次串行 NPU 推理。

**优化:** 使用双缓冲流水线：CPU 预处理第 N+1 张人脸的同时，NPU 推理第 N 张人脸。

---

### 29. SSE 超时重置漏洞

**文件:** `src/gui_services/ai_analysis_service.cc:339-346`

```cpp
// 每收到数据就重置超时
timeout_timer_->start(TIMEOUT_MS);  // 60 秒
```

**风险:** 服务器每 59 秒发送 1 字节即可无限保持连接。

**修复:** 添加总请求超时（如 5 分钟）+ 每块空闲超时（60 秒）。

---

### 30. Agent 循环检测不完善

**文件:** `src/agent/react_agent.cc:91-115`

仅检测连续 2 次**完全相同**的工具调用。若 Agent 交替调用两个等效但参数略有不同的工具，循环检测不会触发。

**修复:** 添加结果哈希比较，检测"不同调用、相同结果"的循环模式。

---

### 31. get_names/get_user_ids 返回无锁引用

**文件:** `include/app/feature_library.h:113-118`

```cpp
const std::vector<std::string>& get_names() const { return lib_face_name_; }
const std::vector<int>& get_user_ids() const { return lib_user_ids_; }
```

返回内部 vector 的引用，调用方迭代时可能被其他线程修改。

**修复:** 返回拷贝而非引用，或要求调用方持有共享锁。

---

### 32. 统计量 atomic 复合更新不严格

**文件:** `src/app/recognition_thread.cc:281-283`

```cpp
avg_align_time_ = avg_align_time_ * 0.9f + total_align_time * 0.1f;
```

`avg_*` 成员是 `std::atomic<float>`，所以不是普通意义上的非原子数据竞争。但 `load` → 计算 → `store` 组合不是原子读改写；如果未来出现多写者，会丢更新。当前实际影响主要是统计值近似，不是崩溃风险。

**修复:** 使用 `compare_exchange_weak` 循环实现原子 RMW，或明确接受近似值并保留当前实现（当前实际影响极低）。

---

### 33. N+1 查询模式

**文件:** `gui/src/gui/main_window.cc:691-712`

```cpp
for (const auto& record : records) {
    auto user = user_service_->get_user(record.user_id);  // ❌ 每条记录一次查询
    // ...
}
```

**修复:** 批量查询所有涉及的用户，建立 `user_id → User` 映射表。

---

### 34. SELECT * 通配符

**文件:** 所有 DAO 文件（`user_dao.cc:95`、`face_feature_dao.cc:69`、`attendance_record_dao.cc:71`）

**问题:** 列增删或重排时会破坏结果映射，且阻止 SQLite 使用覆盖索引。

**修复:** 改为显式列名 `SELECT id, name, department, ...`。

---

### 35. SQLITE_TRANSIENT 不必要拷贝

**文件:** `src/database/database_manager.cc:59`

```cpp
return sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
```

`SQLITE_TRANSIENT` 让 SQLite 自行拷贝字符串。考勤记录插入是热路径，若调用方保证字符串生命周期到 `sqlite3_step()` 之后，可改用 `SQLITE_STATIC` 避免拷贝。

---

### 36. batch_insert 重复 prepare

**文件:** `src/database/face_feature_dao.cc:170-192`

`batch_insert` 循环调用 `insert()`，每次创建新的 `PreparedStatement`（调用 `sqlite3_prepare_v2`）。

**修复:** 预编译一次，循环中使用 `reset()` 重置绑定。

---

### 37. find_all_active 无 reserve

**文件:** `src/database/face_feature_dao.cc:120-140`

返回 `std::vector<FaceFeature>` 时无 `reserve()`，100 个用户 × 2KB/特征 = 多次 `push_back` 重新分配。

**修复:** 先 `COUNT(*)` 查询总数，然后 `reserve(count)`。

---

## P3 — 低优先级（改进建议）

### 38. V4L2 摄像头无自动重连

**文件:** `src/hardware/camera_util.cc:275-289`

`VIDIOC_DQBUF` 失败后只设置错误标志，不尝试关闭并重新打开设备。

**修复:** 实现指数退避重连（如 1s、2s、4s，最多 5 次）。

---

### 39. close_usb_camera 未排空缓冲区

**文件:** `src/hardware/camera_util.cc:362-374`

`VIDIOC_STREAMOFF` 后立即释放 mmap 缓冲区，但内核可能仍引用已出队的缓冲区。

**修复:** 在 `STREAMOFF` 前循环 `DQBUF` 直到 `EAGAIN`。

---

### 40. 摄像头全局状态阻止多实例

**文件:** `src/hardware/camera_util.cc:30-42`

所有状态（`fd`、`buffers`、`v4l2_fmt`）为文件作用域静态全局变量，只能支持单摄像头。

**修复:** 封装为 `V4l2Camera` 类，支持多实例。

---

### 41. FPS 计数器数据竞争

**文件:** `src/hardware/camera_util.cc:308-315`

`last_fps_calc_time` 是非原子 `steady_clock::time_point`，`read_usb_raw_packet()` 写入，`close_usb_camera()` 可从另一线程调用。

**修复:** 改为 `std::atomic` 或加锁保护。

---

### 42. 设备路径未校验

**文件:** `src/hardware/camera_util.cc:117`

`device` 参数直接拼接到 "/dev/video"，无数字校验，存在路径遍历风险。

**修复:** 校验 `device` 仅包含数字字符。

---

### 43. LocalLLMThread 单例生命周期风险

**文件:** `src/app/local_llm_thread.cc:17-26`

`LocalLLMThread` 通过 `new LocalLLMThread(qApp)` 创建，Qt parent-child 机制通常会在 `qApp` 析构时删除对象，因此不能简单定性为内存泄漏。但 `s_instance` 裸指针不会清空，线程停止和 RKLLM 资源释放依赖应用退出析构顺序，退出阶段存在时序风险。

**修复:** 提供显式 `shutdown()` / `destroyInstance()`，在 GUI 退出流程中先停止推理、释放模型、`wait()` 线程，再清空单例指针。

---

### 44. MPP 解码可能阻塞 stop()

**文件:** `src/app/preprocessing_thread.cc:269-275`

`stop()` 通知 `cv_npu_input_` 但不通知 `cv_output_`。若线程阻塞在 `read_usb_raw_packet` 或 MPP 解码中，`join()` 将无限等待。

**修复:** 添加超时机制或取消标志。

---

### 45. GUI 组件无上限

**文件:** `gui/src/ui/dashboard_page.cc:412-501`

聊天消息和渲染块的 widget 数量无上限，长时间使用持续增长。

**修复:** 添加最大历史条数（如 200 条），超出时删除最早的 widget。

---

### 46. 静默丢帧无统计

**文件:** 多个线程的队列溢出处理

```cpp
while (queue_.size() >= MAX_QUEUE_SIZE) {
    queue_.pop();  // ❌ 无日志、无计数器
}
```

**修复:** 添加 `dropped_frames_` 原子计数器，纳入性能报告。

---

### 47. 队列大小未差异化

**文件:** `include/app/preprocessing_thread.h:141`、`recognition_thread.h:150`、`postprocess_thread.h:73`

三个队列阶段使用相同或相近的队列大小，但处理速率差异大：
- 预处理：30 FPS（摄像头帧率）
- 后处理：50+ FPS（NPU 推理速度）
- 识别：取决于人脸数（最慢阶段）

**修复:** 根据各阶段速率差异化配置队列大小。

---

### 48. localtime_r 不可移植

**文件:** `gui/src/gui/main_window.cc:1365`

`localtime_r` 是 POSIX 函数，Windows/MSVC 不可用。

**修复:** 使用 `std::localtime` 或条件编译提供 Windows 兼容。

---

### 49. 头文件不自包含

**文件:** `include/core/facenet.h:10`

**当前状态（2026-06-04）:** 已修复。头文件已补充 `#include <opencv2/core.hpp>`。

使用 `cv::Mat` 但未 `#include <opencv2/core.hpp>`，依赖调用方恰好先包含。

**修复:** 添加必要的 `#include`。

---

### 50. C 风格结构体定义

**文件:** `include/core/postprocess.h:66-70`

```cpp
typedef struct _detect_result_group_t { ... } detect_result_group_t;
```

C++ 中无需 `typedef struct`，直接 `struct DetectResultGroup { ... };` 即可。

---

## 收益预估

| 优化项 | 预期收益 | 优先级 |
|--------|----------|--------|
| Release 构建 + `-mcpu=cortex-a76` + LTO | **3-10x** 整体性能提升 | P0 |
| FaceNet 零拷贝 | **减少 2-4ms/人脸** | P1 |
| DFL 解码缓存优化 | **减少 1-2ms/帧** | P1 |
| NMS 优化 | **减少 0.5-1ms/帧** | P1 |
| GUI 线程数据库查询移出 | **消除 UI 卡顿** | P1 |
| NEON SIMD 优化 | **2-4x** 向量运算加速 | P2 |
| NPU 锁持有时间优化 | **减少 2-3ms/帧** 等待时间 | P1 |
| YOLO 输出零拷贝后处理 | **减少 1ms/帧** | P1 |
| 正则/排序/数据库优化 | 减少 CPU 开销 | P2 |

---

## 建议优化顺序

### 第一阶段：关键修复（1-2 天）

1. 修复 P0 线程安全问题（#1, #2, #31；#3 当前已修复）
2. 构建脚本添加 Release 模式（#5）
3. NPU 资源管理器（#4）

### 第二阶段：性能关键路径（3-5 天）

4. FaceNet 零拷贝（#7）
5. GUI 线程数据库查询缓存（#9）
6. NPU 锁持有时间优化（#10, #27）
7. YOLO 输出零拷贝后处理（#12）
8. DFL 解码缓存优化（#6）

### 第三阶段：性能提升（1 周）

9. NEON SIMD 优化（#18）
10. NMS 算法优化（#8）
11. cos_similarity 优化（#11）
12. FaceNet 串行推理流水线化（#28）
13. 构建系统完善（#24）

### 第四阶段：代码质量（持续）

14. 代码重复消除（#19）
15. 数据库优化（#22, #33-37）
16. 配置外部化（#23）
17. 健壮性改进（#14, #15, #38-50）

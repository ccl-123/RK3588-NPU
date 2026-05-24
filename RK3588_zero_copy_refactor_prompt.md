# RK3588 零拷贝链路重构提示词

## 任务目标

请基于当前项目 `C++/face_recognition_cap`，继续重构 RK3588 人脸识别考勤系统的视频采集、解码、预处理与 YOLO 推理链路，目标是实现接近官方例程方式的：

```text
V4L2 → MPP → RGA → RKNN Zero-Copy
```

当前目标是消除以下 CPU 深拷贝或 CPU 计算路径：

- OpenCV `cv::imdecode` CPU 软解码；
- OpenCV `cv::copyMakeBorder` 边框填充；
- `cv::Mat::clone()` 整帧深拷贝；
- `rknn_inputs_set` 输入拷贝；
- `rknn_outputs_get` 输出拷贝。

最终链路应尽量实现：

```text
V4L2 mmap/DQBUF
        ↓
Raw MJPEG packet
        ↓
MPP hardware decode
        ↓
MppFrame / MppBuffer / dma-buf fd
        ↓
RGA NV12 → RGB888 + mirror + resize + letterbox
        ↓
RKNN input rknn_tensor_mem / dma-buf fd
        ↓
rknn_run
        ↓
RKNN output rknn_tensor_mem virt_addr postprocess
```

最终目标不是“看起来少拷贝”，而是尽量对齐官方 `rknn_zero_copy`、`rknn_yolov5_demo`、`mpi_dec_test.c`、RGA im2d 示例，实现真正的：

```text
MPP 解码物理缓冲 → RGA 物理处理 → RKNN zero-copy 输入内存
```

---

## 1. 官方 RKNN 例程参考路径

### 1.1 RKNN 零拷贝例程

零拷贝例程目录：

```text
[rknpu2]/examples/rknn_zero_copy/
```

本机路径：

```text
file:///home/cl/EC-A3588Q/RK_NPU2_SDK/rknn-toolkit2-v2.4.0-2026-01-17/rknpu2/examples/rknn_zero_copy/
```

请重点学习其中 RKNN Runtime zero-copy API 的使用方式：

```cpp
rknn_create_mem
rknn_set_io_mem
rknn_tensor_mem
rknn_run
rknn_destroy_mem
```

要求 YOLO 推理路径尽量和该官方例程保持一致，真正使用 RKNN Runtime 的 zero-copy API，而不是只减少部分 `memcpy`。

### 1.2 RKNN 例程总目录

```text
file:///home/cl/EC-A3588Q/RK_NPU2_SDK/rknn-toolkit2-v2.4.0-2026-01-17/rknpu2/examples/
```

该目录仅作为官方例程参考，不允许直接作为最终项目编译和链接路径。

---

## 2. MPP 硬件解码参考路径

请参考 MPP SDK 原始测试代码，尤其是 MJPEG/JPEG 硬件解码流程。

MPP 单元测试与解码例程代码：

```text
[mpp]/test/
```

本机路径：

```text
file:///home/cl/EC-A3588Q/firefly_rk3588_SDK/external/mpp/test/
```

重点参考文件：

```text
mpi_dec_test.c
mpi_dec_multi_test.c
```

其中 `mpi_dec_test.c` 极度推荐参考，尤其是以下内容：

```cpp
decode_put_packet
decode_get_frame
MPP_DEC_SET_INFO_CHANGE_READY
mpp_frame_get_buffer
mpp_buffer_get_fd
```

必须正确处理 MPP 解码过程中的 `info change` 状态，不能简单忽略。

MPP 解码输出需要尽量取得 `MppFrame` 关联的 `dma-buf fd`，用于后续 RGA 输入：

```cpp
MppBuffer mpp_buf = mpp_frame_get_buffer(frame);
int decoded_fd = mpp_buffer_get_fd(mpp_buf);
```

---

## 3. RGA 参考路径

请参考 RGA API 与项目内第三方库路径，实现：

- NV12 → RGB888；
- 缩放；
- 水平镜像；
- Letterbox 补黑边；
- fd buffer 包装；
- RGA 输入输出格式检查。

RGA 预编译库与 include 头文件原始参考路径：

```text
file:///home/cl/EC-A3588Q/RK_NPU2_SDK/rknn-toolkit2-v2.4.0-2026-01-17/rknpu2/examples/3rdparty/rga/
```

RGA SDK 源码与测试目录：

```text
file:///home/cl/EC-A3588Q/firefly_rk3588_SDK/external/rknpu2/examples/3rdparty/rga/
```

项目内实际编译使用路径应为：

```text
C++/3rdparty/rga/
```

如果项目当前已经有 RGA 依赖配置，请复用项目内已有路径，不要新增 SDK 绝对路径。

重点使用或参考：

```cpp
imfill
improcess
wrapbuffer_fd
wrapbuffer_virtualaddr
imcheck
```

RGA 处理目标：

1. MPP 解码输出 `dma-buf fd` 作为 RGA 输入；
2. RKNN zero-copy 输入内存对应的 fd 作为 RGA 输出；
3. RGA 直接把最终 letterbox 后的 RGB888 图像写入 RKNN 输入物理内存；
4. 不能再走 `cv::Mat` 深拷贝作为主推理路径。

---

## 4. 第三方库路径与 SDK 版本要求

当前项目已经把对齐 SDK 版本的第三方库复制到了项目内：

```text
C++/3rdparty/rknn/
C++/3rdparty/rga/
C++/3rdparty/mpp/
```

后续修改必须满足：

```text
CMakeLists.txt
cross_build.sh
运行时 LD_LIBRARY_PATH
install 打包目录
```

全部使用项目内 `C++/3rdparty/...` 路径。

不要在构建脚本中直接引用以下 SDK 绝对路径：

```text
/home/cl/EC-A3588Q/RK_NPU2_SDK/...
/home/cl/EC-A3588Q/firefly_rk3588_SDK/...
```

这些 SDK 绝对路径只作为官方例程参考路径，不作为最终项目编译和链接路径。

构建系统需要检查并修正：

```cmake
target_include_directories
target_link_libraries
install/copy librockchip_mpp.so
install/copy librga.so
install/copy librknnrt.so
```

部署后板端应能通过以下命令正常运行：

```bash
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
./face_recognition_cap_gui
```

---

## 5. 必须实现的零拷贝链路要求

请严格按照以下链路重构：

```text
V4L2 mmap/DQBUF
        ↓
Raw MJPEG packet
        ↓
MPP hardware decode
        ↓
MppFrame / MppBuffer / dma-buf fd
        ↓
RGA NV12 → RGB888 + mirror + resize + letterbox
        ↓
RKNN input rknn_tensor_mem / dma-buf fd
        ↓
rknn_run
        ↓
RKNN output rknn_tensor_mem virt_addr postprocess
```

关键要求：

1. **采集与预处理合并**（详见 §5.1、§5.2）：删除 `usb_capture_thread_func`，仅在 `PreprocessingThread` 内 DQBUF/QBUF；
2. `camera_util` 只负责 V4L2 原始 MJPEG 包抓取与释放，不做 `imdecode`；
3. `PreprocessingThread` 内统一完成 MPP 解码和 RGA 预处理；
4. RGA 输出目标必须是 RKNN zero-copy 输入内存；
5. YOLO 推理不能再依赖 `rknn_inputs_set`；
6. YOLO 输出尽量用 `rknn_set_io_mem` 绑定输出内存，并从 `output_mems[i]->virt_addr` 后处理；
7. 不允许用 `cv::Mat::clone()`、`memcpy()`、`copyMakeBorder()` 作为主链路；
8. 如因 Qt UI 显示预览必须生成图像，可单独保留低频或旁路转换，但不能污染主推理链路。

---

## 5.1 采集线程现状（重构前，必须理解）

当前 USB 摄像头路径是**双线程 + 中间“已解码 BGR 帧”缓存**，与零拷贝目标冲突。

### 架构示意（现状）

```text
┌─────────────────────────────────────────────────────────────┐
│ 线程 A：usb_capture_thread_func()  (camera_util.cc)         │
│   VIDIOC_DQBUF → mmap MJPEG 指针                             │
│   cv::imdecode (CPU 软解) → cv::Mat BGR 新内存               │
│   shared_ptr<cv::Mat> → current_frame (+ frame_mutex)        │
│   VIDIOC_QBUF                                                │
└───────────────────────────┬─────────────────────────────────┘
                            │ 浅拷贝读指针
                            ▼
┌─────────────────────────────────────────────────────────────┐
│ 线程 B：PreprocessingThread::thread_func()                  │
│   read_usb_frame() → 拷贝/引用 BGR Mat                       │
│   RGA / OpenCV：flip、resize、copyMakeBorder                 │
│   output_queue_ → 主线程 YOLO                                │
└─────────────────────────────────────────────────────────────┘

旁路：face_recognition_app.cc 也可直接 read_usb_frame()（注册预览等）
```

### 现状中的问题

| 环节 | 位置 | 问题 |
|------|------|------|
| CPU 软解 | `usb_capture_thread_func` | 30fps×1280×720 MJPEG，`imdecode` 占满单核 |
| 双线程切换 | A→B | 锁竞争、`frame_sequence` 游标、无效 wake |
| 伪“零拷贝” | `read_usb_frame` | 仅 Mat 头浅拷贝，像素已在 A 线程解码并分配 |
| V4L2 缓冲持有时间 | 采集线程 | QBUF 在软解之后才归还，缓冲周转慢 |
| 与 MPP 不匹配 | 整条链 | 上游已是 BGR 虚拟内存，无法接 MPP→RGA fd 链 |

### 相关符号（重构时要删除或弱化）

```text
camera_util.cc:
  capture_thread, capture_running
  usb_capture_thread_func()
  current_frame, frame_mutex, frame_sequence
  start_usb_capture_thread() / stop_usb_capture_thread()
  read_usb_frame()  — 主链路不再使用；可保留为调试/单帧快照（内部走 raw+软解 或禁用）

face_recognition_app.cc:
  init_camera() 里 start_usb_capture_thread()
  个别路径 read_usb_frame() — 需改为从 PreprocessingThread 取帧或 raw 调试接口
```

---

## 5.2 采集与预处理线程合并（目标架构，必须按此实现）

**原则：`camera_util` 只做 V4L2 设备与 mmap 缓冲池；`PreprocessingThread` 单线程完成「拉流 → 解码 → RGA → 投递」。**

不再存在独立的“采集 + 软解”后台线程，也不再维护 `current_frame` 环形语义。

### 架构示意（目标）

```text
┌─────────────────────────────────────────────────────────────┐
│ 唯一热路径线程：PreprocessingThread::thread_func()           │
│                                                              │
│  1. read_usb_raw_packet()     // DQBUF，仅 MJPEG 指针+index   │
│  2. MPP 解码                   // packet→NV12 MppFrame/dma-fd │
│  3. RGA                        // NV12 fd → NPU input fd      │
│  4. （可选）RGA/拷贝 → UI 预览缓冲                            │
│  5. release MppFrame                                         │
│  6. release_usb_raw_packet()   // QBUF，必须在 MPP 消费后     │
│  7. push PreprocessTask（轻量信号；processed 指向 NPU virt）   │
└───────────────────────────┬─────────────────────────────────┘
                            ▼
                   主线程：yolov8_face_run_zero_copy()
```

```text
┌─────────────────────────────────────────────────────────────┐
│ camera_util（无业务线程，可被多线程 ioctl 但约定单消费者）    │
│   load_usb_camera / close_usb_camera                         │
│   read_usb_raw_packet / release_usb_raw_packet               │
│   get_camera_fps / has_usb_camera_error                      │
│   （可选）VIDIOC_EXPBUF → export_fd 供调试，MPP 输入用 memcpy │
└─────────────────────────────────────────────────────────────┘
```

### 合并带来的收益

1. **去掉 `imdecode` 线程**：MJPEG 只在 MPP 硬解一次。
2. **V4L2 缓冲生命周期清晰**：DQBUF → MPP 读完 JPEG → QBUF，不跨线程悬挂。
3. **无中间 BGR 全局帧**：减少锁、序列号、丢帧策略复杂度。
4. **与零拷贝对齐**：解码输出 fd 在同一线程立刻交给 RGA，无需再落 CPU Mat。

### V4L2 缓冲生命周期（合并后硬性规则）

```text
read_usb_raw_packet()
    → 持有 buffer_index，禁止其他线程 QBUF
MPP 使用完毕（packet deinit / task 输入回收）
    → release_usb_raw_packet(buffer_index)
```

- `REQ_COUNT` 一般为 4；单线程串行处理时，**一帧未完成不得 DQBUF 下一帧**（除非实现多缓冲流水线且每路 index 独立记账）。
- 禁止：在 `release_usb_raw_packet` 之前对同一 mmap 区域做下一次 `imdecode` 或异步访问。

### `start_usb_capture_thread()` 的处理

| 方案 | 做法 |
|------|------|
| **推荐** | 删除后台采集线程；`start_usb_capture_thread()` 改为空实现或仅 `capture_running=true` 标记设备已 STREAMON（兼容旧调用） |
| 启动顺序 | `load_usb_camera` → `PreprocessingThread::start()`，**不再** `start_usb_capture_thread()` 拉独立线程 |
| 停止顺序 | `PreprocessingThread::stop()` → `stop_usb_capture_thread()`（STREAMOFF 打断 DQBUF）→ `close_usb_camera` |

### `read_usb_frame()` 的处置

| 调用方 | 重构后 |
|--------|--------|
| `PreprocessingThread` | **禁止**再调用；改 raw packet 路径 |
| `face_recognition_app` 注册/单帧 | 从 `PreprocessingThread::get_result()` 取 `orig_img`；或提供 `read_usb_frame` 兼容层（内部警告 + 临时软解，仅调试） |
| 帧率统计 | `get_camera_fps()` 改在 `read_usb_raw_packet` 内计数 |

### 与 `output_queue_` 的关系

合并的是 **camera_util 采集线程 ↔ 预处理线程**，不是取消预处理与主线程分工：

```text
PreprocessingThread（合并后仍保留）
    → output_queue_ + cv_output_
    → FaceRecognitionApp 主循环 get_result / YOLO

删除的是：camera_util 内部线程 ↔ PreprocessingThread 之间的 current_frame 层
```

### 实施检查清单（合并专项）

- [ ] `usb_capture_thread_func` 及 `capture_thread` 已删除或 `#if 0`
- [ ] `init_camera()` 不再依赖独立采集线程完成解码
- [ ] `PreprocessingThread` 循环内成对调用 `read_usb_raw_packet` / `release_usb_raw_packet`
- [ ] 全仓库 `grep read_usb_frame` 主链路已为 0（允许测试/调试文件保留）
- [ ] 停止相机时 STREAMOFF 能打断预处理线程中的 DQBUF 阻塞
- [ ] 日志中不再出现采集线程 `imdecode` 相关 CPU 热点

---

## 5.3 原代码清理要求（重构完成的必要条件）

零拷贝重构**不是**在新路径旁挂一套 MPP/RGA 逻辑、旧路径仍默认可用。交付前必须**删除或彻底断开**旧采集/软解/CPU 推理链路，避免：

- 双路径并存导致误调旧 API、内存泄漏或竞态；
- 注释/文档仍描述 `shared_ptr` 采集模型，误导后续维护；
- `grep` 仍能搜到主链路里的 `imdecode`、`rknn_inputs_set` 等符号。

**原则：能删则删；暂时不能删的对外符号必须 `[[deprecated]]` + 运行时 `spdlog::warn` 一次，并在头文件注明移除版本。禁止用大块 `#if 0` 长期留在生产代码中。**

### 5.3.1 `camera_util` 必须移除或清空的实现

| 类别 | 符号 / 代码 | 要求 |
|------|-------------|------|
| 后台线程 | `capture_thread`、`usb_capture_thread_func()` | **整段删除** |
| 全局帧缓存 | `current_frame`、`frame_mutex`、`frame_sequence`（采集侧游标） | **删除**；帧率统计改在 `read_usb_raw_packet` |
| CPU 软解 | `cv::imdecode` 及 `opencv2` 在采集路径的依赖 | **删除**；若全文件不再用 OpenCV，移除 `#include <opencv2/...>` |
| 旧读帧 API | `read_usb_frame()` | **删除**或改为仅调试（见下）；头文件不得再作为主接口文档化 |
| 误导注释 | 文件头「独立采集线程 + shared_ptr 零拷贝」等 | **改写**为「V4L2 Raw 包接口，解码在 PreprocessingThread」 |
| 冗长说明块 | `camera_util.cc` 末尾旧数据流 ASCII 注释（shared_ptr 流程） | **删除**或替换为 Raw+MPP 流程 |

`start_usb_capture_thread()` / `stop_usb_capture_thread()`：

- 若保留：实现改为**仅** `capture_running` + `STREAMON`/`STREAMOFF`（无 `std::thread`），并改名或加注释说明「非采集线程」；
- `close_usb_camera()` 内不得再 `join` 已不存在的 `capture_thread`。

### 5.3.2 `PreprocessingThread` 必须移除或替换的实现

| 类别 | 符号 / 代码 | 替换为 |
|------|-------------|--------|
| 读帧 | `read_frame()` + `read_usb_frame` 调用 | `read_usb_raw_packet` + MPP + RGA |
| 游标 | `frame_sequence_cursor_`（消费采集线程序列号） | 删除；队列只保留 `PreprocessTask` |
| CPU 预处理主路径 | `process_with_rga()` 内 `copyMakeBorder`、`cv::resize` 作主路径 | RGA `imfill` + `improcess` 写 NPU fd；`USE_RGA=false` 可保留 OpenCV **降级分支**，但须与零拷贝路径 `#ifdef` 或运行时二选一，不得并行执行 |
| 无用成员 | `resized_buffer_` 等仅服务 CPU padding 的缓冲 | 零拷贝稳定后删除或仅降级分支使用 |
| 构造注释 | 「use_async_usb」等已失效参数说明 | 更新 `preprocessing_thread.h` |

### 5.3.3 YOLO / 主循环必须清理的 CPU 推理路径

| 类别 | 位置 | 要求 |
|------|------|------|
| 输入拷贝 | `yolov8_face_run` + `rknn_inputs_set` | 主链路改为 `yolov8_face_run_zero_copy`；旧函数可删或仅单测 |
| 输出拷贝 | `rknn_outputs_get` + 再 memcpy 到后处理缓冲 | 改为读 `output_mems[i]->virt_addr` |
| 初始化 | `create_yolov8_face` 与 zero_copy init 重复绑定 | 统一入口，避免 ctx 上重复 `rknn_set_io_mem` |
| 主循环 | `face_recognition_app.cc` 内对 `orig_img` 再 `copyMakeBorder` + `yolov8_face_run` | 删除；检测输入仅来自 `PreprocessTask.processed_img`（NPU 已写入） |
| 单帧/注册 | `get_current_frame()` → `read_usb_frame` | 改为从 `PreprocessingThread` 最新任务或专用快照 API，**不得**回到采集线程软解 |

**说明：** `facenet.cc` 的 `rknn_inputs_set` 本次可不动，但须在报告中写明；不要复制 YOLO 的旧模式到 FaceNet。

### 5.3.4 调用方与头文件同步

重构后全仓库检索并处理每一处引用：

```bash
cd C++/face_recognition_cap
rg -n "read_usb_frame|start_usb_capture_thread|usb_capture_thread|imdecode|read_frame\(|process_with_rga|yolov8_face_run\(|rknn_inputs_set|rknn_outputs_get|copyMakeBorder|frame_sequence_cursor"
```

| 文件 | 典型需改点 |
|------|------------|
| `face_recognition_app.cc` | `init_camera()` 去掉「启动异步采集线程」；`get_current_frame()` / 注册拍照 |
| `camera_util.h` | 删除 `read_usb_frame` 声明，增加 raw 包 API |
| `model_manager.cc` | zero_copy 初始化与释放 |
| `postprocess_thread.cc` | 确认后处理读 zero_copy 输出 |
| `docs/`、`README`、`camera_util.cc` 注释 | 与实现一致 |

GUI 中 `current_frame_` 变量名可保留（仅为 UI 缓存），但**数据来源**必须是预处理线程输出的 `orig_img`，而非 `read_usb_frame`。

### 5.3.5 构建、路径与临时实验代码

| 项 | 要求 |
|----|------|
| CMake / `cross_build.sh` | 移除仅旧路径需要的宏；确认链接 `librockchip_mpp` 后无未使用 OpenCV 模块可选项说明 |
| SDK 绝对路径 | 不得保留 `/home/cl/.../RK_NPU2_SDK/...` 作为 **target_link** 路径（仅文档可参考） |
| 实验性 MPP | 删除失败的 `decode_put_packet` 重试循环、`mpp_try_decode_*` 等未使用辅助函数 |
| 未跟踪临时代码 | 勿将 `_backup`、`.bak`、`#if 0` 大块旧实现提交进主分支 |

### 5.3.6 清理完成验收（与 §9 一并检查）

交付前必须满足：

1. 上述 `rg` 命令在主链路源文件中，**除降级/测试外无命中**（或命中处附「仅 DEBUG」注释）。
2. `camera_util.cc` 行数应显著减少（无 `imdecode` 循环体）。
3. 运行时日志无「异步采集线程已启动」类旧文案，除非已改为「V4L2 raw mode」。
4. 板端 `ldd face_recognition_cap_gui` 含 `librockchip_mpp.so`，且进程线程数不再包含额外常驻采集线程（可用 `ps -T` 对比重构前后）。
5. 在 PR/总结中附 **「已删除符号清单」** 与 **「仍保留的 CPU 旁路及原因」** 两张表。

### 5.3.7 推荐提交说明格式

```text
## 已删除（旧链路）
- usb_capture_thread_func, read_usb_frame, ...

## 仍保留的 CPU 路径（非主链路）
- UI 预览 orig_img.clone()：原因 ...
- Config::Performance::USE_RGA=false 降级：原因 ...

## grep 自检
（粘贴 rg 关键命令输出摘要，0 命中或仅列 DEBUG 文件）
```

---

## 6. 需要重点修改的文件

请优先检查并修改以下文件：

```text
C++/face_recognition_cap/CMakeLists.txt
C++/face_recognition_cap/cross_build.sh

C++/face_recognition_cap/include/hardware/camera_util.h
C++/face_recognition_cap/src/hardware/camera_util.cc

C++/face_recognition_cap/include/app/preprocessing_thread.h
C++/face_recognition_cap/src/app/preprocessing_thread.cc

C++/face_recognition_cap/include/core/yolov8_face.h
C++/face_recognition_cap/src/core/yolov8_face.cc

C++/face_recognition_cap/src/app/face_recognition_app.cc
```

---

## 7. 建议接口设计

### 7.1 camera_util（瘦身为 V4L2 驱动层，见 §5.2）

将原来的软解码 + 后台采集线程替换为 **仅 Raw 包接口**（由 `PreprocessingThread` 单线程调用）：

```cpp
bool read_usb_raw_packet(void** packet_data,
                         uint32_t* packet_size,
                         uint32_t* buffer_index,
                         int* packet_fd = nullptr,        // 可选：VIDIOC_EXPBUF
                         uint32_t* buffer_length = nullptr);

void release_usb_raw_packet(uint32_t buffer_index);
```

要求：

1. `read_usb_raw_packet` 只做 `VIDIOC_DQBUF`，返回 V4L2 mmap buffer 的指针、`bytesused` 和 `buffer_index`；
2. `release_usb_raw_packet` 只做 `VIDIOC_QBUF`；
3. **禁止**在本模块内启动 `usb_capture_thread_func`、`cv::imdecode`、维护 `current_frame`；
4. buffer 生命周期：MPP 完成 packet 消费之后才能 QBUF（见 §5.2）；
5. `start_usb_capture_thread()` / `stop_usb_capture_thread()` 仅保留 STREAMON/OFF 与停止语义，**不创建解码线程**。

### 7.2 PreprocessingThread（合并后的唯一采集+预处理线程，见 §5.2）

本线程 = 原「采集线程」+「预处理线程」的热路径合体；`thread_func` 内顺序执行 V4L2→MPP→RGA。

增加 MPP 解码器上下文：

```cpp
MppCtx mpp_ctx = nullptr;
MppApi* mpp_api = nullptr;
```

增加 RKNN 输入内存注入信息：

```cpp
int npu_input_fd = -1;
void* npu_input_virt = nullptr;
int input_width = 640;
int input_height = 640;
int input_channel = 3;
```

处理流程（同一线程内顺序执行，无跨线程帧缓存）：

```text
read_usb_raw_packet
→ mpp packet decode
→ get MppFrame
→ get decoded dma-buf fd
→ RGA imfill black background
→ RGA improcess resize/mirror/format convert into RKNN input fd
→ release MppFrame
→ release_usb_raw_packet          // 必须在 MPP 消费 JPEG 之后
→ push lightweight task signal    // processed_img 浅包装 NPU virt_addr
```

### 7.3 YOLOv8 Face

增加 zero-copy 初始化和运行接口：

```cpp
int yolov8_face_init_zero_copy(rknn_context ctx,
                               rknn_tensor_mem** input_mem,
                               std::vector<rknn_tensor_mem*>& output_mems);

int yolov8_face_run_zero_copy(rknn_context ctx);

int yolov8_face_release_zero_copy(rknn_context ctx,
                                  rknn_tensor_mem* input_mem,
                                  std::vector<rknn_tensor_mem*>& output_mems);
```

要求：

1. 初始化阶段查询 input/output tensor attributes；
2. 使用 `rknn_create_mem` 创建 input/output tensor memory；
3. 使用 `rknn_set_io_mem` 绑定输入输出；
4. 推理时只调用 `rknn_run`；
5. 后处理直接读取 `output_mems[i]->virt_addr`；
6. release 阶段正确调用 `rknn_destroy_mem`。

---

## 8. 编译和链接要求

请修改 CMake 和交叉编译脚本，使其使用项目内第三方库路径：

```text
C++/3rdparty/rknn/
C++/3rdparty/rga/
C++/3rdparty/mpp/
```

需要确认以下内容全部来自项目内 `C++/3rdparty`：

```text
rknn include path
rga include path
mpp include path
librknnrt.so
librga.so
librockchip_mpp.so
```

如果库文件名有版本后缀，例如：

```text
librockchip_mpp.so.1
```

请在脚本中兼容 `.so` 和 `.so.1` 两种情况。

---

## 9. 验收标准

完成后请给出以下结果：

1. 修改过的文件列表；
2. 每个文件的关键改动说明；
3. CMake 和 `cross_build.sh` 的第三方库路径检查结果；
4. 是否还存在 SDK 绝对路径引用；
5. 是否还存在主链路中的 `cv::imdecode`、`cv::copyMakeBorder`、`clone()`、`rknn_inputs_set`（见 §5.3，`rg` 自检结果）；
6. 是否已按 §5.3 删除旧采集线程、`read_usb_frame`、重复 YOLO CPU 路径；
7. 是否使用了 `rknn_create_mem`、`rknn_set_io_mem`、`rknn_run`；
8. 是否正确处理了 MPP `info change`；
9. 是否能成功交叉编译；
10. 板端运行命令；
11. 是否附「已删除符号清单」与 grep 自检（§5.3.6、§5.3.7）；
12. 当前仍未完成或需要人工确认的问题。

---

## 10. 约束条件

请严格遵守：

1. 不要大规模重写业务逻辑；
2. 不要破坏现有 Qt UI、用户管理、考勤记录、数据库逻辑；
3. 不要影响 w600k_ResNet50 人脸特征识别链路，除非确实需要适配 YOLO 检测结果输入；
4. 保持代码结构清晰，新增 MPP/RGA/RKNN zero-copy 封装时优先放在硬件层或 core 层；
5. 修改前先搜索现有项目中的 RGA、RKNN、CMake、cross_build 相关配置，避免重复造路径；
6. 每一步修改后尽量保持可编译；
7. 遇到 fd 生命周期、stride 对齐、RGA 格式不匹配、MPP info change 等问题，不要跳过，要在代码注释和最终报告中说明处理方式；
8. **必须清理旧代码**（§5.3）：不得新旧双链路同时编译进主路径；交付前完成 `rg` 自检并列出已删除符号。

---

## 11. 最终要求

请不要只做表层替换。重构完成后必须明确说明：

- 当前是否已经实现真正 zero-copy；
- 哪些环节仍存在 CPU 拷贝；
- 为什么这些拷贝暂时不可避免；
- 后续如果要进一步优化，需要修改哪些模块；
- 当前实现与官方 `rknn_zero_copy`、`rknn_yolov5_demo`、`mpi_dec_test.c` 的对应关系。

请优先保证工程可编译、链路清晰、资源释放正确，再进行性能优化。

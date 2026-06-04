# RK3588 NPU 人脸识别系统后续修复计划

> 创建日期: 2026-06-04  
> 当前分支: yolov8n-face-dev  
> 目标: 在已完成的三批修复基础上，继续按风险和收益逐步清理剩余问题。

## 已完成修复

- 线程安全:
  - `FeatureLibrary` 读接口加锁，列表 getter 改为返回拷贝。
  - `FaceRecognitionApp::initialized_`、`camera_initialized_` 改为 atomic。
  - `RecognitionThread` 平均耗时统计改为 CAS 更新。
- 热路径性能:
  - YOLO 输出拷贝移出 NPU 输入锁。
  - NMS 外层候选框坐标移出内层循环。
  - `RecognitionTask` 改为移动提交。
  - `PerformanceMonitor` 单帧指标批量记录。
- 健壮性:
  - `l2_normalize()`、`cos_similarity()` 增加零值保护。
  - SSE 增加总请求超时。
  - 摄像头设备号校验。
  - `close_usb_camera()` 在 `STREAMOFF` 前排空缓冲。
  - Dashboard AI 聊天历史增加数量上限。
  - 新增 `NpuResourceManager`，集中仲裁 `VISION_ACTIVE` / `LLM_ACTIVE` / `IDLE`，并移除 RKNN 释放后的 `500ms` 固定等待。
  - 移除 GUI 侧 RKNN 释放影子状态，避免继续维护分散的 NPU 资源状态。
- 数据库:
  - DAO 查询去除 `SELECT *`，改为显式列清单。
  - `FaceFeatureDAO::batch_insert()` 事务内复用 PreparedStatement。
  - `FaceFeatureDAO::find_all_active()` 增加 `reserve()`。

## 待修复问题

### P0 / 架构风险

1. ~~NPU 资源集中仲裁器~~（已修复）
   - `FaceRecognitionApp` 加载 RKNN 前申请 `VISION_ACTIVE`，释放模型后回到 `IDLE`。
   - `LocalLLMThread` 初始化 RKLLM 前申请 `LLM_ACTIVE`，销毁/失败后回到 `IDLE`。
   - `release_models()` 已移除 `500ms` 固定等待，状态切换由 `NpuResourceManager` 统一记录和拒绝冲突申请。
   - GUI 仅保留异步任务重入保护和 LLM 释放等待标志，不再保存 RKNN 已为 LLM 释放的资源影子状态。

### P1 / 性能关键路径

2. DFL 解码跨步访问
   - `postprocess.cc::process_i8()` 仍按 NCHW 通道跨步读取。
   - 可选方案: channel tile、预取、或重排为更连续的访问模式。

3. FaceNet 零拷贝
   - `facenet_inference()` 仍使用 `rknn_inputs_set` / `rknn_outputs_get`。
   - 建议参考 YOLO zero-copy 路径实现输入/输出 io mem。

4. GUI 线程数据库查询
   - `MainWindow::on_frame_ready()` 仍逐帧调用 `is_duplicate_check()` 和 `auto_determine_check_type()`。
   - 建议增加内存缓存或后台查询队列。

5. FaceNet 输出生命周期
   - 当前返回 RKNN 内部输出 buffer 指针，依赖调用方释放。
   - 建议改为调用方提供 512-float 输出缓冲，内部完成拷贝和释放。

6. `similarTransform()` 重写
   - 当前 SVD 分支仍有历史逻辑问题。
   - 建议用更清晰的相似变换实现，或封装 OpenCV 仿射估计。

7. 快排与 softmax 优化
   - `quick_sort_indice_inverse()` 仍是递归手写排序并修改概率数组。
   - DFL 每边 softmax 仍重复扫描。

### P2 / 中优先级

8. `PreparedStatement` 每列读取加锁
   - DAO 已显式列清单，但 column getter 仍每列单独获取 `db_mutex_`。
   - 建议增加行级读取批处理接口，或在 DAO 层集中持锁。

9. 预览 RGA 在 NPU 输入锁内执行
   - 当前预处理线程仍在 `npu_mem_mutex_` 内生成 UI 预览。
   - 建议只在锁内写 NPU 输入，预览转换移到锁外。

10. FaceNet 多人脸串行推理
    - 可考虑 CPU 对齐与 NPU 推理流水线化。





## 验证命令

```bash
RK3588-NPU/C++/face_recognition_cap/cross_build.sh
```

涉及摄像头、NPU 或 RKLLM 资源切换的改动，还需要在 RK3588 板端运行 GUI 做手工验证。

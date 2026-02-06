# 多线程复审报告

**复审日期**: 2026-02-06
**复审范围**: `RecognitionThread`、`LocalLLMThread`、`AgentWorker`、`ConfigManager`、`ConversationMemory`

---

## 修复验证（与当前代码一致）

| 问题 | 状态 | 验证说明 |
|------|------|----------|
| RecognitionThread 回调读写无互斥保护 | ❌未修复 | `set_*_callback()` 无锁写入（72-82），`process_task()` 无锁读取并调用（182/235/279/294） |
| RKLLM 回调线程不确定 | ✅可接受 | `LocalLLMThread` 在工作线程 `emit` 信号，Qt 跨线程连接默认队列派发，当前模式可用 |
| ConfigManager 单例线程安全 | 🔄部分修复 | 已改为 Meyers Singleton（`config_manager.cc:32-34`，commit `52d330e`）；但 `QSettings* settings_` 为共享实例，无显式读写锁 |
| ConversationMemory 非递归锁 | ✅可接受 | 当前 `trimMessages()` 仅在持锁路径调用，未见递归加锁路径 |
| AgentWorker 停止响应依赖 LLM 调用返回 | 🔄部分修复 | `wrapped_callback` 已检查 `stop_requested_`；若 RKLLM 正在长推理，仍需等待当前调用返回 |

---

## 关键未修复项详解

### 1) RecognitionThread 回调竞态风险（中优先级）

- 写入点：`set_recognition_callback` / `set_frame_callback` / `set_registration_callback`
- 读取点：`process_task()` 中多处直接判断与调用
- 风险：`std::function` 跨线程并发读写无同步，存在数据竞争

**建议修复**:
1. 新增 `callback_mutex_`
2. setter 使用 `std::lock_guard<std::mutex>`
3. `process_task()` 先在锁内复制回调到局部变量，再在锁外调用

---

## 当前评估

| 分类 | 数量 |
|------|------|
| 已修复 / 可接受 | 2 |
| 部分修复 | 2 |
| 未修复 | 1 |

**总体结论**: 多线程主干设计可用，最需要尽快处理的是 `RecognitionThread` 回调同步问题。

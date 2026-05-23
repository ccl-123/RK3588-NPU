# 本地大规模语言模型 (On-Device LLM) 部署指南

> **适用平台**: Rockchip RK3588 (NPU)  
> **模型格式**: RKLLM (W8A8/W4A16 量化)  
> **文档版本**: v1.3
> **最后更新**: 2026-04-01

## 1. 为什么选择本地部署？(核心优势)

相较于外部云端 API，在边缘设备 (Edge Device) 本地运行大模型具有不可替代的优势：

*   **🛡️ 极致隐私安全**: 所有考勤数据、人员信息、对话记录**完全不出域**，无需上传到云端，从根本上消除了数据泄露风险。
*   **⚡ 极低延迟响应**: 省去了网络传输耗时，首字生成延迟 (TTFT) 可低至 **< 200ms**，提供丝滑的实时对话体验。
*   **💰 零运营成本**: 利用设备自带的 NPU 算力（6 TOPS），**无需支付昂贵的 API 调用费用**（Token 计费）。
*   **📶 离线闭环运行**: 在无网络或内网环境中依然能提供完整的智能分析服务，业务连续性极强。

---

## 2. 部署准备 (文件清单)

在开始部署前，请确保您已准备好以下核心文件：

### 2.1 模型文件 (.rkllm)
这是经过量化和转换后的模型权重文件，专用于 RK3588 NPU。
*   **推荐模型**: `Qwen2-1.5B-Instruct` 或 `Qwen1.5-1.8B-Chat`
*   **目标路径**: `/home/firefly/open_project/qwen3-vl-2b-instruct_w8a8_rk3588.rkllm` (示例)
*   **获取方式**: 使用 `rknn-llm` 工具链转换，或从官方 Model Zoo 下载 RK3588 适配版本。

### 2.2 运行时库 (Runtime Libraries)
确保以下动态库文件存在，并包含在 `LD_LIBRARY_PATH` 中：
*   **`librkllmrt.so`**: RKLLM 推理引擎核心库。
    *   *项目中已包含*: `C++/rkllm_runtime/lib/librkllmrt.so`
*   **`librknnrt.so`**: RKNN 推理引擎核心库 (用于视觉模型)。
    *   *项目中已包含*: `C++/runtime/librknn_api/aarch64/librknnrt.so`

---

## 3. 部署步骤 (操作指南)

### 步骤 1: 传输模型文件
将准备好的 `.rkllm` 模型文件上传到 RK3588 开发板的指定目录。

```bash
# 在 PC 端执行 (假设开发板 IP 为 192.168.1.103)
scp qwen3-vl-2b-instruct_w8a8_rk3588.rkllm firefly@192.168.1.103:/home/firefly/open_project/
```

### 步骤 2: 配置模型路径
推荐直接使用环境变量，而不是手改代码常量：

```bash
export LOCAL_LLM_MODEL_PATH=/home/firefly/open_project/qwen3-vl-2b-instruct_w8a8_rk3588.rkllm
```

说明：

- 代码优先读取 `LOCAL_LLM_MODEL_PATH`
- 只有环境变量为空时，才会回退到 `config.h` 默认值

### 步骤 3: 编译项目
使用提供的交叉编译脚本进行构建。构建脚本会自动处理库文件的链接。

```bash
cd C++/face_recognition_cap
./cross_build.sh
```

### 步骤 4: 部署程序
构建完成后，将可执行文件和库文件同步到设备。

```bash
# 脚本会自动执行部署，或者手动传输 install 目录
scp -r install/face_recognition_cap firefly@192.168.1.103:/home/firefly/open_project/edge2-npu/C++/
```

### 步骤 5: 运行验证
在开发板上运行程序，并确保环境变量配置正确。

```bash
# SSH 登录开发板
ssh firefly@192.168.1.103

# 进入部署目录
cd /home/firefly/open_project/edge2-npu/C++/face_recognition_cap

# 设置库路径 (关键步骤：确保能找到 librkllmrt.so)
export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH

# 启动 GUI 程序
./face_recognition_cap_gui -platform xcb
```

## 4.1 与远端模式的关系

本地模式与远端模式可以共存：

- 本地模式依赖 `LOCAL_LLM_MODEL_PATH`
- 远端模式通过 `LLAMA_CPP_SERVER_URL` 调用 OpenAI 兼容接口

远端模式示例：

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```

---

## 4. 验证本地 LLM 功能

1.  启动程序后，点击主界面右上角的 **"智能看板"** (Smart Dashboard) 图标。
2.  进入看板页面后，找到底部的输入框区域。
3.  点击输入框左侧的 **"☁ (云端)"** 按钮，将其切换为 **"🖥 (本地)"** 模式（按钮变浅色，提示"本地大模型"）。
4.  观察状态栏，显示 **"加载中..."**，约 5-10 秒后变为绿色 **"就绪"**。
5.  在输入框中提问，例如："分析一下今天的考勤情况" 或 "谁迟到了？"。
6.  系统将以 **流式打字机** 效果实时输出分析结果。

---

## 5. 故障排查 (Troubleshooting)

### Q1: 点击"加载"后程序闪退？
*   **原因**: 通常是模型文件路径错误，或 NPU 显存不足。
*   **检查**:
    1. 确认 `config.h` 中的路径文件确实存在。
    2. 检查 `LD_LIBRARY_PATH` 是否包含了 `librkllmrt.so`。
    3. 查看终端日志，搜索 `rkllm_init` 错误码。

### Q2: 提示 "rknn_run failed: -1" 或 NPU 相关错误？
*   **原因**: NPU 资源冲突。RKNN 和 RKLLM 不能同时运行。
*   **解决**: 程序设计了自动互斥机制，**请勿在后台手动运行其他占用 NPU 的程序**。

### Q3: 回复内容被截断？
*   **原因**: 输出超过了 `MAX_NEW_TOKENS` 限制。
*   **解决**: 增大 `config.h` 中的 `MAX_NEW_TOKENS` 值（建议不超过 1024）。

### Q4: 提示 "context limit exceeded"？
*   **原因**: 输入的考勤数据量过大，超过了模型支持的上下文窗口 (4096 tokens)。
*   **解决**: 减少查询的时间范围（例如从 30 天改为 7 天），或在 Service 层实现数据摘要逻辑。

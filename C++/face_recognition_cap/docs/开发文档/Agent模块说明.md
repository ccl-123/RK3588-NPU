# AI Agent 模块说明

> 版本: v2.0
> 更新日期: 2026-04-01

## 概述

当前项目中的 AI Agent 已经从早期的“本地问答 + 少量工具调用”演进为一套完整的端云双模式工具型助手：

- 本地模式：基于 RKLLM 的 Agent / Chat 双模式
- 远端模式：通过 `LLAMA_CPP_SERVER_URL` 调用 OpenAI 兼容 `/v1/chat/completions`
- 工具系统：支持全局考勤、单员工考勤、部门考勤、考勤排行、缺卡缺勤、用户查询、系统信息、帮助、计算器

因此，旧文档中“云端 Agent 未实现”“工具只有 5 个核心工具”等描述已经不再准确。

## 当前能力状态

| 模块 | 状态 | 说明 |
|------|------|------|
| ReAct Agent 核心 | ✅ 完成 | 多轮思考-行动-观察循环 |
| 工具注册与执行 | ✅ 完成 | ToolRegistry + ToolExecutor + 结构化结果 |
| 对话记忆管理 | ✅ 完成 | 保留最近用户轮次，不再按固定消息数粗裁剪 |
| 本地 Agent | ✅ 完成 | RKLLM 本地推理 |
| OpenAI 兼容远端 | ✅ 完成 | 自定义 `/v1/chat/completions` 接口 |
| 单员工考勤工具 | ✅ 完成 | `lookup_user_attendance` |
| 部门考勤工具 | ✅ 完成 | `lookup_department_attendance` |
| 排名工具 | ✅ 完成 | `lookup_attendance_ranking` |
| 缺卡缺勤工具 | ✅ 完成 | `lookup_missing_attendance` |

## 架构链路

```text
用户输入
  ↓
Dashboard / AiAnalysisService / LocalAiAnalysisService
  ↓
AgentService
  ↓
ReactAgent
  ├─ 解析模型输出
  ├─ 识别 <tool_call> 调用工具
  ├─ 接收结构化 ToolExecutionResult
  ├─ 将结果回灌为“工具执行结果”观察块
  └─ 输出 <answer> 最终回答
```

## 远端协议说明

### OpenAI 兼容远端

远端模式使用 OpenAI 兼容接口：

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```

注意：

- 只写基地址，不要手动补 `/v1/chat/completions`
- 变量名虽然保留 `LLAMA_CPP_*`，但不要求服务必须是 llama.cpp
- 只要兼容 OpenAI Chat Completions 基本协议即可

详细说明见：

- [OpenAI兼容接口接入说明.md](/home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap/docs/开发文档/OpenAI兼容接口接入说明.md)

## 环境变量清单

### 本地 RKLLM

```bash
export LOCAL_LLM_MODEL_PATH=/path/to/your_model.rkllm
```

### OpenAI 兼容远端

```bash
export LLAMA_CPP_SERVER_URL=http://127.0.0.1:8080
export LLAMA_CPP_SERVER_MODEL=gpt-4o-mini
export LLAMA_CPP_SERVER_API_KEY=sk-your-key
```

## 当前内置工具

当前默认工具集已经不是旧版文档里的 5 个，而是下面 9 个：

### 1. `query_attendance`

全局考勤查询工具，适合：

- “今天多少人打卡”
- “近 30 天异常记录”
- “本周迟到情况”

### 2. `lookup_user_attendance`

单员工考勤工具，适合：

- “张三这周出勤怎么样”
- “李四最近一次打卡是什么时候”
- “王五本月异常打卡有哪些”

### 3. `lookup_department_attendance`

部门考勤工具，适合：

- “研发部今天出勤率多少”
- “销售部近 30 天异常情况怎么样”
- “行政部今天谁没来”

### 4. `lookup_attendance_ranking`

排行工具，适合：

- “本月迟到最多的是谁”
- “最近 7 天异常最多的前 5 个人是谁”
- “哪个部门异常率最高”

### 5. `lookup_missing_attendance`

缺卡/缺勤工具，适合：

- “今天谁还没签到”
- “谁只签到没签退”
- “哪些人连续两天没打卡”

### 6. `query_user`

用户信息查询工具，适合：

- “系统里一共有多少员工”
- “张三属于哪个部门”
- “列出所有用户”

### 7. `system_info`

系统状态与配置工具，适合：

- “现在几点”
- “当前识别阈值是多少”
- “摄像头配置是什么”

### 8. `help`

工具帮助说明。

### 9. `calculator`

简单四则运算与百分比计算。

## 工具调用格式

模型通过 XML 风格标签发起调用：

```xml
<tool_call>{"name":"lookup_department_attendance","arguments":{"department":"研发部","query_type":"summary","date_range":"today"}}</tool_call>
```

工具执行后，系统不再使用旧文档里的私有 `<|tool_response|>` 包装，而是以统一“工具执行结果”观察块继续驱动后续推理。

最终答案格式仍然是：

```xml
<answer>最终回答内容</answer>
```

## 配置项

关键配置位于：

- [config.h](/home/cl/EC-A3588Q/face_attendance/RK3588-NPU/C++/face_recognition_cap/include/config/config.h)

重点项包括：

```cpp
namespace Config::Agent {
    constexpr int MAX_ITERATIONS = 10;
    constexpr int CONVERSATION_HISTORY = 10;
    constexpr int LLM_TIMEOUT_MS = 200000;
    constexpr bool STREAM_OUTPUT = true;

    namespace Cloud {
    }
}
```

## 扩展新工具时不要漏改的地方

新增工具时，至少要同步处理这几处：

1. `include/tools/` 与 `src/tools/` 新增工具类
2. `AgentService::registerBuiltinTools()` 注册工具
3. `CMakeLists.txt` 纳入源码
4. `HelpTool` 更新用户可见帮助
5. `PromptTemplates` 更新工具说明和示例
6. 如果工具语义较大，补用户文档或 README

否则就会出现“代码里已经有工具，但模型提示词和帮助里不知道它存在”的文档漂移问题。

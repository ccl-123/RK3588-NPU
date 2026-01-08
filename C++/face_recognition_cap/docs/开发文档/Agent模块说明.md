# AI Agent 模块说明

> 版本: v1.3
> 更新日期: 2026-01-08

## 概述

本项目实现了完整的 ReAct (Reasoning and Acting) Agent 框架，支持本地 LLM 进行智能问答和工具调用。Agent 可以理解用户自然语言问题，自动调用相关工具获取数据，并生成智能回答。

## 功能完成状态

| 模块 | 状态 | 说明 |
|------|------|------|
| ReAct Agent 核心 | ✅ 完成 | 思考-行动-观察循环 |
| 工具注册与执行 | ✅ 完成 | ToolRegistry + ToolExecutor |
| 考勤查询工具 | ✅ 完成 | 今日/本周/本月考勤数据 |
| 用户查询工具 | ✅ 完成 | 用户列表/搜索/统计 |
| 系统信息工具 | ✅ 完成 | 时间/状态/配置查询 |
| 对话记忆管理 | ✅ 完成 | 多轮对话上下文 |
| 本地 LLM 集成 | ✅ 完成 | RKLLM + Agent 模式自动启用 |
| Agent 自动初始化 | ✅ 完成 | Dashboard 页面加载时自动初始化 |
| Agent 自动调用 | ✅ 完成 | 本地 LLM 模式自动使用 Agent（v1.3） |
| 云端 Agent 模式 | ❌ 未实现 | 云端仅支持传统提示词模式 |

> **注意**: RAG 和 Embedding 模块已移除（v1.2），因为当前没有可用的嵌入模型支持。

## 架构设计

```
用户输入
   ↓
AgentService (统一入口)
   ↓
ReactAgent (ReAct 循环)
   ├── 解析 LLM 输出
   ├── 识别工具调用 → ToolExecutor → 执行工具 → 返回结果
   ├── 识别思考过程 → 继续推理
   └── 识别最终答案 → 返回用户
   ↓
LocalAiAnalysisService (LLM 推理)
```

## 目录结构

```
include/
├── agent/
│   ├── agent_service.h       # 统一服务入口
│   ├── conversation_memory.h # 对话记忆管理
│   ├── react_agent.h         # ReAct Agent 核心
│   ├── tool_executor.h       # 工具执行器
│   └── tool_registry.h       # 工具注册表
├── tools/
│   ├── attendance_tool.h     # 考勤查询工具
│   ├── base_tool.h           # 工具抽象基类
│   ├── system_tool.h         # 系统工具集
│   └── user_tool.h           # 用户查询工具
├── gui_services/             # GUI 服务（从 gui/ 解耦）
│   ├── ai_analysis_service.h
│   ├── ai_prompt_builder.h
│   ├── local_ai_analysis_service.h
│   └── ...
└── gui_utils/                # GUI 工具类（从 gui/ 解耦）
    ├── audio_manager.h
    ├── config_manager.h
    └── ...
```

## 核心组件

### 1. BaseTool (工具基类)

所有工具的抽象接口：

```cpp
class BaseTool {
public:
    virtual QString name() const = 0;           // 工具名称
    virtual QString description() const = 0;    // 工具描述
    virtual QJsonObject parametersSchema() const = 0;  // 参数 Schema
    virtual QString execute(const QJsonObject& args) = 0;  // 执行
};
```

### 2. ToolRegistry (工具注册表)

管理所有可用工具：

```cpp
ToolRegistry registry;
registry.registerTool(std::make_unique<AttendanceTool>(svc));
QString tools_json = registry.getToolsJson();
```

### 3. ReactAgent (ReAct 核心)

实现思考-行动-观察循环：

```cpp
ReactAgent agent(tools, memory, config);
QString answer = agent.run(user_input, llm_callback);
```

### 4. AgentService (统一服务)

对外统一接口：

```cpp
AgentService service(config);
service.registerBuiltinTools(attendance_svc, user_svc);
QString answer = service.chat("今天有多少人打卡？", llm_callback);
```

## 内置工具详解

目前系统内置了 5 个核心工具，覆盖考勤、用户、系统管理等常用场景。

### 1. 考勤查询工具 (`query_attendance`)

用于查询和统计考勤数据，支持按天、周、月或指定日期查询。

- **功能**:
  - 查询今日实时考勤概况
  - 统计本周/本月出勤趋势
  - 查询指定日期的历史记录
  - 统计指标：总人数、签到数、迟到数、早退数、正常打卡数

- **参数说明**:
  - `date_range` (可选): 时间范围，支持 `today` (今日), `week` (本周), `month` (本月)
  - `date` (可选): 指定日期，格式 `YYYY-MM-DD` (如 "2023-10-01")
  - *注*: `date` 参数优先级高于 `date_range`

- **调用示例**:
  - "今天有多少人打卡？" → `{"date_range": "today"}`
  - "本周迟到情况如何？" → `{"date_range": "week"}`
  - "查询2024年1月1日的考勤" → `{"date": "2024-01-01"}`

### 2. 用户查询工具 (`query_user`)

用于查询用户信息、搜索特定员工或查看整体人员统计。

- **功能**:
  - **统计信息**: 总用户数、启用/禁用数、各部门人数分布
  - **精确查找**: 按 ID 获取详细信息
  - **模糊搜索**: 按姓名搜索用户（支持模糊匹配）
  - **列表查询**: 列出所有注册用户

- **参数说明**:
  - `action` (必填): 操作类型
    - `stats`: 获取统计概览 (默认)
    - `get_by_id`: 按 ID 查询 (需提供 `user_id`)
    - `get_by_name`: 按姓名查询 (需提供 `name`)
    - `list_all`: 列出所有用户
  - `user_id` (可选): 用户 ID (整数)
  - `name` (可选): 用户姓名或关键词

- **调用示例**:
  - "现在有多少个员工？" → `{"action": "stats"}`
  - "帮我查一下王小明的资料" → `{"action": "get_by_name", "name": "王小明"}`
  - "ID是1001的员工是谁？" → `{"action": "get_by_id", "user_id": 1001}`
  - "列出所有研发部的人" → `{"action": "list_all"}` (Agent 会自行过滤返回的列表)

### 3. 系统信息工具 (`system_info`)

获取设备运行状态、时间信息和系统配置。

- **功能**:
  - **日期时间**: 当前日期、时间、星期、时段（上午/下午等）
  - **系统状态**: 设备ID、位置、摄像头参数、识别阈值、LLM 模型路径
  - **考勤配置**: 上下班时间、迟到/早退容忍阈值、当前是否工作时间

- **参数说明**:
  - `query_type` (可选): 查询类型
    - `all`: 所有信息 (默认)
    - `datetime`: 仅日期时间
    - `status`: 硬件与系统状态
    - `config`: 考勤规则配置

- **调用示例**:
  - "现在几点了？" → `{"query_type": "datetime"}`
  - "系统配置参数是什么？" → `{"query_type": "status"}`
  - "几点算迟到？" → `{"query_type": "config"}`

### 4. 数学计算工具 (`calculator`)

处理涉及数值计算的查询，弥补 LLM 数学能力的不足。

- **功能**: 支持加减乘除及百分比计算
- **参数**: `expression` (数学表达式字符串)
- **示例**: "出勤率是多少？(15/20)" → `{"expression": "15/20*100"}`

### 5. 帮助工具 (`help`)

提供系统功能说明和使用指南。

- **功能**:解释可用工具和功能范围
- **参数**: `topic` (tools/attendance/user/all)
- **示例**: "你能做什么？" → `{"topic": "all"}`

## 工具调用格式

LLM 使用 XML 标签格式调用工具：

```xml
<tool_call>{"name":"query_attendance","arguments":{"date_range":"today"}}</tool_call>
```

工具返回结果：

```xml
<|tool_response|>
[query_attendance 返回结果]
今日考勤统计...
<|/tool_response|>
```

最终答案：

```xml
<answer>根据查询结果，今日共有 15 人签到...</answer>
```

## 配置项

在 `include/config/config.h` 中配置：

```cpp
namespace Config::Agent {
    constexpr int MAX_ITERATIONS = 5;        // ReAct 最大迭代次数
    constexpr int CONVERSATION_HISTORY = 10; // 保留的对话轮数
    constexpr int LLM_TIMEOUT_MS = 60000;    // LLM 超时时间
    constexpr bool STREAM_OUTPUT = true;     // 流式输出

    namespace Tools {
        constexpr bool ENABLE_ATTENDANCE = true;
        constexpr bool ENABLE_USER = true;
        constexpr bool ENABLE_SYSTEM = true;
    }
}
```

## 扩展新工具

### 步骤 1: 创建工具类

```cpp
// include/tools/my_tool.h
#pragma once
#include "tools/base_tool.h"

namespace agent {

class MyTool : public BaseTool {
public:
    QString name() const override { return "my_tool"; }
    QString description() const override {
        return "我的自定义工具描述";
    }
    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"param1", QJsonObject{
                    {"type", "string"},
                    {"description", "参数1说明"}
                }}
            }},
            {"required", QJsonArray{"param1"}}
        };
    }
    QString execute(const QJsonObject& args) override {
        QString param1 = args["param1"].toString();
        // 实现工具逻辑
        return "工具执行结果";
    }
};

} // namespace agent
```

### 步骤 2: 注册工具

```cpp
// 在 AgentService 或 LocalAiAnalysisService 中
tools_->registerTool(std::make_unique<MyTool>());
```

### 步骤 3: 更新 CMakeLists.txt

```cmake
set(TOOLS_SOURCES
    src/tools/attendance_tool.cc
    src/tools/user_tool.cc
    src/tools/system_tool.cc
    src/tools/my_tool.cc  # 新增
)
```

## UI 集成

Dashboard 页面显示 Agent 状态：

- 🤔 思考中... (蓝色)
- 🔧 调用工具: xxx (黄色)
- ✅ xxx 完成 (绿色)

相关信号：

```cpp
// LocalAiAnalysisService
signals:
    void agentThinking();
    void agentToolCalling(const QString& tool_name);
    void agentToolCompleted(const QString& tool_name, const QString& result);
```

## 云端 vs 本地模式

| 特性 | 云端模式 (AiAnalysisService) | 本地模式 (LocalAiAnalysisService) |
|------|------------------------------|-----------------------------------|
| Agent 支持 | ❌ 不支持 | ✅ 支持 |
| 工具调用 | ❌ 无 | ✅ 有 |
| 模式切换 | ❌ 仅 Chat | ✅ Agent / Chat 可切换 |
| 提示词系统 | AiPromptBuilder (数据+问题) | ReactAgent 或 AiPromptBuilder |
| 离线运行 | ❌ 需要网络 | ✅ 完全离线 |
| 响应速度 | 依赖网络 | 本地推理，首字 <200ms |

### Agent/Chat 模式切换

本地 LLM 模式下支持两种对话模式：

| 模式 | 按钮显示 | 说明 |
|------|----------|------|
| **Agent 模式** | `Agent` (绿色) | 智能工具调用，自动查询数据库获取考勤数据 |
| **Chat 模式** | `Chat` (橙色) | 传统对话，需手动附带数据上下文 |

**UI 位置**: Dashboard 页面底部输入框旁的 `Agent/Chat` 按钮

**切换逻辑**:
- 按钮仅在本地 LLM 加载完成后启用
- 云端模式下按钮禁用（云端不支持 Agent）
- 切换后立即生效，影响下一次发送

## Agent 调用链路

### 完整调用流程

```
用户在 Dashboard 输入问题并点击发送
                ↓
        DashboardPage::on_ai_input_send()
                ↓
        DashboardPage::on_ai_analysis_clicked()
                ↓ (is_local_llm_ == true)
        LocalAiAnalysisService::requestAnalysis()
                ↓
    ┌───────────────────────────────────────┐
    │  检查 agent_mode_ && agent_service_   │
    └───────────────────────────────────────┘
                ↓                    ↓
        [Agent 模式]            [Chat 模式]
                ↓                    ↓
    requestAgentChat()      AiPromptBuilder::buildPrompt()
                ↓                    ↓
    AgentService::chat()    LocalLLMThread::requestInference()
                ↓                    ↓
    ReactAgent::run()           直接返回 LLM 输出
                ↓
    ┌─────────────────────────────────────────┐
    │           ReAct 循环 (最多 5 次)          │
    │  1. 调用 LLM 获取输出                    │
    │  2. 解析输出类型:                        │
    │     - <tool_call> → ToolExecutor        │
    │     - <answer> → 返回最终答案            │
    │     - <thought> → 继续思考               │
    │  3. 工具执行后将结果反馈给 LLM           │
    │  4. 循环直到获得 <answer> 或达到上限     │
    └─────────────────────────────────────────┘
                ↓
        emit answerReady(answer)
                ↓
        DashboardPage::on_ai_result_ready()
                ↓
        显示在聊天界面
```

### 关键代码位置

| 步骤 | 文件 | 函数/行号 |
|------|------|-----------|
| UI 发送 | `dashboard_page.cc` | `on_ai_input_send()` |
| 模式判断 | `local_ai_analysis_service.cc:74` | `if (agent_mode_ && agent_service_)` |
| Agent 入口 | `local_ai_analysis_service.cc:158` | `requestAgentChat()` |
| ReAct 循环 | `react_agent.cc:43` | `while (running_ && iteration < max)` |
| 工具解析 | `tool_executor.cc:26` | `parseToolCall()` |
| 工具执行 | `tool_executor.cc:60` | `execute()` |

### 提示词系统说明

当前存在**两套独立的提示词系统**：

1. **Chat 模式** (`AiPromptBuilder::buildPrompt`)
   - 格式: `时间 + 考勤数据 + 用户问题`
   - 用于: 云端 API 和本地 Chat 模式
   - 特点: 简单直接，数据由 UI 层收集并附带

2. **Agent 模式** (`ReactAgent::getDefaultSystemPrompt`)
   - 格式: `系统提示 + 工具定义 + 对话历史 + 用户问题`
   - 用于: 本地 Agent 模式
   - 特点: ReAct 循环，Agent 自动调用工具获取数据

**选择建议**:
- 需要实时查询数据 → Agent 模式
- 需要分析已有数据 → Chat 模式（UI 会自动附带数据）

## 注意事项

1. **NPU 资源互斥**: Agent 使用 RKLLM，与人脸识别 RKNN 共享 NPU，需要互斥使用
2. **线程安全**: ConversationMemory 使用 mutex 保护，支持多线程访问
3. **Qt MOC**: 带有 Q_OBJECT 的类需要在 .cc 文件末尾包含 moc 文件
4. **模式切换**: Agent 模式按钮仅在本地 LLM 加载后启用

---

*最后更新: 2026-01-08*

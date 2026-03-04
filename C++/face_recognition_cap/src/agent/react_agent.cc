/**
 * @file react_agent.cc
 * @brief ReAct Agent 实现
 */

#include "agent/react_agent.h"
#include <QRegularExpression>
#include <spdlog/spdlog.h>

namespace agent {

ReactAgent::ReactAgent(ToolRegistry* tools,
                       ConversationMemory* memory,
                       const AgentConfig& config,
                       QObject* parent)
    : QObject(parent)
    , tools_(tools)
    , executor_(tools)
    , memory_(memory)
    , config_(config) {}

QString ReactAgent::run(const QString& user_input,
                        std::function<QString(const QString&)> llm_callback) {
    running_ = true;

    // 获取上下文
    QString context = memory_ ? memory_->getContext() : QString();

    // 构建初始 Prompt
    QString prompt = buildPrompt(user_input, context);

    // 记录用户消息
    if (memory_) {
        memory_->addUserMessage(user_input);
    }

    QString final_answer;
    int iteration = 0;

    emit thinkingStarted();

    // ReAct 循环
    while (running_ && iteration < config_.max_iterations) {
        iteration++;
        spdlog::info("ReAct iteration {}/{}", iteration, config_.max_iterations);

        // 调用 LLM
        QString llm_output = llm_callback(prompt);

        if (llm_output.isEmpty()) {
            spdlog::warn("LLM returned empty output");
            break;
        }

        // 解析输出类型
        StepType step = parseStepType(llm_output);
        spdlog::info("ReAct: LLM output length={}, step={}, first 200 chars: {}",
            llm_output.length(), static_cast<int>(step),
            llm_output.left(200).toStdString());

        switch (step) {
            case StepType::Answer: {
                // 最终答案
                final_answer = extractContent(llm_output, "answer");
                if (final_answer.isEmpty()) {
                    // 如果提取失败，使用整个输出
                    final_answer = llm_output;
                    // 清理标签
                    final_answer.remove(QRegularExpression(R"(<\|?/?answer\|?>)"));
                }
                final_answer = final_answer.trimmed();

                if (memory_) {
                    memory_->addAssistantMessage(final_answer);
                }
                emit answerReady(final_answer);
                running_ = false;
                break;
            }

            case StepType::ToolCall: {
                // 工具调用
                auto call = executor_.parseToolCall(llm_output);
                if (call.valid) {
                    emit toolCalling(call.name);

                    QString result = executor_.execute(call);

                    if (memory_) {
                        memory_->addToolMessage(call.name, result);
                    }
                    emit toolCompleted(call.name, result);

                    // 将工具结果反馈给 LLM
                    // 注意：keep_history=1 时 RKLLM 内部 KV Cache 已缓存之前的输出，
                    // 只需传工具结果（增量），不要重复拼接 llm_output 造成 prompt 膨胀
                    QString observation = executor_.formatToolResponse(call.name, result);
                    prompt = observation + "\n\n请根据工具返回的结果继续回答用户问题。如果已经可以回答，请用 <answer>...</answer> 格式给出最终答案。\n";
                } else {
                    spdlog::warn("Failed to parse tool call from: {}",
                        llm_output.left(100).toStdString());
                    // 作为普通输出处理 — KV Cache 已缓存，只传引导语
                    prompt = "请用正确格式重试。\n";
                }
                break;
            }

            case StepType::Thought: {
                // 继续思考 — keep_history KV Cache 已缓存思考内容，只传引导语
                spdlog::debug("Agent thinking: {}...",
                    llm_output.left(50).toStdString());
                prompt = "请继续思考并决定下一步行动。\n";
                break;
            }

            default: {
                // 无法解析，检查是否可以直接作为答案
                if (!llm_output.contains("<") && !llm_output.contains("{")) {
                    // 纯文本回答
                    final_answer = llm_output.trimmed();
                    if (memory_) {
                        memory_->addAssistantMessage(final_answer);
                    }
                    emit answerReady(final_answer);
                    running_ = false;
                } else {
                    // 尝试继续
                    prompt = "请用 <answer>...</answer> 格式给出最终答案，或使用 <tool_call>...</tool_call> 调用工具。\n";
                }
                break;
            }
        }

        emit iterationCompleted(iteration, config_.max_iterations);
    }

    if (iteration >= config_.max_iterations && final_answer.isEmpty()) {
        final_answer = "抱歉，我无法在有限的步骤内完成这个任务。请尝试简化问题或分步询问。";
        if (memory_) {
            memory_->addAssistantMessage(final_answer);
        }
        emit answerReady(final_answer);
    }

    running_ = false;
    return final_answer;
}

void ReactAgent::stop() {
    running_ = false;
    spdlog::info("ReactAgent stop requested");
}

QString ReactAgent::getSystemPrompt() const {
    return config_.system_prompt.isEmpty()
        ? getDefaultSystemPrompt()
        : config_.system_prompt;
}

void ReactAgent::setSystemPrompt(const QString& prompt) {
    config_.system_prompt = prompt;
}

ReactAgent::StepType ReactAgent::parseStepType(const QString& output) {
    // 查找各标签最后出现的位置，优先使用最后出现的标签
    // 这样可以正确处理包含历史内容的输出
    int answer_pos = -1;
    int tool_call_pos = -1;
    int thought_pos = -1;

    // 检测答案标签
    QRegularExpression re_answer(R"(<\|?answer\|?>)");
    auto match = re_answer.match(output);
    while (match.hasMatch()) {
        answer_pos = match.capturedStart();
        match = re_answer.match(output, match.capturedEnd());
    }

    // 检测工具调用标签
    QRegularExpression re_tool(R"(<\|?tool_call\|?>)");
    match = re_tool.match(output);
    while (match.hasMatch()) {
        tool_call_pos = match.capturedStart();
        match = re_tool.match(output, match.capturedEnd());
    }

    // 检测思考标签
    QRegularExpression re_thought(R"(<\|?thought\|?>)");
    match = re_thought.match(output);
    while (match.hasMatch()) {
        thought_pos = match.capturedStart();
        match = re_thought.match(output, match.capturedEnd());
    }

    // 返回最后出现的标签类型
    int max_pos = -1;
    StepType result = StepType::Unknown;

    if (answer_pos > max_pos) {
        max_pos = answer_pos;
        result = StepType::Answer;
    }
    if (tool_call_pos > max_pos) {
        max_pos = tool_call_pos;
        result = StepType::ToolCall;
    }
    if (thought_pos > max_pos) {
        max_pos = thought_pos;
        result = StepType::Thought;
    }

    // 如果没有找到标签，检测 JSON 格式的工具调用
    if (result == StepType::Unknown) {
        if (output.contains("\"name\"") && output.contains("\"arguments\"")) {
            result = StepType::ToolCall;
        }
    }

    return result;
}

QString ReactAgent::extractContent(const QString& output, const QString& tag) {
    // 匹配 <tag>...</tag> 或 <|tag|>...</|tag|>
    // 提取最后一个匹配的内容（处理包含历史内容的输出）
    QRegularExpression re(
        QString(R"(<\|?%1\|?>(.*?)<\|?/?%1\|?>)").arg(tag),
        QRegularExpression::DotMatchesEverythingOption
    );

    QString last_content;
    auto it = re.globalMatch(output);
    while (it.hasNext()) {
        auto match = it.next();
        last_content = match.captured(1).trimmed();
    }

    if (!last_content.isEmpty()) {
        return last_content;
    }

    // 尝试匹配只有开始标签的情况（提取最后一个）
    QRegularExpression re2(QString(R"(<\|?%1\|?>([^<]*))").arg(tag),
        QRegularExpression::DotMatchesEverythingOption);
    it = re2.globalMatch(output);
    while (it.hasNext()) {
        auto match = it.next();
        last_content = match.captured(1).trimmed();
    }

    return last_content;
}

QString ReactAgent::buildPrompt(const QString& user_input, const QString& context) {
    QString prompt;

    // 云端 LLM 已在服务端预设系统提示，跳过
    if (!config_.skip_system_prompt) {
        prompt = getSystemPrompt() + "\n\n";

        // 添加工具定义
        if (tools_ && tools_->size() > 0) {
            prompt += "## 可用工具\n";
            prompt += tools_->getToolsJson() + "\n\n";
        }
    }

    // 添加上下文
    if (!context.isEmpty()) {
        prompt += "## 对话历史\n" + context + "\n\n";
    }

    // 添加当前问题
    if (config_.skip_system_prompt) {
        // 云端模式：只发送用户问题
        prompt += user_input;
    } else {
        prompt += "## 当前问题\n用户: " + user_input + "\n\n";
    }

    return prompt;
}

QString ReactAgent::getDefaultSystemPrompt() const {
    return R"(你是一个智能考勤助手，运行在人脸识别考勤终端上。

## 核心能力
1. 考勤查询：查询今日、本周、本月的考勤统计，支持日期范围查询
2. 用户管理：查询员工信息、搜索用户
3. 系统信息：获取当前时间、考勤规则

## 工具调用规范
当需要查询数据时，你必须使用以下格式调用工具：

<tool_call>{"name":"工具名","arguments":{"参数名":"参数值"}}</tool_call>

可用工具：
1. query_attendance - 查询考勤数据
   参数:
   - query_type: stats(统计)、records(详细记录)、late(迟到名单)、early_leave(早退名单)
   - date_range: today/week/month (预设范围)
   - date: YYYY-MM-DD (指定单日)
   - start_date: YYYY-MM-DD (开始日期，与end_date配合使用)
   - end_date: YYYY-MM-DD (结束日期，与start_date配合使用)
   - filter: all(全部记录)、anomaly(仅异常:迟到+早退)，默认all
   示例:
   - 查今日统计: {"name":"query_attendance","arguments":{"date_range":"today"}}
   - 查今日迟到名单: {"name":"query_attendance","arguments":{"query_type":"late","date_range":"today"}}
   - 查某天详细记录: {"name":"query_attendance","arguments":{"query_type":"records","date":"2026-01-10"}}
   - 查日期范围统计: {"name":"query_attendance","arguments":{"start_date":"2026-01-01","end_date":"2026-01-10"}}
   - 查日期范围异常记录: {"name":"query_attendance","arguments":{"query_type":"records","start_date":"2026-01-01","end_date":"2026-01-10","filter":"anomaly"}}

2. query_user - 查询用户信息
   参数: action (get_by_id/get_by_name/list_all/stats), user_id, name

3. get_system_info - 获取系统信息
   参数: info_type (time/config/status)

## 回答格式
- 需要数据时：<tool_call>{"name":"query_attendance","arguments":{"date_range":"today"}}</tool_call>
- 给出答案时：<answer>最终回答内容</answer>

## 示例
用户：今天有多少人打卡？
助手：<tool_call>{"name":"query_attendance","arguments":{"date_range":"today"}}</tool_call>

用户：今天谁迟到了？
助手：<tool_call>{"name":"query_attendance","arguments":{"query_type":"late","date_range":"today"}}</tool_call>

用户：查询上周所有异常考勤
助手：<tool_call>{"name":"query_attendance","arguments":{"query_type":"records","start_date":"2026-01-06","end_date":"2026-01-10","filter":"anomaly"}}</tool_call>

用户：系统里有几个人？
助手：<tool_call>{"name":"query_user","arguments":{"action":"stats"}}</tool_call>

## 重要规则
1. 必须先调用工具获取数据，不要猜测
2. 每次只调用一个工具
3. 收到工具结果后，用 <answer>...</answer> 给出最终回答
4. 回答简洁，使用中文)";
}

} // namespace agent

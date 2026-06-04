/**
 * @file react_agent.cc
 * @brief ReAct Agent 实现
 */

#include "agent/react_agent.h"
#include "agent/prompt_templates.h"
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

    // 重置循环检测状态
    last_tool_call_key_.clear();
    consecutive_same_call_ = 0;
    last_tool_result_text_.clear();

    // 获取上下文（历史对话，不含当前这轮）
    QString context = memory_ ? memory_->getContext() : QString();

    // 构建初始 Prompt（完整版，包含系统提示 + 工具定义 + 历史 + 用户问题）
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

        // 调用 LLM（第一轮传完整 prompt，后续轮次传增量，KV Cache 自动缓存历史）
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
                    // ★ 循环检测：检查是否与上次调用完全相同
                    QString call_key = call.name + "|" +
                        QString::fromUtf8(QJsonDocument(call.arguments)
                            .toJson(QJsonDocument::Compact));
                    if (call_key == last_tool_call_key_) {
                        consecutive_same_call_++;
                        if (consecutive_same_call_ >= 2) {
                            spdlog::warn("Detected identical tool-call loop for '{}' with args "
                                "{}. Short-circuiting with tool result.",
                                call.name.toStdString(),
                                QString::fromUtf8(QJsonDocument(call.arguments)
                                    .toJson(QJsonDocument::Compact)).toStdString());
                            // 使用上一次的工具结果直接作为答案
                            final_answer = last_tool_result_text_;
                            if (memory_) {
                                memory_->addAssistantMessage(final_answer);
                            }
                            emit answerReady(final_answer);
                            running_ = false;
                            break;
                        }
                    } else {
                        consecutive_same_call_ = 1;
                        last_tool_call_key_ = call_key;
                    }

                    emit toolCalling(call.name);
                    emit toolInvocationReady(call);
                    if (memory_) {
                        memory_->addToolInvocationMessage(call);
                    }

                    ToolExecutionResult result = executor_.execute(call);

                    if (memory_) {
                        memory_->addToolResultMessage(result);
                    }
                    emit toolCompleted(call.name, result.promptText());
                    emit toolResultReady(result);

                    // 保存工具结果用于循环短路
                    last_tool_result_text_ = result.promptText();

                    // 将工具结果反馈给 LLM（增量追加到 KV Cache）
                    // keep_history=1 时 chat_template 会自动包装为新的对话轮次
                    QString observation = executor_.formatToolResponse(result);
                    prompt = observation;
                } else {
                    spdlog::warn("Failed to parse tool call from: {}",
                        llm_output.left(100).toStdString());
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
        // 超过最大迭代次数，如有工具结果则返回它，否则给出默认提示
        if (!last_tool_result_text_.isEmpty()) {
            final_answer = last_tool_result_text_;
        } else {
            final_answer = "抱歉，我无法在有限的步骤内完成这个任务。请尝试简化问题或分步询问。";
        }
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
    // 查找各标签最后出现的位置
    int answer_pos = -1;
    int tool_call_pos = -1;
    int thought_pos = -1;

    // 检测答案标签
    static const QRegularExpression re_answer(R"(<\|?answer\|?>)");
    auto match = re_answer.match(output);
    while (match.hasMatch()) {
        answer_pos = match.capturedStart();
        match = re_answer.match(output, match.capturedEnd());
    }

    // 检测工具调用标签
    static const QRegularExpression re_tool(R"(<\|?tool_call\|?>)");
    match = re_tool.match(output);
    while (match.hasMatch()) {
        tool_call_pos = match.capturedStart();
        match = re_tool.match(output, match.capturedEnd());
    }

    // 检测思考标签，兼容 <thought> 和 <think>
    static const QRegularExpression re_thought(R"(<\|?(?:thought|think)\|?>)");
    match = re_thought.match(output);
    while (match.hasMatch()) {
        thought_pos = match.capturedStart();
        match = re_thought.match(output, match.capturedEnd());
    }

    // ★ 当 tool_call 和 answer 同时出现时，优先执行 tool_call
    // 因为模型可能在尚未获得工具结果时就生成了占位式 answer，
    // 必须先执行工具获取真实数据，再让模型生成最终答案
    if (tool_call_pos >= 0 && answer_pos >= 0) {
        spdlog::info("Both <tool_call> and <answer> found in same output, "
            "prioritizing tool_call (tool@{}, answer@{})",
            tool_call_pos, answer_pos);
        return StepType::ToolCall;
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
    const QString tools_json = (tools_ && tools_->size() > 0) ? tools_->getToolsJson() : QString();
    return PromptTemplates::buildAgentPrompt(
        getSystemPrompt(),
        tools_json,
        context,
        user_input,
        config_.skip_system_prompt);
}

QString ReactAgent::getDefaultSystemPrompt() const {
    return PromptTemplates::buildAgentSystemPrompt();
}

} // namespace agent

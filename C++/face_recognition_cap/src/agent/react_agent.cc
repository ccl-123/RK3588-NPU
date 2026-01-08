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
                    QString observation = executor_.formatToolResponse(call.name, result);
                    prompt += "\n" + llm_output + "\n" + observation + "\n";
                    prompt += "\n请根据工具返回的结果继续回答用户问题。如果已经可以回答，请用 <answer>...</answer> 格式给出最终答案。\n";
                } else {
                    spdlog::warn("Failed to parse tool call from: {}",
                        llm_output.left(100).toStdString());
                    // 作为普通输出处理
                    prompt += "\n" + llm_output + "\n";
                }
                break;
            }

            case StepType::Thought: {
                // 继续思考，将思考内容加入上下文
                spdlog::debug("Agent thinking: {}...",
                    llm_output.left(50).toStdString());
                prompt += "\n" + llm_output + "\n";
                prompt += "\n请继续思考并决定下一步行动。\n";
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
                    prompt += "\n" + llm_output + "\n";
                    prompt += "\n请用 <answer>...</answer> 格式给出最终答案，或使用 <tool_call>...</tool_call> 调用工具。\n";
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
    // 检测答案标签
    if (output.contains(QRegularExpression(R"(<\|?answer\|?>)"))) {
        return StepType::Answer;
    }

    // 检测工具调用标签
    if (output.contains(QRegularExpression(R"(<\|?tool_call\|?>)"))) {
        return StepType::ToolCall;
    }

    // 检测思考标签
    if (output.contains(QRegularExpression(R"(<\|?thought\|?>)"))) {
        return StepType::Thought;
    }

    // 检测 JSON 格式的工具调用
    if (output.contains("\"name\"") && output.contains("\"arguments\"")) {
        return StepType::ToolCall;
    }

    return StepType::Unknown;
}

QString ReactAgent::extractContent(const QString& output, const QString& tag) {
    // 匹配 <tag>...</tag> 或 <|tag|>...<|/tag|>
    QRegularExpression re(
        QString(R"(<\|?%1\|?>(.*?)<\|?/?%1\|?>)").arg(tag),
        QRegularExpression::DotMatchesEverythingOption
    );

    auto match = re.match(output);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    // 尝试匹配只有开始标签的情况
    QRegularExpression re2(QString(R"(<\|?%1\|?>(.*)$)").arg(tag),
        QRegularExpression::DotMatchesEverythingOption);
    match = re2.match(output);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    return QString();
}

QString ReactAgent::buildPrompt(const QString& user_input, const QString& context) {
    QString prompt = getSystemPrompt() + "\n\n";

    // 添加工具定义
    if (tools_ && tools_->size() > 0) {
        prompt += "## 可用工具\n";
        prompt += tools_->getToolsJson() + "\n\n";
    }

    // 添加上下文
    if (!context.isEmpty()) {
        prompt += "## 对话历史\n" + context + "\n\n";
    }

    // 添加当前问题
    prompt += "## 当前问题\n用户: " + user_input + "\n\n";
    prompt += "请分析问题并选择合适的行动：\n";
    prompt += "- 如果需要查询数据，使用 <tool_call>{...}</tool_call> 调用工具\n";
    prompt += "- 如果可以直接回答，使用 <answer>...</answer> 给出答案\n";

    return prompt;
}

QString ReactAgent::getDefaultSystemPrompt() const {
    return R"(你是一个智能考勤助手 (Attendance Agent)，部署在人脸识别考勤终端上。

## 核心能力
1. **考勤查询**: 查询今日、本周、本月的考勤统计和详细记录
2. **用户管理**: 查询员工信息、搜索用户、统计人数
3. **系统信息**: 获取当前时间、系统状态、考勤规则配置
4. **数学计算**: 进行简单的数学运算（出勤率、平均值等）

## 可用工具
- `query_attendance`: 查询考勤数据 (period: today/week/month)
- `query_user`: 查询用户信息 (query_type: list/search/count, keyword: 搜索关键词)
- `system_info`: 获取系统信息 (query_type: datetime/status/config/all)
- `calculator`: 数学计算 (expression: 数学表达式)
- `help`: 获取帮助信息

## 回答格式
1. 需要查询数据时:
   <tool_call>{"name":"工具名","arguments":{"参数名":"参数值"}}</tool_call>

2. 给出最终答案时:
   <answer>答案内容</answer>

3. 需要思考时:
   <thought>思考过程</thought>

## 工作流程
1. 理解用户问题
2. 判断是否需要调用工具获取数据
3. 如需数据，调用相应工具
4. 根据工具返回结果生成答案
5. 用 <answer> 标签包裹最终回答

## 注意事项
- 优先使用工具获取准确数据，不要猜测
- 回答简洁明了，使用中文
- 数字和统计结果要准确
- 如果工具返回错误，告知用户并建议解决方案
- 对于无法处理的请求，礼貌地说明限制)";
}

} // namespace agent

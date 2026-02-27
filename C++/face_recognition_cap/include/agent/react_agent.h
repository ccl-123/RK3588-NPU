/**
 * @file react_agent.h
 * @brief ReAct Agent - 思考-行动-观察循环实现
 */

#pragma once

#include <QString>
#include <QObject>
#include <functional>
#include <atomic>
#include "agent/tool_registry.h"
#include "agent/tool_executor.h"
#include "agent/conversation_memory.h"

namespace agent {

/**
 * @brief Agent 配置
 */
struct AgentConfig {
    int max_iterations = 5;         ///< ReAct 最大循环次数
    bool stream_output = true;      ///< 是否流式输出
    QString system_prompt;          ///< 系统提示词（可选，使用默认）
    bool skip_system_prompt = false; ///< 跳过系统提示（云端 LLM 已在服务端预设）

    AgentConfig() = default;
};

/**
 * @brief ReAct Agent - 思考-行动-观察循环
 *
 * 实现 ReAct (Reasoning and Acting) 模式:
 * 1. 用户输入 → LLM 推理
 * 2. 检测输出类型:
 *    - <thought>...</thought>: 继续思考
 *    - <tool_call>...</tool_call>: 执行工具
 *    - <answer>...</answer>: 最终答案
 * 3. 工具执行后，将结果作为 <observation> 反馈给 LLM
 * 4. 循环直到输出最终答案或达到最大迭代次数
 */
class ReactAgent : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param tools 工具注册表
     * @param memory 对话记忆
     * @param config Agent 配置
     * @param parent 父对象
     */
    ReactAgent(ToolRegistry* tools,
               ConversationMemory* memory,
               const AgentConfig& config = AgentConfig{},
               QObject* parent = nullptr);

    virtual ~ReactAgent() = default;

    /**
     * @brief 处理用户输入（同步版本，需要子类实现 LLM 调用）
     * @param user_input 用户输入
     * @param llm_callback LLM 调用回调，传入 prompt 返回 LLM 输出
     * @return 最终答案
     */
    QString run(const QString& user_input,
                std::function<QString(const QString&)> llm_callback);

    /**
     * @brief 停止当前执行
     */
    void stop();

    /**
     * @brief 是否正在运行
     * @return 运行状态
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief 获取系统提示词
     * @return 系统提示词
     */
    QString getSystemPrompt() const;

    /**
     * @brief 设置系统提示词
     * @param prompt 系统提示词
     */
    void setSystemPrompt(const QString& prompt);

signals:
    /**
     * @brief 生成 token（流式输出）
     * @param token 生成的 token
     */
    void tokenGenerated(const QString& token);

    /**
     * @brief 开始思考
     */
    void thinkingStarted();

    /**
     * @brief 正在调用工具
     * @param tool_name 工具名称
     */
    void toolCalling(const QString& tool_name);

    /**
     * @brief 工具调用完成
     * @param tool_name 工具名称
     * @param result 工具结果
     */
    void toolCompleted(const QString& tool_name, const QString& result);

    /**
     * @brief 最终答案就绪
     * @param answer 答案
     */
    void answerReady(const QString& answer);

    /**
     * @brief 迭代完成
     * @param iteration 当前迭代次数
     * @param max_iterations 最大迭代次数
     */
    void iterationCompleted(int iteration, int max_iterations);

private:
    ToolRegistry* tools_;
    ToolExecutor executor_;
    ConversationMemory* memory_;
    AgentConfig config_;
    std::atomic<bool> running_{false};

    /**
     * @brief 步骤类型
     */
    enum class StepType {
        Thought,    ///< 思考中
        ToolCall,   ///< 工具调用
        Answer,     ///< 最终答案
        Unknown     ///< 无法解析
    };

    /**
     * @brief 解析步骤类型
     * @param output LLM 输出
     * @return 步骤类型
     */
    StepType parseStepType(const QString& output);

    /**
     * @brief 提取标签内容
     * @param output LLM 输出
     * @param tag 标签名（如 "answer"）
     * @return 标签内容
     */
    QString extractContent(const QString& output, const QString& tag);

    /**
     * @brief 构建初始 Prompt
     * @param user_input 用户输入
     * @param context 上下文
     * @return 完整的 Prompt
     */
    QString buildPrompt(const QString& user_input, const QString& context);

    /**
     * @brief 获取默认系统提示词
     * @return 默认提示词
     */
    QString getDefaultSystemPrompt() const;
};

} // namespace agent

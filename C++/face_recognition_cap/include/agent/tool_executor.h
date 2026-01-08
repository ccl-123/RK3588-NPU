/**
 * @file tool_executor.h
 * @brief 工具执行器 - 解析 LLM 输出并执行工具调用
 */

#pragma once

#include <QString>
#include <QJsonObject>
#include "agent/tool_registry.h"

namespace agent {

/**
 * @brief 工具调用信息
 */
struct ToolCall {
    QString name;           ///< 工具名称
    QJsonObject arguments;  ///< 工具参数
    bool valid = false;     ///< 是否有效

    QString toString() const;
};

/**
 * @brief 工具执行器 - 解析并执行工具调用
 *
 * 支持解析以下格式的工具调用:
 * - <tool_call>{"name":"xxx","arguments":{...}}</tool_call>
 * - <|tool_call|>{"name":"xxx","arguments":{...}}
 * - {"name":"xxx","arguments":{...}} (纯 JSON)
 */
class ToolExecutor {
public:
    /**
     * @brief 构造函数
     * @param registry 工具注册表（不转移所有权）
     */
    explicit ToolExecutor(ToolRegistry* registry);

    /**
     * @brief 解析 LLM 输出中的工具调用
     * @param llm_output LLM 的原始输出
     * @return 解析出的工具调用信息
     */
    ToolCall parseToolCall(const QString& llm_output);

    /**
     * @brief 执行工具调用
     * @param call 工具调用信息
     * @return 工具执行结果
     */
    QString execute(const ToolCall& call);

    /**
     * @brief 格式化工具响应供 LLM 继续推理
     * @param tool_name 工具名称
     * @param result 工具执行结果
     * @return 格式化后的响应字符串
     */
    QString formatToolResponse(const QString& tool_name, const QString& result);

    /**
     * @brief 检测 LLM 输出是否包含工具调用
     * @param llm_output LLM 的原始输出
     * @return 是否包含工具调用
     */
    bool hasToolCall(const QString& llm_output) const;

private:
    ToolRegistry* registry_;

    /**
     * @brief 从文本中提取 JSON 字符串
     * @param text 原始文本
     * @return 提取的 JSON 字符串，失败返回空
     */
    QString extractJson(const QString& text);
};

} // namespace agent

/**
 * @file base_tool.h
 * @brief Agent 工具基类 - 所有 Agent 工具的抽象接口
 */

#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include "agent/tool_types.h"

namespace agent {

/**
 * @brief 工具基类 - 定义工具接口规范
 *
 * 所有自定义工具需要继承此类并实现以下方法:
 * - name(): 工具名称
 * - description(): 工具描述
 * - parametersSchema(): 参数 JSON Schema
 * - executeWithResult(): 执行工具并返回结构化结果
 */
class BaseTool {
public:
    virtual ~BaseTool() = default;

    /**
     * @brief 获取工具名称
     * @return 工具的唯一标识符，如 "query_attendance"
     */
    virtual QString name() const = 0;

    /**
     * @brief 获取工具描述
     * @return 工具功能的自然语言描述，供 LLM 理解
     */
    virtual QString description() const = 0;

    /**
     * @brief 获取参数 Schema
     * @return OpenAI Function Calling 风格的 JSON Schema
     */
    virtual QJsonObject parametersSchema() const = 0;

    /**
     * @brief 执行工具并返回结构化结果
     * @param invocation 工具调用
     * @return 结构化结果
     */
    virtual ToolExecutionResult executeWithResult(const ToolInvocation& invocation) = 0;

    /**
     * @brief 兼容旧接口：返回展示文本
     * @param args 参数对象
     * @return 工具执行结果文本
     */
    QString execute(const QJsonObject& args) {
        ToolInvocation invocation;
        invocation.name = name();
        invocation.arguments = args;
        invocation.valid = true;
        return executeWithResult(invocation).display_text;
    }

    ToolDefinition definition() const {
        ToolDefinition def;
        def.name = name();
        def.description = description();
        def.input_schema = parametersSchema();
        return def;
    }

    /**
     * @brief 生成 OpenAI 风格的工具定义 JSON
     * @return 完整的工具定义，用于 rkllm_set_function_tools()
     */
    QJsonObject toToolDefinition() const {
        return definition().toJson();
    }

    /**
     * @brief 生成工具定义的 JSON 字符串
     * @return JSON 字符串
     */
    QString toToolDefinitionString() const {
        return QString::fromUtf8(
            QJsonDocument(toToolDefinition()).toJson(QJsonDocument::Compact)
        );
    }
};

} // namespace agent

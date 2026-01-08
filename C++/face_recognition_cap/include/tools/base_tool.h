/**
 * @file base_tool.h
 * @brief Agent 工具基类 - 所有 Agent 工具的抽象接口
 */

#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

namespace agent {

/**
 * @brief 工具基类 - 定义工具接口规范
 *
 * 所有自定义工具需要继承此类并实现以下方法:
 * - name(): 工具名称
 * - description(): 工具描述
 * - parametersSchema(): 参数 JSON Schema
 * - execute(): 执行工具
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
     * @brief 执行工具
     * @param args 参数对象
     * @return 工具执行结果的文本描述
     */
    virtual QString execute(const QJsonObject& args) = 0;

    /**
     * @brief 生成 OpenAI 风格的工具定义 JSON
     * @return 完整的工具定义，用于 rkllm_set_function_tools()
     */
    QJsonObject toToolDefinition() const {
        QJsonObject tool;
        tool["name"] = name();
        tool["description"] = description();
        tool["parameters"] = parametersSchema();
        return tool;
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

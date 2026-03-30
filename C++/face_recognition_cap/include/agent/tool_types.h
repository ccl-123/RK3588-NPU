/**
 * @file tool_types.h
 * @brief Agent 工具调用相关的统一结构
 */

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QString>

namespace agent {

struct ToolDefinition {
    QString name;
    QString description;
    QJsonObject input_schema;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["description"] = description;
        obj["parameters"] = input_schema;
        return obj;
    }
};

struct ToolInvocation {
    QString call_id;
    QString name;
    QJsonObject arguments;
    QString raw_text;
    bool valid = false;

    QString toString() const {
        if (!valid) {
            return "ToolInvocation{invalid}";
        }

        return QString("ToolInvocation{id=%1,name=%2,args=%3}")
            .arg(call_id.isEmpty() ? "-" : call_id,
                 name,
                 QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));
    }
};

struct ToolExecutionResult {
    QString call_id;
    QString name;
    bool ok = false;
    QString display_text;
    QJsonObject output;
    QString error;

    QString promptText() const {
        return ok ? display_text : QString("错误: %1").arg(error);
    }
};

}  // namespace agent

Q_DECLARE_METATYPE(agent::ToolInvocation)
Q_DECLARE_METATYPE(agent::ToolExecutionResult)

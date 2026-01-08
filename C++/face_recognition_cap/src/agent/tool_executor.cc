/**
 * @file tool_executor.cc
 * @brief 工具执行器实现
 */

#include "agent/tool_executor.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <spdlog/spdlog.h>

namespace agent {

QString ToolCall::toString() const {
    if (!valid) {
        return "ToolCall{invalid}";
    }
    return QString("ToolCall{name=%1, args=%2}")
        .arg(name)
        .arg(QString::fromUtf8(QJsonDocument(arguments).toJson(QJsonDocument::Compact)));
}

ToolExecutor::ToolExecutor(ToolRegistry* registry)
    : registry_(registry) {}

ToolCall ToolExecutor::parseToolCall(const QString& llm_output) {
    ToolCall call;

    // 提取 JSON 字符串
    QString json_str = extractJson(llm_output);
    if (json_str.isEmpty()) {
        return call;
    }

    // 解析 JSON
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(json_str.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        spdlog::warn("Failed to parse tool call JSON: {}", error.errorString().toStdString());
        return call;
    }

    if (!doc.isObject()) {
        spdlog::warn("Tool call JSON is not an object");
        return call;
    }

    QJsonObject obj = doc.object();
    call.name = obj["name"].toString();
    call.arguments = obj["arguments"].toObject();
    call.valid = !call.name.isEmpty();

    if (call.valid) {
        spdlog::debug("Parsed tool call: {}", call.toString().toStdString());
    }

    return call;
}

QString ToolExecutor::execute(const ToolCall& call) {
    if (!call.valid) {
        return "错误: 无效的工具调用";
    }

    if (!registry_) {
        return "错误: 工具注册表未初始化";
    }

    BaseTool* tool = registry_->getTool(call.name);
    if (!tool) {
        spdlog::warn("Unknown tool: {}", call.name.toStdString());
        return QString("错误: 未知工具 '%1'").arg(call.name);
    }

    spdlog::info("Executing tool: {} with args: {}",
        call.name.toStdString(),
        QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact)).toStdString());

    try {
        QString result = tool->execute(call.arguments);
        spdlog::debug("Tool '{}' returned: {}", call.name.toStdString(),
            result.left(100).toStdString());
        return result;
    } catch (const std::exception& e) {
        spdlog::error("Tool execution failed: {}", e.what());
        return QString("错误: 工具执行失败 - %1").arg(e.what());
    }
}

QString ToolExecutor::formatToolResponse(const QString& tool_name, const QString& result) {
    return QString("<|tool_response|>\n[%1 返回结果]\n%2\n<|/tool_response|>")
        .arg(tool_name, result);
}

bool ToolExecutor::hasToolCall(const QString& llm_output) const {
    // 检测常见的工具调用标记
    static QRegularExpression re(
        R"(<\|?tool_call\|?>|"name"\s*:\s*"[^"]+"\s*,\s*"arguments")",
        QRegularExpression::CaseInsensitiveOption
    );
    return re.match(llm_output).hasMatch();
}

QString ToolExecutor::extractJson(const QString& text) {
    // 模式1: <tool_call>...</tool_call> 或 <|tool_call|>...
    static QRegularExpression re1(
        R"(<\|?tool_call\|?>(.+?)(?:</?(?:\|?tool_call\|?)>|$))",
        QRegularExpression::DotMatchesEverythingOption
    );

    auto match = re1.match(text);
    if (match.hasMatch()) {
        QString content = match.captured(1).trimmed();
        // 找到 JSON 对象
        int start = content.indexOf('{');
        int end = content.lastIndexOf('}');
        if (start >= 0 && end > start) {
            return content.mid(start, end - start + 1);
        }
    }

    // 模式2: 直接查找包含 name 和 arguments 的 JSON 对象
    static QRegularExpression re2(
        R"(\{[^{}]*"name"[^{}]*"arguments"[^{}]*\{[^{}]*\}[^{}]*\})",
        QRegularExpression::DotMatchesEverythingOption
    );

    match = re2.match(text);
    if (match.hasMatch()) {
        return match.captured(0);
    }

    // 模式3: 尝试提取任何 JSON 对象
    int start = text.indexOf('{');
    int end = text.lastIndexOf('}');
    if (start >= 0 && end > start) {
        QString potential_json = text.mid(start, end - start + 1);
        // 验证是否包含必要字段
        if (potential_json.contains("\"name\"") && potential_json.contains("\"arguments\"")) {
            return potential_json;
        }
    }

    return QString();
}

} // namespace agent

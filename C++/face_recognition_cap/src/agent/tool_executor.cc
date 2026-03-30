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
    call.call_id = obj["id"].toString(obj["call_id"].toString());
    call.name = obj["name"].toString();
    call.arguments = obj["arguments"].toObject();
    call.raw_text = llm_output;
    call.valid = !call.name.isEmpty();

    if (call.valid) {
        spdlog::debug("Parsed tool call: {}", call.toString().toStdString());
    }

    return call;
}

ToolExecutionResult ToolExecutor::execute(const ToolCall& call) {
    ToolExecutionResult result;
    result.call_id = call.call_id;
    result.name = call.name;

    if (!call.valid) {
        result.error = "无效的工具调用";
        result.display_text = "错误: 无效的工具调用";
        return result;
    }

    if (!registry_) {
        result.error = "工具注册表未初始化";
        result.display_text = "错误: 工具注册表未初始化";
        return result;
    }

    BaseTool* tool = registry_->getTool(call.name);
    if (!tool) {
        spdlog::warn("Unknown tool: {}", call.name.toStdString());
        result.error = QString("未知工具 '%1'").arg(call.name);
        result.display_text = QString("错误: 未知工具 '%1'").arg(call.name);
        return result;
    }

    spdlog::info("Executing tool: {} with args: {}",
        call.name.toStdString(),
        QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact)).toStdString());

    try {
        result = tool->executeWithResult(call);
        spdlog::debug("Tool '{}' returned: {}", call.name.toStdString(),
            result.display_text.left(100).toStdString());
        return result;
    } catch (...) {
        result.ok = false;
        result.error = "未知异常";
        result.display_text = "错误: 工具执行失败 - 未知异常";
        return result;
    }
}

QString ToolExecutor::formatToolResponse(const ToolExecutionResult& result) {
    return QString("<|tool_response|>\n[%1 返回结果]\n%2\n<|/tool_response|>")
        .arg(result.name, result.promptText());
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

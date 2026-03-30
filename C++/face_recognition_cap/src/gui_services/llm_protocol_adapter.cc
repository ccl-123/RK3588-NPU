/**
 * @file llm_protocol_adapter.cc
 * @brief 统一解析腾讯云 / llama.cpp SSE 协议
 */

#include "gui_services/llm_protocol_adapter.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace gui_services {

namespace {

bool takeSseBlock(QByteArray& buffer, QByteArray& block) {
    int idx = buffer.indexOf("\n\n");
    int sep_len = 2;
    if (idx == -1) {
        idx = buffer.indexOf("\r\n\r\n");
        sep_len = 4;
        if (idx == -1) {
            return false;
        }
    }

    block = buffer.left(idx).trimmed();
    buffer.remove(0, idx + sep_len);
    return true;
}

QByteArray collectDataLines(const QByteArray& block, QString* event_type) {
    QByteArray data_buffer;
    const QList<QByteArray> lines = block.split('\n');
    for (const QByteArray& line_raw : lines) {
        const QByteArray line = line_raw.trimmed();
        if (line.startsWith("event:") || line.startsWith("event :")) {
            const int colon = line.indexOf(':');
            if (event_type) {
                *event_type = QString::fromUtf8(line.mid(colon + 1)).trimmed();
            }
        } else if (line.startsWith("data:") || line.startsWith("data :")) {
            const int colon = line.indexOf(':');
            const QByteArray content = line.mid(colon + 1).trimmed();
            if (!data_buffer.isEmpty()) {
                data_buffer.append(content);
            } else {
                data_buffer = content;
            }
        }
    }
    return data_buffer;
}

}  // namespace

QVector<ProtocolChunkEvent> LlmProtocolAdapter::consumeTencentSse(QByteArray& buffer) {
    QVector<ProtocolChunkEvent> events;
    QByteArray block;
    while (takeSseBlock(buffer, block)) {
        if (block.isEmpty()) {
            continue;
        }

        QString event_type;
        const QByteArray data_buffer = collectDataLines(block, &event_type);
        if (data_buffer.isEmpty()) {
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data_buffer);
        if (!doc.isObject()) {
            continue;
        }

        const QJsonObject root = doc.object();
        const QJsonObject payload = root.contains("payload") ? root.value("payload").toObject() : root;

        if (event_type.isEmpty()) {
            event_type = root.value("type").toString(root.value("event").toString());
        }

        if (event_type == "workflow" || event_type == "workflow_status") {
            continue;
        }

        if (event_type == "reply") {
            ProtocolChunkEvent event;
            event.kind = "delta";
            event.text = payload.value("content").toString();
            event.final = payload.value("is_final").toBool(root.value("is_final").toBool());
            event.data = payload;
            if (!event.text.isEmpty() || event.final) {
                events.push_back(event);
            }
            continue;
        }

        if (event_type == "thought") {
            ProtocolChunkEvent event;
            event.kind = "reasoning";
            event.text = payload.value("content").toString();
            event.data = payload;
            if (!event.text.isEmpty()) {
                events.push_back(event);
            }
            continue;
        }

        if (event_type == "error") {
            ProtocolChunkEvent event;
            event.kind = "error";
            event.data = root;
            const QJsonObject err = root.value("error").toObject();
            event.text = err.value("message").toString();
            events.push_back(event);
        }
    }

    return events;
}

QVector<ProtocolChunkEvent> LlmProtocolAdapter::consumeLlamaCppSse(QByteArray& buffer) {
    QVector<ProtocolChunkEvent> events;
    QByteArray block;
    while (takeSseBlock(buffer, block)) {
        if (block.isEmpty()) {
            continue;
        }

        QString event_type;
        const QByteArray data_buffer = collectDataLines(block, &event_type);
        if (data_buffer == "[DONE]") {
            ProtocolChunkEvent done;
            done.kind = "done";
            done.final = true;
            events.push_back(done);
            continue;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(data_buffer);
        if (!doc.isObject()) {
            continue;
        }
        const QJsonObject obj = doc.object();
        const QJsonArray choices = obj.value("choices").toArray();
        if (choices.isEmpty()) {
            continue;
        }

        const QJsonObject choice = choices.first().toObject();
        const QJsonObject delta = choice.value("delta").toObject();

        if (delta.contains("content")) {
            ProtocolChunkEvent event;
            event.kind = "delta";
            event.text = delta.value("content").toString();
            event.data = delta;
            if (!event.text.isEmpty()) {
                events.push_back(event);
            }
        }

        if (delta.contains("reasoning_content")) {
            ProtocolChunkEvent event;
            event.kind = "reasoning";
            event.text = delta.value("reasoning_content").toString();
            event.data = delta;
            if (!event.text.isEmpty()) {
                events.push_back(event);
            }
        }

        const QJsonArray tool_calls = delta.value("tool_calls").toArray();
        for (const auto& tool_value : tool_calls) {
            const QJsonObject tool = tool_value.toObject();
            const QJsonObject function = tool.value("function").toObject();

            if (function.contains("name")) {
                ProtocolChunkEvent event;
                event.kind = "tool_name";
                event.text = function.value("name").toString();
                event.data = tool;
                events.push_back(event);
            }

            if (function.contains("arguments")) {
                ProtocolChunkEvent event;
                event.kind = "tool_args";
                event.text = function.value("arguments").toString();
                event.data = tool;
                events.push_back(event);
            }
        }

        if (!choice.value("finish_reason").isNull()) {
            ProtocolChunkEvent event;
            event.kind = "done";
            event.final = true;
            event.text = choice.value("finish_reason").toString();
            events.push_back(event);
        }
    }

    return events;
}

}  // namespace gui_services

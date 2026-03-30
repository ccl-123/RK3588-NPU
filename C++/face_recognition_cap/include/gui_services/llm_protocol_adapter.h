/**
 * @file llm_protocol_adapter.h
 * @brief 统一封装不同 LLM 流式协议到标准事件
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QVector>
#include <QString>

namespace gui_services {

struct ProtocolChunkEvent {
    QString kind;    ///< delta / error / done / reasoning / tool_name / tool_args
    QString text;
    bool final = false;
    QJsonObject data;
};

class LlmProtocolAdapter {
public:
    static QVector<ProtocolChunkEvent> consumeTencentSse(QByteArray& buffer);
    static QVector<ProtocolChunkEvent> consumeLlamaCppSse(QByteArray& buffer);
};

}  // namespace gui_services

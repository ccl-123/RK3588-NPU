/**
 * @file stream_event.h
 * @brief 统一的 Agent / LLM 流式事件定义
 */

#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace agent {

struct AgentStreamEvent {
    quint64 request_id = 0;
    quint64 seq = 0;
    QString phase;    ///< model / agent / tool / system
    QString type;     ///< start / delta / thinking / tool_call / tool_result / done / error
    QString channel;  ///< assistant / status / tool / reasoning
    QString text;
    QJsonObject data;
    bool final = false;
};

}  // namespace agent

Q_DECLARE_METATYPE(agent::AgentStreamEvent)

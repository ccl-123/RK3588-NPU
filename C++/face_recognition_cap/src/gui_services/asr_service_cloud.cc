/**
 * @file asr_service_cloud.cc
 * @brief 云端 ASR 语音识别服务实现部分
 */

#include "gui_services/asr_service.h"
#include "config/config.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <spdlog/spdlog.h>

void AsrService::sendAsrRequest(const QByteArray& wav_data) {
    transcribing_ = true;
    emit transcribingStateChanged(true);

    // Base64 编码
    QString base64_audio = QString::fromLatin1(wav_data.toBase64());
    QString data_url = QStringLiteral("data:audio/wav;base64,") + base64_audio;

    spdlog::info("ASR: sending request, WAV size={}, Base64 size={}",
                 wav_data.size(), base64_audio.size());

    // 检查大小限制（10MB）
    if (base64_audio.size() > 10 * 1024 * 1024) {
        transcribing_ = false;
        emit transcribingStateChanged(false);
        emit asrError(QStringLiteral("录音过长，音频数据超过 10MB 限制"));
        return;
    }

    // 构建请求体
    QJsonObject input_audio;
    input_audio["data"] = data_url;

    QJsonObject content_item;
    content_item["type"] = QStringLiteral("input_audio");
    content_item["input_audio"] = input_audio;

    QJsonArray content_array;
    content_array.append(content_item);

    QJsonObject message;
    message["role"] = QStringLiteral("user");
    message["content"] = content_array;

    QJsonArray messages;
    messages.append(message);

    QJsonObject asr_options;
    asr_options["language"] = QString::fromUtf8(Config::MiMoASR::getLanguage());

    QJsonObject body;
    body["model"] = QString::fromUtf8(Config::MiMoASR::MODEL);
    body["messages"] = messages;
    body["asr_options"] = asr_options;

    // 发送请求
    QNetworkRequest request(QUrl(QString::fromUtf8(Config::MiMoASR::getBaseUrl())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray apiKey = QByteArray(Config::MiMoASR::getApiKey());
    request.setRawHeader("api-key", apiKey);
    request.setRawHeader("Authorization", "Bearer " + apiKey);

    current_reply_ = network_manager_->post(request, QJsonDocument(body).toJson());
    QNetworkReply* reply = current_reply_;

    timeout_timer_->start(Config::MiMoASR::REQUEST_TIMEOUT_MS);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAsrResponse(reply);
    });
}

void AsrService::handleAsrResponse(QNetworkReply* reply) {
    timeout_timer_->stop();

    if (!current_reply_ || current_reply_ != reply) {
        reply->deleteLater();
        return;
    }

    transcribing_ = false;
    emit transcribingStateChanged(false);

    if (reply->error() != QNetworkReply::NoError) {
        int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 尝试解析错误响应体
        QByteArray response_data = reply->readAll();
        QString error_msg;

        if (!response_data.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(response_data);
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                if (root.contains("error")) {
                    QJsonObject err = root["error"].toObject();
                    error_msg = err["message"].toString();
                }
            }
        }

        if (error_msg.isEmpty()) {
            error_msg = reply->errorString();
        }

        spdlog::error("ASR: request failed (HTTP {}): {}", http_status, error_msg.toStdString());
        emit asrError(QStringLiteral("语音识别失败: ") + error_msg);
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    // 解析成功响应
    QByteArray response_data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(response_data);

    if (!doc.isObject()) {
        spdlog::error("ASR: invalid JSON response");
        emit asrError(QStringLiteral("语音识别响应格式错误"));
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        spdlog::warn("ASR: empty choices in response");
        emit asrError(QStringLiteral("语音识别未返回结果"));
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    QJsonObject first_choice = choices[0].toObject();
    QJsonObject msg = first_choice["message"].toObject();
    QString text = msg["content"].toString().trimmed();

    spdlog::info("ASR: transcription result: '{}'", text.left(100).toStdString());

    if (text.isEmpty()) {
        emit asrError(QStringLiteral("未识别到语音内容，请重试"));
    } else {
        // 流式输出：逐字符发送以模拟流式填充效果
        emit transcriptionReady(text);
        emit transcriptionFinished(text);
    }

    reply->deleteLater();
    current_reply_.clear();
}

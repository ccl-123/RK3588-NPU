/**
 * @file ai_analysis_service.cc
 * @author CL
 * @brief 
 * @date 2025-12-21
 *对话端接口文档：https://cloud.tencent.com/document/product/1759/105561
 * 
 * @copyright Copyright (c) 2025
 */
#include "services/ai_analysis_service.h"
#include "service/attendance_service.h"
#include "config/config.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUuid>
#include <QTimer>
#include <QDateTime>
#include <spdlog/spdlog.h>
#include <cstring>  // for strlen

AiAnalysisService* AiAnalysisService::instance() {
    static AiAnalysisService s_instance;
    return &s_instance;
}

AiAnalysisService::AiAnalysisService(QObject* parent)
    : QObject(parent)
    , current_retry_count_(0)
    , completed_(false)
    , is_incremental_(false) {  // 默认 false（文档说明）
    network_manager_ = new QNetworkAccessManager(this);

    // 检查环境变量是否已设置
    const char* app_key = Config::TencentAI::getAppKey();
    const char* secret_id = Config::TencentAI::getSecretId();
    const char* secret_key = Config::TencentAI::getSecretKey();

    if (!app_key || strlen(app_key) == 0) {
        spdlog::warn("TENCENT_APP_KEY environment variable is not set");
    } else {
        spdlog::info("TENCENT_APP_KEY loaded from environment (length: {})", strlen(app_key));
    }

    if (!secret_id || strlen(secret_id) == 0) {
        spdlog::warn("TENCENT_SECRET_ID environment variable is not set");
    } else {
        spdlog::info("TENCENT_SECRET_ID loaded from environment: {}", secret_id);
    }

    if (!secret_key || strlen(secret_key) == 0) {
        spdlog::warn("TENCENT_SECRET_KEY environment variable is not set");
    } else {
        spdlog::info("TENCENT_SECRET_KEY loaded from environment (length: {})", strlen(secret_key));
    }

    // 创建超时定时器
    timeout_timer_ = new QTimer(this);
    timeout_timer_->setSingleShot(true);
    connect(timeout_timer_, &QTimer::timeout, this, [this]() {
        if (current_reply_) {
            spdlog::warn("AI analysis request timeout");

            // 检查是否需要重试
            if (current_retry_count_ < MAX_RETRIES) {
                spdlog::info("Retrying AI analysis request ({}/{})",
                            current_retry_count_ + 1, MAX_RETRIES);

                // 清理当前请求
                if (current_reply_) {
                    current_reply_->abort();
                    current_reply_->deleteLater();
                    current_reply_.clear();
                }

                // 延迟后重试
                QTimer::singleShot(RETRY_DELAY_MS * (current_retry_count_ + 1), this, [this]() {
                    doRequest(current_stats_, current_trend_summary_,
                             current_detail_records_, current_user_prompt_,
                             current_range_days_,
                             current_retry_count_ + 1);
                });
            } else {
                emit errorOccurred("请求超时，请检查网络连接后重试");
                cleanup();
            }
        }
    });
}

AiAnalysisService::~AiAnalysisService() {
    cleanup();
}

void AiAnalysisService::cleanup() {
    if (timeout_timer_) {
        timeout_timer_->stop();
    }

    if (current_reply_) {
        current_reply_->abort();
        current_reply_->deleteLater();
        current_reply_.clear();
    }

    sse_buffer_.clear();
    incremental_buffer_.clear();
    current_retry_count_ = 0;
    completed_ = false;  // 重置完成标志，为下一次请求做准备
}

void AiAnalysisService::cancelAnalysis() {
    if (current_reply_) {
        spdlog::info("AI analysis cancelled by user");
        cleanup();
        emit analysisCancelled();
    }
}

void AiAnalysisService::requestAnalysis(const service::AttendanceStatistics& stats,
                                        const QString& trend_summary,
                                        const QString& detail_records,
                                        const QString& user_prompt,
                                        int range_days) {
    // 如果已有请求在进行，先取消
    if (current_reply_) {
        spdlog::warn("Previous AI analysis request is still running, cancelling it");
        cleanup();
    }

    // 保存参数用于重试
    current_stats_ = stats;
    current_trend_summary_ = trend_summary;
    current_detail_records_ = detail_records;
    current_retry_count_ = 0;
    current_user_prompt_ = user_prompt;
    current_range_days_ = range_days;
    completed_ = false;  // 重置完成标志
    incremental_buffer_.clear();  // 清空增量缓冲

    // 发送开始信号
    emit analysisStarted();

    // 执行请求
    doRequest(stats, trend_summary, detail_records, user_prompt, range_days, 0);
}

void AiAnalysisService::doRequest(const service::AttendanceStatistics& stats,
                                   const QString& trend_summary,
                                   const QString& detail_records,
                                   const QString& user_prompt,
                                   int range_days,
                                   int retry_count) {
    current_retry_count_ = retry_count;

    QUrl url(QString::fromStdString(Config::TencentAI::API_URL));
    QNetworkRequest request(url);

    // 设置请求头（SSE 接口需要 Content-Type 和 Accept）
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");  // 关键：告诉代理/CDN 这是 SSE 流

    // 构建数据内容 (仅发送数据，让云端 Agent 处理分析逻辑)
    QString current_time_str = QDateTime::currentDateTime().toString("MM月dd日 HH:mm");
    
    QString data_title = (range_days > 1) ? QString("【近 %1 日全量考勤数据】").arg(range_days) : QString("【今日考勤数据概览】");
    QString stats_date_label = (range_days > 1) ? QString("截止日期") : QString("统计日期");
    QString detail_title = (range_days > 1) ? QString("【考勤明细流水 (近 %1 日)】").arg(range_days) : QString("【今日打卡明细】");

    QString content = QString(
        "【当前时间】: %1\n\n"
        "%2\n"
        "%3: %4\n"
        "今日打卡总人数: %5\n"
        "今日签到人数: %6\n"
        "今日签退人数: %7\n"
        "今日迟到人数: %8\n"
        "今日早退人数: %9\n\n"
        "%10\n%11\n\n"
        "【趋势统计数据】\n%12\n\n"
        "【用户问题】\n%13"
    ).arg(current_time_str)
     .arg(data_title)
     .arg(stats_date_label)
     .arg(QString::fromStdString(stats.date))
     .arg(stats.total_count)
     .arg(stats.check_in_count)
     .arg(stats.check_out_count)
     .arg(stats.late_count)
     .arg(stats.early_leave_count)
     .arg(detail_title)
     .arg(detail_records)
     .arg(trend_summary)
     .arg(user_prompt.isEmpty() ? QStringLiteral("请生成今日考勤综合分析。") : user_prompt);

    // 生成唯一的 session_id 和 request_id
    // 文档说 request_id "非必填但建议必填"，用于排查串联
    QString session_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 构建 JSON Body（严格按文档 105561 格式）
    QJsonObject jsonBody;

    // 必填字段
    jsonBody["session_id"] = session_id;
    jsonBody["bot_app_key"] = QString::fromUtf8(Config::TencentAI::getAppKey());
    jsonBody["visitor_biz_id"] = QString::fromStdString(Config::TencentAI::VISITOR_BIZ_ID);
    jsonBody["content"] = content;

    // 建议必填字段（文档明确提及）
    jsonBody["request_id"] = request_id;  // 用于排查串联，建议必填

    // 可选字段（文档中存在的字段）
    // 注意：incremental 默认 false，true 才是"增量片段"
    jsonBody["incremental"] = true;  // 启用增量输出（默认 false）
    jsonBody["streaming_throttle"] = 10;  // 每积攒 N 字符回包一次（默认 10）
    jsonBody["search_network"] = "disable";  // 禁用联网搜索
    jsonBody["stream"] = "enable";  // 文档中存在，启用流式传输
    jsonBody["workflow_status"] = "enable";  // 启用工作流

    // 记录请求信息（跟请求体一致）
    is_incremental_ = jsonBody.value("incremental").toBool(false);  // 默认 false

    if (retry_count > 0) {
        spdlog::info("Retrying AI analysis request ({}/{})", retry_count, MAX_RETRIES);
    } else {
        spdlog::info("Sending AI analysis request (Tencent SSE Mode)...");
    }
    spdlog::info("Session ID: {}, Request ID: {}", session_id.toStdString(), request_id.toStdString());

    // 清空缓冲区
    sse_buffer_.clear();

    current_reply_ = network_manager_->post(request, QJsonDocument(jsonBody).toJson());
    QNetworkReply* reply = current_reply_;

    // 启动超时定时器
    timeout_timer_->start(TIMEOUT_MS);

    // 处理流式数据（腾讯云 SSE 事件流格式）
    // 关键：支持多行 data 拼接、正确的 incremental 语义、防止重复 emit
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (!current_reply_ || current_reply_ != reply) {
            return;  // 请求已被取消
        }

        // 重置超时定时器（有数据到达）
        if (timeout_timer_->isActive()) {
            timeout_timer_->start(TIMEOUT_MS);
        }

        QByteArray newData = reply->readAll();
        sse_buffer_.append(newData);

        while (true) {
            // SSE 事件以空行分隔（\n\n 或 \r\n\r\n）
            // 关键：先取 block，再 remove，避免丢包/乱包
            int idx = sse_buffer_.indexOf("\n\n");
            int separator_len = 2;
            if (idx == -1) {
                // 尝试 \r\n\r\n
                idx = sse_buffer_.indexOf("\r\n\r\n");
                separator_len = 4;
                if (idx == -1) break;
            }

            // 正确顺序：先取 block，再 remove
            QByteArray eventData = sse_buffer_.left(idx).trimmed();
            sse_buffer_.remove(0, idx + separator_len);

            // 忽略空行/心跳行（某些代理会插入）
            if (eventData.isEmpty()) {
                continue;
            }

            // 按行解析事件块
            QString eventType;
            QByteArray dataBuffer;  // 用于拼接多行 data

            QList<QByteArray> lines = eventData.split('\n');
            for (const QByteArray& line : lines) {
                QByteArray trimmedLine = line.trimmed();

                // 解析 event 行（兼容 "event:reply" 和 "event: reply"）
                if (trimmedLine.startsWith("event:") || trimmedLine.startsWith("event :")) {
                    int colonIdx = trimmedLine.indexOf(':');
                    eventType = QString::fromUtf8(trimmedLine.mid(colonIdx + 1)).trimmed();
                }
                // 解析 data 行（兼容 "data:{...}" 和 "data: {...}"）
                else if (trimmedLine.startsWith("data:") || trimmedLine.startsWith("data :")) {
                    int colonIdx = trimmedLine.indexOf(':');
                    QByteArray dataContent = trimmedLine.mid(colonIdx + 1).trimmed();
                    // 支持多行 data 拼接（防止粘包/分段）
                    if (!dataBuffer.isEmpty()) {
                        dataBuffer.append(dataContent);
                    } else {
                        dataBuffer = dataContent;
                    }
                }
            }

            // 解析 data JSON
            QJsonObject root;
            QJsonObject payload;
            if (!dataBuffer.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(dataBuffer);
                if (doc.isObject()) {
                    root = doc.object();
                    // 兼容两种格式：
                    // 1. {"type":"reply","payload":{...}}
                    // 2. {"payload":{...}}
                    if (root.contains("payload")) {
                        payload = root["payload"].toObject();
                    } else {
                        payload = root;
                    }
                }
            }

            // 如果没有 event 行，尝试从 data JSON 中解析 type（兼容模式）
            if (eventType.isEmpty()) {
                eventType = root["type"].toString();
            }
            if (eventType.isEmpty()) {
                eventType = root["event"].toString();
            }

            // 如果依然拿不到事件类型，但有 content，默认当作 reply 处理
            if (eventType.isEmpty()) {
                const QString content_probe = payload["content"].toString();
                if (!content_probe.isEmpty()) {
                    eventType = "reply";
                }
            }

            if (eventType.isEmpty()) {
                continue;  // 跳过无效事件
            }

            // 处理各类事件
            if (eventType == "reply") {
                // 处理回复事件
                QString content = payload["content"].toString();
                bool is_final = payload["is_final"].toBool();
                if (!is_final && root.contains("is_final")) {
                    is_final = root["is_final"].toBool();
                }
                bool is_evil = payload["is_evil"].toBool();

                spdlog::debug("Reply event: content_len={}, is_final={}, is_evil={}",
                             content.length(), is_final, is_evil);

                if (is_evil) {
                    spdlog::warn("Content flagged as sensitive");
                    timeout_timer_->stop();
                    emit errorOccurred("内容包含敏感信息，已被拦截");
                    // 异步清理，避免在 readyRead 回调中直接 delete reply
                    QTimer::singleShot(0, this, &AiAnalysisService::cleanup);
                    continue;
                }

                if (!content.isEmpty()) {
                    spdlog::info("Received AI content: {} chars, is_final={}", content.length(), is_final);
                    // 处理 incremental 语义
                    if (is_incremental_) {
                        // incremental=true：content 是增量片段，需要 append
                        incremental_buffer_.append(content);
                        emit analysisResultReady(content);
                    } else {
                        // incremental=false：content 可能覆盖之前答案，需要 replace
                        incremental_buffer_ = content;
                        emit analysisResultReady(content);
                    }
                } else {
                    spdlog::warn("Received empty content, is_final={}", is_final);
                }

                // 结束判断：is_final == true（不是 OpenAI 的 [DONE] 标记）
                if (is_final) {
                    spdlog::info("AI analysis completed (is_final=true)");
                    timeout_timer_->stop();

                    // 防止重复 emit analysisFinished
                    if (!completed_) {
                        completed_ = true;
                        emit analysisFinished();
                    }

                    // 不要立即 cleanup()，让连接自然关闭
                    // finished 信号会在连接关闭后触发，那时再清理
                    // 这样可以确保所有数据都被处理完
                }
            }
            else if (eventType == "error") {
                // 处理错误事件（无 payload，直接在 root 中）
                QJsonObject error;
                if (dataBuffer.isEmpty()) {
                    continue;
                }
                QJsonDocument doc = QJsonDocument::fromJson(dataBuffer);
                if (doc.isObject()) {
                    error = doc.object()["error"].toObject();
                }

                int code = error["code"].toInt();
                QString message = error["message"].toString();
                spdlog::error("AI error: code={}, message={}", code, message.toStdString());

                timeout_timer_->stop();

                // 特定错误码处理
                if (code == 460011) {
                    emit errorOccurred("超出并发数限制，请稍后再试");
                } else if (code == 460032) {
                    emit errorOccurred("模型余额不足，请联系管理员");
                } else if (code == 460034) {
                    emit errorOccurred("输入内容过长，请减少数据量");
                } else {
                    emit errorOccurred(QString("错误 %1: %2").arg(code).arg(message));
                }

                // 异步清理
                QTimer::singleShot(0, this, &AiAnalysisService::cleanup);
            }
            else if (eventType == "token_stat") {
                // 处理 token 统计事件
                int token_count = payload["token_count"].toInt();
                spdlog::info("Token count: {}", token_count);
            }
            else if (eventType == "reference") {
                // 处理参考来源事件
                spdlog::info("Received reference event");
            }
            else if (eventType == "thought") {
                // 处理思考事件（DeepSeek-R1 等模型）
                QString thought_content = payload["content"].toString();
                spdlog::info("Model thinking: {}", thought_content.toStdString());
            }
            else {
                spdlog::warn("Unknown event type: {}", eventType.toStdString());
            }
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (!current_reply_ || current_reply_ != reply) {
            reply->deleteLater();
            return;  // 请求已被取消
        }

        timeout_timer_->stop();

        // 获取 HTTP 状态码（文档明确："需判断取值是否为 200，是则正常返回"）
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 严格按文档：必须是 200 才正常，否则按错误处理
        // 不能用 httpStatus != 200 && error != NoError，会漏掉某些情况
        if (httpStatus != 200) {
            QString err = reply->errorString();
            spdlog::error("AI request failed: HTTP {} (expected 200), {}", httpStatus, err.toStdString());

            // 读取响应体以获取详细错误信息
            QByteArray responseData = reply->readAll();
            if (!responseData.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(responseData);
                if (doc.isObject()) {
                    QJsonObject root = doc.object();
                    if (root.contains("error")) {
                        QJsonObject error = root["error"].toObject();
                        QString errorMsg = error["message"].toString();
                        int errorCode = error["code"].toInt();
                        spdlog::error("Server error: code={}, message={}", errorCode, errorMsg.toStdString());

                        // 检查是否需要重试
                        bool should_retry = false;
                        if (errorCode == 460011 || errorCode == 460020) {
                            // 并发限制或超时，可以重试
                            should_retry = true;
                        }

                        if (should_retry && current_retry_count_ < MAX_RETRIES) {
                            spdlog::info("Network error, will retry ({}/{})",
                                        current_retry_count_ + 1, MAX_RETRIES);
                            reply->deleteLater();
                            current_reply_.clear();

                            // 延迟后重试
                            QTimer::singleShot(RETRY_DELAY_MS * (current_retry_count_ + 1), this, [this]() {
                                doRequest(current_stats_, current_trend_summary_,
                                         current_detail_records_, current_user_prompt_,
                                         current_retry_count_ + 1);
                            });
                            return;
                        }

                        emit errorOccurred(QString("服务器错误 %1: %2").arg(errorCode).arg(errorMsg));
                        QTimer::singleShot(0, this, &AiAnalysisService::cleanup);
                        reply->deleteLater();
                        return;
                    }
                }
            }

            // 网络错误，检查是否需要重试
            if (current_retry_count_ < MAX_RETRIES &&
                (reply->error() == QNetworkReply::TimeoutError ||
                 reply->error() == QNetworkReply::TemporaryNetworkFailureError ||
                 reply->error() == QNetworkReply::NetworkSessionFailedError)) {

                spdlog::info("Network error, will retry ({}/{})",
                            current_retry_count_ + 1, MAX_RETRIES);
                reply->deleteLater();
                current_reply_.clear();

                // 延迟后重试
                QTimer::singleShot(RETRY_DELAY_MS * (current_retry_count_ + 1), this, [this]() {
                    doRequest(current_stats_, current_trend_summary_,
                             current_detail_records_, current_user_prompt_,
                             current_range_days_,
                             current_retry_count_ + 1);
                });
                return;
            }

            emit errorOccurred("网络请求失败: " + err);
            QTimer::singleShot(0, this, &AiAnalysisService::cleanup);
            reply->deleteLater();
            return;
        }

        // HTTP 200 成功：对于 SSE 流式传输，finished 信号表示所有数据传输完成
        spdlog::info("SSE connection finished (HTTP 200)");

        // 正常情况下，应该在 readyRead 中收到 is_final=true 并设置 completed_=true
        // 如果到这里 completed_ 还是 false，说明：
        // 1. 服务器没有发送 is_final=true 就关闭了连接（异常情况）
        // 2. 或者数据还在缓冲区中没有被 readyRead 处理（极少见）
        if (!completed_) {
            spdlog::warn("SSE connection closed without receiving is_final=true");
            completed_ = true;

            // 如果有接收到数据，视为成功完成（容错处理）
            if (!incremental_buffer_.isEmpty()) {
                spdlog::info("Treating as completed due to received data");
                emit analysisFinished();
            } else {
                // 没有收到任何数据，视为错误
                emit errorOccurred("服务器未返回任何数据");
            }
        }

        //  正常完成：只清理资源，不调用 abort()
        // 手动清理，避免调用 cleanup() 中的 abort()
        if (timeout_timer_) {
            timeout_timer_->stop();
        }
        if (current_reply_) {
            current_reply_->deleteLater();
            current_reply_.clear();
        }
        sse_buffer_.clear();
        incremental_buffer_.clear();
        current_retry_count_ = 0;
        completed_ = false;

        reply->deleteLater();
    });
}

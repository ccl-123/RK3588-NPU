/**
 * @file ai_analysis_service.cc
 * @author CL
 * @brief 
 * @date 2025-12-21
 *对话端接口文档：https://cloud.tencent.com/document/product/1759/105561
 * 
 * @copyright Copyright (c) 2025
 */
#include "gui_services/ai_analysis_service.h"
#include "service/attendance_service.h"
#include "config/config.h"
#include "gui_services/ai_prompt_builder.h"
#include "agent/agent_worker.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QUuid>
#include <QTimer>
#include <QEventLoop>
#include <QThread>
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
    , is_incremental_(false)
    , agent_mode_(true) {
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
                    doCloudRequest(current_stats_, current_trend_summary_,
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

    // 清理 Agent 相关
    if (agent_running_.load()) {
        agent_cancel_requested_ = true;
        if (agent_service_) {
            if (agent_service_->thread() != QThread::currentThread()) {
                QMetaObject::invokeMethod(agent_service_.get(), &agent::AgentService::stop, Qt::QueuedConnection);
            } else {
                agent_service_->stop();
            }
        }
        if (current_worker_) {
            current_worker_->requestStop();
        }
        if (current_thread_) {
            current_thread_->requestInterruption();
            current_thread_->quit();
            // 不要在 cleanup 中等待，避免阻塞主线程。
            // 线程的实际清理由其 finished 信号连接的 deleteLater 完成。
        }
    }

    sse_buffer_.clear();
    incremental_buffer_.clear();
    current_retry_count_ = 0;
    completed_ = false;  // 重置完成标志，为下一次请求做准备
}

bool AiAnalysisService::isAnalyzing() const {
    return current_reply_ != nullptr || agent_running_.load();
}

void AiAnalysisService::cancelAnalysis() {
    // 取消 HTTP 请求
    if (current_reply_) {
        spdlog::info("Cloud AI analysis cancelled by user");
        cleanup();
        emit analysisCancelled();
        return;
    }

    // 取消 Agent 请求
    if (agent_running_.load()) {
        spdlog::info("Cloud Agent analysis cancelled by user");
        agent_cancel_requested_ = true;
        agent_active_request_id_ = 0;
        if (agent_service_) {
            if (agent_service_->thread() != QThread::currentThread()) {
                QMetaObject::invokeMethod(agent_service_.get(), &agent::AgentService::stop, Qt::QueuedConnection);
            } else {
                agent_service_->stop();
            }
        }
        if (current_worker_) {
            current_worker_->requestStop();
        }
        if (current_thread_) {
            current_thread_->requestInterruption();
            current_thread_->quit();
        }
        agent_running_ = false;
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

    doCloudRequest(stats, trend_summary, detail_records, user_prompt, range_days, 0);
}

void AiAnalysisService::doCloudRequest(const service::AttendanceStatistics& stats,
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

    // 构建 Prompt
    QString content = AiPromptBuilder::buildPrompt(
        stats, trend_summary, detail_records, user_prompt, range_days);

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

            if (eventType.isEmpty()) {
                continue;  // 跳过无效事件
            }

            spdlog::debug("SSE event: type={}, dataLen={}", eventType.toStdString(), dataBuffer.size());

            // 处理各类事件
            if (eventType == "workflow_status" || eventType == "workflow") {
                spdlog::debug("Ignoring workflow event");
                continue;
            }
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
                    // 空 content 但 is_final=true 可能是工作流结束信号，忽略
                    if (is_final) {
                        spdlog::debug("Ignoring empty content with is_final=true (likely workflow signal)");
                        continue;
                    }
                    spdlog::warn("Received empty content, is_final={}", is_final);
                }

                // 注意：不在这里判断结束！
                // 腾讯云 SSE 可能发送多个 reply 事件，每个都有自己的 is_final
                // 第一个 reply 可能带 is_final=true（快速摘要），但后续还有更多数据
                // 真正的结束判断放在 finished 信号处理中（HTTP 连接关闭时）
                if (is_final) {
                    spdlog::info("Received is_final=true marker (total {} chars so far), waiting for connection close",
                                incremental_buffer_.length());
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
                                doCloudRequest(current_stats_, current_trend_summary_,
                                         current_detail_records_, current_user_prompt_,
                                         current_range_days_,
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
                    doCloudRequest(current_stats_, current_trend_summary_,
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

        // HTTP 200 成功：SSE 连接关闭，这才是真正的结束时机
        spdlog::info("SSE connection finished (HTTP 200), total received: {} chars", incremental_buffer_.length());

        // 在连接关闭时才触发 analysisFinished
        // 这确保了所有数据都已接收完毕，按钮状态才会改变
        if (!completed_) {
            completed_ = true;

            if (!incremental_buffer_.isEmpty()) {
                spdlog::info("AI analysis completed (connection closed, {} chars)", incremental_buffer_.length());
                emit analysisFinished();
            } else {
                // 没有收到任何数据，视为错误
                spdlog::warn("SSE connection closed without receiving any data");
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

// ==================== Cloud Agent 功能实现 ====================

void AiAnalysisService::requestAgentChat(const QString& user_input) {
    if (!agent_service_) {
        emit errorOccurred("Agent 未初始化");
        return;
    }

    if (agent_running_.load()) {
        spdlog::warn("Cloud Agent already running, ignoring new request");
        return;
    }

    spdlog::info("Cloud Agent chat request: {}", user_input.left(50).toStdString());

    const uint64_t request_id = ++agent_request_seq_;
    agent_active_request_id_ = request_id;

    agent_running_ = true;
    agent_cancel_requested_ = false;
    emit analysisStarted();

    // 创建工作线程
    current_thread_ = new QThread;
    if (agent_service_->thread() != current_thread_) {
        agent_service_->moveToThread(current_thread_);
    }
    current_worker_ = new agent::AgentWorker(agent_service_.get());
    current_worker_->moveToThread(current_thread_);

    // 设置同步云端 LLM 回调
    current_worker_->setLlmCallback(createSyncCloudLlmCallback());

    // 线程启动逻辑
    connect(current_thread_, &QThread::started, current_worker_, [this, user_input]() {
        current_worker_->process(user_input);
    });

    // 转发信号
    connect(current_worker_, &agent::AgentWorker::thinkingStarted, this, &AiAnalysisService::agentThinking);
    connect(current_worker_, &agent::AgentWorker::toolCalling, this, &AiAnalysisService::agentToolCalling);
    connect(current_worker_, &agent::AgentWorker::toolCompleted, this, &AiAnalysisService::agentToolCompleted);
    connect(current_worker_, &agent::AgentWorker::chunkReady, this, &AiAnalysisService::analysisResultReady);
    connect(current_worker_, &agent::AgentWorker::errorOccurred, this, &AiAnalysisService::errorOccurred);

    // 完成处理
    connect(current_worker_, &agent::AgentWorker::finished, this, [this, request_id](const QString& answer) {
        if (agent_active_request_id_.load() != request_id) {
            return;
        }
        agent_running_ = false;
        current_worker_ = nullptr;
        current_thread_ = nullptr;

        if (!agent_cancel_requested_) {
            // 发送最终答案到 UI
            if (!answer.isEmpty()) {
                emit analysisResultReady(answer);
            }
            emit analysisFinished();
        }
    });

    // 自动清理与线程归位
    QThread* worker_thread = current_thread_;
    connect(current_worker_, &agent::AgentWorker::finished, current_worker_, [this, worker_thread]() {
        if (agent_service_ && agent_service_->thread() != QCoreApplication::instance()->thread()) {
            agent_service_->moveToThread(QCoreApplication::instance()->thread());
        }
        if (worker_thread) {
            worker_thread->quit();
        }
    });
    connect(current_thread_, &QThread::finished, current_worker_, &QObject::deleteLater);
    connect(current_thread_, &QThread::finished, current_thread_, &QObject::deleteLater);

    current_thread_->start();
}

void AiAnalysisService::initializeAgent(service::AttendanceService* attendance_svc,
                                         service::UserService* user_svc) {
    agent::AgentConfig config;
    config.max_iterations = Config::Agent::MAX_ITERATIONS;
    config.stream_output = Config::Agent::STREAM_OUTPUT;
    config.skip_system_prompt = Config::Agent::Cloud::PRESET_SYSTEM_PROMPT;

    agent_service_ = std::make_unique<agent::AgentService>(config, nullptr);
    if (attendance_svc || user_svc) {
        agent_service_->registerBuiltinTools(attendance_svc, user_svc);
    }
    spdlog::info("Cloud Agent initialized with {} tools (skip_system_prompt={})",
        agent_service_->getToolCount(), config.skip_system_prompt);
}

void AiAnalysisService::setAgentMode(bool enabled) {
    agent_mode_ = enabled;
    spdlog::info("Cloud Agent mode {}", enabled ? "enabled" : "disabled");
}

void AiAnalysisService::clearAgentHistory() {
    if (agent_service_) {
        agent_service_->clearHistory();
        spdlog::info("Cloud Agent history cleared");
    }
}

std::function<QString(const QString&)> AiAnalysisService::createSyncCloudLlmCallback() {
    return [this](const QString& prompt) -> QString {
        if (agent_cancel_requested_.load()) {
            return QString();
        }
        return doSyncCloudRequest(prompt);
    };
}

QString AiAnalysisService::doSyncCloudRequest(const QString& prompt) {
    QString result;
    bool error = false;

    spdlog::debug("Cloud Agent sync request, prompt length: {}", prompt.length());

    // 由于是在 AgentWorker 线程中，我们可以使用本地 QEventLoop 进行同步等待
    QEventLoop loop;

    // 创建线程局部的 QNetworkAccessManager，确保信号槽在当前线程正确执行
    QNetworkAccessManager local_manager;

    // 同步请求实现，使用 SSE 获取流式输出
    QUrl url(QString::fromStdString(Config::TencentAI::API_URL));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");

    QString session_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject jsonBody;
    jsonBody["session_id"] = session_id;
    jsonBody["request_id"] = request_id;
    jsonBody["bot_app_key"] = QString::fromUtf8(Config::TencentAI::getAppKey());
    jsonBody["visitor_biz_id"] = QString::fromStdString(Config::TencentAI::VISITOR_BIZ_ID);
    jsonBody["content"] = prompt;
    jsonBody["incremental"] = true;
    jsonBody["streaming_throttle"] = 10;
    jsonBody["search_network"] = "disable";
    jsonBody["stream"] = "enable";
    jsonBody["workflow_status"] = "enable";

    QNetworkReply* reply = local_manager.post(request, QJsonDocument(jsonBody).toJson());

    QByteArray sse_buffer;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QNetworkReply::readyRead, &loop, [this, reply, &loop, &sse_buffer, &result, &timer]() {
        if (agent_cancel_requested_.load()) {
            reply->abort();
            loop.quit();
            return;
        }

        if (timer.isActive()) {
            timer.start(Config::Agent::LLM_TIMEOUT_MS);
        }
        sse_buffer.append(reply->readAll());

        while (true) {
            int idx = sse_buffer.indexOf("\n\n");
            int separator_len = 2;
            if (idx == -1) {
                idx = sse_buffer.indexOf("\r\n\r\n");
                separator_len = 4;
                if (idx == -1) break;
            }

            QByteArray event_data = sse_buffer.left(idx).trimmed();
            sse_buffer.remove(0, idx + separator_len);

            if (event_data.isEmpty()) {
                continue;
            }

            QString event_type;
            QByteArray data_buffer;
            QList<QByteArray> lines = event_data.split('\n');
            for (const QByteArray& line : lines) {
                QByteArray trimmed = line.trimmed();
                if (trimmed.startsWith("event:") || trimmed.startsWith("event :")) {
                    int colon_idx = trimmed.indexOf(':');
                    event_type = QString::fromUtf8(trimmed.mid(colon_idx + 1)).trimmed();
                } else if (trimmed.startsWith("data:") || trimmed.startsWith("data :")) {
                    int colon_idx = trimmed.indexOf(':');
                    QByteArray data_content = trimmed.mid(colon_idx + 1).trimmed();
                    if (!data_buffer.isEmpty()) {
                        data_buffer.append(data_content);
                    } else {
                        data_buffer = data_content;
                    }
                }
            }

            if (event_type == "workflow_status" || event_type == "workflow") {
                continue;
            }

            if (data_buffer.isEmpty()) {
                continue;
            }

            QJsonDocument doc = QJsonDocument::fromJson(data_buffer);
            if (!doc.isObject()) {
                continue;
            }

            QJsonObject root = doc.object();
            QJsonObject payload = root.contains("payload") ? root["payload"].toObject() : root;
            QString content = payload["content"].toString();
            if (content.isEmpty()) {
                continue;
            }

            result += content;
            // 流式输出到 UI（调试用）
            emit analysisResultReady(content);
        }
    });

    connect(reply, &QNetworkReply::finished, &loop, [&loop, &result]() {
        spdlog::info("Cloud Agent SSE finished, total result length: {}", result.length());
        spdlog::debug("Cloud Agent SSE result: {}", result.left(200).toStdString());
        loop.quit();
    });

    // 超时处理
    connect(&timer, &QTimer::timeout, &loop, [&loop, &error, reply]() {
        error = true;
        reply->abort();
        loop.quit();
    });
    timer.start(Config::Agent::LLM_TIMEOUT_MS);

    loop.exec();

    if (!error && reply->error() == QNetworkReply::NoError) {
        if (result.isEmpty()) {
            spdlog::warn("Cloud Agent: SSE finished without content");
        }
    } else {
        if (error) {
            spdlog::error("Sync Cloud Agent request timeout after {}ms", Config::Agent::LLM_TIMEOUT_MS);
        } else {
            spdlog::error("Sync Cloud Agent request failed: {}", reply->errorString().toStdString());
        }
    }

    reply->deleteLater();
    return result;
}

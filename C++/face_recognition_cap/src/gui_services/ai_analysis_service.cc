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
#include "gui_services/llm_protocol_adapter.h"
#include "agent/agent_worker.h"
#include "agent/incremental_response_parser.h"
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

namespace {

enum class RemoteProvider {
    Tencent,
    LlamaCpp,
};

RemoteProvider detect_remote_provider() {
    const QString llama_base = QString::fromUtf8(Config::LlamaCpp::getBaseUrl()).trimmed();
    if (!llama_base.isEmpty()) {
        return RemoteProvider::LlamaCpp;
    }
    return RemoteProvider::Tencent;
}

QUrl build_remote_url(RemoteProvider provider) {
    if (provider == RemoteProvider::LlamaCpp) {
        QString url = QString::fromUtf8(Config::LlamaCpp::getBaseUrl()).trimmed();
        
        // 1. 如果用户提供了完整路径（包含 /chat/completions），则直接使用
        if (url.contains("/chat/completions")) {
            return QUrl(url);
        }

        // 2. 去掉结尾斜杠，方便后续统一拼接
        if (url.endsWith('/')) {
            url.chop(1);
        }

        // 3. 智能拼接：
        // 如果 URL 以 /v1 结尾，说明用户已经指定了 API 版本，只需追加功能路径
        if (url.endsWith("/v1")) {
            return QUrl(url + "/chat/completions");
        }
        
        // 4. 默认行为：视为基地址，追加完整路径
        return QUrl(url + "/v1/chat/completions");
    }
    return QUrl(QString::fromStdString(Config::TencentAI::API_URL));
}

void apply_remote_auth(RemoteProvider provider, QNetworkRequest& request) {
    if (provider != RemoteProvider::LlamaCpp) {
        return;
    }

    const QByteArray api_key = QByteArray(Config::LlamaCpp::getApiKey()).trimmed();
    if (api_key.isEmpty()) {
        return;
    }

    request.setRawHeader("Authorization", "Bearer " + api_key);
}

QJsonObject build_remote_request(RemoteProvider provider,
                                 const QString& content,
                                 bool stream_enabled) {
    if (provider == RemoteProvider::LlamaCpp) {
        QJsonObject body;
        body["model"] = QString::fromUtf8(Config::LlamaCpp::getModel());
        body["stream"] = stream_enabled;
        body["messages"] = QJsonArray{
            QJsonObject{
                {"role", "user"},
                {"content", content}
            }
        };
        return body;
    }

    QString session_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString request_id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject body;
    body["session_id"] = session_id;
    body["request_id"] = request_id;
    body["bot_app_key"] = QString::fromUtf8(Config::TencentAI::getAppKey());
    body["visitor_biz_id"] = QString::fromStdString(Config::TencentAI::VISITOR_BIZ_ID);
    body["content"] = content;
    body["incremental"] = true;
    body["streaming_throttle"] = 10;
    body["search_network"] = "disable";
    body["stream"] = "enable";
    body["workflow_status"] = "enable";
    return body;
}

QVector<gui_services::ProtocolChunkEvent> consume_remote_events(RemoteProvider provider, QByteArray& buffer) {
    if (provider == RemoteProvider::LlamaCpp) {
        return gui_services::LlmProtocolAdapter::consumeLlamaCppSse(buffer);
    }
    return gui_services::LlmProtocolAdapter::consumeTencentSse(buffer);
}

}  // namespace

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
    qRegisterMetaType<agent::AgentStreamEvent>("agent::AgentStreamEvent");
    qRegisterMetaType<agent::ToolInvocation>("agent::ToolInvocation");
    qRegisterMetaType<agent::ToolExecutionResult>("agent::ToolExecutionResult");
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

    if (detect_remote_provider() == RemoteProvider::LlamaCpp) {
        spdlog::info("Remote LLM provider: llama.cpp ({})", Config::LlamaCpp::getBaseUrl());
        if (std::strlen(Config::LlamaCpp::getApiKey()) == 0) {
            spdlog::warn("LLAMA_CPP_SERVER_API_KEY is not set");
        } else {
            spdlog::info("LLAMA_CPP_SERVER_API_KEY loaded from environment (length: {})",
                         std::strlen(Config::LlamaCpp::getApiKey()));
        }
    } else {
        spdlog::info("Remote LLM provider: Tencent LKE");
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

void AiAnalysisService::beginStreamRequest() {
    active_stream_request_id_ = ++stream_request_seq_;
    stream_event_seq_ = 0;
    stream_has_visible_output_ = false;
}

void AiAnalysisService::emitStreamEvent(const QString& phase,
                                        const QString& type,
                                        const QString& channel,
                                        const QString& text,
                                        const QJsonObject& data,
                                        bool final) {
    agent::AgentStreamEvent event;
    event.request_id = active_stream_request_id_;
    event.seq = ++stream_event_seq_;
    event.phase = phase;
    event.type = type;
    event.channel = channel;
    event.text = text;
    event.data = data;
    event.final = final;

    if (channel == "assistant" && type == "delta" && !text.isEmpty()) {
        stream_has_visible_output_ = true;
    }

    emit streamEventReady(event);
}

void AiAnalysisService::emitAssistantDelta(const QString& text,
                                           const QString& phase,
                                           bool final) {
    if (text.isEmpty()) {
        return;
    }
    emit analysisResultReady(text);
    emitStreamEvent(phase, "delta", "assistant", text, QJsonObject(), final);
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
    beginStreamRequest();

    // 发送开始信号
    emit analysisStarted();
    emitStreamEvent("model", "start", "status");

    doCloudRequest(stats, trend_summary, detail_records, user_prompt, range_days, 0);
}

void AiAnalysisService::doCloudRequest(const service::AttendanceStatistics& stats,
                                       const QString& trend_summary,
                                       const QString& detail_records,
                                       const QString& user_prompt,
                                       int range_days,
                                       int retry_count) {
    current_retry_count_ = retry_count;

    const RemoteProvider provider = detect_remote_provider();
    QUrl url(build_remote_url(provider));
    QNetworkRequest request(url);

    // 设置请求头（SSE 接口需要 Content-Type 和 Accept）
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");  // 关键：告诉代理/CDN 这是 SSE 流
    apply_remote_auth(provider, request);

    // 构建 Prompt
    QString content = AiPromptBuilder::buildPrompt(
        stats, trend_summary, detail_records, user_prompt, range_days);

    QJsonObject jsonBody = build_remote_request(provider, content, true);
    is_incremental_ = provider == RemoteProvider::Tencent
        ? jsonBody.value("incremental").toBool(false)
        : true;

    if (retry_count > 0) {
        spdlog::info("Retrying AI analysis request ({}/{})", retry_count, MAX_RETRIES);
    } else {
        spdlog::info("Sending AI analysis request (provider={})...",
                     provider == RemoteProvider::LlamaCpp ? "llama.cpp" : "tencent");
    }

    // 清空缓冲区
    sse_buffer_.clear();

    current_reply_ = network_manager_->post(request, QJsonDocument(jsonBody).toJson());
    QNetworkReply* reply = current_reply_;

    // 启动超时定时器
    timeout_timer_->start(TIMEOUT_MS);

    // 处理流式数据（腾讯云 SSE 事件流格式）
    // 关键：支持多行 data 拼接、正确的 incremental 语义、防止重复 emit
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, provider]() {
        if (!current_reply_ || current_reply_ != reply) {
            return;  // 请求已被取消
        }

        // 重置超时定时器（有数据到达）
        if (timeout_timer_->isActive()) {
            timeout_timer_->start(TIMEOUT_MS);
        }

        sse_buffer_.append(reply->readAll());

        const auto events = consume_remote_events(provider, sse_buffer_);
        for (const auto& event : events) {
            if (event.kind == "delta") {
                const QString content = event.text;
                if (!content.isEmpty()) {
                    if (is_incremental_) {
                        incremental_buffer_.append(content);
                    } else {
                        incremental_buffer_ = content;
                    }
                    emitAssistantDelta(content, "model", event.final);
                }
                continue;
            }

            if (event.kind == "reasoning" && !event.text.isEmpty()) {
                emitStreamEvent("model", "reasoning", "reasoning", event.text);
                continue;
            }

            if (event.kind == "error") {
                const QJsonObject error = event.data.value("error").toObject();
                const int code = error.value("code").toInt();
                const QString message = error.value("message").toString(event.text);
                timeout_timer_->stop();

                if (code == 460011) {
                    emitStreamEvent("model", "error", "status", "超出并发数限制，请稍后再试");
                    emit errorOccurred("超出并发数限制，请稍后再试");
                } else if (code == 460032) {
                    emitStreamEvent("model", "error", "status", "模型余额不足，请联系管理员");
                    emit errorOccurred("模型余额不足，请联系管理员");
                } else if (code == 460034) {
                    emitStreamEvent("model", "error", "status", "输入内容过长，请减少数据量");
                    emit errorOccurred("输入内容过长，请减少数据量");
                } else {
                    emitStreamEvent("model", "error", "status",
                                    QString("错误 %1: %2").arg(code).arg(message));
                    emit errorOccurred(QString("错误 %1: %2").arg(code).arg(message));
                }

                QTimer::singleShot(0, this, &AiAnalysisService::cleanup);
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
                emitStreamEvent("model", "done", "assistant", QString(), QJsonObject(), true);
                emit analysisFinished();
            } else {
                // 没有收到任何数据，视为错误
                spdlog::warn("SSE connection closed without receiving any data");
                emitStreamEvent("model", "error", "status", "服务器未返回任何数据");
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
    agent_failed_ = false;
    beginStreamRequest();
    emit analysisStarted();
    emitStreamEvent("agent", "start", "status");

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
    connect(current_worker_, &agent::AgentWorker::thinkingStarted, this, [this]() {
        emit agentThinking();
        emitStreamEvent("agent", "thinking", "status", "思考中");
    });
    connect(current_worker_, &agent::AgentWorker::toolCalling, this, &AiAnalysisService::agentToolCalling);
    connect(current_worker_, &agent::AgentWorker::toolCompleted, this, &AiAnalysisService::agentToolCompleted);
    connect(current_worker_, &agent::AgentWorker::toolInvocationReady, this, [this](const agent::ToolInvocation& invocation) {
        QJsonObject data;
        data["tool_name"] = invocation.name;
        data["call_id"] = invocation.call_id;
        data["arguments"] = invocation.arguments;
        emitStreamEvent("tool", "tool_call", "status", invocation.name, data);
    });
    connect(current_worker_, &agent::AgentWorker::toolResultReady, this, [this](const agent::ToolExecutionResult& result) {
        QJsonObject data;
        data["tool_name"] = result.name;
        data["call_id"] = result.call_id;
        data["result"] = result.promptText();
        data["ok"] = result.ok;
        data["output"] = result.output;
        if (!result.error.isEmpty()) {
            data["error"] = result.error;
        }
        emitStreamEvent("tool", "tool_result", "status", result.name, data);
    });
    connect(current_worker_, &agent::AgentWorker::errorOccurred, this, [this, request_id](const QString& error) {
        if (agent_active_request_id_.load() != request_id) {
            return;
        }
        agent_failed_ = true;
        emitStreamEvent("agent", "error", "status", error);
        emit errorOccurred(error);
    });

    // 完成处理
    connect(current_worker_, &agent::AgentWorker::finished, this, [this, request_id](const QString& answer) {
        if (agent_active_request_id_.load() != request_id) {
            return;
        }
        agent_running_ = false;
        current_worker_ = nullptr;
        current_thread_ = nullptr;

        if (agent_cancel_requested_) {
            spdlog::info("Cloud Agent chat was cancelled");
        } else if (agent_failed_.load()) {
            spdlog::warn("Cloud Agent chat finished after failure, skipping success completion");
        } else {
            if (!stream_has_visible_output_.load() && !answer.isEmpty()) {
                emitAssistantDelta(answer, "agent", true);
            }
            emitStreamEvent("agent", "done", "assistant", QString(), QJsonObject(), true);
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
    const RemoteProvider provider = detect_remote_provider();
    agent::AgentConfig config;
    config.max_iterations = Config::Agent::MAX_ITERATIONS;
    config.stream_output = Config::Agent::STREAM_OUTPUT;
    config.skip_system_prompt = provider == RemoteProvider::Tencent
        ? Config::Agent::Cloud::PRESET_SYSTEM_PROMPT
        : false;

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
    bool cancelled = false;
    bool error_reported = false;
    QString error_msg;
    agent::IncrementalResponseParser parser;
    const RemoteProvider provider = detect_remote_provider();

    spdlog::debug("Cloud Agent sync request, prompt length: {}", prompt.length());

    // 由于是在 AgentWorker 线程中，我们可以使用本地 QEventLoop 进行同步等待
    QEventLoop loop;

    // 创建线程局部的 QNetworkAccessManager，确保信号槽在当前线程正确执行
    QNetworkAccessManager local_manager;

    // 同步请求实现，使用 SSE 获取流式输出
    QUrl url(build_remote_url(provider));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");
    apply_remote_auth(provider, request);
    QJsonObject jsonBody = build_remote_request(provider, prompt, true);
    QNetworkReply* reply = local_manager.post(request, QJsonDocument(jsonBody).toJson());

    QByteArray sse_buffer;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QNetworkReply::readyRead, &loop, [this, reply, &loop, &sse_buffer, &result, &timer, &cancelled, &error, &error_reported, &error_msg, &parser, provider]() {
        if (agent_cancel_requested_.load()) {
            cancelled = true;
            reply->abort();
            loop.quit();
            return;
        }

        if (timer.isActive()) {
            timer.start(Config::Agent::LLM_TIMEOUT_MS);
        }
        sse_buffer.append(reply->readAll());

        const auto events = consume_remote_events(provider, sse_buffer);
        for (const auto& event : events) {
            if (event.kind == "delta") {
                result += event.text;
                const auto parsed = parser.push(event.text);
                emitAssistantDelta(parsed.assistant_delta, "agent", parsed.answer_finished || event.final);
                continue;
            }

            if (event.kind == "reasoning" && !event.text.isEmpty()) {
                emitStreamEvent("agent", "reasoning", "reasoning", event.text);
                continue;
            }

            if (event.kind == "error") {
                error = true;
                const QJsonObject err = event.data.value("error").toObject();
                const int code = err.value("code").toInt();
                const QString message = err.value("message").toString(event.text);
                error_msg = code > 0
                    ? QString("云端 LLM 错误 %1: %2").arg(code).arg(message)
                    : (message.isEmpty() ? QStringLiteral("云端 LLM 返回错误") : message);
                agent_failed_ = true;
                if (!error_reported) {
                    emitStreamEvent("agent", "error", "status", error_msg);
                    emit errorOccurred(error_msg);
                    error_reported = true;
                }
                reply->abort();
                loop.quit();
                return;
            }
        }
    });

    connect(reply, &QNetworkReply::finished, &loop, [&loop, &result]() {
        spdlog::info("Cloud Agent SSE finished, total result length: {}", result.length());
        spdlog::debug("Cloud Agent SSE result: {}", result.left(200).toStdString());
        loop.quit();
    });

    // 超时处理
    connect(&timer, &QTimer::timeout, &loop, [this, &loop, &error, &error_reported, &error_msg, reply]() {
        error = true;
        error_msg = "云端 LLM 请求超时";
        agent_failed_ = true;
        if (!error_reported) {
            emitStreamEvent("agent", "error", "status", error_msg);
            emit errorOccurred(error_msg);
            error_reported = true;
        }
        reply->abort();
        loop.quit();
    });
    timer.start(Config::Agent::LLM_TIMEOUT_MS);

    // 取消轮询：避免等待网络超时才响应取消
    QTimer cancel_timer;
    cancel_timer.setInterval(50);
    connect(&cancel_timer, &QTimer::timeout, &loop, [this, reply, &loop, &cancelled]() {
        if (!agent_cancel_requested_.load()) {
            return;
        }
        cancelled = true;
        reply->abort();
        loop.quit();
    });
    cancel_timer.start();

    loop.exec();
    cancel_timer.stop();
    timer.stop();

    if (cancelled) {
        spdlog::info("Sync Cloud Agent request cancelled");
        reply->deleteLater();
        return QString();
    }

    if (!error && reply->error() == QNetworkReply::NoError) {
        if (result.isEmpty()) {
            spdlog::warn("Cloud Agent: SSE finished without content");
        }
    } else {
        if (!error_msg.isEmpty()) {
            spdlog::error("Sync Cloud Agent request failed: {}", error_msg.toStdString());
        } else if (error) {
            spdlog::error("Sync Cloud Agent request timeout after {}ms", Config::Agent::LLM_TIMEOUT_MS);
        } else {
            spdlog::error("Sync Cloud Agent request failed: {}", reply->errorString().toStdString());
        }

        if (!error_reported) {
            if (error_msg.isEmpty()) {
                error_msg = error
                    ? QStringLiteral("云端 LLM 请求失败")
                    : QString("云端 LLM 请求失败: %1").arg(reply->errorString());
            }
            agent_failed_ = true;
            emitStreamEvent("agent", "error", "status", error_msg);
            emit errorOccurred(error_msg);
        }
    }

    reply->deleteLater();
    return result;
}

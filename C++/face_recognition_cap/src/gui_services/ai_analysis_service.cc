/**
 * @file ai_analysis_service.cc
 * @author CL
 * @brief 
 * @date 2025-12-21
 * 
 * @copyright Copyright (c) 2025
 */
#include "gui_services/ai_analysis_service.h"
#include "service/attendance_service.h"
#include "config/config.h"
#include "gui_services/llm_protocol_adapter.h"
#include "agent/agent_worker.h"
#include "agent/incremental_response_parser.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QTimer>
#include <QEventLoop>
#include <QThread>
#include <spdlog/spdlog.h>
#include <cstring>  // for strlen

namespace {

QUrl build_remote_url() {
    QString url = QString::fromUtf8(Config::LlamaCpp::getBaseUrl()).trimmed();
    if (url.isEmpty()) {
        return QUrl();
    }

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

void apply_remote_auth(QNetworkRequest& request) {
    const QByteArray api_key = QByteArray(Config::LlamaCpp::getApiKey()).trimmed();
    if (api_key.isEmpty()) {
        return;
    }

    request.setRawHeader("Authorization", "Bearer " + api_key);
}

QJsonObject build_remote_request(const QString& content,
                                 bool stream_enabled) {
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

QVector<gui_services::ProtocolChunkEvent> consume_remote_events(QByteArray& buffer) {
    return gui_services::LlmProtocolAdapter::consumeLlamaCppSse(buffer);
}

}  // namespace

AiAnalysisService* AiAnalysisService::instance() {
    static AiAnalysisService s_instance;
    return &s_instance;
}

AiAnalysisService::AiAnalysisService(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<agent::AgentStreamEvent>("agent::AgentStreamEvent");
    qRegisterMetaType<agent::ToolInvocation>("agent::ToolInvocation");
    qRegisterMetaType<agent::ToolExecutionResult>("agent::ToolExecutionResult");

    if (std::strlen(Config::LlamaCpp::getBaseUrl()) == 0) {
        spdlog::warn("LLAMA_CPP_SERVER_URL is not set; remote OpenAI-compatible LLM is disabled");
    } else {
        spdlog::info("Remote LLM provider: OpenAI-compatible ({})", Config::LlamaCpp::getBaseUrl());
    }
    if (std::strlen(Config::LlamaCpp::getApiKey()) == 0) {
        spdlog::warn("LLAMA_CPP_SERVER_API_KEY is not set");
    } else {
        spdlog::info("LLAMA_CPP_SERVER_API_KEY loaded from environment (length: {})",
                     std::strlen(Config::LlamaCpp::getApiKey()));
    }

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
}

bool AiAnalysisService::isAnalyzing() const {
    return agent_running_.load();
}

void AiAnalysisService::cancelAnalysis() {
    if (agent_running_.load()) {
        spdlog::info("Cloud Agent analysis cancelled by user");
        cleanup();
        agent_running_ = false;
        emit analysisCancelled();
    }
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
    agent::AgentConfig config;
    config.max_iterations = Config::Agent::MAX_ITERATIONS;
    config.stream_output = Config::Agent::STREAM_OUTPUT;
    config.skip_system_prompt = false;
    config.use_llm_session_cache = false;
    config.include_tool_overview = true;
    config.include_structured_tool_output = false;

    agent_service_ = std::make_unique<agent::AgentService>(config, nullptr);
    service::AttendanceService* enabled_attendance =
        Config::Agent::Tools::ENABLE_ATTENDANCE ? attendance_svc : nullptr;
    service::UserService* enabled_user =
        Config::Agent::Tools::ENABLE_USER ? user_svc : nullptr;
    if (enabled_attendance || enabled_user) {
        agent_service_->registerBuiltinTools(enabled_attendance, enabled_user);
    }
    spdlog::info("Cloud Agent initialized with {} tools (skip_system_prompt={})",
        agent_service_->getToolCount(), config.skip_system_prompt);
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

    spdlog::debug("Cloud Agent sync request, prompt length: {}", prompt.length());

    // 由于是在 AgentWorker 线程中，我们可以使用本地 QEventLoop 进行同步等待
    QEventLoop loop;

    // 创建线程局部的 QNetworkAccessManager，确保信号槽在当前线程正确执行
    QNetworkAccessManager local_manager;

    // 同步请求实现，使用 SSE 获取流式输出
    QUrl url(build_remote_url());
    if (url.isEmpty() || !url.isValid()) {
        error_msg = QStringLiteral("未配置 OpenAI 兼容服务地址，请设置 LLAMA_CPP_SERVER_URL");
        agent_failed_ = true;
        emitStreamEvent("agent", "error", "status", error_msg);
        emit errorOccurred(error_msg);
        return QString();
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "text/event-stream");
    apply_remote_auth(request);
    QJsonObject jsonBody = build_remote_request(prompt, true);
    QNetworkReply* reply = local_manager.post(request, QJsonDocument(jsonBody).toJson());

    QByteArray sse_buffer;
    QTimer timer;
    timer.setSingleShot(true);

    connect(reply, &QNetworkReply::readyRead, &loop, [this, reply, &loop, &sse_buffer, &result, &timer, &cancelled, &error, &error_reported, &error_msg, &parser]() {
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

        const auto events = consume_remote_events(sse_buffer);
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

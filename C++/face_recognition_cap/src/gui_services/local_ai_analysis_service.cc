/**
 * @file local_ai_analysis_service.cc
 * @brief 本地 RKLLM 分析服务，支持 Chat 和 Agent 双模式。
 *
 * 架构说明：
 * - Chat 模式：传统提示词模式，将考勤数据附带到 prompt 中发送给 LLM
 * - Agent 模式：ReAct 智能体模式，LLM 通过工具调用自主获取所需数据
 *
 * 流式输出机制：
 * - Chat 模式：直接转发 LLM 输出的每个 chunk
 * - Agent 模式：检测 <answer> 标签后开始流式输出最终答案
 */
#include "gui_services/local_ai_analysis_service.h"

#include "app/local_llm_thread.h"
#include "gui_services/ai_prompt_builder.h"
#include "config/config.h"
#include "agent/agent_service.h"
#include "agent/agent_worker.h"
#include "agent/incremental_response_parser.h"
#include <spdlog/spdlog.h>
#include <QEventLoop>
#include <QTimer>
#include <QCoreApplication>
#include <QThread>

// ==================== 单例与生命周期 ====================

LocalAiAnalysisService* LocalAiAnalysisService::instance() {
    static LocalAiAnalysisService s_instance;
    return &s_instance;
}

LocalAiAnalysisService::LocalAiAnalysisService(QObject* parent)
    : QObject(parent)
    , local_analyzing_(false) {
    qRegisterMetaType<agent::AgentStreamEvent>("agent::AgentStreamEvent");
    qRegisterMetaType<agent::ToolInvocation>("agent::ToolInvocation");
    qRegisterMetaType<agent::ToolExecutionResult>("agent::ToolExecutionResult");
    // MOC 在 namespace 内生成的信号签名使用不带命名空间的类型名，需同时注册短名
    qRegisterMetaType<agent::ToolInvocation>("ToolInvocation");
    qRegisterMetaType<agent::ToolExecutionResult>("ToolExecutionResult");
    auto local_llm = LocalLLMThread::instance();
    connect(local_llm, &LocalLLMThread::modelReady, this, &LocalAiAnalysisService::onLocalLLMReady);
    connect(local_llm, &LocalLLMThread::modelFailed, this, &LocalAiAnalysisService::onLocalLLMFailed);
    connect(local_llm, &LocalLLMThread::chunkReady, this, &LocalAiAnalysisService::onLocalLLMChunk);
    connect(local_llm, &LocalLLMThread::inferenceFinished, this, &LocalAiAnalysisService::onLocalLLMFinished);
    connect(local_llm, &LocalLLMThread::errorOccurred, this, &LocalAiAnalysisService::onLocalLLMError);
    connect(local_llm, &LocalLLMThread::modelReleased, this, &LocalAiAnalysisService::onLocalLLMReleased);
}

LocalAiAnalysisService::~LocalAiAnalysisService() = default;

void LocalAiAnalysisService::beginStreamRequest() {
    active_stream_request_id_ = ++stream_request_seq_;
    stream_event_seq_ = 0;
    stream_has_visible_output_ = false;
}

void LocalAiAnalysisService::emitStreamEvent(const QString& phase,
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

void LocalAiAnalysisService::emitAssistantDelta(const QString& text,
                                                const QString& phase,
                                                bool final) {
    if (text.isEmpty()) {
        return;
    }
    emit analysisResultReady(text);
    emitStreamEvent(phase, "delta", "assistant", text, QJsonObject(), final);
}

bool LocalAiAnalysisService::initializeLocalLLM(const QString& model_path) {
    return LocalLLMThread::instance()->initModel(
        model_path,
        Config::LocalLLM::MAX_NEW_TOKENS,
        Config::LocalLLM::MAX_CONTEXT_LEN);
}

bool LocalAiAnalysisService::isLocalLLMReady() const {
    return LocalLLMThread::instance()->isModelReady();
}

bool LocalAiAnalysisService::isAnalyzing() const {
    return local_analyzing_;
}

void LocalAiAnalysisService::cancelAnalysis() {
    if (!local_analyzing_ && !agent_running_.load()) {
        return;
    }

    spdlog::info("Cancelling analysis (cooperative stop)...");

    // 设置取消标志
    agent_cancel_requested_ = true;

    // 协作式停止：发送停止信号，不阻塞等待
    // 1. 停止 Agent 服务
    if (agent_service_) {
        if (agent_service_->thread() != QThread::currentThread()) {
            QMetaObject::invokeMethod(agent_service_.get(), &agent::AgentService::stop, Qt::QueuedConnection);
        } else {
            agent_service_->stop();
        }
    }

    // 2. 停止 AgentWorker
    if (current_worker_) {
        current_worker_->requestStop();
    }

    // 3. 中断 LLM 推理
    LocalLLMThread::instance()->abortInference();

    // 4. 请求线程退出（协作式）
    if (current_thread_) {
        current_thread_->requestInterruption();
        current_thread_->quit();
    }

    // 立即更新 UI 状态，不等待线程结束
    // 线程的实际清理在 finished 信号回调中完成（见 requestAgentChat）
    local_analyzing_ = false;

    spdlog::info("Cancel request sent, cleanup will happen asynchronously");

    // 发送取消信号
    emit analysisCancelled();
}

void LocalAiAnalysisService::requestAnalysis(const service::AttendanceStatistics& stats,
                                             const QString& trend_summary,
                                             const QString& detail_records,
                                             const QString& user_prompt,
                                             int range_days) {
    if (local_analyzing_) {
        spdlog::warn("Previous local analysis is still running, cancelling it");
        cancelAnalysis();
    }

    if (!isLocalLLMReady()) {
        emit errorOccurred("本地模型未初始化，请先加载模型");
        return;
    }

    // 如果 Agent 模式开启且 Agent 已初始化，使用 Agent 模式进行智能工具调用
    if (agent_mode_ && agent_service_ && agent_service_->getToolCount() > 0) {
        spdlog::info("Using Agent mode with {} tools", agent_service_->getToolCount());

        // 构建用户问题（Agent 会自动调用工具获取数据）
        QString agent_input = user_prompt;
        if (agent_input.isEmpty()) {
            if (range_days == 0) {
                // 纯问答模式，需要用户输入
                emit errorOccurred("请输入您的问题");
                return;
            } else if (range_days == 1) {
                agent_input = "请分析今日考勤情况";
            } else {
                agent_input = QString("请分析近%1日的考勤情况").arg(range_days);
            }
        }

        // 调用 Agent 模式
        requestAgentChat(agent_input);
        return;
    }

    // Chat 模式：使用传统提示词模式（附带数据上下文）
    spdlog::info("Using Chat mode (agent_mode={}, agent_initialized={})",
        agent_mode_, agent_service_ != nullptr);
    if (agent_service_) {
        agent_service_->resetLlmSessionCache();
    }
    LocalLLMThread::instance()->resetContext();
    QString prompt = AiPromptBuilder::buildPrompt(
        stats, trend_summary, detail_records, user_prompt, range_days);

    local_analyzing_ = true;
    beginStreamRequest();
    emit analysisStarted();
    emitStreamEvent("model", "start", "status");
    spdlog::info("Sending prompt to local LLM ({} chars)", prompt.length());
    LocalLLMThread::instance()->requestInference(prompt, false);
}

// ==================== LLM 事件处理 ====================

void LocalAiAnalysisService::onLocalLLMReady() {
    spdlog::info("Local LLM model loaded");
    if (agent_service_) {
        agent_service_->resetLlmSessionCache();
    }
    emit localLLMReady();
}

void LocalAiAnalysisService::onLocalLLMFailed(const QString& error) {
    spdlog::error("Local LLM init failed: {}", error.toStdString());
    if (agent_service_) {
        agent_service_->resetLlmSessionCache();
    }
    emitStreamEvent("model", "error", "status", "本地模型加载失败: " + error);
    emit errorOccurred("本地模型加载失败: " + error);
}

void LocalAiAnalysisService::onLocalLLMChunk(const QString& chunk) {
    // Agent 模式：由 requestAgentChat 内部的 llm_callback 处理
    // Chat 模式：直接转发给 UI
    if (agent_running_ || !local_analyzing_) {
        return;
    }
    emitAssistantDelta(chunk, "model");
}

void LocalAiAnalysisService::onLocalLLMFinished() {
    // Agent 模式：由 requestAgentChat 统一触发 analysisFinished
    if (agent_running_ || !local_analyzing_) {
        return;
    }
    local_analyzing_ = false;
    emitStreamEvent("model", "done", "assistant", QString(), QJsonObject(), true);
    emit analysisFinished();
}

void LocalAiAnalysisService::onLocalLLMError(const QString& error) {
    // Agent 模式：由 llm_callback 内部处理
    if (agent_running_ || !local_analyzing_) {
        return;
    }
    local_analyzing_ = false;
    emitStreamEvent("model", "error", "status", error);
    emit errorOccurred(error);
}

void LocalAiAnalysisService::onLocalLLMReleased() {
    local_analyzing_ = false;
    if (agent_service_) {
        agent_service_->resetLlmSessionCache();
    }
    spdlog::info("Local LLM model released");
    emit localLLMReleased();
}

// ==================== Agent 功能实现 ====================

void LocalAiAnalysisService::initializeAgent(service::AttendanceService* attendance_svc,
                                              service::UserService* user_svc) {
    if (agent_service_) {
        spdlog::warn("Agent already initialized, reinitializing...");
    }

    agent::AgentConfig config;
    config.max_iterations = Config::Agent::MAX_ITERATIONS;
    config.stream_output = Config::Agent::STREAM_OUTPUT;
    config.use_llm_session_cache = true;
    config.include_tool_overview = true;
    config.include_structured_tool_output = false;

    agent_service_ = std::make_unique<agent::AgentService>(config, nullptr);

    // 根据配置注册工具
    if (Config::Agent::Tools::ENABLE_ATTENDANCE || Config::Agent::Tools::ENABLE_USER) {
        agent_service_->registerBuiltinTools(
            Config::Agent::Tools::ENABLE_ATTENDANCE ? attendance_svc : nullptr,
            Config::Agent::Tools::ENABLE_USER ? user_svc : nullptr
        );
    }

    // 连接 Agent 信号
    // 注意：不在这里连接 thinkingStarted/toolCalling/toolCompleted，
    // 因为 requestAgentChat 中已通过 AgentWorker 转发这些信号，
    // AgentWorker 内部会把 AgentService 的信号再转发一次，
    // 如果这里也连接就会导致信号重复触发（UI 显示多条状态）。
    // 连接统一在 requestAgentChat() 中通过 worker 完成。

    // 注意：answerReady 信号不再转发，因为流式输出已在 requestAgentChat 中通过
    // analysisResultReady 逐 chunk 发送。如果再发完整答案会导致 UI 重复显示。

    spdlog::info("Agent initialized with {} tools", agent_service_->getToolCount());
}

void LocalAiAnalysisService::requestAgentChat(const QString& user_input) {
    if (!agent_service_) {
        emit errorOccurred("Agent 未初始化");
        return;
    }

    if (!isLocalLLMReady()) {
        emit errorOccurred("本地模型未加载，请先启动 LLM");
        return;
    }

    // 防止重复启动（不再取消前一个，而是拒绝新请求）
    if (agent_running_.load()) {
        spdlog::warn("Agent already running, ignoring new request");
        return;
    }

    spdlog::info("Agent chat request: {}", user_input.left(50).toStdString());

    // 重置状态标志
    local_analyzing_ = true;
    agent_running_ = true;
    agent_cancel_requested_ = false;
    agent_failed_ = false;
    beginStreamRequest();
    emit analysisStarted();
    emitStreamEvent("agent", "start", "status");

    // 创建工作线程（按需启动，完成后自动销毁）
    QThread* thread = new QThread;
    if (agent_service_ && agent_service_->thread() != thread) {
        agent_service_->moveToThread(thread);
    }
    agent::AgentWorker* worker = new agent::AgentWorker(agent_service_.get());
    worker->moveToThread(thread);

    // 设置 LLM 回调
    worker->setLlmCallback(createThreadSafeLlmCallback());

    // 启动时执行
    connect(thread, &QThread::started, worker, [worker, user_input]() {
        worker->process(user_input);
    });

    // 转发状态信号
    connect(worker, &agent::AgentWorker::thinkingStarted, this, [this]() {
        emit agentThinking();
        emitStreamEvent("agent", "thinking", "status", "思考中");
    });
    connect(worker, &agent::AgentWorker::toolCalling, this, &LocalAiAnalysisService::agentToolCalling);
    connect(worker, &agent::AgentWorker::toolCompleted, this, &LocalAiAnalysisService::agentToolCompleted);
    connect(worker, &agent::AgentWorker::toolInvocationReady, this, [this](const agent::ToolInvocation& invocation) {
        QJsonObject data;
        data["tool_name"] = invocation.name;
        data["call_id"] = invocation.call_id;
        data["arguments"] = invocation.arguments;
        emitStreamEvent("tool", "tool_call", "status", invocation.name, data);
    });
    connect(worker, &agent::AgentWorker::toolResultReady, this, [this](const agent::ToolExecutionResult& result) {
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
    connect(worker, &agent::AgentWorker::errorOccurred, this, [this](const QString& error) {
        agent_failed_ = true;
        emitStreamEvent("agent", "error", "status", error);
        emit errorOccurred(error);
    });

    // 完成后处理
    connect(worker, &agent::AgentWorker::finished, this, [this](const QString& answer) {
        agent_running_ = false;
        local_analyzing_ = false;
        current_worker_ = nullptr;
        current_thread_ = nullptr;

        if (agent_cancel_requested_) {
            spdlog::info("Agent chat was cancelled");
        } else if (agent_failed_.load()) {
            spdlog::warn("Agent chat finished after failure, skipping success completion");
        } else {
            if (!stream_has_visible_output_.load() && !answer.isEmpty()) {
                emitAssistantDelta(answer, "agent", true);
                spdlog::info("Agent chat completed, answer length: {}", answer.length());
            }
            emitStreamEvent("agent", "done", "assistant", QString(), QJsonObject(), true);
            emit analysisFinished();
        }
    });

    // 自动清理与线程归位
    connect(worker, &agent::AgentWorker::finished, worker, [this, thread]() {
        if (agent_service_ && agent_service_->thread() != QCoreApplication::instance()->thread()) {
            agent_service_->moveToThread(QCoreApplication::instance()->thread());
        }
        if (thread) {
            thread->quit();
        }
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // 保存引用用于取消
    current_worker_ = worker;
    current_thread_ = thread;

    thread->start();
    spdlog::info("Agent worker thread started");
}

// ==================== Agent 模式控制 ====================

void LocalAiAnalysisService::setAgentMode(bool enabled) {
    agent_mode_ = enabled;
    spdlog::info("Agent mode {}", enabled ? "enabled" : "disabled");
}

void LocalAiAnalysisService::clearAgentHistory() {
    if (agent_service_) {
        agent_service_->clearHistory();
        spdlog::info("Agent history cleared");
    }
}

// ==================== 线程安全 LLM 回调 ====================

std::function<QString(const QString&)> LocalAiAnalysisService::createThreadSafeLlmCallback() {
    /**
     * 创建线程安全的 LLM 回调函数
     *
     * 工作原理：
     * 1. 在工作线程中调用此回调
     * 2. 通过 QMetaObject::invokeMethod 将请求转发到主线程
     * 3. 使用 QEventLoop 在工作线程中等待结果
     * 4. 通过信号收集 LLM 输出的 chunks 并拼接
     *
     * 注意：QEventLoop 在工作线程中使用是安全的，不会阻塞 UI
     */
    return [this](const QString& prompt) -> QString {
        // 检查取消标志
        if (agent_cancel_requested_.load()) {
            return QString();
        }

        QString result;
        bool finished = false;
        bool error_occurred = false;
        bool cancelled = false;
        QString error_msg;

        agent::IncrementalResponseParser parser;

        // 在工作线程中创建事件循环
        QEventLoop loop;

        auto* llm = LocalLLMThread::instance();

        // 临时连接：收集 chunks，转发到 UI 实现流式输出
        auto conn_chunk = connect(llm, &LocalLLMThread::chunkReady,
            &loop, [this, &result, &parser](const QString& chunk) {
                result += chunk;
                const auto parsed = parser.push(chunk);
                
                // 流式转发 answer 内容（如最终回答或工具调用前的引导语）
                if (!parsed.assistant_delta.isEmpty()) {
                    emitAssistantDelta(parsed.assistant_delta, "agent", parsed.answer_finished);
                }
                
                // 流式转发 reasoning 内容（思考过程）
                if (!parsed.reasoning_delta.isEmpty()) {
                    emitStreamEvent("agent", "reasoning", "reasoning", parsed.reasoning_delta);
                }
            }, Qt::QueuedConnection);

        // 临时连接：推理完成
        auto conn_finished = connect(llm, &LocalLLMThread::inferenceFinished,
            &loop, [&finished, &loop]() {
                finished = true;
                loop.quit();
            }, Qt::QueuedConnection);

        // 临时连接：错误处理
        auto conn_error = connect(llm, &LocalLLMThread::errorOccurred,
            &loop, [&error_occurred, &error_msg, &loop](const QString& error) {
                error_occurred = true;
                error_msg = error;
                loop.quit();
            }, Qt::QueuedConnection);

        // 在主线程中启动推理
        QMetaObject::invokeMethod(llm, [llm, prompt]() {
            llm->requestInference(prompt);
        }, Qt::QueuedConnection);

        // 设置超时（防止无限等待）
        QTimer timeout_timer;
        timeout_timer.setSingleShot(true);
        connect(&timeout_timer, &QTimer::timeout, &loop, [&loop, &error_occurred, &error_msg, llm]() {
            error_occurred = true;
            error_msg = "LLM 推理超时";
            // 超时后主动中止 LLM 推理，避免资源浪费
            llm->abortInference();
            loop.quit();
        });
        timeout_timer.start(Config::Agent::LLM_TIMEOUT_MS);  // 使用 Agent LLM 超时配置

        // 取消轮询：避免等待到超时才响应停止
        QTimer cancel_timer;
        cancel_timer.setInterval(50);
        connect(&cancel_timer, &QTimer::timeout, &loop,
                [this, &loop, &cancelled, llm]() {
            if (!agent_cancel_requested_.load()) {
                return;
            }
            cancelled = true;
            llm->abortInference();
            loop.quit();
        });
        cancel_timer.start();

        // 等待完成
        loop.exec();

        // 停止超时计时器（如果正常完成则取消超时）
        timeout_timer.stop();
        cancel_timer.stop();

        // 断开临时连接
        disconnect(conn_chunk);
        disconnect(conn_finished);
        disconnect(conn_error);

        if (cancelled) {
            spdlog::info("LLM callback cancelled");
            return QString();
        }

        if (error_occurred) {
            spdlog::error("LLM callback error: {}", error_msg.toStdString());
            agent_failed_ = true;
            emitStreamEvent("agent", "error", "status", error_msg);
            emit errorOccurred(error_msg);
            return QString();
        }

        spdlog::debug("LLM callback completed, result length: {}", result.length());
        return result;
    };
}

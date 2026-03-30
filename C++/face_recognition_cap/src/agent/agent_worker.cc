/**
 * @file agent_worker.cc
 * @brief Agent 工作线程实现
 *
 * 在独立线程中执行 ReAct 循环，避免阻塞 UI 主线程。
 */

#include "agent/agent_worker.h"
#include "agent/agent_service.h"
#include <spdlog/spdlog.h>

namespace agent {

AgentWorker::AgentWorker(AgentService* service, QObject* parent)
    : QObject(parent)
    , service_(service)
    , stop_requested_(false) {
    spdlog::debug("AgentWorker created");
}

AgentWorker::~AgentWorker() {
    spdlog::debug("AgentWorker destroyed");
}

void AgentWorker::setLlmCallback(std::function<QString(const QString&)> callback) {
    llm_callback_ = std::move(callback);
}

void AgentWorker::process(const QString& input) {
    spdlog::info("AgentWorker::process started: {}", input.left(50).toStdString());

    if (!service_) {
        emit errorOccurred("Agent 服务未初始化");
        emit finished(QString());
        return;
    }

    if (!llm_callback_) {
        emit errorOccurred("LLM 回调未设置");
        emit finished(QString());
        return;
    }

    stop_requested_ = false;

    // 连接 Agent 信号转发
    auto conn_thinking = connect(service_, &AgentService::thinkingStarted,
                                  this, &AgentWorker::thinkingStarted);
    auto conn_tool_calling = connect(service_, &AgentService::toolCalling,
                                      this, &AgentWorker::toolCalling);
    auto conn_tool_invocation = connect(service_, &AgentService::toolInvocationReady,
                                         this, &AgentWorker::toolInvocationReady);
    auto conn_tool_completed = connect(service_, &AgentService::toolCompleted,
                                        this, &AgentWorker::toolCompleted);
    auto conn_tool_result = connect(service_, &AgentService::toolResultReady,
                                     this, &AgentWorker::toolResultReady);

    // 包装 LLM 回调，添加停止检查
    auto wrapped_callback = [this](const QString& prompt) -> QString {
        if (stop_requested_.load()) {
            spdlog::info("AgentWorker: stop requested, returning empty");
            return QString();
        }
        return llm_callback_(prompt);
    };

    // 执行 Agent 对话（在工作线程中同步执行）
    QString answer;
    try {
        answer = service_->chat(input, wrapped_callback);
    } catch (const std::exception& e) {
        spdlog::error("AgentWorker::process exception: {}", e.what());
        emit errorOccurred(QString("处理异常: %1").arg(e.what()));
    }

    // 断开信号连接
    disconnect(conn_thinking);
    disconnect(conn_tool_calling);
    disconnect(conn_tool_invocation);
    disconnect(conn_tool_completed);
    disconnect(conn_tool_result);

    if (stop_requested_.load()) {
        spdlog::info("AgentWorker::process cancelled");
    } else {
        spdlog::info("AgentWorker::process completed, answer length: {}", answer.length());
    }

    emit finished(answer);
}

void AgentWorker::requestStop() {
    spdlog::info("AgentWorker::requestStop called");
    stop_requested_ = true;

    // 同时停止 Agent 服务
    if (service_) {
        service_->stop();
    }
}

} // namespace agent

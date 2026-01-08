/**
 * @file agent_service.cc
 * @brief Agent 服务实现
 */

#include "agent/agent_service.h"
#include "tools/attendance_tool.h"
#include "tools/user_tool.h"
#include "tools/system_tool.h"
#include "config/config.h"
#include <spdlog/spdlog.h>

namespace agent {

AgentService::AgentService(const AgentConfig& config, QObject* parent)
    : QObject(parent)
    , tools_(std::make_unique<ToolRegistry>())
    , memory_(std::make_unique<ConversationMemory>(Config::Agent::CONVERSATION_HISTORY))
    , config_(config) {

    // 创建 ReactAgent
    agent_ = std::make_unique<ReactAgent>(
        tools_.get(),
        memory_.get(),
        config_,
        this
    );

    // 注册系统工具（始终可用）
    if (Config::Agent::Tools::ENABLE_SYSTEM) {
        tools_->registerTool(std::make_unique<SystemTool>());
        tools_->registerTool(std::make_unique<HelpTool>());
        tools_->registerTool(std::make_unique<CalculatorTool>());
        spdlog::info("Registered system tools");
    }

    connectSignals();
    spdlog::info("AgentService initialized");
}

AgentService::~AgentService() {
    stop();
    spdlog::info("AgentService destroyed");
}

void AgentService::registerBuiltinTools(service::AttendanceService* attendance,
                                         service::UserService* user) {
    if (attendance) {
        tools_->registerTool(std::make_unique<AttendanceTool>(attendance));
        spdlog::info("Registered AttendanceTool");
    }

    if (user) {
        tools_->registerTool(std::make_unique<UserTool>(user));
        spdlog::info("Registered UserTool");
    }

    spdlog::info("Registered {} builtin tools", tools_->size());
}

void AgentService::registerTool(std::unique_ptr<BaseTool> tool) {
    if (tool) {
        QString name = tool->name();
        tools_->registerTool(std::move(tool));
        spdlog::info("Registered custom tool: {}", name.toStdString());
    }
}

QString AgentService::chat(const QString& user_input,
                           std::function<QString(const QString&)> llm_callback) {
    if (!agent_) {
        spdlog::error("Agent not initialized");
        return "错误: Agent 未初始化";
    }

    spdlog::info("AgentService.chat: {}", user_input.left(50).toStdString());

    return agent_->run(user_input, llm_callback);
}

void AgentService::clearHistory() {
    if (memory_) {
        memory_->clearSession();
        spdlog::info("Conversation history cleared");
    }
}

void AgentService::stop() {
    if (agent_) {
        agent_->stop();
    }
}

bool AgentService::isRunning() const {
    return agent_ && agent_->isRunning();
}

void AgentService::connectSignals() {
    if (!agent_) return;

    connect(agent_.get(), &ReactAgent::tokenGenerated,
            this, &AgentService::tokenGenerated);

    connect(agent_.get(), &ReactAgent::thinkingStarted,
            this, &AgentService::thinkingStarted);

    connect(agent_.get(), &ReactAgent::toolCalling,
            this, &AgentService::toolCalling);

    connect(agent_.get(), &ReactAgent::toolCompleted,
            this, &AgentService::toolCompleted);

    connect(agent_.get(), &ReactAgent::answerReady,
            this, &AgentService::answerReady);
}

} // namespace agent

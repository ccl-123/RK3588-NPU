/**
 * @file agent_service.h
 * @brief Agent 服务 - 对外统一接口
 */

#pragma once

#include <memory>
#include <QString>
#include <QObject>
#include "agent/react_agent.h"
#include "agent/tool_registry.h"
#include "agent/conversation_memory.h"
#include "agent/tool_types.h"
#include "service/attendance_service.h"
#include "service/user_service.h"

namespace agent {

/**
 * @brief Agent 服务 - 对外统一接口
 *
 * 使用方式:
 *   auto agent = std::make_unique<AgentService>();
 *   agent->registerBuiltinTools(attendance_svc, user_svc);
 *   QString answer = agent->chat("今天有多少人打卡了？", llm_callback);
 */
class AgentService : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param config Agent 配置
     * @param parent 父对象
     */
    explicit AgentService(const AgentConfig& config = AgentConfig{},
                          QObject* parent = nullptr);

    ~AgentService() override;

    /**
     * @brief 注册内置工具
     * @param attendance 考勤服务
     * @param user 用户服务
     */
    void registerBuiltinTools(service::AttendanceService* attendance,
                              service::UserService* user);

    /**
     * @brief 注册自定义工具
     * @param tool 工具实例（所有权转移）
     */
    void registerTool(std::unique_ptr<BaseTool> tool);

    /**
     * @brief 对话接口
     * @param user_input 用户输入
     * @param llm_callback LLM 调用回调，传入 prompt 返回 LLM 输出
     * @return 最终答案
     */
    QString chat(const QString& user_input,
                 std::function<QString(const QString&)> llm_callback);

    /**
     * @brief 清空对话历史
     */
    void clearHistory();

    /**
     * @brief 仅标记 LLM 端会话缓存失效，不清理 ConversationMemory
     */
    void resetLlmSessionCache();

    /**
     * @brief 停止当前执行
     */
    void stop();

    /**
     * @brief 是否正在运行
     * @return 运行状态
     */
    bool isRunning() const;

    /**
     * @brief 获取工具注册表
     * @return 工具注册表指针
     */
    ToolRegistry* getToolRegistry() { return tools_.get(); }

    /**
     * @brief 获取对话记忆
     * @return 对话记忆指针
     */
    ConversationMemory* getMemory() { return memory_.get(); }

    /**
     * @brief 获取已注册的工具数量
     * @return 工具数量
     */
    size_t getToolCount() const { return tools_->size(); }

    /**
     * @brief 获取工具定义 JSON（用于 rkllm_set_function_tools）
     * @return JSON 字符串
     */
    QString getToolsJson() const { return tools_->getToolsJson(); }

signals:
    /**
     * @brief 生成 token（流式输出）
     * @param token 生成的 token
     */
    void tokenGenerated(const QString& token);

    /**
     * @brief 开始思考
     */
    void thinkingStarted();

    /**
     * @brief 正在调用工具
     * @param tool_name 工具名称
     */
    void toolCalling(const QString& tool_name);
    void toolInvocationReady(const ToolInvocation& invocation);

    /**
     * @brief 工具调用完成
     * @param tool_name 工具名称
     * @param result 工具结果
     */
    void toolCompleted(const QString& tool_name, const QString& result);
    void toolResultReady(const ToolExecutionResult& result);

    /**
     * @brief 最终答案就绪
     * @param answer 答案
     */
    void answerReady(const QString& answer);

private:
    std::unique_ptr<ToolRegistry> tools_;
    std::unique_ptr<ConversationMemory> memory_;
    std::unique_ptr<ReactAgent> agent_;
    AgentConfig config_;

    /**
     * @brief 连接信号
     */
    void connectSignals();
};

} // namespace agent

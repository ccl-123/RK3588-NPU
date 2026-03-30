/**
 * @file agent_worker.h
 * @brief Agent 工作线程类
 *
 * 将 ReAct 循环移至独立线程执行，避免阻塞 UI 主线程。
 * 按需创建，完成后自动销毁。
 */

#ifndef AGENT_WORKER_H
#define AGENT_WORKER_H

#include <QObject>
#include <atomic>
#include <functional>
#include "agent/tool_types.h"

namespace agent {

class AgentService;

/**
 * @brief Agent 工作线程类
 *
 * 在独立线程中执行 Agent 对话，通过信号与主线程通信。
 */
class AgentWorker : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 构造函数
     * @param service Agent 服务指针（不转移所有权）
     * @param parent 父对象
     */
    explicit AgentWorker(AgentService* service, QObject* parent = nullptr);

    ~AgentWorker() override;

    /**
     * @brief 设置 LLM 回调函数
     * @param callback LLM 调用回调，传入 prompt 返回 LLM 输出
     */
    void setLlmCallback(std::function<QString(const QString&)> callback);

public slots:
    /**
     * @brief 执行 Agent 对话（在工作线程中调用）
     * @param input 用户输入
     */
    void process(const QString& input);

    /**
     * @brief 请求停止当前处理
     */
    void requestStop();

signals:
    /**
     * @brief 流式输出 chunk
     * @param chunk 输出片段
     */
    void chunkReady(const QString& chunk);

    /**
     * @brief 思考开始
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
     * @param result 工具返回结果
     */
    void toolCompleted(const QString& tool_name, const QString& result);
    void toolResultReady(const ToolExecutionResult& result);

    /**
     * @brief 处理完成
     * @param answer 最终答案
     */
    void finished(const QString& answer);

    /**
     * @brief 发生错误
     * @param error 错误信息
     */
    void errorOccurred(const QString& error);

private:
    AgentService* service_;
    std::function<QString(const QString&)> llm_callback_;
    std::atomic<bool> stop_requested_{false};
};

} // namespace agent

#endif // AGENT_WORKER_H

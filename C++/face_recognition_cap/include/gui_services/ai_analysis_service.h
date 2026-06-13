#ifndef AI_ANALYSIS_SERVICE_H
#define AI_ANALYSIS_SERVICE_H

#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>
#include <functional>
#include "service/attendance_service.h"
#include "service/user_service.h"
#include "agent/agent_service.h"
#include "agent/stream_event.h"

// 前向声明
namespace agent {
class AgentWorker;
}

class AiAnalysisService : public QObject {
    Q_OBJECT

public:
    static AiAnalysisService* instance();

    // Agent 对话接口
    void requestAgentChat(const QString& user_input);

    // 新增：初始化 Agent
    void initializeAgent(service::AttendanceService* attendance_svc,
                         service::UserService* user_svc);

    // 取消当前分析请求
    void cancelAnalysis();

    // 检查是否正在分析
    bool isAnalyzing() const;

    // 新增：清空 Agent 对话历史
    void clearAgentHistory();

signals:
    // 分析结果信号（增量内容）
    void analysisResultReady(const QString& result);
    // 统一流式事件
    void streamEventReady(const agent::AgentStreamEvent& event);
    // 分析完成信号
    void analysisFinished();
    // 错误信号
    void errorOccurred(const QString& errorMsg);
    // 分析开始信号
    void analysisStarted();
    // 分析取消信号
    void analysisCancelled();

    // 新增：Agent 状态信号
    void agentThinking();
    void agentToolCalling(const QString& tool_name);
    void agentToolCompleted(const QString& tool_name, const QString& result);

private:
    explicit AiAnalysisService(QObject* parent = nullptr);
    ~AiAnalysisService();

    // 清理当前请求
    void cleanup();

    /**
     * @brief 创建同步的云端 LLM 回调函数
     * @return LLM 回调函数
     */
    std::function<QString(const QString&)> createSyncCloudLlmCallback();

    /**
     * @brief 执行同步云端请求（阻塞，用于 Agent）
     */
    QString doSyncCloudRequest(const QString& prompt);

    // Agent 相关
    std::atomic<bool> agent_running_{false};
    std::atomic<bool> agent_cancel_requested_{false};
    std::atomic<bool> agent_failed_{false};
    std::atomic<uint64_t> agent_request_seq_{0};
    std::atomic<uint64_t> agent_active_request_id_{0};
    std::atomic<uint64_t> stream_request_seq_{0};
    std::atomic<uint64_t> stream_event_seq_{0};
    std::atomic<bool> stream_has_visible_output_{false};
    uint64_t active_stream_request_id_ = 0;
    std::unique_ptr<agent::AgentService> agent_service_;

    // Agent 工作线程
    QThread* current_thread_ = nullptr;
    agent::AgentWorker* current_worker_ = nullptr;

    void beginStreamRequest();
    void emitStreamEvent(const QString& phase,
                         const QString& type,
                         const QString& channel,
                         const QString& text = QString(),
                         const QJsonObject& data = QJsonObject(),
                         bool final = false);
    void emitAssistantDelta(const QString& text,
                            const QString& phase = QStringLiteral("model"),
                            bool final = false);
};

#endif // AI_ANALYSIS_SERVICE_H

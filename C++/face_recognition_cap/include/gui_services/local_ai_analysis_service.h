#ifndef LOCAL_AI_ANALYSIS_SERVICE_H
#define LOCAL_AI_ANALYSIS_SERVICE_H

#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>
#include <functional>
#include "service/attendance_service.h"
#include "service/user_service.h"
#include "agent/agent_service.h"

// 前向声明
namespace agent {
class AgentWorker;
}

class LocalAiAnalysisService : public QObject {
    Q_OBJECT

public:
    static LocalAiAnalysisService* instance();

    bool initializeLocalLLM(const QString& model_path);
    bool isLocalLLMReady() const;

    // 原有的考勤分析接口
    void requestAnalysis(const service::AttendanceStatistics& stats,
                         const QString& trend_summary,
                         const QString& detail_records = "",
                         const QString& user_prompt = "",
                         int range_days = 1);

    // 新增：Agent 对话接口
    void requestAgentChat(const QString& user_input);

    // 新增：初始化 Agent（需要在服务初始化后调用）
    void initializeAgent(service::AttendanceService* attendance_svc,
                         service::UserService* user_svc);

    // 新增：Agent 模式开关
    void setAgentMode(bool enabled);
    bool isAgentMode() const { return agent_mode_; }

    void cancelAnalysis();
    bool isAnalyzing() const;

    // 新增：清空 Agent 对话历史
    void clearAgentHistory();

signals:
    void analysisResultReady(const QString& result);
    void analysisFinished();
    void errorOccurred(const QString& errorMsg);
    void analysisStarted();
    void analysisCancelled();
    void localLLMReady();
    void localLLMReleased();

    // 新增：Agent 状态信号
    void agentThinking();
    void agentToolCalling(const QString& tool_name);
    void agentToolCompleted(const QString& tool_name, const QString& result);

private slots:
    void onLocalLLMReady();
    void onLocalLLMFailed(const QString& error);
    void onLocalLLMChunk(const QString& chunk);
    void onLocalLLMFinished();
    void onLocalLLMError(const QString& error);
    void onLocalLLMReleased();

private:
    explicit LocalAiAnalysisService(QObject* parent = nullptr);
    ~LocalAiAnalysisService();

    /**
     * @brief 创建线程安全的 LLM 回调函数
     * @return LLM 回调函数
     */
    std::function<QString(const QString&)> createThreadSafeLlmCallback();

    bool local_analyzing_;
    bool agent_mode_ = true;  // 默认启用 Agent 模式（与 DashboardPage 保持一致）
    std::atomic<bool> agent_running_{false};       // Agent 推理进行中
    std::atomic<bool> agent_cancel_requested_{false};  // 取消请求标志
    std::unique_ptr<agent::AgentService> agent_service_;

    // Agent 工作线程（按需创建，完成后自动销毁）
    QThread* current_thread_ = nullptr;
    agent::AgentWorker* current_worker_ = nullptr;
};

#endif  // LOCAL_AI_ANALYSIS_SERVICE_H

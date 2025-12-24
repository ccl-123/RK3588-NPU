#ifndef AI_ANALYSIS_SERVICE_H
#define AI_ANALYSIS_SERVICE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <functional>
#include "service/attendance_service.h"


// LLM 后端类型
enum class LLMBackendType {
    Cloud,    // 云端 API (腾讯云混元)
    Local     // 本地 RKLLM (librkllmrt.so)
};

class AiAnalysisService : public QObject {
    Q_OBJECT

public:
    static AiAnalysisService* instance();

    // === 后端切换 ===
    // 设置 LLM 后端类型
    void setBackend(LLMBackendType backend);
    LLMBackendType currentBackend() const { return current_backend_; }

    // 初始化本地 LLM (仅当使用 Local 后端时需要)
    // model_path: 模型文件路径
    // 返回: 是否开始初始化
    bool initializeLocalLLM(const QString& model_path);

    // 检查本地 LLM 是否已就绪
    bool isLocalLLMReady() const;

    // 发起 AI 分析请求
    // stats: 当日统计数据
    // trend_summary: 趋势简报
    // detail_records: 详细记录字符串
    void requestAnalysis(const service::AttendanceStatistics& stats,
                         const QString& trend_summary,
                         const QString& detail_records = "",
                         const QString& user_prompt = "",
                         int range_days = 1);

    // 取消当前分析请求
    void cancelAnalysis();

    // 检查是否正在分析
    bool isAnalyzing() const;

signals:
    // 分析结果信号（增量内容）
    void analysisResultReady(const QString& result);
    // 分析完成信号
    void analysisFinished();
    // 错误信号
    void errorOccurred(const QString& errorMsg);
    // 分析开始信号
    void analysisStarted();
    // 分析取消信号
    void analysisCancelled();
    // 本地 LLM 就绪信号
    void localLLMReady();
    // 后端切换信号
    void backendChanged(LLMBackendType backend);

private slots:
    // 本地 LLM 信号处理
    void onLocalLLMReady();
    void onLocalLLMFailed(const QString& error);
    void onLocalLLMChunk(const QString& chunk);
    void onLocalLLMFinished();
    void onLocalLLMError(const QString& error);

private:
    explicit AiAnalysisService(QObject* parent = nullptr);
    ~AiAnalysisService();

    // 清理当前请求
    void cleanup();

    // 执行云端请求（带重试）
    void doCloudRequest(const service::AttendanceStatistics& stats,
                        const QString& trend_summary,
                        const QString& detail_records,
                        const QString& user_prompt,
                        int range_days,
                        int retry_count = 0);

    // 执行本地 LLM 请求
    void doLocalRequest(const service::AttendanceStatistics& stats,
                        const QString& trend_summary,
                        const QString& detail_records,
                        const QString& user_prompt,
                        int range_days);

    // 构建发送给 LLM 的 prompt
    QString buildPrompt(const service::AttendanceStatistics& stats,
                        const QString& trend_summary,
                        const QString& detail_records,
                        const QString& user_prompt,
                        int range_days);

    QNetworkAccessManager* network_manager_;

    // SSE 缓冲区，用于处理粘包/分包
    QByteArray sse_buffer_;

    // 当前请求
    QPointer<QNetworkReply> current_reply_;

    // 超时定时器
    QTimer* timeout_timer_;

    // 重试相关
    static constexpr int MAX_RETRIES = 3;
    static constexpr int TIMEOUT_MS = 60000;  // 60秒超时
    static constexpr int RETRY_DELAY_MS = 2000;  // 重试延迟2秒

    // 当前请求参数（用于重试）
    service::AttendanceStatistics current_stats_;
    QString current_trend_summary_;
    QString current_detail_records_;
    QString current_user_prompt_;
    int current_range_days_;
    int current_retry_count_;

    // 防止重复 emit analysisFinished
    bool completed_;

    // 增量输出缓冲
    QString incremental_buffer_;
    bool is_incremental_;

    // 后端管理
    LLMBackendType current_backend_;
    bool local_analyzing_;
};

#endif // AI_ANALYSIS_SERVICE_H

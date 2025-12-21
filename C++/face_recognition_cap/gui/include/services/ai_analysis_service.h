#ifndef AI_ANALYSIS_SERVICE_H
#define AI_ANALYSIS_SERVICE_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <functional>
#include "service/attendance_service.h"

namespace db {
    class AttendanceRecordDAO; // Forward declaration for DailyStats
}

class AiAnalysisService : public QObject {
    Q_OBJECT

public:
    static AiAnalysisService* instance();

    // 发起 AI 分析请求
    // stats: 当日统计数据
    // trend_summary: 趋势简报
    // detail_records: 详细记录字符串
    void requestAnalysis(const service::AttendanceStatistics& stats,
                         const QString& trend_summary,
                         const QString& detail_records = "",
                         const QString& user_prompt = "");

    // 取消当前分析请求
    void cancelAnalysis();

    // 检查是否正在分析
    bool isAnalyzing() const { return current_reply_ != nullptr; }

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

private:
    explicit AiAnalysisService(QObject* parent = nullptr);
    ~AiAnalysisService();

    // 清理当前请求
    void cleanup();

    // 执行实际请求（带重试）
    void doRequest(const service::AttendanceStatistics& stats,
                   const QString& trend_summary,
                   const QString& detail_records,
                   const QString& user_prompt,
                   int retry_count = 0);

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
    int current_retry_count_;

    // 防止重复 emit analysisFinished（关键：确保信号只发一次）
    bool completed_;

    // 增量输出缓冲（当 incremental=true 时，需要 append；false 时需要 replace）
    QString incremental_buffer_;
    bool is_incremental_;
};

#endif // AI_ANALYSIS_SERVICE_H

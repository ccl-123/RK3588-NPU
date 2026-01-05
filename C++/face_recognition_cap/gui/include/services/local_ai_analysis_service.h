#ifndef LOCAL_AI_ANALYSIS_SERVICE_H
#define LOCAL_AI_ANALYSIS_SERVICE_H

#include <QObject>
#include "service/attendance_service.h"

class LocalAiAnalysisService : public QObject {
    Q_OBJECT

public:
    static LocalAiAnalysisService* instance();

    bool initializeLocalLLM(const QString& model_path);
    bool isLocalLLMReady() const;

    void requestAnalysis(const service::AttendanceStatistics& stats,
                         const QString& trend_summary,
                         const QString& detail_records = "",
                         const QString& user_prompt = "",
                         int range_days = 1);

    void cancelAnalysis();
    bool isAnalyzing() const;

signals:
    void analysisResultReady(const QString& result);
    void analysisFinished();
    void errorOccurred(const QString& errorMsg);
    void analysisStarted();
    void analysisCancelled();
    void localLLMReady();
    void localLLMReleased();

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

    bool local_analyzing_;
};

#endif  // LOCAL_AI_ANALYSIS_SERVICE_H

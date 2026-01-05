/**
 * @file local_ai_analysis_service.cc
 * @brief Local RKLLM analysis service.
 */
#include "services/local_ai_analysis_service.h"

#include "app/local_llm_thread.h"
#include "services/ai_prompt_builder.h"
#include "config/config.h"
#include <spdlog/spdlog.h>

LocalAiAnalysisService* LocalAiAnalysisService::instance() {
    static LocalAiAnalysisService s_instance;
    return &s_instance;
}

LocalAiAnalysisService::LocalAiAnalysisService(QObject* parent)
    : QObject(parent)
    , local_analyzing_(false) {
    auto local_llm = LocalLLMThread::instance();
    connect(local_llm, &LocalLLMThread::modelReady, this, &LocalAiAnalysisService::onLocalLLMReady);
    connect(local_llm, &LocalLLMThread::modelFailed, this, &LocalAiAnalysisService::onLocalLLMFailed);
    connect(local_llm, &LocalLLMThread::chunkReady, this, &LocalAiAnalysisService::onLocalLLMChunk);
    connect(local_llm, &LocalLLMThread::inferenceFinished, this, &LocalAiAnalysisService::onLocalLLMFinished);
    connect(local_llm, &LocalLLMThread::errorOccurred, this, &LocalAiAnalysisService::onLocalLLMError);
    connect(local_llm, &LocalLLMThread::modelReleased, this, &LocalAiAnalysisService::onLocalLLMReleased);
}

LocalAiAnalysisService::~LocalAiAnalysisService() = default;

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
    if (local_analyzing_) {
        LocalLLMThread::instance()->abortInference();
        local_analyzing_ = false;
        spdlog::info("Local LLM analysis cancelled by user");
        emit analysisCancelled();
    }
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

    QString prompt = AiPromptBuilder::buildPrompt(
        stats, trend_summary, detail_records, user_prompt, range_days);

    local_analyzing_ = true;
    emit analysisStarted();
    spdlog::info("Sending prompt to local LLM ({} chars)", prompt.length());
    LocalLLMThread::instance()->requestInference(prompt);
}

void LocalAiAnalysisService::onLocalLLMReady() {
    spdlog::info("Local LLM model loaded");
    emit localLLMReady();
}

void LocalAiAnalysisService::onLocalLLMFailed(const QString& error) {
    spdlog::error("Local LLM init failed: {}", error.toStdString());
    emit errorOccurred("本地模型加载失败: " + error);
}

void LocalAiAnalysisService::onLocalLLMChunk(const QString& chunk) {
    if (!local_analyzing_) {
        return;
    }
    emit analysisResultReady(chunk);
}

void LocalAiAnalysisService::onLocalLLMFinished() {
    if (!local_analyzing_) {
        return;  // 忽略非本服务触发的信号
    }
    local_analyzing_ = false;
    emit analysisFinished();
}

void LocalAiAnalysisService::onLocalLLMError(const QString& error) {
    if (!local_analyzing_) {
        return;  // 忽略非本服务触发的信号
    }
    local_analyzing_ = false;
    emit errorOccurred(error);
}

void LocalAiAnalysisService::onLocalLLMReleased() {
    local_analyzing_ = false;
    spdlog::info("Local LLM model released");
    emit localLLMReleased();
}

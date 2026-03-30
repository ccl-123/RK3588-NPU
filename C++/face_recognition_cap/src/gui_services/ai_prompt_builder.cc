/**
 * @file ai_prompt_builder.cc
 * @brief Build prompts for AI analysis requests.
 */
#include "gui_services/ai_prompt_builder.h"

QString AiPromptBuilder::buildPrompt(const service::AttendanceStatistics& stats,
                                     const QString& trend_summary,
                                     const QString& detail_records,
                                     const QString& user_prompt,
                                     int range_days) {
    return agent::PromptTemplates::buildAnalysisPrompt(
        stats, trend_summary, detail_records, user_prompt, range_days);
}

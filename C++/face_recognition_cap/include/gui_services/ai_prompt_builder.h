#ifndef AI_PROMPT_BUILDER_H
#define AI_PROMPT_BUILDER_H

#include <QString>
#include "service/attendance_service.h"
#include "agent/prompt_templates.h"

class AiPromptBuilder {
public:
    static QString buildPrompt(const service::AttendanceStatistics& stats,
                               const QString& trend_summary,
                               const QString& detail_records,
                               const QString& user_prompt,
                               int range_days);
};

#endif  // AI_PROMPT_BUILDER_H

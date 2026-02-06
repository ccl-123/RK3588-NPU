/**
 * @file ai_prompt_builder.cc
 * @brief Build prompts for AI analysis requests.
 */
#include "gui_services/ai_prompt_builder.h"

#include <QDateTime>

QString AiPromptBuilder::buildPrompt(const service::AttendanceStatistics& stats,
                                     const QString& trend_summary,
                                     const QString& detail_records,
                                     const QString& user_prompt,
                                     int range_days) {
    (void)trend_summary;
    QString current_time_str = QDateTime::currentDateTime().toString("MM月dd日 HH:mm");
    QString content;

    if (range_days == 0) {
        content = QString(
            "时间: %1\n"
            "Q: %2"
        ).arg(current_time_str)
         .arg(user_prompt.isEmpty() ? QStringLiteral("你好，请问有什么可以帮助您的？") : user_prompt);
    } else {
        QString range_label = (range_days > 1) ? QString("近%1天").arg(range_days) : QString("今日");
        QString stats_date_label = (range_days > 1) ? QString("截止") : QString("日期");
        QString detail_title = (range_days > 1) ? QString("明细(近%1天)").arg(range_days) : QString("明细(今日)");

        content = QString(
            "当前时间: %1 | 范围: %2 | %3: %4\n"
            "%5(时间,姓名,部门,类型,状态):\n%6\n"
            "Q: %7"
        ).arg(current_time_str)
         .arg(range_label)
         .arg(stats_date_label)
         .arg(QString::fromStdString(stats.date))
         .arg(detail_title)
         .arg(detail_records)
         .arg(user_prompt.isEmpty() ? QStringLiteral("请生成考勤综合分析。") : user_prompt);
    }
    return content;
}

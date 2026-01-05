/**
 * @file ai_prompt_builder.cc
 * @brief Build prompts for AI analysis requests.
 */
#include "services/ai_prompt_builder.h"

#include <QDateTime>

QString AiPromptBuilder::buildPrompt(const service::AttendanceStatistics& stats,
                                     const QString& trend_summary,
                                     const QString& detail_records,
                                     const QString& user_prompt,
                                     int range_days) {
    QString current_time_str = QDateTime::currentDateTime().toString("MM月dd日 HH:mm");
    QString content;

    if (range_days == 0) {
        content = QString(
            "【当前时间】: %1\n\n"
            "【用户问题】\n%2"
        ).arg(current_time_str)
         .arg(user_prompt.isEmpty() ? QStringLiteral("你好，请问有什么可以帮助您的？") : user_prompt);
    } else {
        QString data_title = (range_days > 1) ? QString("【近 %1 日全量考勤数据】").arg(range_days) : QString("【今日考勤数据概览】");
        QString stats_date_label = (range_days > 1) ? QString("截止日期") : QString("统计日期");
        QString detail_title = (range_days > 1) ? QString("【考勤明细流水 (近 %1 日)】").arg(range_days) : QString("【今日打卡明细】");

        content = QString(
            "【当前时间】: %1\n\n"
            "%2\n"
            "%3: %4\n"
            "今日打卡总人数: %5\n"
            "今日签到人数: %6\n"
            "今日签退人数: %7\n"
            "今日迟到人数: %8\n"
            "今日早退人数: %9\n\n"
            "%10\n%11\n\n"
            "【趋势统计数据】\n%12\n\n"
            "【用户问题】\n%13"
        ).arg(current_time_str)
         .arg(data_title)
         .arg(stats_date_label)
         .arg(QString::fromStdString(stats.date))
         .arg(stats.total_count)
         .arg(stats.check_in_count)
         .arg(stats.check_out_count)
         .arg(stats.late_count)
         .arg(stats.early_leave_count)
         .arg(detail_title)
         .arg(detail_records)
         .arg(trend_summary)
         .arg(user_prompt.isEmpty() ? QStringLiteral("请生成今日考勤综合分析。") : user_prompt);
    }
    return content;
}

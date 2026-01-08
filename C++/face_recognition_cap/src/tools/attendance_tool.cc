/**
 * @file attendance_tool.cc
 * @brief 考勤查询工具实现
 */

#include "tools/attendance_tool.h"
#include <QDate>
#include <QJsonArray>
#include <spdlog/spdlog.h>

namespace agent {

AttendanceTool::AttendanceTool(service::AttendanceService* service)
    : attendance_service_(service) {}

QJsonObject AttendanceTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"date_range", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"today", "week", "month"}},
                {"description", "查询的时间范围：today(今日)、week(本周)、month(本月)"}
            }},
            {"date", QJsonObject{
                {"type", "string"},
                {"description", "指定日期，格式 YYYY-MM-DD，与 date_range 二选一"}
            }}
        }},
        {"required", QJsonArray{}}
    };
}

QString AttendanceTool::execute(const QJsonObject& args) {
    if (!attendance_service_) {
        return "错误: 考勤服务未初始化";
    }

    // 优先使用指定日期
    if (args.contains("date") && !args["date"].toString().isEmpty()) {
        return queryDate(args["date"].toString());
    }

    // 使用日期范围（兼容 date_range 和 period 参数名）
    QString range = args["date_range"].toString();
    if (range.isEmpty()) {
        range = args["period"].toString();  // 兼容 LLM 可能使用的 period 参数
    }
    if (range.isEmpty()) {
        range = "today";  // 默认查询今日
    }

    // 标准化参数值
    range = range.toLower().trimmed();

    if (range == "today" || range == "日" || range == "今天" || range == "今日") {
        return queryToday();
    } else if (range == "week" || range == "周" || range == "本周" || range == "这周") {
        return queryWeek();
    } else if (range == "month" || range == "月" || range == "本月" || range == "这月") {
        return queryMonth();
    }

    return QString("错误: 无效的日期范围 '%1'，支持 today/week/month").arg(range);
}

QString AttendanceTool::queryToday() {
    QString date = getTodayDate();
    spdlog::debug("Querying today's attendance: {}", date.toStdString());

    auto stats = attendance_service_->get_statistics(date.toStdString());
    return formatStatistics(stats);
}

QString AttendanceTool::queryWeek() {
    QString start_date = getWeekStartDate();
    QString end_date = getTodayDate();
    spdlog::debug("Querying week attendance: {} to {}",
        start_date.toStdString(), end_date.toStdString());

    auto stats_list = attendance_service_->get_statistics_range(
        start_date.toStdString(), end_date.toStdString());

    if (stats_list.empty()) {
        return QString("本周 (%1 至 %2) 暂无考勤记录").arg(start_date, end_date);
    }

    // 汇总统计
    int total_count = 0;
    int check_in_count = 0;
    int late_count = 0;
    int early_leave_count = 0;
    int normal_count = 0;

    for (const auto& stats : stats_list) {
        total_count += stats.total_count;
        check_in_count += stats.check_in_count;
        late_count += stats.late_count;
        early_leave_count += stats.early_leave_count;
        normal_count += stats.normal_count;
    }

    return QString(
        "本周考勤统计 (%1 至 %2):\n"
        "- 统计天数: %3 天\n"
        "- 总打卡次数: %4\n"
        "- 签到次数: %5\n"
        "- 正常打卡: %6 次\n"
        "- 迟到: %7 次\n"
        "- 早退: %8 次"
    ).arg(start_date, end_date)
     .arg(stats_list.size())
     .arg(total_count)
     .arg(check_in_count)
     .arg(normal_count)
     .arg(late_count)
     .arg(early_leave_count);
}

QString AttendanceTool::queryMonth() {
    QString start_date = getMonthStartDate();
    QString end_date = getTodayDate();
    spdlog::debug("Querying month attendance: {} to {}",
        start_date.toStdString(), end_date.toStdString());

    auto stats_list = attendance_service_->get_statistics_range(
        start_date.toStdString(), end_date.toStdString());

    if (stats_list.empty()) {
        return QString("本月 (%1 至 %2) 暂无考勤记录").arg(start_date, end_date);
    }

    // 汇总统计
    int total_count = 0;
    int check_in_count = 0;
    int late_count = 0;
    int early_leave_count = 0;
    int normal_count = 0;

    for (const auto& stats : stats_list) {
        total_count += stats.total_count;
        check_in_count += stats.check_in_count;
        late_count += stats.late_count;
        early_leave_count += stats.early_leave_count;
        normal_count += stats.normal_count;
    }

    return QString(
        "本月考勤统计 (%1 至 %2):\n"
        "- 统计天数: %3 天\n"
        "- 总打卡次数: %4\n"
        "- 签到次数: %5\n"
        "- 正常打卡: %6 次\n"
        "- 迟到: %7 次\n"
        "- 早退: %8 次"
    ).arg(start_date, end_date)
     .arg(stats_list.size())
     .arg(total_count)
     .arg(check_in_count)
     .arg(normal_count)
     .arg(late_count)
     .arg(early_leave_count);
}

QString AttendanceTool::queryDate(const QString& date) {
    spdlog::debug("Querying attendance for date: {}", date.toStdString());

    auto stats = attendance_service_->get_statistics(date.toStdString());
    return formatStatistics(stats);
}

QString AttendanceTool::formatStatistics(const service::AttendanceStatistics& stats) {
    if (stats.total_count == 0) {
        return QString("%1 暂无考勤记录").arg(QString::fromStdString(stats.date));
    }

    return QString(
        "考勤统计 (%1):\n"
        "- 总打卡人数: %2\n"
        "- 签到人数: %3\n"
        "- 签退人数: %4\n"
        "- 正常: %5 人\n"
        "- 迟到: %6 人\n"
        "- 早退: %7 人"
    ).arg(QString::fromStdString(stats.date))
     .arg(stats.total_count)
     .arg(stats.check_in_count)
     .arg(stats.check_out_count)
     .arg(stats.normal_count)
     .arg(stats.late_count)
     .arg(stats.early_leave_count);
}

QString AttendanceTool::getTodayDate() {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString AttendanceTool::getWeekStartDate() {
    QDate today = QDate::currentDate();
    // dayOfWeek(): Monday = 1, Sunday = 7
    int daysToMonday = today.dayOfWeek() - 1;
    return today.addDays(-daysToMonday).toString("yyyy-MM-dd");
}

QString AttendanceTool::getMonthStartDate() {
    QDate today = QDate::currentDate();
    return QDate(today.year(), today.month(), 1).toString("yyyy-MM-dd");
}

} // namespace agent

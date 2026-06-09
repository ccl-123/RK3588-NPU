/**
 * @file attendance_tool.cc
 * @brief 考勤查询工具实现
 */

#include "tools/attendance_tool.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <spdlog/spdlog.h>

namespace agent {

namespace {

QString attendance_status_text(int status) {
    switch (status) {
        case db::STATUS_NORMAL: return "正常";
        case db::STATUS_LATE: return "迟到";
        case db::STATUS_EARLY_LEAVE: return "早退";
        default: return "未知";
    }
}

QString check_type_text(int type) {
    return type == db::CHECK_IN ? "签到" : "签退";
}

QJsonObject stats_to_json(const service::AttendanceStatistics& stats) {
    return QJsonObject{
        {"date", QString::fromStdString(stats.date)},
        {"total_count", stats.total_count},
        {"check_in_count", stats.check_in_count},
        {"check_out_count", stats.check_out_count},
        {"late_count", stats.late_count},
        {"early_leave_count", stats.early_leave_count},
        {"normal_count", stats.normal_count},
    };
}

QJsonObject record_to_json(const db::AttendanceRecord& record) {
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(record.check_time);
    return QJsonObject{
        {"record_id", record.record_id},
        {"user_id", record.user_id},
        {"user_name", QString::fromStdString(record.user_name)},
        {"check_time", dt.toString(Qt::ISODate)},
        {"check_type", check_type_text(record.check_type)},
        {"status", attendance_status_text(record.status)},
        {"similarity", record.similarity},
        {"device_id", QString::fromStdString(record.device_id)},
        {"remark", QString::fromStdString(record.remark)},
    };
}

QString format_stats_text(const QJsonObject& obj, const QString& title_prefix) {
    if (obj.value("total_count").toInt() == 0) {
        return QString("%1 暂无考勤记录").arg(obj.value("date").toString());
    }

    return QString(
        "%1 (%2):\n"
        "- 总打卡人数: %3\n"
        "- 签到人数: %4\n"
        "- 签退人数: %5\n"
        "- 正常: %6 人\n"
        "- 迟到: %7 人\n"
        "- 早退: %8 人"
    ).arg(title_prefix)
     .arg(obj.value("date").toString())
     .arg(obj.value("total_count").toInt())
     .arg(obj.value("check_in_count").toInt())
     .arg(obj.value("check_out_count").toInt())
     .arg(obj.value("normal_count").toInt())
     .arg(obj.value("late_count").toInt())
     .arg(obj.value("early_leave_count").toInt());
}

QString format_records_text(const QString& title,
                            const QJsonArray& records,
                            bool grouped_by_date = false) {
    if (records.isEmpty()) {
        return title + "\n暂无记录";
    }

    QString text = title + "\n";
    QString current_date;
    for (const auto& value : records) {
        const auto obj = value.toObject();
        const QString iso_time = obj.value("check_time").toString();
        const QDateTime dt = QDateTime::fromString(iso_time, Qt::ISODate);
        const QString date = dt.date().toString("yyyy-MM-dd");

        if (grouped_by_date && date != current_date) {
            current_date = date;
            text += QString("\n[%1]\n").arg(date);
        }

        text += QString("- %1: %2 %3 (%4)\n")
            .arg(obj.value("user_name").toString())
            .arg(dt.time().toString("HH:mm:ss"))
            .arg(obj.value("check_type").toString())
            .arg(obj.value("status").toString());
    }
    return text.trimmed();
}

QString format_names_text(const QString& date, const QString& label, const QJsonArray& names) {
    if (names.isEmpty()) {
        return QString("%1 没有人%2").arg(date, label);
    }

    QStringList list;
    for (const auto& value : names) {
        list.append(value.toString());
    }

    return QString("%1 %2人员 (%3人):\n- %4")
        .arg(date)
        .arg(label)
        .arg(list.size())
        .arg(list.join("\n- "));
}

}  // namespace

AttendanceTool::AttendanceTool(service::AttendanceService* service)
    : attendance_service_(service) {}

QJsonObject AttendanceTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"query_type", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"stats", "records", "late", "early_leave"}},
                {"description", "查询类型：stats(统计)、records(详细记录)、late(迟到名单)、early_leave(早退名单)"}
            }},
            {"date_range", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"today", "week", "month"}},
                {"description", "查询的时间范围：today(今日)、week(本周)、month(本月)"}
            }},
            {"date", QJsonObject{
                {"type", "string"},
                {"description", "指定日期，格式 YYYY-MM-DD"}
            }},
            {"start_date", QJsonObject{
                {"type", "string"},
                {"description", "开始日期，格式 YYYY-MM-DD，与 end_date 配合使用查询日期范围"}
            }},
            {"end_date", QJsonObject{
                {"type", "string"},
                {"description", "结束日期，格式 YYYY-MM-DD，与 start_date 配合使用查询日期范围"}
            }},
            {"filter", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"all", "anomaly"}},
                {"description", "过滤条件：all(全部记录)、anomaly(仅异常:迟到+早退)，默认 all"}
            }}
        }},
        {"required", QJsonArray{}}
    };
}

ToolExecutionResult AttendanceTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!attendance_service_) {
        result.ok = false;
        result.error = "错误: 考勤服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    QString query_type = args.value("query_type").toString().toLower().trimmed();
    if (query_type.isEmpty()) {
        query_type = "stats";
    }

    QString filter = args.value("filter").toString().toLower().trimmed();
    const bool anomaly_only = (filter == "anomaly");
    QString start_date = args.value("start_date").toString().trimmed();
    QString end_date = args.value("end_date").toString().trimmed();
    QString date = args.value("date").toString().trimmed();
    QString date_range = args.value("date_range").toString().toLower().trimmed();

    result.output["query_type"] = query_type;
    result.output["filter"] = filter;
    result.output["date"] = date;
    result.output["date_range"] = date_range;
    result.output["start_date"] = start_date;
    result.output["end_date"] = end_date;

    if (!start_date.isEmpty() && !end_date.isEmpty()) {
        if (query_type == "stats") {
            const auto stats_list = attendance_service_->get_statistics_range(
                start_date.toStdString(), end_date.toStdString());

            QJsonArray days;
            int total_count = 0, check_in_count = 0, check_out_count = 0;
            int late_count = 0, early_leave_count = 0, normal_count = 0;
            for (const auto& stats : stats_list) {
                days.append(stats_to_json(stats));
                total_count += stats.total_count;
                check_in_count += stats.check_in_count;
                check_out_count += stats.check_out_count;
                late_count += stats.late_count;
                early_leave_count += stats.early_leave_count;
                normal_count += stats.normal_count;
            }

            result.ok = true;
            result.output["mode"] = "range_stats";
            result.output["days"] = days;
            result.output["summary"] = QJsonObject{
                {"start_date", start_date},
                {"end_date", end_date},
                {"days_count", static_cast<int>(stats_list.size())},
                {"total_count", total_count},
                {"check_in_count", check_in_count},
                {"check_out_count", check_out_count},
                {"late_count", late_count},
                {"early_leave_count", early_leave_count},
                {"normal_count", normal_count},
            };
            result.display_text = QString(
                "考勤统计 (%1 至 %2):\n"
                "- 统计天数: %3 天\n"
                "- 总打卡次数: %4\n"
                "- 签到次数: %5\n"
                "- 签退次数: %6\n"
                "- 正常打卡: %7 次\n"
                "- 迟到: %8 次\n"
                "- 早退: %9 次"
            ).arg(start_date, end_date)
             .arg(stats_list.size())
             .arg(total_count)
             .arg(check_in_count)
             .arg(check_out_count)
             .arg(normal_count)
             .arg(late_count)
             .arg(early_leave_count);
            return result;
        }

        auto records = attendance_service_->query_records_range(start_date.toStdString(), end_date.toStdString());
        std::vector<db::AttendanceRecord> filtered;
        if (anomaly_only) {
            for (const auto& record : records) {
                if (record.status == db::STATUS_LATE || record.status == db::STATUS_EARLY_LEAVE) {
                    filtered.push_back(record);
                }
            }
        } else {
            filtered = std::move(records);
        }

        QJsonArray items;
        for (const auto& record : filtered) {
            items.append(record_to_json(record));
        }

        result.ok = true;
        result.output["mode"] = "range_records";
        result.output["records"] = items;
        result.output["anomaly_only"] = anomaly_only;
        result.display_text = format_records_text(
            anomaly_only
                ? QString("异常考勤记录 (%1 至 %2, 共 %3 条):").arg(start_date, end_date).arg(items.size())
                : QString("考勤详细记录 (%1 至 %2, 共 %3 条):").arg(start_date, end_date).arg(items.size()),
            items,
            true);
        return result;
    }

    if (date.isEmpty()) {
        if (date_range.isEmpty() || date_range == "today" || date_range == "今日" || date_range == "今天") {
            date = getTodayDate();
        } else if (date_range == "week" || date_range == "本周") {
            date = getTodayDate();
        } else if (date_range == "month" || date_range == "本月") {
            date = getTodayDate();
        } else {
            date = getTodayDate();
        }
        result.output["date"] = date;
    }

    if (query_type == "stats") {
        if (date_range == "week" || date_range == "本周") {
            const QString week_start = getWeekStartDate();
            const auto stats_list = attendance_service_->get_statistics_range(
                week_start.toStdString(), getTodayDate().toStdString());
            QJsonArray days;
            int total_count = 0, check_in_count = 0, check_out_count = 0;
            int late_count = 0, early_leave_count = 0, normal_count = 0;
            for (const auto& stats : stats_list) {
                days.append(stats_to_json(stats));
                total_count += stats.total_count;
                check_in_count += stats.check_in_count;
                check_out_count += stats.check_out_count;
                late_count += stats.late_count;
                early_leave_count += stats.early_leave_count;
                normal_count += stats.normal_count;
            }
            result.ok = true;
            result.output["mode"] = "preset_week_stats";
            result.output["days"] = days;
            result.output["summary"] = QJsonObject{
                {"start_date", week_start},
                {"end_date", getTodayDate()},
                {"days_count", static_cast<int>(stats_list.size())},
                {"total_count", total_count},
                {"check_in_count", check_in_count},
                {"check_out_count", check_out_count},
                {"late_count", late_count},
                {"early_leave_count", early_leave_count},
                {"normal_count", normal_count},
            };
            result.display_text = QString(
                "本周考勤统计 (%1 至 %2):\n"
                "- 统计天数: %3 天\n"
                "- 总打卡次数: %4\n"
                "- 签到次数: %5\n"
                "- 签退次数: %6\n"
                "- 正常打卡: %7 次\n"
                "- 迟到: %8 次\n"
                "- 早退: %9 次"
            ).arg(week_start, getTodayDate())
             .arg(stats_list.size())
             .arg(total_count)
             .arg(check_in_count)
             .arg(check_out_count)
             .arg(normal_count)
             .arg(late_count)
             .arg(early_leave_count);
            return result;
        }

        if (date_range == "month" || date_range == "本月") {
            const QString month_start = getMonthStartDate();
            const auto stats_list = attendance_service_->get_statistics_range(
                month_start.toStdString(), getTodayDate().toStdString());
            QJsonArray days;
            int total_count = 0, check_in_count = 0, check_out_count = 0;
            int late_count = 0, early_leave_count = 0, normal_count = 0;
            for (const auto& stats : stats_list) {
                days.append(stats_to_json(stats));
                total_count += stats.total_count;
                check_in_count += stats.check_in_count;
                check_out_count += stats.check_out_count;
                late_count += stats.late_count;
                early_leave_count += stats.early_leave_count;
                normal_count += stats.normal_count;
            }
            result.ok = true;
            result.output["mode"] = "preset_month_stats";
            result.output["days"] = days;
            result.output["summary"] = QJsonObject{
                {"start_date", month_start},
                {"end_date", getTodayDate()},
                {"days_count", static_cast<int>(stats_list.size())},
                {"total_count", total_count},
                {"check_in_count", check_in_count},
                {"check_out_count", check_out_count},
                {"late_count", late_count},
                {"early_leave_count", early_leave_count},
                {"normal_count", normal_count},
            };
            result.display_text = QString(
                "本月考勤统计 (%1 至 %2):\n"
                "- 统计天数: %3 天\n"
                "- 总打卡次数: %4\n"
                "- 签到次数: %5\n"
                "- 签退次数: %6\n"
                "- 正常打卡: %7 次\n"
                "- 迟到: %8 次\n"
                "- 早退: %9 次"
            ).arg(month_start, getTodayDate())
             .arg(stats_list.size())
             .arg(total_count)
             .arg(check_in_count)
             .arg(check_out_count)
             .arg(normal_count)
             .arg(late_count)
             .arg(early_leave_count);
            return result;
        }

        const auto stats = attendance_service_->get_statistics(date.toStdString());
        result.ok = true;
        result.output["mode"] = "single_day_stats";
        result.output["statistics"] = stats_to_json(stats);
        result.display_text = format_stats_text(stats_to_json(stats), "考勤统计");
        return result;
    }

    if (query_type == "records") {
        auto records = attendance_service_->query_records_by_date(date.toStdString());
        QJsonArray items;
        for (const auto& record : records) {
            items.append(record_to_json(record));
        }
        result.ok = true;
        result.output["mode"] = "single_day_records";
        result.output["records"] = items;
        result.display_text = format_records_text(
            QString("考勤详细记录 (%1):").arg(date),
            items,
            false);
        return result;
    }

    if (query_type == "late" || query_type == "early_leave") {
        auto records = attendance_service_->query_records_by_date(date.toStdString());
        QJsonArray names;
        const int target_status = (query_type == "late") ? db::STATUS_LATE : db::STATUS_EARLY_LEAVE;
        for (const auto& record : records) {
            if (record.status == target_status) {
                const QString user_name = QString::fromStdString(record.user_name);
                bool exists = false;
                for (const auto& value : names) {
                    if (value.toString() == user_name) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    names.append(user_name);
                }
            }
        }

        result.ok = true;
        result.output["mode"] = query_type == "late" ? "late_list" : "early_leave_list";
        result.output["names"] = names;
        result.display_text = format_names_text(date, query_type == "late" ? "迟到" : "早退", names);
        return result;
    }

    result.ok = false;
    result.error = QString("错误: 无效的查询类型 '%1'，支持 stats/records/late/early_leave").arg(query_type);
    result.display_text = result.error;
    result.output["error"] = result.error;
    return result;
}

QString AttendanceTool::getTodayDate() {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString AttendanceTool::getWeekStartDate() {
    QDate today = QDate::currentDate();
    int daysToMonday = today.dayOfWeek() - 1;
    return today.addDays(-daysToMonday).toString("yyyy-MM-dd");
}

QString AttendanceTool::getMonthStartDate() {
    QDate today = QDate::currentDate();
    return QDate(today.year(), today.month(), 1).toString("yyyy-MM-dd");
}

}  // namespace agent

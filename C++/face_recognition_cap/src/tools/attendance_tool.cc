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

QString AttendanceTool::execute(const QJsonObject& args) {
    if (!attendance_service_) {
        return "错误: 考勤服务未初始化";
    }

    // 获取查询类型
    QString query_type = args["query_type"].toString().toLower().trimmed();
    if (query_type.isEmpty()) {
        query_type = "stats";  // 默认统计查询
    }

    // 获取过滤条件
    QString filter = args["filter"].toString().toLower().trimmed();
    bool anomaly_only = (filter == "anomaly");

    // 检查是否使用日期范围查询
    QString start_date = args["start_date"].toString().trimmed();
    QString end_date = args["end_date"].toString().trimmed();

    if (!start_date.isEmpty() && !end_date.isEmpty()) {
        // 日期范围查询
        spdlog::info("Date range query: {} to {}, filter={}",
            start_date.toStdString(), end_date.toStdString(), filter.toStdString());

        if (query_type == "stats" || query_type == "统计") {
            // 日期范围统计
            auto stats_list = attendance_service_->get_statistics_range(
                start_date.toStdString(), end_date.toStdString());

            if (stats_list.empty()) {
                return QString("日期范围 (%1 至 %2) 暂无考勤记录").arg(start_date, end_date);
            }

            // 汇总统计
            int total_count = 0, check_in_count = 0, late_count = 0;
            int early_leave_count = 0, normal_count = 0;

            for (const auto& stats : stats_list) {
                total_count += stats.total_count;
                check_in_count += stats.check_in_count;
                late_count += stats.late_count;
                early_leave_count += stats.early_leave_count;
                normal_count += stats.normal_count;
            }

            return QString(
                "考勤统计 (%1 至 %2):\n"
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
        } else {
            // 日期范围记录查询
            return queryRecordsRange(start_date, end_date, anomaly_only);
        }
    }

    // 获取单日日期
    QString date = args["date"].toString();
    if (date.isEmpty()) {
        QString range = args["date_range"].toString().toLower().trimmed();
        if (range.isEmpty() || range == "today" || range == "今日" || range == "今天") {
            date = getTodayDate();
        } else if (range == "week" || range == "本周") {
            // 对于周/月统计，使用原有逻辑
            if (query_type == "stats") {
                return queryWeek();
            }
            date = getTodayDate();  // 其他查询类型使用今日
        } else if (range == "month" || range == "本月") {
            if (query_type == "stats") {
                return queryMonth();
            }
            date = getTodayDate();
        } else {
            date = getTodayDate();
        }
    }

    // 根据查询类型执行
    if (query_type == "stats" || query_type == "统计") {
        return queryDate(date);
    } else if (query_type == "records" || query_type == "记录" || query_type == "详细") {
        return queryRecords(date);
    } else if (query_type == "late" || query_type == "迟到") {
        return queryLateList(date);
    } else if (query_type == "early_leave" || query_type == "早退") {
        return queryEarlyLeaveList(date);
    }

    return QString("错误: 无效的查询类型 '%1'，支持 stats/records/late/early_leave").arg(query_type);
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

QString AttendanceTool::queryRecords(const QString& date) {
    spdlog::debug("Querying attendance records for date: {}", date.toStdString());

    auto records = attendance_service_->query_records_by_date(date.toStdString());

    if (records.empty()) {
        return QString("%1 暂无考勤记录").arg(date);
    }

    QString result = QString("考勤详细记录 (%1):\n").arg(date);
    for (const auto& record : records) {
        QString status_str;
        switch (record.status) {
            case 1: status_str = "正常"; break;
            case 2: status_str = "迟到"; break;
            case 3: status_str = "早退"; break;
            default: status_str = "未知"; break;
        }

        QString type_str = record.check_type == 1 ? "签到" : "签退";
        QDateTime check_time = QDateTime::fromSecsSinceEpoch(record.check_time);

        result += QString("- %1: %2 %3 (%4)\n")
            .arg(QString::fromStdString(record.user_name))
            .arg(check_time.toString("HH:mm:ss"))
            .arg(type_str)
            .arg(status_str);
    }

    return result;
}

QString AttendanceTool::queryLateList(const QString& date) {
    spdlog::debug("Querying late list for date: {}", date.toStdString());

    auto records = attendance_service_->query_records_by_date(date.toStdString());

    QStringList late_names;
    for (const auto& record : records) {
        if (record.status == 2) {  // 2 = 迟到
            QString name = QString::fromStdString(record.user_name);
            if (!late_names.contains(name)) {
                late_names.append(name);
            }
        }
    }

    if (late_names.empty()) {
        return QString("%1 没有人迟到").arg(date);
    }

    return QString("%1 迟到人员 (%2人):\n- %3")
        .arg(date)
        .arg(late_names.size())
        .arg(late_names.join("\n- "));
}

QString AttendanceTool::queryEarlyLeaveList(const QString& date) {
    spdlog::debug("Querying early leave list for date: {}", date.toStdString());

    auto records = attendance_service_->query_records_by_date(date.toStdString());

    QStringList early_leave_names;
    for (const auto& record : records) {
        if (record.status == 3) {  // 3 = 早退
            QString name = QString::fromStdString(record.user_name);
            if (!early_leave_names.contains(name)) {
                early_leave_names.append(name);
            }
        }
    }

    if (early_leave_names.empty()) {
        return QString("%1 没有人早退").arg(date);
    }

    return QString("%1 早退人员 (%2人):\n- %3")
        .arg(date)
        .arg(early_leave_names.size())
        .arg(early_leave_names.join("\n- "));
}

QString AttendanceTool::queryRecordsRange(const QString& start_date,
                                           const QString& end_date,
                                           bool anomaly_only) {
    spdlog::info("Querying records range: {} to {}, anomaly_only={}",
        start_date.toStdString(), end_date.toStdString(), anomaly_only);

    auto records = attendance_service_->query_records_range(
        start_date.toStdString(), end_date.toStdString());

    if (records.empty()) {
        return QString("日期范围 (%1 至 %2) 暂无考勤记录").arg(start_date, end_date);
    }

    // 过滤记录
    std::vector<db::AttendanceRecord> filtered_records;
    if (anomaly_only) {
        for (const auto& record : records) {
            if (record.status == 2 || record.status == 3) {  // 2=迟到, 3=早退
                filtered_records.push_back(record);
            }
        }
    } else {
        filtered_records = records;
    }

    if (filtered_records.empty()) {
        return QString("日期范围 (%1 至 %2) 没有异常考勤记录").arg(start_date, end_date);
    }

    // 按日期分组显示
    QString result;
    if (anomaly_only) {
        result = QString("异常考勤记录 (%1 至 %2, 共 %3 条):\n")
            .arg(start_date, end_date)
            .arg(filtered_records.size());
    } else {
        result = QString("考勤详细记录 (%1 至 %2, 共 %3 条):\n")
            .arg(start_date, end_date)
            .arg(filtered_records.size());
    }

    QString current_date;
    for (const auto& record : filtered_records) {
        QDateTime check_time = QDateTime::fromSecsSinceEpoch(record.check_time);
        QString record_date = check_time.toString("yyyy-MM-dd");

        // 日期变化时添加日期标题
        if (record_date != current_date) {
            current_date = record_date;
            result += QString("\n[%1]\n").arg(record_date);
        }

        QString status_str;
        switch (record.status) {
            case 1: status_str = "正常"; break;
            case 2: status_str = "迟到"; break;
            case 3: status_str = "早退"; break;
            default: status_str = "未知"; break;
        }

        QString type_str = record.check_type == 1 ? "签到" : "签退";

        result += QString("- %1: %2 %3 (%4)\n")
            .arg(QString::fromStdString(record.user_name))
            .arg(check_time.toString("HH:mm:ss"))
            .arg(type_str)
            .arg(status_str);
    }

    return result;
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

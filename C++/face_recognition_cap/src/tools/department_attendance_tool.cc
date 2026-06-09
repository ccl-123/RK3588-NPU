/**
 * @file department_attendance_tool.cc
 * @brief 部门考勤查询工具实现
 */

#include "tools/department_attendance_tool.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <map>

namespace agent {

namespace {

QString normalize_department(const std::string& department) {
    const QString value = QString::fromStdString(department).trimmed();
    return value.isEmpty() ? QStringLiteral("未分配") : value;
}

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

QString format_record_line(const db::AttendanceRecord& record) {
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(record.check_time);
    return QString("- %1 %2 %3 %4")
        .arg(QString::fromStdString(record.user_name))
        .arg(dt.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(check_type_text(record.check_type))
        .arg(attendance_status_text(record.status));
}

}  // namespace

DepartmentAttendanceTool::DepartmentAttendanceTool(service::AttendanceService* attendance_service,
                                                   service::UserService* user_service)
    : attendance_service_(attendance_service),
      user_service_(user_service) {}

QJsonObject DepartmentAttendanceTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"department", QJsonObject{
                {"type", "string"},
                {"description", "部门名称或关键词，必填"}
            }},
            {"query_type", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"summary", "anomaly", "missing", "records"}},
                {"description", "查询类型：summary(摘要)、anomaly(异常记录)、missing(缺勤名单)、records(全部记录)"}
            }},
            {"date_range", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"today", "week", "month"}},
                {"description", "查询时间范围：today(今日)、week(本周)、month(本月)"}
            }},
            {"date", QJsonObject{
                {"type", "string"},
                {"description", "指定单日，格式 YYYY-MM-DD"}
            }},
            {"start_date", QJsonObject{
                {"type", "string"},
                {"description", "开始日期，格式 YYYY-MM-DD，与 end_date 配合使用"}
            }},
            {"end_date", QJsonObject{
                {"type", "string"},
                {"description", "结束日期，格式 YYYY-MM-DD，与 start_date 配合使用"}
            }}
        }},
        {"required", QJsonArray{"department"}}
    };
}

ToolExecutionResult DepartmentAttendanceTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!attendance_service_ || !user_service_) {
        result.ok = false;
        result.error = "错误: 部门考勤查询依赖的服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    const QString department_keyword = args.value("department").toString().trimmed();
    QString query_type = args.value("query_type").toString("summary").toLower().trimmed();

    result.output["department"] = department_keyword;
    result.output["query_type"] = query_type;

    if (department_keyword.isEmpty()) {
        result.ok = false;
        result.error = "错误: 请提供 department";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const auto users = user_service_->get_all_users(1);
    std::map<QString, std::vector<db::UserInfo>> department_map;
    for (const auto& user : users) {
        department_map[normalize_department(user.department)].push_back(user);
    }

    QString matched_department;
    for (const auto& pair : department_map) {
        if (pair.first.contains(department_keyword, Qt::CaseInsensitive)) {
            if (!matched_department.isEmpty()) {
                result.ok = false;
                result.error = QString("匹配到多个部门，请提供更精确的名称: %1 / %2")
                    .arg(matched_department, pair.first);
                result.display_text = result.error;
                result.output["error"] = result.error;
                return result;
            }
            matched_department = pair.first;
        }
    }

    if (matched_department.isEmpty()) {
        result.ok = false;
        result.error = QString("未找到部门 '%1'").arg(department_keyword);
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const auto& department_users = department_map[matched_department];
    QSet<int> department_user_ids;
    QJsonArray department_user_array;
    for (const auto& user : department_users) {
        department_user_ids.insert(user.user_id);
        department_user_array.append(QJsonObject{
            {"user_id", user.user_id},
            {"user_name", QString::fromStdString(user.user_name)}
        });
    }

    QString start_date = args.value("start_date").toString().trimmed();
    QString end_date = args.value("end_date").toString().trimmed();
    QString date = args.value("date").toString().trimmed();
    QString date_range = args.value("date_range").toString("today").toLower().trimmed();
    QString range_label;

    if (!date.isEmpty()) {
        start_date = date;
        end_date = date;
        range_label = date;
    } else if (!start_date.isEmpty() && !end_date.isEmpty()) {
        range_label = QString("%1 至 %2").arg(start_date, end_date);
    } else if (date_range == "week" || date_range == "本周") {
        start_date = getWeekStartDate();
        end_date = getTodayDate();
        range_label = QString("本周(%1 至 %2)").arg(start_date, end_date);
    } else if (date_range == "month" || date_range == "本月") {
        start_date = getMonthStartDate();
        end_date = getTodayDate();
        range_label = QString("本月(%1 至 %2)").arg(start_date, end_date);
    } else {
        start_date = getTodayDate();
        end_date = getTodayDate();
        range_label = QString("今日(%1)").arg(start_date);
    }

    auto records = attendance_service_->query_records_range(start_date.toStdString(), end_date.toStdString());
    std::vector<db::AttendanceRecord> filtered_records;
    filtered_records.reserve(records.size());
    for (const auto& record : records) {
        if (department_user_ids.contains(record.user_id)) {
            filtered_records.push_back(record);
        }
    }
    std::sort(filtered_records.begin(), filtered_records.end(), [](const db::AttendanceRecord& a, const db::AttendanceRecord& b) {
        return a.check_time > b.check_time;
    });

    QSet<int> present_user_ids;
    int late_count = 0;
    int early_leave_count = 0;
    QJsonArray record_array;
    for (const auto& record : filtered_records) {
        present_user_ids.insert(record.user_id);
        record_array.append(record_to_json(record));
        if (record.status == db::STATUS_LATE) {
            ++late_count;
        } else if (record.status == db::STATUS_EARLY_LEAVE) {
            ++early_leave_count;
        }
    }

    result.ok = true;
    result.output["matched_department"] = matched_department;
    result.output["users"] = department_user_array;
    result.output["start_date"] = start_date;
    result.output["end_date"] = end_date;

    if (query_type == "missing") {
        QJsonArray missing_users;
        QStringList lines;
        for (const auto& user : department_users) {
            if (!present_user_ids.contains(user.user_id)) {
                missing_users.append(QJsonObject{
                    {"user_id", user.user_id},
                    {"user_name", QString::fromStdString(user.user_name)}
                });
                lines.append(QString("- %1 (ID: %2)")
                    .arg(QString::fromStdString(user.user_name))
                    .arg(user.user_id));
            }
        }

        result.output["mode"] = "missing";
        result.output["missing_users"] = missing_users;
        result.display_text = missing_users.isEmpty()
            ? QString("%1 在 %2 没有缺勤人员").arg(matched_department, range_label)
            : QString("%1 在 %2 缺勤人员 (%3 人):\n%4")
                .arg(matched_department, range_label)
                .arg(missing_users.size())
                .arg(lines.join("\n"));
        return result;
    }

    if (query_type == "anomaly") {
        QJsonArray anomaly_records;
        QStringList lines;
        for (const auto& record : filtered_records) {
            if (record.status == db::STATUS_LATE || record.status == db::STATUS_EARLY_LEAVE) {
                anomaly_records.append(record_to_json(record));
                lines.append(format_record_line(record));
            }
        }

        result.output["mode"] = "anomaly";
        result.output["records"] = anomaly_records;
        result.output["late_count"] = late_count;
        result.output["early_leave_count"] = early_leave_count;
        result.display_text = anomaly_records.isEmpty()
            ? QString("%1 在 %2 没有异常打卡").arg(matched_department, range_label)
            : QString("%1 在 %2 的异常记录 (%3 条):\n%4")
                .arg(matched_department, range_label)
                .arg(anomaly_records.size())
                .arg(lines.join("\n"));
        return result;
    }

    if (query_type == "records") {
        result.output["mode"] = "records";
        result.output["records"] = record_array;
        if (record_array.isEmpty()) {
            result.display_text = QString("%1 在 %2 没有打卡记录").arg(matched_department, range_label);
        } else {
            QStringList lines;
            for (const auto& record : filtered_records) {
                lines.append(format_record_line(record));
            }
            result.display_text = QString("%1 在 %2 的打卡记录 (%3 条):\n%4")
                .arg(matched_department, range_label)
                .arg(record_array.size())
                .arg(lines.join("\n"));
        }
        return result;
    }

    const int employee_count = static_cast<int>(department_users.size());
    const int present_count = present_user_ids.size();
    const int absent_count = employee_count - present_count;
    const double attendance_rate = employee_count > 0
        ? (static_cast<double>(present_count) * 100.0 / employee_count)
        : 0.0;

    result.output["mode"] = "summary";
    result.output["summary"] = QJsonObject{
        {"employee_count", employee_count},
        {"present_count", present_count},
        {"absent_count", absent_count},
        {"attendance_rate", attendance_rate},
        {"total_records", static_cast<int>(filtered_records.size())},
        {"late_count", late_count},
        {"early_leave_count", early_leave_count}
    };
    result.display_text = QString(
        "%1 在 %2 的考勤摘要:\n"
        "- 部门人数: %3\n"
        "- 有打卡人数: %4\n"
        "- 缺勤人数: %5\n"
        "- 出勤覆盖率: %6%%\n"
        "- 打卡总次数: %7\n"
        "- 迟到次数: %8\n"
        "- 早退次数: %9"
    ).arg(matched_department, range_label)
     .arg(employee_count)
     .arg(present_count)
     .arg(absent_count)
     .arg(attendance_rate, 0, 'f', 1)
     .arg(filtered_records.size())
     .arg(late_count)
     .arg(early_leave_count);
    return result;
}

QString DepartmentAttendanceTool::getTodayDate() const {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString DepartmentAttendanceTool::getWeekStartDate() const {
    const QDate today = QDate::currentDate();
    return today.addDays(-(today.dayOfWeek() - 1)).toString("yyyy-MM-dd");
}

QString DepartmentAttendanceTool::getMonthStartDate() const {
    const QDate today = QDate::currentDate();
    return QDate(today.year(), today.month(), 1).toString("yyyy-MM-dd");
}

}  // namespace agent

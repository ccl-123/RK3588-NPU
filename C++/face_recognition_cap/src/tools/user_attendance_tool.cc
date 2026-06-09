/**
 * @file user_attendance_tool.cc
 * @brief 单用户考勤查询工具实现
 */

#include "tools/user_attendance_tool.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>

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
    return QString("- %1 %2 %3 (%4)")
        .arg(dt.toString("yyyy-MM-dd HH:mm:ss"))
        .arg(check_type_text(record.check_type))
        .arg(attendance_status_text(record.status))
        .arg(record.similarity, 0, 'f', 2);
}

}  // namespace

UserAttendanceTool::UserAttendanceTool(service::AttendanceService* attendance_service,
                                       service::UserService* user_service)
    : attendance_service_(attendance_service),
      user_service_(user_service) {}

QJsonObject UserAttendanceTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"query_type", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"summary", "records", "latest", "anomaly"}},
                {"description", "查询类型：summary(摘要)、records(详细记录)、latest(最近一次打卡)、anomaly(异常打卡)"}
            }},
            {"user_id", QJsonObject{
                {"type", "integer"},
                {"description", "用户ID，和 name 二选一"}
            }},
            {"name", QJsonObject{
                {"type", "string"},
                {"description", "用户姓名关键词，和 user_id 二选一"}
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
        {"required", QJsonArray{}},
        {"anyOf", QJsonArray{
            QJsonObject{{"required", QJsonArray{"user_id"}}},
            QJsonObject{{"required", QJsonArray{"name"}}}
        }}
    };
}

ToolExecutionResult UserAttendanceTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!attendance_service_ || !user_service_) {
        result.ok = false;
        result.error = "错误: 用户考勤查询依赖的服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    QString query_type = args.value("query_type").toString("summary").toLower().trimmed();
    QString name_param = args.value("name").toString().trimmed();
    const int user_id = args.value("user_id").toInt(args.value("id").toInt());

    result.output["query_type"] = query_type;
    result.output["user_id"] = user_id;
    result.output["name"] = name_param;

    db::UserInfo user;
    if (user_id > 0) {
        if (!user_service_->get_user(user_id, user)) {
            result.ok = false;
            result.error = QString("未找到ID为 %1 的用户").arg(user_id);
            result.display_text = result.error;
            result.output["error"] = result.error;
            return result;
        }
    } else if (!name_param.isEmpty()) {
        const auto users = user_service_->get_all_users();
        std::vector<db::UserInfo> matches;
        for (const auto& item : users) {
            if (QString::fromStdString(item.user_name).contains(name_param, Qt::CaseInsensitive)) {
                matches.push_back(item);
            }
        }

        if (matches.empty()) {
            result.ok = false;
            result.error = QString("未找到姓名包含 '%1' 的用户").arg(name_param);
            result.display_text = result.error;
            result.output["error"] = result.error;
            return result;
        }

        if (matches.size() > 1) {
            QJsonArray candidates;
            QStringList lines;
            for (const auto& item : matches) {
                candidates.append(QJsonObject{
                    {"user_id", item.user_id},
                    {"user_name", QString::fromStdString(item.user_name)},
                    {"department", QString::fromStdString(item.department)}
                });
                lines.append(QString("- %1 (ID: %2, 部门: %3)")
                    .arg(QString::fromStdString(item.user_name))
                    .arg(item.user_id)
                    .arg(QString::fromStdString(item.department.empty() ? "未设置" : item.department)));
            }

            result.ok = false;
            result.error = QString("匹配到多个用户，请改用 user_id 指定:\n%1").arg(lines.join("\n"));
            result.display_text = result.error;
            result.output["candidates"] = candidates;
            result.output["error"] = result.error;
            return result;
        }

        user = matches.front();
    } else {
        result.ok = false;
        result.error = "错误: 请提供 user_id 或 name";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
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

    result.output["start_date"] = start_date;
    result.output["end_date"] = end_date;
    result.output["range_label"] = range_label;
    result.output["user"] = QJsonObject{
        {"user_id", user.user_id},
        {"user_name", QString::fromStdString(user.user_name)},
        {"employee_id", QString::fromStdString(user.employee_id)},
        {"department", QString::fromStdString(user.department)},
        {"position", QString::fromStdString(user.position)}
    };

    auto records = attendance_service_->query_user_records(user.user_id, start_date.toStdString(), end_date.toStdString());
    std::sort(records.begin(), records.end(), [](const db::AttendanceRecord& a, const db::AttendanceRecord& b) {
        return a.check_time > b.check_time;
    });

    if (query_type == "latest") {
        if (records.empty()) {
            result.ok = false;
            result.error = QString("%1 在 %2 没有打卡记录")
                .arg(QString::fromStdString(user.user_name), range_label);
            result.display_text = result.error;
            result.output["error"] = result.error;
            return result;
        }

        result.ok = true;
        result.output["mode"] = "latest";
        result.output["record"] = record_to_json(records.front());
        result.display_text = QString("%1 最近一次打卡:\n%2")
            .arg(QString::fromStdString(user.user_name), format_record_line(records.front()));
        return result;
    }

    QJsonArray items;
    int check_in_count = 0;
    int check_out_count = 0;
    int late_count = 0;
    int early_leave_count = 0;
    int normal_count = 0;

    for (const auto& record : records) {
        if (record.check_type == db::CHECK_IN) {
            ++check_in_count;
        } else if (record.check_type == db::CHECK_OUT) {
            ++check_out_count;
        }

        if (record.status == db::STATUS_LATE) {
            ++late_count;
        } else if (record.status == db::STATUS_EARLY_LEAVE) {
            ++early_leave_count;
        } else {
            ++normal_count;
        }
    }

    if (query_type == "anomaly") {
        for (const auto& record : records) {
            if (record.status == db::STATUS_LATE || record.status == db::STATUS_EARLY_LEAVE) {
                items.append(record_to_json(record));
            }
        }

        result.ok = true;
        result.output["mode"] = "anomaly";
        result.output["records"] = items;
        result.output["late_count"] = late_count;
        result.output["early_leave_count"] = early_leave_count;
        if (items.isEmpty()) {
            result.display_text = QString("%1 在 %2 没有异常打卡")
                .arg(QString::fromStdString(user.user_name), range_label);
        } else {
            QStringList lines;
            for (const auto& item : items) {
                const auto obj = item.toObject();
                lines.append(QString("- %1 %2 %3")
                    .arg(obj.value("check_time").toString(),
                         obj.value("check_type").toString(),
                         obj.value("status").toString()));
            }
            result.display_text = QString(
                "%1 在 %2 的异常打卡 (%3 条):\n%4"
            ).arg(QString::fromStdString(user.user_name), range_label)
             .arg(items.size())
             .arg(lines.join("\n"));
        }
        return result;
    }

    if (query_type == "records") {
        for (const auto& record : records) {
            items.append(record_to_json(record));
        }

        result.ok = true;
        result.output["mode"] = "records";
        result.output["records"] = items;
        if (items.isEmpty()) {
            result.display_text = QString("%1 在 %2 没有打卡记录")
                .arg(QString::fromStdString(user.user_name), range_label);
        } else {
            QStringList lines;
            for (const auto& record : records) {
                lines.append(format_record_line(record));
            }
            result.display_text = QString("%1 在 %2 的打卡记录 (%3 条):\n%4")
                .arg(QString::fromStdString(user.user_name), range_label)
                .arg(records.size())
                .arg(lines.join("\n"));
        }
        return result;
    }

    result.ok = true;
    result.output["mode"] = "summary";
    result.output["summary"] = QJsonObject{
        {"total_records", static_cast<int>(records.size())},
        {"check_in_count", check_in_count},
        {"check_out_count", check_out_count},
        {"late_count", late_count},
        {"early_leave_count", early_leave_count},
        {"normal_count", normal_count},
        {"latest_check_time", records.empty() ? QString() : QDateTime::fromSecsSinceEpoch(records.front().check_time).toString(Qt::ISODate)}
    };

    if (records.empty()) {
        result.display_text = QString("%1 在 %2 没有打卡记录")
            .arg(QString::fromStdString(user.user_name), range_label);
    } else {
        result.display_text = QString(
            "%1 在 %2 的考勤摘要:\n"
            "- 打卡总次数: %3\n"
            "- 签到次数: %4\n"
            "- 签退次数: %5\n"
            "- 正常打卡: %6\n"
            "- 迟到次数: %7\n"
            "- 早退次数: %8\n"
            "- 最近一次打卡: %9"
        ).arg(QString::fromStdString(user.user_name), range_label)
         .arg(records.size())
         .arg(check_in_count)
         .arg(check_out_count)
         .arg(normal_count)
         .arg(late_count)
         .arg(early_leave_count)
         .arg(QDateTime::fromSecsSinceEpoch(records.front().check_time).toString("yyyy-MM-dd HH:mm:ss"));
    }

    return result;
}

QString UserAttendanceTool::getTodayDate() const {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString UserAttendanceTool::getWeekStartDate() const {
    const QDate today = QDate::currentDate();
    return today.addDays(-(today.dayOfWeek() - 1)).toString("yyyy-MM-dd");
}

QString UserAttendanceTool::getMonthStartDate() const {
    const QDate today = QDate::currentDate();
    return QDate(today.year(), today.month(), 1).toString("yyyy-MM-dd");
}

}  // namespace agent

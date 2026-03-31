/**
 * @file missing_attendance_tool.cc
 * @brief 缺卡/缺勤查询工具实现
 */

#include "tools/missing_attendance_tool.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <QSet>
#include <map>

namespace agent {

namespace {

QString normalize_department(const std::string& department) {
    const QString value = QString::fromStdString(department).trimmed();
    return value.isEmpty() ? QStringLiteral("未分配") : value;
}

}  // namespace

MissingAttendanceTool::MissingAttendanceTool(service::AttendanceService* attendance_service,
                                             service::UserService* user_service)
    : attendance_service_(attendance_service),
      user_service_(user_service) {}

QJsonObject MissingAttendanceTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"query_type", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"absent", "missing_check_in", "missing_check_out", "consecutive_absent"}},
                {"description", "查询类型：absent(当天完全缺勤)、missing_check_in(未签到)、missing_check_out(只签到未签退)、consecutive_absent(连续多天未打卡)"}
            }},
            {"date", QJsonObject{
                {"type", "string"},
                {"description", "目标日期，格式 YYYY-MM-DD，默认今日"}
            }},
            {"department", QJsonObject{
                {"type", "string"},
                {"description", "可选，部门过滤"}
            }},
            {"days", QJsonObject{
                {"type", "integer"},
                {"description", "仅用于 consecutive_absent，表示连续缺勤天数，默认 2"}
            }}
        }},
        {"required", QJsonArray{}}
    };
}

ToolExecutionResult MissingAttendanceTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!attendance_service_ || !user_service_) {
        result.ok = false;
        result.error = "错误: 缺卡查询依赖的服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    const QString query_type = args.value("query_type").toString("absent").toLower().trimmed();
    const QString date = args.value("date").toString(getTodayDate()).trimmed();
    const QString department_filter = args.value("department").toString().trimmed();
    const int days = qMax(1, args.value("days").toInt(2));

    result.output["query_type"] = query_type;
    result.output["date"] = date;
    result.output["department"] = department_filter;
    result.output["days"] = days;

    const auto active_users = user_service_->get_all_users(1);
    std::vector<db::UserInfo> filtered_users;
    for (const auto& user : active_users) {
        const QString department = normalize_department(user.department);
        if (department_filter.isEmpty() || department.contains(department_filter, Qt::CaseInsensitive)) {
            filtered_users.push_back(user);
        }
    }

    if (filtered_users.empty()) {
        result.ok = false;
        result.error = department_filter.isEmpty()
            ? QStringLiteral("没有可用于缺卡分析的启用用户")
            : QString("未找到部门 '%1' 下的启用用户").arg(department_filter);
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    QJsonArray users_array;
    QStringList lines;

    if (query_type == "consecutive_absent") {
        const QDate end_date = QDate::fromString(date, "yyyy-MM-dd");
        const QDate start_date = end_date.addDays(-(days - 1));
        auto records = attendance_service_->query_records_range(
            start_date.toString("yyyy-MM-dd").toStdString(),
            end_date.toString("yyyy-MM-dd").toStdString());
        std::map<int, QSet<QString>> user_days;
        for (const auto& record : records) {
            user_days[record.user_id].insert(QDateTime::fromSecsSinceEpoch(record.check_time).date().toString("yyyy-MM-dd"));
        }

        for (const auto& user : filtered_users) {
            if (user_days[user.user_id].isEmpty()) {
                users_array.append(QJsonObject{
                    {"user_id", user.user_id},
                    {"user_name", QString::fromStdString(user.user_name)},
                    {"department", normalize_department(user.department)}
                });
                lines.append(QString("- %1 (ID: %2, 部门: %3)")
                    .arg(QString::fromStdString(user.user_name))
                    .arg(user.user_id)
                    .arg(normalize_department(user.department)));
            }
        }

        result.ok = true;
        result.output["mode"] = "consecutive_absent";
        result.output["users"] = users_array;
        result.display_text = users_array.isEmpty()
            ? QString("最近 %1 天没有连续缺勤人员").arg(days)
            : QString("最近 %1 天连续未打卡人员 (%2 人):\n%3")
                .arg(days)
                .arg(users_array.size())
                .arg(lines.join("\n"));
        return result;
    }

    const auto records = attendance_service_->query_records_by_date(date.toStdString());
    QSet<int> any_record_users;
    QSet<int> check_in_users;
    QSet<int> check_out_users;
    for (const auto& record : records) {
        any_record_users.insert(record.user_id);
        if (record.check_type == db::CHECK_IN) {
            check_in_users.insert(record.user_id);
        } else if (record.check_type == db::CHECK_OUT) {
            check_out_users.insert(record.user_id);
        }
    }

    for (const auto& user : filtered_users) {
        const bool has_any = any_record_users.contains(user.user_id);
        const bool has_check_in = check_in_users.contains(user.user_id);
        const bool has_check_out = check_out_users.contains(user.user_id);

        bool matched = false;
        if (query_type == "missing_check_in") {
            matched = !has_check_in;
        } else if (query_type == "missing_check_out") {
            matched = has_check_in && !has_check_out;
        } else {
            matched = !has_any;
        }

        if (!matched) {
            continue;
        }

        users_array.append(QJsonObject{
            {"user_id", user.user_id},
            {"user_name", QString::fromStdString(user.user_name)},
            {"department", normalize_department(user.department)},
            {"has_check_in", has_check_in},
            {"has_check_out", has_check_out}
        });
        lines.append(QString("- %1 (ID: %2, 部门: %3)")
            .arg(QString::fromStdString(user.user_name))
            .arg(user.user_id)
            .arg(normalize_department(user.department)));
    }

    result.ok = true;
    result.output["mode"] = query_type;
    result.output["users"] = users_array;
    if (query_type == "missing_check_in") {
        result.display_text = users_array.isEmpty()
            ? QString("%1 没有未签到人员").arg(date)
            : QString("%1 未签到人员 (%2 人):\n%3")
                .arg(date)
                .arg(users_array.size())
                .arg(lines.join("\n"));
    } else if (query_type == "missing_check_out") {
        result.display_text = users_array.isEmpty()
            ? QString("%1 没有只签到未签退人员").arg(date)
            : QString("%1 只签到未签退人员 (%2 人):\n%3")
                .arg(date)
                .arg(users_array.size())
                .arg(lines.join("\n"));
    } else {
        result.display_text = users_array.isEmpty()
            ? QString("%1 没有完全缺勤人员").arg(date)
            : QString("%1 完全缺勤人员 (%2 人):\n%3")
                .arg(date)
                .arg(users_array.size())
                .arg(lines.join("\n"));
    }
    return result;
}

QString MissingAttendanceTool::getTodayDate() const {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

}  // namespace agent

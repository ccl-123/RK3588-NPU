/**
 * @file attendance_ranking_tool.cc
 * @brief 考勤排名工具实现
 */

#include "tools/attendance_ranking_tool.h"

#include <QDate>
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>
#include <map>

namespace agent {

namespace {

QString normalize_department(const std::string& department) {
    const QString value = QString::fromStdString(department).trimmed();
    return value.isEmpty() ? QStringLiteral("未分配") : value;
}

int inclusive_days(const QDate& start_date, const QDate& end_date) {
    return qMax(1, static_cast<int>(start_date.daysTo(end_date) + 1));
}

struct RankingItem {
    QString label;
    double score = 0.0;
    QString detail;
    QJsonObject output;
};

QString ranking_type_label(const QString& ranking_type) {
    if (ranking_type == "late") {
        return QStringLiteral("迟到");
    }
    if (ranking_type == "early_leave") {
        return QStringLiteral("早退");
    }
    if (ranking_type == "anomaly_rate") {
        return QStringLiteral("异常率");
    }
    if (ranking_type == "attendance_rate") {
        return QStringLiteral("出勤覆盖率");
    }
    return QStringLiteral("异常");
}

}  // namespace

AttendanceRankingTool::AttendanceRankingTool(service::AttendanceService* attendance_service,
                                             service::UserService* user_service)
    : attendance_service_(attendance_service),
      user_service_(user_service) {}

QJsonObject AttendanceRankingTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"scope", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"user", "department"}},
                {"description", "排行对象：user(员工) 或 department(部门)"}
            }},
            {"ranking_type", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"late", "early_leave", "anomaly", "anomaly_rate", "attendance_rate"}},
                {"description", "排行指标：late(迟到次数)、early_leave(早退次数)、anomaly(异常次数)、anomaly_rate(异常率)、attendance_rate(出勤覆盖率)"}
            }},
            {"limit", QJsonObject{
                {"type", "integer"},
                {"description", "返回前 N 名，默认 5"}
            }},
            {"order", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"desc", "asc"}},
                {"description", "排序方向：desc(从高到低，默认) 或 asc(从低到高)"}
            }},
            {"department", QJsonObject{
                {"type", "string"},
                {"description", "可选，仅对 user 排行做部门过滤"}
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
        {"required", QJsonArray{}}
    };
}

ToolExecutionResult AttendanceRankingTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!attendance_service_ || !user_service_) {
        result.ok = false;
        result.error = "错误: 排名查询依赖的服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    const QString scope = args.value("scope").toString("user").toLower().trimmed();
    const QString ranking_type = args.value("ranking_type").toString("anomaly").toLower().trimmed();
    const QString order = args.value("order").toString("desc").toLower().trimmed();
    const int limit = qMax(1, args.value("limit").toInt(5));
    const QString department_filter = args.value("department").toString().trimmed();

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

    result.output["scope"] = scope;
    result.output["ranking_type"] = ranking_type;
    result.output["order"] = order;
    result.output["limit"] = limit;
    result.output["start_date"] = start_date;
    result.output["end_date"] = end_date;

    const auto active_users = user_service_->get_all_users(1);
    std::map<int, db::UserInfo> users_by_id;
    std::map<QString, std::vector<db::UserInfo>> departments;
    for (const auto& user : active_users) {
        users_by_id[user.user_id] = user;
        departments[normalize_department(user.department)].push_back(user);
    }

    auto records = attendance_service_->query_records_range(start_date.toStdString(), end_date.toStdString());
    const QDate start_qdate = QDate::fromString(start_date, "yyyy-MM-dd");
    const QDate end_qdate = QDate::fromString(end_date, "yyyy-MM-dd");
    const int range_days = inclusive_days(start_qdate, end_qdate);

    std::vector<RankingItem> ranking_items;

    if (scope == "department") {
        for (const auto& pair : departments) {
            const QString& department = pair.first;
            const auto& users = pair.second;
            QSet<int> user_ids;
            for (const auto& user : users) {
                user_ids.insert(user.user_id);
            }

            int total_records = 0;
            int late_count = 0;
            int early_leave_count = 0;
            int anomaly_count = 0;
            QSet<int> present_users;

            for (const auto& record : records) {
                if (!user_ids.contains(record.user_id)) {
                    continue;
                }
                ++total_records;
                present_users.insert(record.user_id);
                if (record.status == db::STATUS_LATE) {
                    ++late_count;
                    ++anomaly_count;
                } else if (record.status == db::STATUS_EARLY_LEAVE) {
                    ++early_leave_count;
                    ++anomaly_count;
                }
            }

            RankingItem item;
            item.label = department;
            item.output["department"] = department;
            item.output["employee_count"] = static_cast<int>(users.size());
            item.output["present_users"] = present_users.size();
            item.output["total_records"] = total_records;
            item.output["late_count"] = late_count;
            item.output["early_leave_count"] = early_leave_count;
            item.output["anomaly_count"] = anomaly_count;

            if (ranking_type == "attendance_rate") {
                item.score = users.empty() ? 0.0 : (static_cast<double>(present_users.size()) * 100.0 / users.size());
                item.detail = QString("出勤覆盖率 %1%% (%2/%3)")
                    .arg(item.score, 0, 'f', 1)
                    .arg(present_users.size())
                    .arg(users.size());
            } else if (ranking_type == "late") {
                item.score = late_count;
                item.detail = QString("迟到 %1 次").arg(late_count);
            } else if (ranking_type == "early_leave") {
                item.score = early_leave_count;
                item.detail = QString("早退 %1 次").arg(early_leave_count);
            } else if (ranking_type == "anomaly_rate") {
                item.score = total_records == 0 ? 0.0 : (static_cast<double>(anomaly_count) * 100.0 / total_records);
                item.detail = QString("异常率 %1%% (%2/%3)")
                    .arg(item.score, 0, 'f', 1)
                    .arg(anomaly_count)
                    .arg(total_records);
            } else {
                item.score = anomaly_count;
                item.detail = QString("异常 %1 次").arg(anomaly_count);
            }

            item.output["score"] = item.score;
            ranking_items.push_back(item);
        }
    } else {
        QSet<int> allowed_user_ids;
        for (const auto& user : active_users) {
            const QString department = normalize_department(user.department);
            if (department_filter.isEmpty() || department.contains(department_filter, Qt::CaseInsensitive)) {
                allowed_user_ids.insert(user.user_id);
            }
        }

        std::map<int, int> late_count_map;
        std::map<int, int> early_leave_count_map;
        std::map<int, int> anomaly_count_map;
        std::map<int, int> total_records_map;
        std::map<int, QSet<QString>> attendance_days_map;

        for (const auto& record : records) {
            if (!allowed_user_ids.contains(record.user_id)) {
                continue;
            }
            ++total_records_map[record.user_id];
            attendance_days_map[record.user_id].insert(QDateTime::fromSecsSinceEpoch(record.check_time).date().toString("yyyy-MM-dd"));
            if (record.status == db::STATUS_LATE) {
                ++late_count_map[record.user_id];
                ++anomaly_count_map[record.user_id];
            } else if (record.status == db::STATUS_EARLY_LEAVE) {
                ++early_leave_count_map[record.user_id];
                ++anomaly_count_map[record.user_id];
            }
        }

        for (const auto& user : active_users) {
            if (!allowed_user_ids.contains(user.user_id)) {
                continue;
            }

            RankingItem item;
            item.label = QString::fromStdString(user.user_name);
            item.output["user_id"] = user.user_id;
            item.output["user_name"] = item.label;
            item.output["department"] = normalize_department(user.department);
            item.output["late_count"] = late_count_map[user.user_id];
            item.output["early_leave_count"] = early_leave_count_map[user.user_id];
            item.output["anomaly_count"] = anomaly_count_map[user.user_id];
            item.output["total_records"] = total_records_map[user.user_id];
            item.output["attendance_days"] = attendance_days_map[user.user_id].size();

            if (ranking_type == "attendance_rate") {
                item.score = static_cast<double>(attendance_days_map[user.user_id].size()) * 100.0 / range_days;
                item.detail = QString("出勤覆盖率 %1%% (%2/%3天)")
                    .arg(item.score, 0, 'f', 1)
                    .arg(attendance_days_map[user.user_id].size())
                    .arg(range_days);
            } else if (ranking_type == "late") {
                item.score = late_count_map[user.user_id];
                item.detail = QString("迟到 %1 次").arg(late_count_map[user.user_id]);
            } else if (ranking_type == "early_leave") {
                item.score = early_leave_count_map[user.user_id];
                item.detail = QString("早退 %1 次").arg(early_leave_count_map[user.user_id]);
            } else if (ranking_type == "anomaly_rate") {
                const int total = total_records_map[user.user_id];
                const int anomaly = anomaly_count_map[user.user_id];
                item.score = total == 0 ? 0.0 : (static_cast<double>(anomaly) * 100.0 / total);
                item.detail = QString("异常率 %1%% (%2/%3)")
                    .arg(item.score, 0, 'f', 1)
                    .arg(anomaly)
                    .arg(total);
            } else {
                item.score = anomaly_count_map[user.user_id];
                item.detail = QString("异常 %1 次").arg(anomaly_count_map[user.user_id]);
            }

            item.output["score"] = item.score;
            ranking_items.push_back(item);
        }
    }

    std::sort(ranking_items.begin(), ranking_items.end(), [order](const RankingItem& a, const RankingItem& b) {
        if (a.score == b.score) {
            return a.label < b.label;
        }
        return order == "asc" ? (a.score < b.score) : (a.score > b.score);
    });

    if (static_cast<int>(ranking_items.size()) > limit) {
        ranking_items.resize(limit);
    }

    QJsonArray items;
    QStringList lines;
    int rank = 1;
    for (const auto& item : ranking_items) {
        QJsonObject obj = item.output;
        obj["rank"] = rank;
        items.append(obj);
        lines.append(QString("%1. %2 - %3").arg(rank).arg(item.label, item.detail));
        ++rank;
    }

    result.ok = true;
    result.output["mode"] = "ranking";
    result.output["items"] = items;
    const QString ranking_label = ranking_type_label(ranking_type);
    result.display_text = lines.isEmpty()
        ? QString("%1 没有可用的排名数据").arg(range_label)
        : QString("%1 %2排行:\n%3").arg(range_label, ranking_label, lines.join("\n"));
    return result;
}

QString AttendanceRankingTool::getTodayDate() const {
    return QDate::currentDate().toString("yyyy-MM-dd");
}

QString AttendanceRankingTool::getWeekStartDate() const {
    const QDate today = QDate::currentDate();
    return today.addDays(-(today.dayOfWeek() - 1)).toString("yyyy-MM-dd");
}

QString AttendanceRankingTool::getMonthStartDate() const {
    const QDate today = QDate::currentDate();
    return QDate(today.year(), today.month(), 1).toString("yyyy-MM-dd");
}

}  // namespace agent

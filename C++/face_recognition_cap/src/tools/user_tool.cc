/**
 * @file user_tool.cc
 * @brief 用户查询工具实现
 */

#include "tools/user_tool.h"

#include <QJsonArray>
#include <QJsonObject>
#include <map>
#include <spdlog/spdlog.h>

namespace agent {

namespace {

QJsonObject user_to_json(const db::UserInfo& user, int feature_count) {
    return QJsonObject{
        {"user_id", user.user_id},
        {"user_name", QString::fromStdString(user.user_name)},
        {"employee_id", QString::fromStdString(user.employee_id)},
        {"department", QString::fromStdString(user.department)},
        {"position", QString::fromStdString(user.position)},
        {"status", user.status == db::USER_ENABLED ? "启用" : "禁用"},
        {"feature_count", feature_count},
    };
}

QString format_user_text(const QJsonObject& user) {
    return QString(
        "用户信息:\n"
        "- ID: %1\n"
        "- 姓名: %2\n"
        "- 工号: %3\n"
        "- 部门: %4\n"
        "- 职位: %5\n"
        "- 状态: %6\n"
        "- 人脸特征数: %7"
    ).arg(user.value("user_id").toInt())
     .arg(user.value("user_name").toString())
     .arg(user.value("employee_id").toString().isEmpty() ? "未设置" : user.value("employee_id").toString())
     .arg(user.value("department").toString().isEmpty() ? "未设置" : user.value("department").toString())
     .arg(user.value("position").toString().isEmpty() ? "未设置" : user.value("position").toString())
     .arg(user.value("status").toString())
     .arg(user.value("feature_count").toInt());
}

QString format_users_list_text(const QString& title, const QJsonArray& users) {
    if (users.isEmpty()) {
        return title + "\n暂无数据";
    }

    QString text = title;
    for (const auto& value : users) {
        const auto user = value.toObject();
        text += QString("\n- %1 (ID: %2, 部门: %3)")
            .arg(user.value("user_name").toString())
            .arg(user.value("user_id").toInt())
            .arg(user.value("department").toString().isEmpty() ? "未设置" : user.value("department").toString());
    }
    return text;
}

}  // namespace

UserTool::UserTool(service::UserService* service)
    : user_service_(service) {}

QJsonObject UserTool::parametersSchema() const {
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"action", QJsonObject{
                {"type", "string"},
                {"enum", QJsonArray{"get_by_id", "get_by_name", "list_all", "stats"}},
                {"description", "查询类型：get_by_id(按ID查询)、get_by_name(按姓名查询)、list_all(列出所有)、stats(统计信息)"}
            }},
            {"user_id", QJsonObject{
                {"type", "integer"},
                {"description", "用户ID，action=get_by_id 时必填"}
            }},
            {"name", QJsonObject{
                {"type", "string"},
                {"description", "用户姓名，action=get_by_name 时必填"}
            }}
        }},
        {"required", QJsonArray{"action"}}
    };
}

ToolExecutionResult UserTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    if (!user_service_) {
        result.ok = false;
        result.error = "错误: 用户服务未初始化";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    const QJsonObject& args = invocation.arguments;
    QString action = args.value("action").toString();
    if (action.isEmpty()) action = args.value("query_type").toString();
    if (action.isEmpty()) action = args.value("type").toString();
    if (action.isEmpty()) action = "stats";
    action = action.toLower().trimmed();

    QString name_param = args.value("name").toString();
    if (name_param.isEmpty()) name_param = args.value("keyword").toString();
    if (name_param.isEmpty()) name_param = args.value("search").toString();

    result.output["action"] = action;
    result.output["user_id"] = args.value("user_id").toInt(args.value("id").toInt());
    result.output["name"] = name_param;

    if (action == "get_by_id" || action == "id") {
        const int user_id = args.contains("user_id") ? args.value("user_id").toInt() : args.value("id").toInt();
        db::UserInfo user;
        if (!user_service_->get_user(user_id, user)) {
            result.ok = false;
            result.error = QString("未找到ID为 %1 的用户").arg(user_id);
            result.display_text = result.error;
            result.output["error"] = result.error;
            return result;
        }

        const auto user_json = user_to_json(user, user_service_->get_feature_count(user.user_id));
        result.ok = true;
        result.output["mode"] = "single_user";
        result.output["user"] = user_json;
        result.display_text = format_user_text(user_json);
        return result;
    }

    if (action == "get_by_name" || action == "search" || action == "find") {
        if (name_param.isEmpty()) {
            result.ok = false;
            result.error = "错误: 缺少 name/keyword 参数";
            result.display_text = result.error;
            result.output["error"] = result.error;
            return result;
        }

        QJsonArray users_json;
        auto users = user_service_->get_all_users();
        for (const auto& user : users) {
            if (QString::fromStdString(user.user_name).contains(name_param, Qt::CaseInsensitive)) {
                users_json.append(user_to_json(user, user_service_->get_feature_count(user.user_id)));
            }
        }

        result.ok = true;
        result.output["mode"] = "search_users";
        result.output["users"] = users_json;
        result.display_text = users_json.isEmpty()
            ? QString("未找到姓名包含 '%1' 的用户").arg(name_param)
            : format_users_list_text(QString("找到 %1 个匹配的用户:").arg(users_json.size()), users_json);
        return result;
    }

    if (action == "list_all" || action == "list" || action == "all") {
        QJsonArray users_json;
        auto users = user_service_->get_all_users();
        for (const auto& user : users) {
            users_json.append(user_to_json(user, user_service_->get_feature_count(user.user_id)));
        }

        result.ok = true;
        result.output["mode"] = "list_all";
        result.output["users"] = users_json;
        result.display_text = users_json.isEmpty()
            ? "系统中暂无注册用户"
            : format_users_list_text(QString("共有 %1 个注册用户:").arg(users_json.size()), users_json);
        return result;
    }

    if (action == "stats" || action == "count" || action == "统计") {
        auto all_users = user_service_->get_all_users(-1);
        auto active_users = user_service_->get_all_users(1);
        QJsonObject departments;
        std::map<std::string, int> dept_count;
        for (const auto& user : active_users) {
            const std::string dept = user.department.empty() ? "未分配" : user.department;
            dept_count[dept]++;
        }
        for (const auto& pair : dept_count) {
            departments[QString::fromStdString(pair.first)] = pair.second;
        }

        result.ok = true;
        result.output["mode"] = "stats";
        result.output["total"] = static_cast<int>(all_users.size());
        result.output["active"] = static_cast<int>(active_users.size());
        result.output["inactive"] = static_cast<int>(all_users.size() - active_users.size());
        result.output["departments"] = departments;

        QString text = QString(
            "用户统计信息:\n"
            "- 总用户数: %1\n"
            "- 启用用户: %2\n"
            "- 禁用用户: %3\n"
            "\n部门分布:"
        ).arg(result.output.value("total").toInt())
         .arg(result.output.value("active").toInt())
         .arg(result.output.value("inactive").toInt());
        for (auto it = departments.begin(); it != departments.end(); ++it) {
            text += QString("\n- %1: %2 人").arg(it.key()).arg(it.value().toInt());
        }
        result.display_text = text;
        return result;
    }

    result.ok = false;
    result.error = QString("错误: 无效的操作 '%1'，支持 get_by_id/get_by_name/list_all/stats").arg(action);
    result.display_text = result.error;
    result.output["error"] = result.error;
    return result;
}

}  // namespace agent

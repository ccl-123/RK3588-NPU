/**
 * @file user_tool.cc
 * @brief 用户查询工具实现
 */

#include "tools/user_tool.h"
#include <map>
#include <QJsonArray>
#include <spdlog/spdlog.h>

namespace agent {

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

QString UserTool::execute(const QJsonObject& args) {
    if (!user_service_) {
        return "错误: 用户服务未初始化";
    }

    // 兼容多种参数名: action, query_type, type
    QString action = args["action"].toString();
    if (action.isEmpty()) {
        action = args["query_type"].toString();
    }
    if (action.isEmpty()) {
        action = args["type"].toString();
    }
    if (action.isEmpty()) {
        action = "stats";  // 默认返回统计
    }

    // 标准化参数值
    action = action.toLower().trimmed();

    // 兼容多种参数名: name, keyword, search
    QString name_param = args["name"].toString();
    if (name_param.isEmpty()) {
        name_param = args["keyword"].toString();
    }
    if (name_param.isEmpty()) {
        name_param = args["search"].toString();
    }

    if (action == "get_by_id" || action == "id") {
        if (!args.contains("user_id") && !args.contains("id")) {
            return "错误: 缺少 user_id 参数";
        }
        int id = args.contains("user_id") ? args["user_id"].toInt() : args["id"].toInt();
        return queryById(id);
    } else if (action == "get_by_name" || action == "search" || action == "find") {
        if (name_param.isEmpty()) {
            return "错误: 缺少 name/keyword 参数";
        }
        return queryByName(name_param);
    } else if (action == "list_all" || action == "list" || action == "all") {
        return queryAll();
    } else if (action == "stats" || action == "count" || action == "统计") {
        return queryStats();
    }

    return QString("错误: 无效的操作 '%1'，支持 get_by_id/get_by_name/list_all/stats").arg(action);
}

QString UserTool::queryById(int user_id) {
    spdlog::debug("Querying user by ID: {}", user_id);

    db::UserInfo user;
    if (!user_service_->get_user(user_id, user)) {
        return QString("未找到ID为 %1 的用户").arg(user_id);
    }

    return formatUser(user);
}

QString UserTool::queryByName(const QString& name) {
    spdlog::debug("Querying user by name: {}", name.toStdString());

    auto users = user_service_->get_all_users();
    std::vector<db::UserInfo> matched;

    for (const auto& user : users) {
        if (QString::fromStdString(user.user_name).contains(name, Qt::CaseInsensitive)) {
            matched.push_back(user);
        }
    }

    if (matched.empty()) {
        return QString("未找到姓名包含 '%1' 的用户").arg(name);
    }

    QString result = QString("找到 %1 个匹配的用户:\n").arg(matched.size());
    for (const auto& user : matched) {
        result += "\n" + formatUser(user) + "\n";
    }

    return result;
}

QString UserTool::queryAll() {
    spdlog::debug("Querying all users");

    auto users = user_service_->get_all_users();

    if (users.empty()) {
        return "系统中暂无注册用户";
    }

    QString result = QString("共有 %1 个注册用户:\n").arg(users.size());
    for (const auto& user : users) {
        result += QString("\n- %1 (ID: %2, 部门: %3)")
            .arg(QString::fromStdString(user.user_name))
            .arg(user.user_id)
            .arg(QString::fromStdString(user.department.empty() ? "未设置" : user.department));
    }

    return result;
}

QString UserTool::queryStats() {
    spdlog::debug("Querying user stats");

    auto all_users = user_service_->get_all_users(-1);  // 所有状态
    auto active_users = user_service_->get_all_users(1);  // 仅启用的

    int total = all_users.size();
    int active = active_users.size();
    int inactive = total - active;

    // 统计部门分布
    std::map<std::string, int> dept_count;
    for (const auto& user : active_users) {
        std::string dept = user.department.empty() ? "未分配" : user.department;
        dept_count[dept]++;
    }

    QString result = QString(
        "用户统计信息:\n"
        "- 总用户数: %1\n"
        "- 启用用户: %2\n"
        "- 禁用用户: %3\n"
        "\n部门分布:"
    ).arg(total).arg(active).arg(inactive);

    for (const auto& pair : dept_count) {
        result += QString("\n- %1: %2 人")
            .arg(QString::fromStdString(pair.first))
            .arg(pair.second);
    }

    return result;
}

QString UserTool::formatUser(const db::UserInfo& user) {
    QString status = user.status == 1 ? "启用" : "禁用";
    int feature_count = user_service_->get_feature_count(user.user_id);

    return QString(
        "用户信息:\n"
        "- ID: %1\n"
        "- 姓名: %2\n"
        "- 工号: %3\n"
        "- 部门: %4\n"
        "- 职位: %5\n"
        "- 状态: %6\n"
        "- 人脸特征数: %7"
    ).arg(user.user_id)
     .arg(QString::fromStdString(user.user_name))
     .arg(QString::fromStdString(user.employee_id.empty() ? "未设置" : user.employee_id))
     .arg(QString::fromStdString(user.department.empty() ? "未设置" : user.department))
     .arg(QString::fromStdString(user.position.empty() ? "未设置" : user.position))
     .arg(status)
     .arg(feature_count);
}

} // namespace agent

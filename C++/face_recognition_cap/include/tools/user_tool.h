/**
 * @file user_tool.h
 * @brief 用户查询工具 - Agent 可调用的用户信息查询工具
 */

#pragma once

#include "tools/base_tool.h"
#include "service/user_service.h"

namespace agent {

/**
 * @brief 用户查询工具
 *
 * 支持查询:
 * - 按用户ID查询
 * - 按姓名查询
 * - 查询所有用户
 * - 查询用户统计信息
 */
class UserTool : public BaseTool {
public:
    /**
     * @brief 构造函数
     * @param service 用户服务（不转移所有权）
     */
    explicit UserTool(service::UserService* service);

    QString name() const override { return "query_user"; }

    QString description() const override {
        return "查询员工信息，支持按ID、姓名查询单个用户，或获取所有用户列表和统计信息";
    }

    QJsonObject parametersSchema() const override;

    QString execute(const QJsonObject& args) override;

private:
    service::UserService* user_service_;

    /**
     * @brief 按ID查询用户
     * @param user_id 用户ID
     * @return 查询结果
     */
    QString queryById(int user_id);

    /**
     * @brief 按姓名查询用户
     * @param name 用户姓名
     * @return 查询结果
     */
    QString queryByName(const QString& name);

    /**
     * @brief 查询所有用户
     * @return 用户列表
     */
    QString queryAll();

    /**
     * @brief 查询用户统计
     * @return 统计信息
     */
    QString queryStats();

    /**
     * @brief 格式化用户信息
     * @param user 用户信息
     * @return 格式化的字符串
     */
    QString formatUser(const db::UserInfo& user);
};

} // namespace agent

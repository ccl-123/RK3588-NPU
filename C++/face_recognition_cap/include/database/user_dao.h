/**
 * @file user_dao.h
 * @brief 用户数据访问对象
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _USER_DAO_H_
#define _USER_DAO_H_

#include "database_manager.h"
#include "database_types.h"
#include <vector>
#include <memory>

namespace db {

/**
 * @brief 用户数据访问类
 */
class UserDAO {
public:
    UserDAO(DatabaseManager* db_manager);
    ~UserDAO();
    
    /**
     * @brief 插入新用户
     * @param user 用户信息
     * @return 新用户ID, -1表示失败
     */
    int insert(const UserInfo& user);
    
    /**
     * @brief 更新用户信息
     * @param user 用户信息
     * @return true成功, false失败
     */
    bool update(const UserInfo& user);
    
    /**
     * @brief 删除用户
     * @param user_id 用户ID
     * @return true成功, false失败
     */
    bool remove(int user_id);
    
    /**
     * @brief 根据ID查询用户
     * @param user_id 用户ID
     * @param user 输出用户信息
     * @return true成功, false失败
     */
    bool find_by_id(int user_id, UserInfo& user);
    
    /**
     * @brief 根据姓名查询用户
     * @param user_name 用户姓名
     * @param user 输出用户信息
     * @return true成功, false失败
     */
    bool find_by_name(const std::string& user_name, UserInfo& user);
    
    /**
     * @brief 根据工号查询用户
     * @param employee_id 工号
     * @param user 输出用户信息
     * @return true成功, false失败
     */
    bool find_by_employee_id(const std::string& employee_id, UserInfo& user);
    
    /**
     * @brief 查询所有用户
     * @param status 状态过滤(-1表示全部, 0=禁用, 1=启用)
     * @return 用户列表
     */
    std::vector<UserInfo> find_all(int status = -1);
    
    /**
     * @brief 根据部门查询用户
     * @param department 部门名称
     * @return 用户列表
     */
    std::vector<UserInfo> find_by_department(const std::string& department);
    
    /**
     * @brief 检查用户名是否存在
     * @param user_name 用户姓名
     * @return true存在, false不存在
     */
    bool exists_by_name(const std::string& user_name);
    
    /**
     * @brief 获取用户总数
     * @param status 状态过滤(-1表示全部)
     * @return 用户数量
     */
    int count(int status = -1);
    
    /**
     * @brief 更新用户状态
     * @param user_id 用户ID
     * @param status 新状态
     * @return true成功, false失败
     */
    bool update_status(int user_id, int status);
    
private:
    /**
     * @brief 从结果集填充用户信息
     */
    void fill_user_from_stmt(PreparedStatement* stmt, UserInfo& user);
    
private:
    DatabaseManager* db_manager_;
};

} // namespace db

#endif // _USER_DAO_H_


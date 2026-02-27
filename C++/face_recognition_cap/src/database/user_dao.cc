/**
 * @file user_dao.cc
 * @brief 用户数据访问对象实现
 * @author CL
 * @date 2025-11-20
 */

#include "database/user_dao.h"
#include <spdlog/spdlog.h>

namespace db {

UserDAO::UserDAO(DatabaseManager* db_manager) 
    : db_manager_(db_manager) {
}

UserDAO::~UserDAO() {
}

int UserDAO::insert(const UserInfo& user) {
    std::string sql = R"(
        INSERT INTO users (user_name, employee_id, department, position, phone, email, photo_path, status)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return -1;

    stmt->bind_string(1, user.user_name);
    // 对于 UNIQUE 字段，空字符串应该绑定为 NULL
    if (user.employee_id.empty()) {
        stmt->bind_null(2);
    } else {
        stmt->bind_string(2, user.employee_id);
    }
    stmt->bind_string(3, user.department);
    stmt->bind_string(4, user.position);
    stmt->bind_string(5, user.phone);
    stmt->bind_string(6, user.email);
    stmt->bind_string(7, user.photo_path);
    stmt->bind_int(8, user.status);
    
    if (!stmt->execute()) {
        spdlog::error("Failed to insert user: {}", user.user_name);
        spdlog::error("Database error: {}", db_manager_->get_last_error());
        return -1;
    }

    int64_t id = stmt->last_insert_id();
    spdlog::info("User inserted with ID: {}", id);
    return static_cast<int>(id);
}

bool UserDAO::update(const UserInfo& user) {
    std::string sql = R"(
        UPDATE users 
        SET user_name = ?, employee_id = ?, department = ?, position = ?,
            phone = ?, email = ?, photo_path = ?, status = ?,
            updated_at = CURRENT_TIMESTAMP
        WHERE user_id = ?
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_string(1, user.user_name);
    if (user.employee_id.empty()) {
        stmt->bind_null(2);
    } else {
        stmt->bind_string(2, user.employee_id);
    }
    stmt->bind_string(3, user.department);
    stmt->bind_string(4, user.position);
    stmt->bind_string(5, user.phone);
    stmt->bind_string(6, user.email);
    stmt->bind_string(7, user.photo_path);
    stmt->bind_int(8, user.status);
    stmt->bind_int(9, user.user_id);
    
    return stmt->execute();
}

bool UserDAO::remove(int user_id) {
    std::string sql = "DELETE FROM users WHERE user_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, user_id);
    return stmt->execute();
}

bool UserDAO::find_by_id(int user_id, UserInfo& user) {
    std::string sql = "SELECT * FROM users WHERE user_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, user_id);
    
    if (stmt->step()) {
        fill_user_from_stmt(stmt.get(), user);
        return true;
    }
    
    return false;
}

bool UserDAO::find_by_name(const std::string& user_name, UserInfo& user) {
    std::string sql = "SELECT * FROM users WHERE user_name = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_string(1, user_name);
    
    if (stmt->step()) {
        fill_user_from_stmt(stmt.get(), user);
        return true;
    }
    
    return false;
}

bool UserDAO::find_by_employee_id(const std::string& employee_id, UserInfo& user) {
    std::string sql = "SELECT * FROM users WHERE employee_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_string(1, employee_id);
    
    if (stmt->step()) {
        fill_user_from_stmt(stmt.get(), user);
        return true;
    }
    
    return false;
}

std::vector<UserInfo> UserDAO::find_all(int status) {
    std::vector<UserInfo> users;
    
    std::string sql = "SELECT * FROM users";
    if (status >= 0) {
        sql += " WHERE status = ?";
    }
    sql += " ORDER BY user_id";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return users;
    
    if (status >= 0) {
        stmt->bind_int(1, status);
    }
    
    while (stmt->step()) {
        UserInfo user;
        fill_user_from_stmt(stmt.get(), user);
        users.push_back(user);
    }
    
    return users;
}

std::vector<UserInfo> UserDAO::find_by_department(const std::string& department) {
    std::vector<UserInfo> users;
    
    std::string sql = "SELECT * FROM users WHERE department = ? ORDER BY user_id";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return users;
    
    stmt->bind_string(1, department);
    
    while (stmt->step()) {
        UserInfo user;
        fill_user_from_stmt(stmt.get(), user);
        users.push_back(user);
    }
    
    return users;
}

bool UserDAO::exists_by_name(const std::string& user_name) {
    std::string sql = "SELECT COUNT(*) FROM users WHERE user_name = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_string(1, user_name);
    
    if (stmt->step()) {
        return stmt->get_column_int(0) > 0;
    }
    
    return false;
}

int UserDAO::count(int status) {
    std::string sql = "SELECT COUNT(*) FROM users";
    if (status >= 0) {
        sql += " WHERE status = ?";
    }
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;
    
    if (status >= 0) {
        stmt->bind_int(1, status);
    }
    
    if (stmt->step()) {
        return stmt->get_column_int(0);
    }
    
    return 0;
}

bool UserDAO::update_status(int user_id, int status) {
    std::string sql = "UPDATE users SET status = ?, updated_at = CURRENT_TIMESTAMP WHERE user_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, status);
    stmt->bind_int(2, user_id);
    
    return stmt->execute();
}

void UserDAO::fill_user_from_stmt(PreparedStatement* stmt, UserInfo& user) {
    user.user_id = stmt->get_column_int(0);
    user.user_name = stmt->get_column_string(1);
    user.employee_id = stmt->get_column_string(2);
    user.department = stmt->get_column_string(3);
    user.position = stmt->get_column_string(4);
    user.phone = stmt->get_column_string(5);
    user.email = stmt->get_column_string(6);
    user.photo_path = stmt->get_column_string(7);
    user.status = stmt->get_column_int(8);
    // created_at 和 updated_at 可以根据需要解析
}

} // namespace db

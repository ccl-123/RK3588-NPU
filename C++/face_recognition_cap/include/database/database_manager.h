/**
 * @file database_manager.h
 * @brief 数据库连接管理器 - 单例模式 + 共享连接串行访问
 * @author CL
 * @date 2025-11-20
 */

#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <sqlite3.h>
#include "database_types.h"

namespace db {

/**
 * @brief SQLite预处理语句封装
 */
class PreparedStatement {
public:
    PreparedStatement(sqlite3* db, std::recursive_mutex& db_mutex, const std::string& sql);
    ~PreparedStatement();
    
    // 绑定参数
    bool bind_int(int index, int value);
    bool bind_int64(int index, int64_t value);
    bool bind_double(int index, double value);
    bool bind_string(int index, const std::string& value);
    bool bind_blob(int index, const void* data, int size);
    bool bind_null(int index);
    
    // 执行
    bool execute();
    bool step();
    void reset();
    
    // 获取结果
    int get_column_int(int index);
    int64_t get_column_int64(int index);
    double get_column_double(int index);
    std::string get_column_string(int index);
    const void* get_column_blob(int index, int& size);
    
    // 获取最后插入的ID
    int64_t last_insert_id();
    
private:
    sqlite3* db_;
    std::recursive_mutex* db_mutex_;
    sqlite3_stmt* stmt_;
    bool prepared_;
};

/**
 * @brief 数据库管理器 - 单例模式（Meyer's Singleton）
 *
 * 使用静态局部变量实现单例，线程安全且自动释放资源
 */
class DatabaseManager {
public:
    // 获取单例实例（Meyer's Singleton，静态局部变量，线程安全）
    static DatabaseManager& instance();

    // 为了兼容旧代码，提供返回指针的版本
    static DatabaseManager* instance_ptr() { return &instance(); }

    // 初始化数据库
    bool initialize(const std::string& db_path);

    // 关闭数据库
    void close();

    // 检查是否已初始化
    bool is_initialized() const { return db_ != nullptr; }

    // 创建预处理语句
    std::shared_ptr<PreparedStatement> prepare(const std::string& sql);

    // 执行SQL语句(无返回结果)
    bool execute(const std::string& sql);

    // 执行查询(返回单个整数)
    int execute_scalar_int(const std::string& sql);

    // 事务控制
    bool begin_transaction();
    bool commit();
    bool rollback();

    // 获取最后的错误信息
    std::string get_last_error() const;

    // 获取原始数据库连接(谨慎使用)
    sqlite3* get_db() { return db_; }

private:
    DatabaseManager();
    ~DatabaseManager();

    // 禁止拷贝和移动
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    DatabaseManager(DatabaseManager&&) = delete;
    DatabaseManager& operator=(DatabaseManager&&) = delete;

    // 初始化表结构
    bool init_tables();

private:
    sqlite3* db_;
    std::recursive_mutex db_mutex_;  // 使用递归锁，允许同一线程多次获取
    std::string db_path_;
    std::string last_error_;
};

} // namespace db

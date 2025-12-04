/**
 * @file database_manager.cc
 * @brief 数据库管理器实现
 * @author CL
 * @date 2025-11-20
 */

#include "database/database_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace db {

// ============================================
// PreparedStatement 实现
// ============================================

PreparedStatement::PreparedStatement(sqlite3* db, const std::string& sql)
    : db_(db), stmt_(nullptr), prepared_(false) {
    
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db_) << std::endl;
        std::cerr << "SQL: " << sql << std::endl;
    } else {
        prepared_ = true;
    }
}

PreparedStatement::~PreparedStatement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
    }
}

bool PreparedStatement::bind_int(int index, int value) {
    if (!prepared_) return false;
    return sqlite3_bind_int(stmt_, index, value) == SQLITE_OK;
}

bool PreparedStatement::bind_int64(int index, int64_t value) {
    if (!prepared_) return false;
    return sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK;
}

bool PreparedStatement::bind_double(int index, double value) {
    if (!prepared_) return false;
    return sqlite3_bind_double(stmt_, index, value) == SQLITE_OK;
}

bool PreparedStatement::bind_string(int index, const std::string& value) {
    if (!prepared_) return false;
    return sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool PreparedStatement::bind_blob(int index, const void* data, int size) {
    if (!prepared_) return false;
    return sqlite3_bind_blob(stmt_, index, data, size, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool PreparedStatement::bind_null(int index) {
    if (!prepared_) return false;
    return sqlite3_bind_null(stmt_, index) == SQLITE_OK;
}

bool PreparedStatement::execute() {
    if (!prepared_) return false;
    int rc = sqlite3_step(stmt_);
    if (rc != SQLITE_DONE) {
        std::cerr << "Execute failed: " << sqlite3_errmsg(db_) << " (code: " << rc << ")" << std::endl;
    }
    sqlite3_reset(stmt_);
    return rc == SQLITE_DONE;
}

bool PreparedStatement::step() {
    if (!prepared_) return false;
    int rc = sqlite3_step(stmt_);
    return rc == SQLITE_ROW;
}

void PreparedStatement::reset() {
    if (stmt_) {
        sqlite3_reset(stmt_);
    }
}

int PreparedStatement::get_column_int(int index) {
    return sqlite3_column_int(stmt_, index);
}

int64_t PreparedStatement::get_column_int64(int index) {
    return sqlite3_column_int64(stmt_, index);
}

double PreparedStatement::get_column_double(int index) {
    return sqlite3_column_double(stmt_, index);
}

std::string PreparedStatement::get_column_string(int index) {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    return text ? std::string(reinterpret_cast<const char*>(text)) : "";
}

const void* PreparedStatement::get_column_blob(int index, int& size) {
    size = sqlite3_column_bytes(stmt_, index);
    return sqlite3_column_blob(stmt_, index);
}

int64_t PreparedStatement::last_insert_id() {
    return sqlite3_last_insert_rowid(db_);
}

// ============================================
// DatabaseManager 实现
// ============================================

DatabaseManager::DatabaseManager() : db_(nullptr) {
}

DatabaseManager::~DatabaseManager() {
    close();
}

DatabaseManager& DatabaseManager::instance() {
    // Meyer's Singleton: 静态局部变量，线程安全（C++11保证）
    // 程序退出时自动调用析构函数释放资源
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::initialize(const std::string& db_path) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);

    if (db_ != nullptr) {
        // 如果已经初始化且路径相同，返回成功
        if (db_path_ == db_path) {
            std::cout << "Database already initialized with same path" << std::endl;
            return true;
        }
        // 如果路径不同，先关闭旧连接
        std::cout << "Database already initialized with different path, closing old connection" << std::endl;
        close();
    }
    
    db_path_ = db_path;
    
    // 打开数据库
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        std::cerr << "Failed to open database: " << last_error_ << std::endl;
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    
    std::cout << "Database opened: " << db_path << std::endl;
    
    // 启用外键约束
    execute("PRAGMA foreign_keys = ON;");
    
    // 初始化表结构
    if (!init_tables()) {
        std::cerr << "Failed to initialize tables" << std::endl;
        close();
        return false;
    }
    
    std::cout << "Database initialized successfully" << std::endl;
    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        std::cout << "Database closed" << std::endl;
    }
}

std::shared_ptr<PreparedStatement> DatabaseManager::prepare(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    
    if (!db_) {
        std::cerr << "Database not initialized" << std::endl;
        return nullptr;
    }
    
    return std::make_shared<PreparedStatement>(db_, sql);
}

bool DatabaseManager::execute(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lock(db_mutex_);
    
    if (!db_) {
        std::cerr << "Database not initialized" << std::endl;
        return false;
    }
    
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        last_error_ = err_msg ? err_msg : "Unknown error";
        std::cerr << "SQL execution failed: " << last_error_ << std::endl;
        std::cerr << "SQL: " << sql << std::endl;
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

int DatabaseManager::execute_scalar_int(const std::string& sql) {
    auto stmt = prepare(sql);
    if (!stmt || !stmt->step()) {
        return 0;
    }
    return stmt->get_column_int(0);
}

bool DatabaseManager::begin_transaction() {
    return execute("BEGIN TRANSACTION;");
}

bool DatabaseManager::commit() {
    return execute("COMMIT;");
}

bool DatabaseManager::rollback() {
    return execute("ROLLBACK;");
}

std::string DatabaseManager::get_last_error() const {
    return last_error_;
}

bool DatabaseManager::init_tables() {
    // 手动创建表结构
    std::vector<std::string> create_sqls = {
        // 用户表
        R"(CREATE TABLE IF NOT EXISTS users (
            user_id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_name VARCHAR(50) NOT NULL UNIQUE,
            employee_id VARCHAR(50) UNIQUE,
            department VARCHAR(100),
            position VARCHAR(100),
            phone VARCHAR(20),
            email VARCHAR(100),
            photo_path VARCHAR(255),
            status INTEGER DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        ))",

        "CREATE INDEX IF NOT EXISTS idx_user_name ON users(user_name)",
        "CREATE INDEX IF NOT EXISTS idx_employee_id ON users(employee_id)",
        "CREATE INDEX IF NOT EXISTS idx_status ON users(status)",

        // 特征表
        R"(CREATE TABLE IF NOT EXISTS face_features (
            feature_id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            feature_vector BLOB NOT NULL,
            feature_quality REAL DEFAULT 0.0,
            source_image VARCHAR(255),
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
        ))",

        "CREATE INDEX IF NOT EXISTS idx_feature_user_id ON face_features(user_id)",

        // 考勤记录表
        R"(CREATE TABLE IF NOT EXISTS attendance_records (
            record_id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            user_name VARCHAR(50) NOT NULL,
            check_time DATETIME NOT NULL,
            check_type INTEGER DEFAULT 1,
            similarity REAL,
            face_image VARCHAR(255),
            device_id VARCHAR(50),
            location VARCHAR(100),
            status INTEGER DEFAULT 1,
            remark TEXT,
            FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
        ))",

        "CREATE INDEX IF NOT EXISTS idx_attendance_user_id ON attendance_records(user_id)",
        "CREATE INDEX IF NOT EXISTS idx_attendance_check_time ON attendance_records(check_time)",
        "CREATE INDEX IF NOT EXISTS idx_attendance_date ON attendance_records(DATE(check_time))",

        // 考勤规则表
        R"(CREATE TABLE IF NOT EXISTS attendance_rules (
            rule_id INTEGER PRIMARY KEY AUTOINCREMENT,
            rule_name VARCHAR(100) NOT NULL,
            work_start_time TEXT NOT NULL,
            work_end_time TEXT NOT NULL,
            late_threshold INTEGER DEFAULT 15,
            early_threshold INTEGER DEFAULT 30,
            duplicate_interval INTEGER DEFAULT 300,
            is_active INTEGER DEFAULT 1,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        ))",

        // 系统日志表
        R"(CREATE TABLE IF NOT EXISTS system_logs (
            log_id INTEGER PRIMARY KEY AUTOINCREMENT,
            log_level INTEGER,
            log_type VARCHAR(50),
            message TEXT,
            user_id INTEGER,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        ))",

        "CREATE INDEX IF NOT EXISTS idx_log_created_at ON system_logs(created_at)",
        "CREATE INDEX IF NOT EXISTS idx_log_type ON system_logs(log_type)"
    };

    // 执行所有建表语句
    for (const auto& sql : create_sqls) {
        if (!execute(sql)) {
            return false;
        }
    }

    // 插入默认考勤规则(如果不存在)
    // 注意: 这里不能使用 execute_scalar_int，因为会导致死锁
    // 直接使用 sqlite3_prepare_v2 和 sqlite3_step
    std::string check_rule = "SELECT COUNT(*) FROM attendance_rules WHERE is_active = 1";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, check_rule.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);

        if (count == 0) {
            std::string insert_rule = R"(
                INSERT INTO attendance_rules
                (rule_name, work_start_time, work_end_time, late_threshold, early_threshold, duplicate_interval, is_active)
                VALUES ('默认规则', '09:00:00', '18:00:00', 15, 30, 300, 1)
            )";
            execute(insert_rule);
        }
    } else {
        if (stmt) sqlite3_finalize(stmt);
    }

    return true;
}

} // namespace db


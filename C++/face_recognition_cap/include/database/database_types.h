/**
 * @file database_types.h
 * @brief 数据库相关数据类型定义
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _DATABASE_TYPES_H_
#define _DATABASE_TYPES_H_

#include <string>
#include <vector>
#include <ctime>
#include <memory>

namespace db {

/**
 * @brief 用户信息结构
 */
struct UserInfo {
    int user_id;
    std::string user_name;
    std::string employee_id;
    std::string department;
    std::string position;
    std::string phone;
    std::string email;
    std::string photo_path;
    int status;  // 1=启用, 0=禁用
    std::time_t created_at;
    std::time_t updated_at;
    
    UserInfo() : user_id(0), status(1), created_at(0), updated_at(0) {}
};

/**
 * @brief 人脸特征结构
 */
struct FaceFeature {
    int feature_id;
    int user_id;
    std::vector<float> feature_vector;  // 512维特征向量
    float feature_quality;
    std::string source_image;
    std::time_t created_at;
    
    FaceFeature() : feature_id(0), user_id(0), feature_quality(0.0f), created_at(0) {
        feature_vector.resize(512, 0.0f);
    }
};

/**
 * @brief 考勤记录结构
 */
struct AttendanceRecord {
    int record_id;
    int user_id;
    std::string user_name;
    std::time_t check_time;
    int check_type;      // 1=签到, 2=签退
    float similarity;
    std::string face_image;
    std::string device_id;
    std::string location;
    int status;          // 1=正常, 2=迟到, 3=早退
    std::string remark;
    
    AttendanceRecord() 
        : record_id(0), user_id(0), check_time(0), 
          check_type(1), similarity(0.0f), status(1) {}
};

/**
 * @brief 考勤规则结构
 */
struct AttendanceRule {
    int rule_id;
    std::string rule_name;
    std::string work_start_time;  // HH:MM:SS
    std::string work_end_time;    // HH:MM:SS
    int late_threshold;           // 迟到阈值(分钟)
    int early_threshold;          // 早退阈值(分钟)
    int duplicate_interval;       // 重复打卡间隔(秒)
    int is_active;
    std::time_t created_at;
    
    AttendanceRule()
        : rule_id(0), late_threshold(15), early_threshold(30),
          duplicate_interval(300), is_active(1), created_at(0) {}
};

/**
 * @brief 系统日志结构
 */
struct SystemLog {
    int log_id;
    int log_level;    // 1=INFO, 2=WARN, 3=ERROR
    std::string log_type;
    std::string message;
    int user_id;
    std::time_t created_at;
    
    SystemLog() : log_id(0), log_level(1), user_id(0), created_at(0) {}
};

/**
 * @brief 日志级别枚举
 */
enum LogLevel {
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

/**
 * @brief 考勤类型枚举
 */
enum CheckType {
    CHECK_IN = 1,   // 签到
    CHECK_OUT = 2   // 签退
};

/**
 * @brief 考勤状态枚举
 */
enum AttendanceStatus {
    STATUS_NORMAL = 1,      // 正常
    STATUS_LATE = 2,        // 迟到
    STATUS_EARLY_LEAVE = 3  // 早退
};

/**
 * @brief 用户状态枚举
 */
enum UserStatus {
    USER_DISABLED = 0,  // 禁用
    USER_ENABLED = 1    // 启用
};

} // namespace db

#endif // _DATABASE_TYPES_H_


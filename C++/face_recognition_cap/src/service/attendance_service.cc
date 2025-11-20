/**
 * @file attendance_service.cc
 * @brief 考勤业务服务实现
 * @author CL
 * @date 2025-11-20
 */

#include "service/attendance_service.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace service {

AttendanceService::AttendanceService(db::DatabaseManager* db_manager)
    : db_manager_(db_manager)
    , check_in_start_hour_(8)
    , check_in_end_hour_(9)
    , check_out_start_hour_(17)
    , check_out_end_hour_(18)
{
    record_dao_ = new db::AttendanceRecordDAO(db_manager_);
    user_dao_ = new db::UserDAO(db_manager_);
}

AttendanceService::~AttendanceService() {
    delete record_dao_;
    delete user_dao_;
}

int AttendanceService::record_attendance(int user_id, const std::string& user_name,
                                        float similarity, const std::string& face_image_path,
                                        int check_type) {
    // 检查重复打卡
    if (is_duplicate_check(user_id, 300)) {
        std::cout << "Duplicate check within 5 minutes, ignored." << std::endl;
        return -1;
    }
    
    // 创建考勤记录
    db::AttendanceRecord record;
    record.user_id = user_id;
    record.user_name = user_name;
    record.check_time = std::time(nullptr);
    record.check_type = check_type;
    record.similarity = similarity;
    record.face_image = face_image_path;
    record.device_id = "device_001";
    record.location = "Main Entrance";
    
    // 判断考勤状态
    record.status = determine_status(record.check_time, check_type);
    
    // 保存到数据库
    int record_id = record_dao_->insert(record);
    
    if (record_id > 0) {
        std::cout << "Attendance recorded: " << user_name 
                  << " (ID:" << user_id << ") "
                  << "Type:" << check_type 
                  << " Status:" << record.status << std::endl;
    }
    
    return record_id;
}

bool AttendanceService::is_duplicate_check(int user_id, int interval_seconds) {
    return record_dao_->has_recent_record(user_id, interval_seconds);
}

int AttendanceService::determine_status(std::time_t check_time, int check_type) {
    std::tm* tm_info = std::localtime(&check_time);
    int hour = tm_info->tm_hour;
    int minute = tm_info->tm_min;
    
    if (check_type == db::CheckType::CHECK_IN) {
        // 签到逻辑
        if (hour < check_in_start_hour_) {
            return db::AttendanceStatus::STATUS_NORMAL;  // 早到也算正常
        } else if (hour < check_in_end_hour_) {
            return db::AttendanceStatus::STATUS_NORMAL;
        } else {
            return db::AttendanceStatus::STATUS_LATE;    // 迟到
        }
    } else if (check_type == db::CheckType::CHECK_OUT) {
        // 签退逻辑
        if (hour < check_out_start_hour_) {
            return db::AttendanceStatus::STATUS_EARLY_LEAVE;  // 早退
        } else {
            return db::AttendanceStatus::STATUS_NORMAL;
        }
    }
    
    return db::AttendanceStatus::STATUS_NORMAL;
}

std::vector<db::AttendanceRecord> AttendanceService::query_user_records(
    int user_id, const std::string& start_date, const std::string& end_date) {
    
    std::time_t start_time = parse_time(start_date + " 00:00:00");
    std::time_t end_time = parse_time(end_date + " 23:59:59");
    
    return record_dao_->find_by_user_id(user_id, start_time, end_time);
}

std::vector<db::AttendanceRecord> AttendanceService::query_records_by_date(
    const std::string& date) {
    return record_dao_->find_by_date(date);
}

AttendanceStatistics AttendanceService::get_statistics(const std::string& date) {
    AttendanceStatistics stats;
    stats.date = date;
    
    // 查询当天所有记录
    auto records = record_dao_->find_by_date(date);
    
    stats.total_count = records.size();
    stats.check_in_count = 0;
    stats.check_out_count = 0;
    stats.late_count = 0;
    stats.early_leave_count = 0;
    stats.normal_count = 0;
    
    for (const auto& record : records) {
        if (record.check_type == db::CheckType::CHECK_IN) {
            stats.check_in_count++;
        } else if (record.check_type == db::CheckType::CHECK_OUT) {
            stats.check_out_count++;
        }
        
        if (record.status == db::AttendanceStatus::STATUS_NORMAL) {
            stats.normal_count++;
        } else if (record.status == db::AttendanceStatus::STATUS_LATE) {
            stats.late_count++;
        } else if (record.status == db::AttendanceStatus::STATUS_EARLY_LEAVE) {
            stats.early_leave_count++;
        }
    }
    
    return stats;
}

std::vector<AttendanceStatistics> AttendanceService::get_monthly_statistics(
    const std::string& year_month) {
    
    std::vector<AttendanceStatistics> monthly_stats;
    
    // 解析年月
    int year, month;
    sscanf(year_month.c_str(), "%d-%d", &year, &month);
    
    // 计算该月的天数
    int days_in_month = 31;
    if (month == 2) {
        days_in_month = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
    } else if (month == 4 || month == 6 || month == 9 || month == 11) {
        days_in_month = 30;
    }
    
    // 统计每一天
    for (int day = 1; day <= days_in_month; day++) {
        std::ostringstream oss;
        oss << year << "-" << std::setfill('0') << std::setw(2) << month 
            << "-" << std::setfill('0') << std::setw(2) << day;
        std::string date = oss.str();
        
        monthly_stats.push_back(get_statistics(date));
    }
    
    return monthly_stats;
}

bool AttendanceService::delete_record(int record_id) {
    return record_dao_->remove(record_id);
}

std::string AttendanceService::get_current_date() {
    std::time_t now = std::time(nullptr);
    std::tm* tm_info = std::localtime(&now);
    
    std::ostringstream oss;
    oss << (tm_info->tm_year + 1900) << "-"
        << std::setfill('0') << std::setw(2) << (tm_info->tm_mon + 1) << "-"
        << std::setfill('0') << std::setw(2) << tm_info->tm_mday;
    return oss.str();
}

std::string AttendanceService::get_current_time() {
    std::time_t now = std::time(nullptr);
    std::tm* tm_info = std::localtime(&now);
    
    std::ostringstream oss;
    oss << (tm_info->tm_year + 1900) << "-"
        << std::setfill('0') << std::setw(2) << (tm_info->tm_mon + 1) << "-"
        << std::setfill('0') << std::setw(2) << tm_info->tm_mday << " "
        << std::setfill('0') << std::setw(2) << tm_info->tm_hour << ":"
        << std::setfill('0') << std::setw(2) << tm_info->tm_min << ":"
        << std::setfill('0') << std::setw(2) << tm_info->tm_sec;
    return oss.str();
}

std::time_t AttendanceService::parse_time(const std::string& time_str) {
    std::tm tm_info = {};
    sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d",
           &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
           &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec);
    
    tm_info.tm_year -= 1900;
    tm_info.tm_mon -= 1;
    
    return std::mktime(&tm_info);
}

} // namespace service


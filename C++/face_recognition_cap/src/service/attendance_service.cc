/**
 * @file attendance_service.cc
 * @brief 考勤业务服务实现
 * @author CL
 * @date 2025-11-20
 */

#include "service/attendance_service.h"
#include <sstream>
#include <iomanip>
#include <spdlog/spdlog.h>

namespace service {

AttendanceService::AttendanceService(db::DatabaseManager* db_manager)
    : db_manager_(db_manager)
    , work_start_hour_(9)
    , work_start_minute_(0)
    , work_end_hour_(18)
    , work_end_minute_(0)
    , late_threshold_(30)
    , early_leave_threshold_(30)
    , allow_multiple_checkin_(false)
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
    std::time_t current_time = std::time(nullptr);
    
    // 如果 check_type 为默认值（1），自动判断是签到还是签退
    if (check_type == db::CheckType::CHECK_IN) {
        check_type = auto_determine_check_type(user_id, current_time);
    }
    
    // 检查是否允许打卡（基于多次签到开关）
    if (allow_multiple_checkin_) {
        // 启用多次签到：只检查短时间内重复（300秒），防止误触
        if (is_duplicate_check(user_id, 300)) {
            spdlog::debug("Duplicate check within 5 minutes, user_id: {}", user_id);
            return -1;
        }
    } else {
        // 禁用多次签到：今天同类型只能打卡一次
        if (has_today_check_record(user_id, check_type)) {
            const char* type_str = (check_type == db::CheckType::CHECK_IN) ? "签到" : "签退";
            spdlog::debug("Already {} today, user_id: {} (multiple check-in disabled)", type_str, user_id);
            return -1;
        }
        
        // 同时也检查短时间内重复（双重保护）
        if (is_duplicate_check(user_id, 300)) {
            spdlog::debug("Duplicate check within 5 minutes, user_id: {}", user_id);
            return -1;
        }
    }
    
    // 创建考勤记录
    db::AttendanceRecord record;
    record.user_id = user_id;
    record.user_name = user_name;
    record.check_time = current_time;
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
        const char* type_str = (check_type == db::CheckType::CHECK_IN) ? "签到" : "签退";
        const char* status_str = (record.status == db::AttendanceStatus::STATUS_NORMAL) ? "正常" :
                                (record.status == db::AttendanceStatus::STATUS_LATE) ? "迟到" : "早退";
        spdlog::info("Attendance recorded: {} (ID: {}, Type: {}, Status: {})", 
                     user_name, user_id, type_str, status_str);
    }
    
    return record_id;
}

bool AttendanceService::is_duplicate_check(int user_id, int interval_seconds) {
    return record_dao_->has_recent_record(user_id, interval_seconds);
}

bool AttendanceService::has_today_check_record(int user_id, int check_type) {
    // 查询今天的所有记录
    std::string today = get_current_date();
    auto today_records = record_dao_->find_by_date(today);
    
    // 检查该用户今天是否已有指定类型的打卡记录
    for (const auto& record : today_records) {
        if (record.user_id == user_id && record.check_type == check_type) {
            return true;  // 今天已有该类型的打卡
        }
    }
    
    return false;  // 今天还没有该类型的打卡
}

int AttendanceService::determine_status(std::time_t check_time, int check_type) {
    std::tm* tm_info = std::localtime(&check_time);
    int hour = tm_info->tm_hour;
    int minute = tm_info->tm_min;
    
    // 将打卡时间转换为分钟数（从 00:00 开始计算）
    int check_minutes = hour * 60 + minute;
    
    if (check_type == db::CheckType::CHECK_IN) {
        // 签到逻辑：基于上班时间 + 迟到阈值
        int work_start_minutes = work_start_hour_ * 60 + work_start_minute_;
        int late_limit_minutes = work_start_minutes + late_threshold_;
        
        if (check_minutes <= late_limit_minutes) {
            return db::AttendanceStatus::STATUS_NORMAL;  // 在迟到阈值内，正常
        } else {
            return db::AttendanceStatus::STATUS_LATE;    // 超过迟到阈值，迟到
        }
    } else if (check_type == db::CheckType::CHECK_OUT) {
        // 签退逻辑：基于下班时间 - 早退阈值
        int work_end_minutes = work_end_hour_ * 60 + work_end_minute_;
        int early_leave_limit_minutes = work_end_minutes - early_leave_threshold_;
        
        if (check_minutes < early_leave_limit_minutes) {
            return db::AttendanceStatus::STATUS_EARLY_LEAVE;  // 提前太多，早退
        } else {
            return db::AttendanceStatus::STATUS_NORMAL;       // 正常签退
        }
    }
    
    return db::AttendanceStatus::STATUS_NORMAL;
}

int AttendanceService::auto_determine_check_type(int user_id, std::time_t current_time) {
    // 查询今天是否已有签到/签退记录
    std::string today = get_current_date();
    auto today_records = record_dao_->find_by_date(today);
    
    // 检查该用户今天的打卡情况
    bool has_checked_in = false;
    bool has_checked_out = false;
    for (const auto& record : today_records) {
        if (record.user_id == user_id) {
            if (record.check_type == db::CheckType::CHECK_IN) {
                has_checked_in = true;
            } else if (record.check_type == db::CheckType::CHECK_OUT) {
                has_checked_out = true;
            }
        }
    }
    
    // 判断当前时间是上午还是下午
    std::tm* tm_info = std::localtime(&current_time);
    int hour = tm_info->tm_hour;
    int minute = tm_info->tm_min;
    int current_minutes = hour * 60 + minute;
    
    // 使用12:00（中午）作为固定分界点，更符合日常习惯
    // 12:00 之前是上午（签到），12:00 及之后是下午（签退）
    const int midday_minutes = 12 * 60;  // 12:00 = 720 分钟
    
    // 判断当前是上午时段还是下午时段
    bool is_afternoon = (current_minutes >= midday_minutes);
    
    // 智能判断（基于时间和打卡历史）：
    // 
    // 上午时段（中点之前）：
    //   - 无论是否签到过 → 签到（重复签到，会被拦截）
    // 
    // 下午时段（中点之后）：
    //   - 如果已签到 + 未签退 → 签退
    //   - 如果已签到 + 已签退 → 签退（重复签退，会被拦截）
    //   - 如果未签到（新用户/上午没来）→ 签退（直接下午打卡）
    
    if (is_afternoon) {
        // 下午时段：统一返回签退
        // 这样新注册用户下午首次打卡也会记录为签退
        return db::CheckType::CHECK_OUT;
    } else {
        // 上午时段：统一返回签到
        return db::CheckType::CHECK_IN;
    }
}

void AttendanceService::set_work_schedule(const std::string& work_start_time,
                                         const std::string& work_end_time,
                                         int late_threshold,
                                         int early_leave_threshold,
                                         bool allow_multiple_checkin) {
    // 解析上班时间（HH:mm 格式）
    sscanf(work_start_time.c_str(), "%d:%d", &work_start_hour_, &work_start_minute_);
    
    // 解析下班时间（HH:mm 格式）
    sscanf(work_end_time.c_str(), "%d:%d", &work_end_hour_, &work_end_minute_);
    
    // 设置阈值
    late_threshold_ = late_threshold;
    early_leave_threshold_ = early_leave_threshold;
    allow_multiple_checkin_ = allow_multiple_checkin;
    
    spdlog::info("Work schedule updated: {} - {} (late: {}min, early_leave: {}min, multiple_checkin: {})", 
                 work_start_time, work_end_time, late_threshold, early_leave_threshold,
                 allow_multiple_checkin ? "yes" : "no");
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


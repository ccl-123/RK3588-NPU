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
    , record_dao_(std::make_unique<db::AttendanceRecordDAO>(db_manager))
    , user_dao_(std::make_unique<db::UserDAO>(db_manager))
{
}

AttendanceService::~AttendanceService() {
    // 智能指针自动释放，无需手动 delete
}

int AttendanceService::record_attendance(int user_id, const std::string& user_name,
                                        float similarity, const std::string& face_image_path,
                                        int check_type) {
    std::time_t current_time = std::time(nullptr);
    WorkScheduleConfig schedule = get_work_schedule_snapshot();
    
    // 如果 check_type 为默认值（1），自动判断是签到还是签退
    if (check_type == db::CheckType::CHECK_IN) {
        check_type = auto_determine_check_type(user_id, current_time);
    }
    
    // 检查是否允许打卡（基于多次签到开关）
    if (schedule.allow_multiple_checkin) {
        // 启用多次签到：只检查短时间内重复，防止误触
        if (is_duplicate_check(user_id, schedule.duplicate_check_interval)) {
            spdlog::debug("Duplicate check within {} seconds, user_id: {}", schedule.duplicate_check_interval, user_id);
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
        if (is_duplicate_check(user_id, schedule.duplicate_check_interval)) {
            spdlog::debug("Duplicate check within {} seconds, user_id: {}", schedule.duplicate_check_interval, user_id);
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
    record.device_id = Config::Default::DEVICE_ID;    // 使用配置值
    record.location = Config::Default::LOCATION;       // 使用配置值
    
    // 判断考勤状态
    record.status = determine_status(record.check_time, check_type);
    
    // 保存到数据库
    int record_id = record_dao_->insert(record);
    
    if (record_id > 0) {
        RecentCheckCacheEntry cache_entry;
        cache_entry.latest_check_time = record.check_time;
        cache_entry.latest_check_type = record.check_type;
        cache_entry.latest_check_date = date_from_time(record.check_time);
        update_recent_check_cache(user_id, cache_entry);

        const char* type_str = (check_type == db::CheckType::CHECK_IN) ? "签到" : "签退";
        const char* status_str = (record.status == db::AttendanceStatus::STATUS_NORMAL) ? "正常" :
                                (record.status == db::AttendanceStatus::STATUS_LATE) ? "迟到" : "早退";
        spdlog::info("Attendance recorded: {} (ID: {}, Type: {}, Status: {})", 
                     user_name, user_id, type_str, status_str);
    }
    
    return record_id;
}

bool AttendanceService::is_duplicate_check(int user_id, int interval_seconds) {
    const std::time_t now = std::time(nullptr);
    RecentCheckCacheEntry cache_entry;

    if (get_recent_check_cache(user_id, cache_entry)) {
        return cache_entry.latest_check_time > 0 &&
               std::difftime(now, cache_entry.latest_check_time) < interval_seconds;
    }

    db::AttendanceRecord latest_record;
    if (!record_dao_->find_latest_by_user_id(user_id, latest_record)) {
        return false;
    }

    cache_entry.latest_check_time = latest_record.check_time;
    cache_entry.latest_check_type = latest_record.check_type;
    cache_entry.latest_check_date = date_from_time(latest_record.check_time);
    update_recent_check_cache(user_id, cache_entry);

    return std::difftime(now, latest_record.check_time) < interval_seconds;
}

bool AttendanceService::has_today_check_record(int user_id, int check_type) {
    std::string today = get_current_date();

    // 先用进程内缓存做快速判断，避免实时预览阶段反复查库
    RecentCheckCacheEntry cache_entry;
    if (get_recent_check_cache(user_id, cache_entry) &&
        cache_entry.latest_check_date == today &&
        cache_entry.latest_check_type == check_type) {
        return true;
    }

    // 缓存未命中时回退到精准 SQL 查询
    return record_dao_->has_user_check_on_date(user_id, today, check_type);
}

int AttendanceService::determine_status(std::time_t check_time, int check_type) {
    WorkScheduleConfig schedule = get_work_schedule_snapshot();
    std::tm tm_info;
    localtime_r(&check_time, &tm_info);
    int hour = tm_info.tm_hour;
    int minute = tm_info.tm_min;
    
    // 将打卡时间转换为分钟数（从 00:00 开始计算）
    int check_minutes = hour * 60 + minute;
    
    if (check_type == db::CheckType::CHECK_IN) {
        // 签到逻辑：基于上班时间 + 迟到阈值
        int work_start_minutes = schedule.work_start_hour * 60 + schedule.work_start_minute;
        int late_limit_minutes = work_start_minutes + schedule.late_threshold;
        
        if (check_minutes <= late_limit_minutes) {
            return db::AttendanceStatus::STATUS_NORMAL;  // 在迟到阈值内，正常
        } else {
            return db::AttendanceStatus::STATUS_LATE;    // 超过迟到阈值，迟到
        }
    } else if (check_type == db::CheckType::CHECK_OUT) {
        // 签退逻辑：基于下班时间 - 早退阈值
        int work_end_minutes = schedule.work_end_hour * 60 + schedule.work_end_minute;
        int early_leave_limit_minutes = work_end_minutes - schedule.early_leave_threshold;
        
        if (check_minutes < early_leave_limit_minutes) {
            return db::AttendanceStatus::STATUS_EARLY_LEAVE;  // 提前太多，早退
        } else {
            return db::AttendanceStatus::STATUS_NORMAL;       // 正常签退
        }
    }
    
    return db::AttendanceStatus::STATUS_NORMAL;
}

int AttendanceService::auto_determine_check_type(int user_id, std::time_t current_time) {
    (void)user_id;
    
    // 判断当前时间是上午还是下午
    std::tm tm_info;
    localtime_r(&current_time, &tm_info);
    int hour = tm_info.tm_hour;
    int minute = tm_info.tm_min;
    int current_minutes = hour * 60 + minute;
    
    // 使用12:00（中午）作为固定分界点，更符合日常习惯
    // 12:00 之前是上午（签到），12:00 及之后是下午（签退）
    const int midday_minutes = 12 * 60;  // 12:00 = 720 分钟
    
    // 判断当前是上午时段还是下午时段
    bool is_afternoon = (current_minutes >= midday_minutes);
    
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
                                         bool allow_multiple_checkin,
                                         int duplicate_check_interval) {
    WorkScheduleConfig updated_schedule = get_work_schedule_snapshot();

    // 解析上班时间（HH:mm 格式），添加错误检查
    int parsed_start = sscanf(work_start_time.c_str(), "%d:%d",
                              &updated_schedule.work_start_hour,
                              &updated_schedule.work_start_minute);
    if (parsed_start != 2) {
        spdlog::warn("Invalid work_start_time format '{}', using default 09:00", work_start_time);
        updated_schedule.work_start_hour = 9;
        updated_schedule.work_start_minute = 0;
    }

    // 解析下班时间（HH:mm 格式），添加错误检查
    int parsed_end = sscanf(work_end_time.c_str(), "%d:%d",
                            &updated_schedule.work_end_hour,
                            &updated_schedule.work_end_minute);
    if (parsed_end != 2) {
        spdlog::warn("Invalid work_end_time format '{}', using default 18:00", work_end_time);
        updated_schedule.work_end_hour = 18;
        updated_schedule.work_end_minute = 0;
    }

    // 设置阈值
    updated_schedule.late_threshold = late_threshold;
    updated_schedule.early_leave_threshold = early_leave_threshold;
    updated_schedule.allow_multiple_checkin = allow_multiple_checkin;
    updated_schedule.duplicate_check_interval = duplicate_check_interval;

    {
        std::lock_guard<std::mutex> lock(work_schedule_mutex_);
        work_schedule_ = updated_schedule;
    }

    spdlog::info("Work schedule updated: {} - {} (late: {}min, early_leave: {}min, multiple_checkin: {}, dup_interval: {}s)",
                 work_start_time, work_end_time, late_threshold, early_leave_threshold,
                 allow_multiple_checkin ? "yes" : "no", duplicate_check_interval);
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

std::vector<db::AttendanceRecord> AttendanceService::query_records_range(
    const std::string& start_date, const std::string& end_date) {
    
    std::time_t start_time = parse_time(start_date + " 00:00:00");
    std::time_t end_time = parse_time(end_date + " 23:59:59");
    
    return record_dao_->find_by_time_range(start_time, end_time);
}

AttendanceStatistics AttendanceService::get_statistics(const std::string& date) {
    AttendanceStatistics stats;
    stats.date = date;

    // 使用 COUNT(DISTINCT user_id) 统计去重后的人数
    stats.total_count = record_dao_->count_by_date(date, 0);  // 总人数（去重）
    stats.check_in_count = record_dao_->count_by_date(date, db::CheckType::CHECK_IN);  // 签到人数（去重）
    stats.check_out_count = record_dao_->count_by_date(date, db::CheckType::CHECK_OUT);  // 签退人数（去重）

    // 统计迟到和早退人数（去重）
    stats.late_count = record_dao_->count_late_by_date(date);
    stats.early_leave_count = record_dao_->count_early_leave_by_date(date);

    // 正常人数 = 总人数 - 迟到人数 - 早退人数
    stats.normal_count = stats.total_count - stats.late_count - stats.early_leave_count;
    if (stats.normal_count < 0) {
        stats.normal_count = 0;  // 防止负数
    }

    return stats;
}

std::vector<AttendanceStatistics> AttendanceService::get_monthly_statistics(
    const std::string& year_month) {
    
    // 计算该月的第一天和最后一天
    int year, month;
    sscanf(year_month.c_str(), "%d-%d", &year, &month);
    
    int days_in_month = 31;
    if (month == 2) {
        days_in_month = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 29 : 28;
    } else if (month == 4 || month == 6 || month == 9 || month == 11) {
        days_in_month = 30;
    }
    
    std::ostringstream start_oss, end_oss;
    start_oss << year << "-" << std::setfill('0') << std::setw(2) << month << "-01";
    end_oss << year << "-" << std::setfill('0') << std::setw(2) << month << "-" << days_in_month;
    
    // 使用新的范围查询接口，避免循环 N 次数据库查询
    return get_statistics_range(start_oss.str(), end_oss.str());
}

std::vector<AttendanceStatistics> AttendanceService::get_statistics_range(
    const std::string& start_date, const std::string& end_date) {
    
    std::vector<AttendanceStatistics> result;
    auto dao_stats = record_dao_->get_daily_stats_in_range(start_date, end_date);
    
    for (const auto& ds : dao_stats) {
        AttendanceStatistics s;
        s.date = ds.date;
        s.total_count = ds.total_records;
        s.late_count = ds.late_count;
        s.early_leave_count = ds.early_leave_count;
        // 注意：DAO 返回的是 distinct users，这里映射到 check_in_count 可能不完全准确，
        // 但对于趋势图来说，attendance_rate 通常分母是 registered_users，分子是 present_users。
        // 在这里我们把 distinct_users 视为“出勤人数”
        s.check_in_count = ds.distinct_users; 
        
        // 其他字段如 check_out_count, normal_count 在聚合查询中未细分，设为 0 或估算
        // 如果需要精确的 check_out_count，需要在 SQL 中增加 SUM(CASE WHEN check_type=2...)
        s.check_out_count = 0; 
        s.normal_count = s.total_count - s.late_count - s.early_leave_count;
        
        result.push_back(s);
    }
    
    return result;
}

bool AttendanceService::delete_record(int record_id) {
    bool removed = record_dao_->remove(record_id);
    if (removed) {
        std::lock_guard<std::mutex> lock(recent_check_cache_mutex_);
        recent_check_cache_.clear();
    }
    return removed;
}

std::string AttendanceService::get_current_date() {
    std::time_t now = std::time(nullptr);
    return date_from_time(now);
}

std::string AttendanceService::get_current_time() {
    std::time_t now = std::time(nullptr);
    std::tm tm_info;
    localtime_r(&now, &tm_info);

    std::ostringstream oss;
    oss << (tm_info.tm_year + 1900) << "-"
        << std::setfill('0') << std::setw(2) << (tm_info.tm_mon + 1) << "-"
        << std::setfill('0') << std::setw(2) << tm_info.tm_mday << " "
        << std::setfill('0') << std::setw(2) << tm_info.tm_hour << ":"
        << std::setfill('0') << std::setw(2) << tm_info.tm_min << ":"
        << std::setfill('0') << std::setw(2) << tm_info.tm_sec;
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

AttendanceService::WorkScheduleConfig AttendanceService::get_work_schedule_snapshot() const {
    std::lock_guard<std::mutex> lock(work_schedule_mutex_);
    return work_schedule_;
}

bool AttendanceService::get_recent_check_cache(int user_id, RecentCheckCacheEntry& entry) const {
    std::lock_guard<std::mutex> lock(recent_check_cache_mutex_);
    auto it = recent_check_cache_.find(user_id);
    if (it == recent_check_cache_.end()) {
        return false;
    }

    entry = it->second;
    return true;
}

void AttendanceService::update_recent_check_cache(int user_id, const RecentCheckCacheEntry& entry) {
    std::lock_guard<std::mutex> lock(recent_check_cache_mutex_);
    recent_check_cache_[user_id] = entry;
}

std::string AttendanceService::date_from_time(std::time_t time_value) {
    std::tm tm_info;
    localtime_r(&time_value, &tm_info);

    std::ostringstream oss;
    oss << (tm_info.tm_year + 1900) << "-"
        << std::setfill('0') << std::setw(2) << (tm_info.tm_mon + 1) << "-"
        << std::setfill('0') << std::setw(2) << tm_info.tm_mday;
    return oss.str();
}

} // namespace service

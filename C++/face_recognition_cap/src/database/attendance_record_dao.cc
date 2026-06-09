/**
 * @file attendance_record_dao.cc
 * @brief 考勤记录数据访问对象实现
 * @author CL
 * @date 2025-11-20
 */

#include "database/attendance_record_dao.h"
#include <sstream>
#include <iomanip>
#include <utility>
#include <spdlog/spdlog.h>

namespace db {

namespace {

constexpr const char* kAttendanceRecordSelectColumns =
    "record_id, user_id, user_name, check_time, check_type, similarity, "
    "face_image, device_id, location, status, remark";

std::pair<std::string, std::string> make_day_bounds(const std::string& date) {
    return {date + " 00:00:00", date + " 23:59:59"};
}

}  // namespace

AttendanceRecordDAO::AttendanceRecordDAO(DatabaseManager* db_manager)
    : db_manager_(db_manager) {
}

AttendanceRecordDAO::~AttendanceRecordDAO() {
}

int AttendanceRecordDAO::insert(const AttendanceRecord& record) {
    std::string sql = R"(
        INSERT INTO attendance_records 
        (user_id, user_name, check_time, check_type, similarity, face_image, device_id, location, status, remark)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return -1;
    
    stmt->bind_int(1, record.user_id);
    stmt->bind_string(2, record.user_name);
    stmt->bind_string(3, time_to_string(record.check_time));
    stmt->bind_int(4, record.check_type);
    stmt->bind_double(5, record.similarity);
    stmt->bind_string(6, record.face_image);
    stmt->bind_string(7, record.device_id);
    stmt->bind_string(8, record.location);
    stmt->bind_int(9, record.status);
    stmt->bind_string(10, record.remark);
    
    if (!stmt->execute()) {
        spdlog::error("Failed to insert attendance record for user: {}", record.user_name);
        return -1;
    }
    
    return static_cast<int>(stmt->last_insert_id());
}

bool AttendanceRecordDAO::remove(int record_id) {
    std::string sql = "DELETE FROM attendance_records WHERE record_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, record_id);
    return stmt->execute();
}

bool AttendanceRecordDAO::find_by_id(int record_id, AttendanceRecord& record) {
    std::string sql = std::string("SELECT ") + kAttendanceRecordSelectColumns +
        " FROM attendance_records WHERE record_id = ?";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, record_id);
    
    if (stmt->step()) {
        fill_record_from_stmt(stmt.get(), record);
        return true;
    }
    
    return false;
}

std::vector<AttendanceRecord> AttendanceRecordDAO::find_by_user_id(int user_id, 
                                                                    std::time_t start_time,
                                                                    std::time_t end_time) {
    std::vector<AttendanceRecord> records;
    
    std::string sql = std::string("SELECT ") + kAttendanceRecordSelectColumns +
        " FROM attendance_records WHERE user_id = ?";
    
    if (start_time > 0) {
        sql += " AND check_time >= ?";
    }
    if (end_time > 0) {
        sql += " AND check_time <= ?";
    }
    sql += " ORDER BY check_time DESC";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return records;
    
    int param_index = 1;
    stmt->bind_int(param_index++, user_id);
    
    if (start_time > 0) {
        stmt->bind_string(param_index++, time_to_string(start_time));
    }
    if (end_time > 0) {
        stmt->bind_string(param_index++, time_to_string(end_time));
    }
    
    while (stmt->step()) {
        AttendanceRecord record;
        fill_record_from_stmt(stmt.get(), record);
        records.push_back(record);
    }
    
    return records;
}

std::vector<AttendanceRecord> AttendanceRecordDAO::find_by_date(const std::string& date) {
    std::vector<AttendanceRecord> records;
    
    // 使用字符串范围比较替代 DATE() 函数，既能利用 check_time 索引，又避免了 DATE() 函数的潜在兼容性问题
    std::string sql = R"(
        SELECT record_id, user_id, user_name, check_time, check_type, similarity,
               face_image, device_id, location, status, remark
        FROM attendance_records
        WHERE check_time >= ? AND check_time <= ? 
        ORDER BY check_time
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return records;
    
    const auto [start_time, end_time] = make_day_bounds(date);
    
    stmt->bind_string(1, start_time);
    stmt->bind_string(2, end_time);
    
    while (stmt->step()) {
        AttendanceRecord record;
        fill_record_from_stmt(stmt.get(), record);
        records.push_back(record);
    }
    
    return records;
}

std::vector<AttendanceRecordDAO::DailyStats> AttendanceRecordDAO::get_daily_stats_in_range(
    const std::string& start_date, const std::string& end_date) {
    
    std::vector<DailyStats> stats_list;
    
    // 使用 GROUP BY 优化统计查询，一次查询即可获取一段时间的数据。
    // 这里按“人数”聚合，与 AttendanceService::get_statistics(date) 的字段语义保持一致。
    // SUBSTR(check_time, 1, 10) 提取 'YYYY-MM-DD'
    std::string sql = R"(
        SELECT 
            SUBSTR(check_time, 1, 10) as day,
            COUNT(DISTINCT user_id) as total_users,
            COUNT(DISTINCT CASE WHEN check_type = 1 THEN user_id END) as check_in_users,
            COUNT(DISTINCT CASE WHEN check_type = 2 THEN user_id END) as check_out_users,
            COUNT(DISTINCT CASE WHEN status = 2 THEN user_id END) as late_users,
            COUNT(DISTINCT CASE WHEN status = 3 THEN user_id END) as early_leave_users,
            COUNT(DISTINCT CASE WHEN status = 1 THEN user_id END) as normal_users,
            COUNT(*) as total_records
        FROM attendance_records
        WHERE check_time >= ? AND check_time <= ?
        GROUP BY day
        ORDER BY day ASC
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return stats_list;
    
    std::string start_time = start_date + " 00:00:00";
    std::string end_time = end_date + " 23:59:59";
    
    stmt->bind_string(1, start_time);
    stmt->bind_string(2, end_time);
    
    while (stmt->step()) {
        DailyStats stat;
        stat.date = stmt->get_column_string(0);
        stat.total_users = stmt->get_column_int(1);
        stat.check_in_users = stmt->get_column_int(2);
        stat.check_out_users = stmt->get_column_int(3);
        stat.late_users = stmt->get_column_int(4);
        stat.early_leave_users = stmt->get_column_int(5);
        stat.normal_users = stmt->get_column_int(6);
        stat.total_records = stmt->get_column_int(7);
        stats_list.push_back(stat);
    }
    
    return stats_list;
}

std::vector<AttendanceRecord> AttendanceRecordDAO::find_by_time_range(std::time_t start_time,
                                                                       std::time_t end_time) {
    std::vector<AttendanceRecord> records;
    
    std::string sql = R"(
        SELECT record_id, user_id, user_name, check_time, check_type, similarity,
               face_image, device_id, location, status, remark
        FROM attendance_records
        WHERE check_time >= ? AND check_time <= ?
        ORDER BY check_time DESC
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return records;
    
    stmt->bind_string(1, time_to_string(start_time));
    stmt->bind_string(2, time_to_string(end_time));
    
    while (stmt->step()) {
        AttendanceRecord record;
        fill_record_from_stmt(stmt.get(), record);
        records.push_back(record);
    }
    
    return records;
}

bool AttendanceRecordDAO::find_latest_by_user_id(int user_id, AttendanceRecord& record) {
    std::string sql = R"(
        SELECT record_id, user_id, user_name, check_time, check_type, similarity,
               face_image, device_id, location, status, remark
        FROM attendance_records
        WHERE user_id = ? 
        ORDER BY check_time DESC 
        LIMIT 1
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, user_id);
    
    if (stmt->step()) {
        fill_record_from_stmt(stmt.get(), record);
        return true;
    }
    
    return false;
}

bool AttendanceRecordDAO::has_recent_record(int user_id, int seconds_ago) {
    std::string sql = R"(
        SELECT COUNT(*) FROM attendance_records
        WHERE user_id = ? 
        AND check_time > datetime('now', 'localtime', '-' || ? || ' seconds')
    )";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;
    
    stmt->bind_int(1, user_id);
    stmt->bind_int(2, seconds_ago);
    
    if (stmt->step()) {
        return stmt->get_column_int(0) > 0;
    }
    
    return false;
}

bool AttendanceRecordDAO::has_user_check_on_date(int user_id, const std::string& date, int check_type) {
    const auto [start_time, end_time] = make_day_bounds(date);
    std::string sql = R"(
        SELECT 1 FROM attendance_records
        WHERE user_id = ?
          AND check_type = ?
          AND check_time >= ?
          AND check_time <= ?
        LIMIT 1
    )";

    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return false;

    stmt->bind_int(1, user_id);
    stmt->bind_int(2, check_type);
    stmt->bind_string(3, start_time);
    stmt->bind_string(4, end_time);

    return stmt->step();
}

int AttendanceRecordDAO::count_by_date(const std::string& date, int check_type) {
    std::string sql = R"(
        SELECT COUNT(DISTINCT user_id) FROM attendance_records
        WHERE check_time >= ? AND check_time <= ?
    )";
    
    if (check_type > 0) {
        sql += " AND check_type = ?";
    }
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;
    
    const auto [start_time, end_time] = make_day_bounds(date);
    stmt->bind_string(1, start_time);
    stmt->bind_string(2, end_time);
    if (check_type > 0) {
        stmt->bind_int(3, check_type);
    }
    
    if (stmt->step()) {
        return stmt->get_column_int(0);
    }
    
    return 0;
}

int AttendanceRecordDAO::count_late_by_date(const std::string& date) {
    std::string sql = R"(
        SELECT COUNT(DISTINCT user_id) FROM attendance_records
        WHERE check_time >= ? AND check_time <= ? AND status = 2
    )";

    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;

    const auto [start_time, end_time] = make_day_bounds(date);
    stmt->bind_string(1, start_time);
    stmt->bind_string(2, end_time);

    if (stmt->step()) {
        return stmt->get_column_int(0);
    }

    return 0;
}

int AttendanceRecordDAO::count_early_leave_by_date(const std::string& date) {
    std::string sql = R"(
        SELECT COUNT(DISTINCT user_id) FROM attendance_records
        WHERE check_time >= ? AND check_time <= ? AND status = 3
    )";

    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;

    const auto [start_time, end_time] = make_day_bounds(date);
    stmt->bind_string(1, start_time);
    stmt->bind_string(2, end_time);

    if (stmt->step()) {
        return stmt->get_column_int(0);
    }

    return 0;
}

int AttendanceRecordDAO::count() {
    std::string sql = "SELECT COUNT(*) FROM attendance_records";
    
    auto stmt = db_manager_->prepare(sql);
    if (!stmt) return 0;
    
    if (stmt->step()) {
        return stmt->get_column_int(0);
    }
    
    return 0;
}

void AttendanceRecordDAO::fill_record_from_stmt(PreparedStatement* stmt, AttendanceRecord& record) {
    record.record_id = stmt->get_column_int(0);
    record.user_id = stmt->get_column_int(1);
    record.user_name = stmt->get_column_string(2);
    
    // 解析 check_time 字符串（第3列）
    std::string time_str = stmt->get_column_string(3);
    record.check_time = string_to_time(time_str);
    
    record.check_type = stmt->get_column_int(4);
    record.similarity = stmt->get_column_double(5);
    record.face_image = stmt->get_column_string(6);
    record.device_id = stmt->get_column_string(7);
    record.location = stmt->get_column_string(8);
    record.status = stmt->get_column_int(9);
    record.remark = stmt->get_column_string(10);
}

std::string AttendanceRecordDAO::time_to_string(std::time_t time) {
    if (time == 0) {
        time = std::time(nullptr);
    }

    std::tm tm_info;
    localtime_r(&time, &tm_info);
    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

std::time_t AttendanceRecordDAO::string_to_time(const std::string& time_str) {
    if (time_str.empty()) {
        return 0;
    }
    
    std::tm tm = {};
    std::istringstream iss(time_str);
    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (iss.fail()) {
        return 0;
    }
    
    return std::mktime(&tm);
}

} // namespace db

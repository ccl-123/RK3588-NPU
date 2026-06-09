/**
 * @file attendance_service.h
 * @brief 考勤业务服务
 * @author CL
 * @date 2025-11-20
 */

#ifndef _ATTENDANCE_SERVICE_H_
#define _ATTENDANCE_SERVICE_H_

#include "database/database_manager.h"
#include "database/attendance_record_dao.h"
#include "database/user_dao.h"
#include "config/config.h"
#include <string>
#include <vector>
#include <memory>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace service {

/**
 * @brief 考勤统计结构
 */
struct AttendanceStatistics {
    std::string date;           // 日期
    int total_count;            // 总打卡人数
    int check_in_count;         // 签到人数
    int check_out_count;        // 签退人数
    int late_count;             // 迟到人数
    int early_leave_count;      // 早退人数
    int normal_count;           // 正常人数
};

/**
 * @brief 考勤业务服务类
 */
class AttendanceService {
public:
    AttendanceService(db::DatabaseManager* db_manager);
    ~AttendanceService();
    
    /**
     * @brief 记录考勤打卡
     * @param user_id 用户ID
     * @param user_name 用户姓名
     * @param similarity 相似度
     * @param face_image_path 人脸图像路径(可选)
     * @param check_type 打卡类型(1=签到, 2=签退)
     * @return 记录ID, -1表示失败
     */
    int record_attendance(int user_id, const std::string& user_name,
                         float similarity, const std::string& face_image_path = "",
                         int check_type = 1);
    
    /**
     * @brief 检查是否重复打卡（短时间内）
     * @param user_id 用户ID
     * @param interval_seconds 时间间隔(秒)
     * @return true重复, false不重复
     */
    bool is_duplicate_check(int user_id, int interval_seconds = Config::Default::DUPLICATE_CHECK_INTERVAL);
    
    /**
     * @brief 检查今天是否已有指定类型的打卡记录
     * @param user_id 用户ID
     * @param check_type 打卡类型（1=签到, 2=签退）
     * @return true已打卡, false未打卡
     */
    bool has_today_check_record(int user_id, int check_type);
    
    /**
     * @brief 判断考勤状态(正常/迟到/早退)
     * @param check_time 打卡时间
     * @param check_type 打卡类型
     * @return 状态码(1=正常, 2=迟到, 3=早退)
     */
    int determine_status(std::time_t check_time, int check_type);
    
    /**
     * @brief 自动判断打卡类型（签到/签退）
     * @param user_id 用户ID
     * @param current_time 当前时间
     * @return 1=签到, 2=签退
     */
    int auto_determine_check_type(int user_id, std::time_t current_time);
    
    /**
     * @brief 设置考勤时间规则（从配置读取）
     * @param work_start_time 上班时间（HH:mm 格式）
     * @param work_end_time 下班时间（HH:mm 格式）
     * @param late_threshold 迟到阈值（分钟）
     * @param early_leave_threshold 早退阈值（分钟）
     * @param allow_multiple_checkin 是否允许一天多次签到
     * @param duplicate_check_interval 防重复签到间隔（秒）
     */
    void set_work_schedule(const std::string& work_start_time,
                          const std::string& work_end_time,
                          int late_threshold,
                          int early_leave_threshold,
                          bool allow_multiple_checkin = false,
                          int duplicate_check_interval = Config::Default::DUPLICATE_CHECK_INTERVAL);
    
    /**
     * @brief 查询用户的考勤记录
     * @param user_id 用户ID
     * @param start_date 开始日期(YYYY-MM-DD)
     * @param end_date 结束日期(YYYY-MM-DD)
     * @return 考勤记录列表
     */
    std::vector<db::AttendanceRecord> query_user_records(int user_id,
                                                          const std::string& start_date,
                                                          const std::string& end_date);
    
    /**
     * @brief 查询指定日期的考勤记录
     * @param date 日期(YYYY-MM-DD)
     * @return 考勤记录列表
     */
    std::vector<db::AttendanceRecord> query_records_by_date(const std::string& date);

    /**
     * @brief 查询指定日期范围的考勤记录
     * @param start_date 开始日期(YYYY-MM-DD)
     * @param end_date 结束日期(YYYY-MM-DD)
     * @return 考勤记录列表
     */
    std::vector<db::AttendanceRecord> query_records_range(const std::string& start_date, const std::string& end_date);

    /**
     * @brief 获取考勤统计
     * @param date 日期(YYYY-MM-DD)
     * @return 统计结果
     */
    AttendanceStatistics get_statistics(const std::string& date);
    
    /**
     * @brief 获取月度考勤统计
     * @param year_month 年月(YYYY-MM)
     * @return 统计结果列表
     */
    std::vector<AttendanceStatistics> get_monthly_statistics(const std::string& year_month);

    /**
     * @brief 获取指定日期范围的统计数据（高性能聚合查询）
     * @param start_date 开始日期(YYYY-MM-DD)
     * @param end_date 结束日期(YYYY-MM-DD)
     * @return 统计结果列表
     */
    std::vector<AttendanceStatistics> get_statistics_range(const std::string& start_date, const std::string& end_date);
    
    /**
     * @brief 删除考勤记录
     * @param record_id 记录ID
     * @return true成功, false失败
     */
    bool delete_record(int record_id);
    
private:
    /**
     * @brief 考勤时间规则快照
     * @note 所有需要跨线程读取的规则都先复制到本地快照，避免读写竞争。
     */
    struct WorkScheduleConfig {
        int work_start_hour = 9;
        int work_start_minute = 0;
        int work_end_hour = 18;
        int work_end_minute = 0;
        int late_threshold = 30;
        int early_leave_threshold = 30;
        bool allow_multiple_checkin = false;
        int duplicate_check_interval = 300;
    };

    /**
     * @brief 最近一次打卡缓存
     * @note 用于减少实时预览阶段针对同一用户的重复查库。
     */
    struct RecentCheckCacheEntry {
        std::time_t latest_check_time = 0;
        int latest_check_type = 0;
        std::string latest_check_date;
    };

    /**
     * @brief 获取当前日期字符串
     */
    std::string get_current_date();
    
    /**
     * @brief 获取当前时间字符串
     */
    std::string get_current_time();
    
    /**
     * @brief 解析时间字符串
     */
    std::time_t parse_time(const std::string& time_str);

    WorkScheduleConfig get_work_schedule_snapshot() const;
    bool get_recent_check_cache(int user_id, RecentCheckCacheEntry& entry) const;
    void update_recent_check_cache(int user_id, const RecentCheckCacheEntry& entry);
    static std::string date_from_time(std::time_t time_value);
    
private:
    db::DatabaseManager* db_manager_;
    std::unique_ptr<db::AttendanceRecordDAO> record_dao_;  // 智能指针管理
    std::unique_ptr<db::UserDAO> user_dao_;                // 智能指针管理

    // 考勤规则配置（支持分钟级精度）
    mutable std::mutex work_schedule_mutex_;
    WorkScheduleConfig work_schedule_;

    // 最近打卡缓存：按 user_id 保存最后一次打卡时间和类型
    mutable std::mutex recent_check_cache_mutex_;
    std::unordered_map<int, RecentCheckCacheEntry> recent_check_cache_;

    // 串行化同一进程内的重复检查与插入，避免识别回调并发产生重复打卡。
    std::mutex record_attendance_mutex_;
};

} // namespace service

#endif // _ATTENDANCE_SERVICE_H_

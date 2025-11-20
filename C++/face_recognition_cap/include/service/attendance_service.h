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
#include <string>
#include <vector>
#include <ctime>

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
     * @brief 检查是否重复打卡
     * @param user_id 用户ID
     * @param interval_seconds 时间间隔(秒)
     * @return true重复, false不重复
     */
    bool is_duplicate_check(int user_id, int interval_seconds = 300);
    
    /**
     * @brief 判断考勤状态(正常/迟到/早退)
     * @param check_time 打卡时间
     * @param check_type 打卡类型
     * @return 状态码(1=正常, 2=迟到, 3=早退)
     */
    int determine_status(std::time_t check_time, int check_type);
    
    /**
     * @brief 查询用户考勤记录
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
     * @brief 删除考勤记录
     * @param record_id 记录ID
     * @return true成功, false失败
     */
    bool delete_record(int record_id);
    
private:
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
    
private:
    db::DatabaseManager* db_manager_;
    db::AttendanceRecordDAO* record_dao_;
    db::UserDAO* user_dao_;
    
    // 考勤规则配置
    int check_in_start_hour_;   // 签到开始时间(小时)
    int check_in_end_hour_;     // 签到结束时间(小时)
    int check_out_start_hour_;  // 签退开始时间(小时)
    int check_out_end_hour_;    // 签退结束时间(小时)
};

} // namespace service

#endif // _ATTENDANCE_SERVICE_H_


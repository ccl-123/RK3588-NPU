/**
 * @file attendance_record_dao.h
 * @brief 考勤记录数据访问对象
 * @author Augment Agent
 * @date 2025-11-20
 */

#ifndef _ATTENDANCE_RECORD_DAO_H_
#define _ATTENDANCE_RECORD_DAO_H_

#include "database_manager.h"
#include "database_types.h"
#include <vector>
#include <memory>

namespace db {

/**
 * @brief 考勤记录数据访问类
 */
class AttendanceRecordDAO {
public:
    AttendanceRecordDAO(DatabaseManager* db_manager);
    ~AttendanceRecordDAO();
    
    /**
     * @brief 插入考勤记录
     * @param record 考勤记录
     * @return 新记录ID, -1表示失败
     */
    int insert(const AttendanceRecord& record);
    
    /**
     * @brief 删除考勤记录
     * @param record_id 记录ID
     * @return true成功, false失败
     */
    bool remove(int record_id);
    
    /**
     * @brief 根据ID查询记录
     * @param record_id 记录ID
     * @param record 输出记录信息
     * @return true成功, false失败
     */
    bool find_by_id(int record_id, AttendanceRecord& record);
    
    /**
     * @brief 查询用户的考勤记录
     * @param user_id 用户ID
     * @param start_time 开始时间(0表示不限)
     * @param end_time 结束时间(0表示不限)
     * @return 记录列表
     */
    std::vector<AttendanceRecord> find_by_user_id(int user_id, 
                                                   std::time_t start_time = 0,
                                                   std::time_t end_time = 0);
    
    /**
     * @brief 查询指定日期的考勤记录
     * @param date 日期字符串(YYYY-MM-DD)
     * @return 记录列表
     */
    std::vector<AttendanceRecord> find_by_date(const std::string& date);
    
    /**
     * @brief 查询指定时间范围的考勤记录
     * @param start_time 开始时间
     * @param end_time 结束时间
     * @return 记录列表
     */
    std::vector<AttendanceRecord> find_by_time_range(std::time_t start_time, 
                                                      std::time_t end_time);
    
    /**
     * @brief 查询用户最近的一条考勤记录
     * @param user_id 用户ID
     * @param record 输出记录信息
     * @return true成功, false失败
     */
    bool find_latest_by_user_id(int user_id, AttendanceRecord& record);
    
    /**
     * @brief 检查用户在指定时间段内是否有打卡记录
     * @param user_id 用户ID
     * @param seconds_ago 多少秒之前
     * @return true有记录, false无记录
     */
    bool has_recent_record(int user_id, int seconds_ago);
    
    /**
     * @brief 统计指定日期的考勤人数
     * @param date 日期字符串(YYYY-MM-DD)
     * @param check_type 考勤类型(0表示全部)
     * @return 考勤人数
     */
    int count_by_date(const std::string& date, int check_type = 0);
    
    /**
     * @brief 统计指定日期的迟到人数
     * @param date 日期字符串(YYYY-MM-DD)
     * @return 迟到人数
     */
    int count_late_by_date(const std::string& date);
    
    /**
     * @brief 统计指定日期的早退人数
     * @param date 日期字符串(YYYY-MM-DD)
     * @return 早退人数
     */
    int count_early_leave_by_date(const std::string& date);
    
    /**
     * @brief 获取记录总数
     * @return 记录数量
     */
    int count();
    
private:
    /**
     * @brief 从结果集填充记录信息
     */
    void fill_record_from_stmt(PreparedStatement* stmt, AttendanceRecord& record);
    
    /**
     * @brief 时间转换为字符串
     */
    std::string time_to_string(std::time_t time);
    
private:
    DatabaseManager* db_manager_;
};

} // namespace db

#endif // _ATTENDANCE_RECORD_DAO_H_


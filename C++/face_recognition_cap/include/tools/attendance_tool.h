/**
 * @file attendance_tool.h
 * @brief 考勤查询工具 - Agent 可调用的考勤数据查询工具
 */

#pragma once

#include "tools/base_tool.h"
#include "service/attendance_service.h"

namespace agent {

/**
 * @brief 考勤查询工具
 *
 * 支持查询:
 * - 今日考勤统计
 * - 本周考勤统计
 * - 本月考勤统计
 * - 指定日期的考勤记录
 * - 迟到/早退人员名单
 */
class AttendanceTool : public BaseTool {
public:
    /**
     * @brief 构造函数
     * @param service 考勤服务（不转移所有权）
     */
    explicit AttendanceTool(service::AttendanceService* service);

    QString name() const override { return "query_attendance"; }

    QString description() const override {
        return "查询考勤数据，支持统计和详细记录查询。"
               "query_type: stats(统计)、records(详细记录)、late(迟到名单)、early_leave(早退名单)。"
               "日期参数: date_range(today/week/month) 或 date(YYYY-MM-DD) 或 start_date+end_date(日期范围)。"
               "filter: all(全部) 或 anomaly(仅异常:迟到+早退)，默认 all";
    }

    QJsonObject parametersSchema() const override;

    QString execute(const QJsonObject& args) override;

private:
    service::AttendanceService* attendance_service_;

    /**
     * @brief 查询今日考勤
     * @return 统计结果
     */
    QString queryToday();

    /**
     * @brief 查询本周考勤
     * @return 统计结果
     */
    QString queryWeek();

    /**
     * @brief 查询本月考勤
     * @return 统计结果
     */
    QString queryMonth();

    /**
     * @brief 查询指定日期考勤
     * @param date 日期 (YYYY-MM-DD)
     * @return 统计结果
     */
    QString queryDate(const QString& date);

    /**
     * @brief 查询详细考勤记录
     * @param date 日期 (YYYY-MM-DD)
     * @return 详细记录
     */
    QString queryRecords(const QString& date);

    /**
     * @brief 查询迟到人员名单
     * @param date 日期 (YYYY-MM-DD)
     * @return 迟到名单
     */
    QString queryLateList(const QString& date);

    /**
     * @brief 查询早退人员名单
     * @param date 日期 (YYYY-MM-DD)
     * @return 早退名单
     */
    QString queryEarlyLeaveList(const QString& date);

    /**
     * @brief 查询日期范围内的考勤记录
     * @param start_date 开始日期 (YYYY-MM-DD)
     * @param end_date 结束日期 (YYYY-MM-DD)
     * @param anomaly_only 是否仅返回异常记录
     * @return 考勤记录
     */
    QString queryRecordsRange(const QString& start_date, const QString& end_date, bool anomaly_only);

    /**
     * @brief 格式化统计结果
     * @param stats 统计数据
     * @return 格式化的字符串
     */
    QString formatStatistics(const service::AttendanceStatistics& stats);

    /**
     * @brief 获取今日日期字符串
     * @return YYYY-MM-DD 格式
     */
    QString getTodayDate();

    /**
     * @brief 获取本周起始日期
     * @return YYYY-MM-DD 格式
     */
    QString getWeekStartDate();

    /**
     * @brief 获取本月起始日期
     * @return YYYY-MM-DD 格式
     */
    QString getMonthStartDate();
};

} // namespace agent

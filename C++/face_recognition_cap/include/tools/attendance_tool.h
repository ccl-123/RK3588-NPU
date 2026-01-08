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
        return "查询考勤统计数据，支持按日期范围查询今日、本周、本月的考勤情况，"
               "包括签到人数、迟到人数、早退人数等统计信息";
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

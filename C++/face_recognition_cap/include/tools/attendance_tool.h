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
    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;

private:
    service::AttendanceService* attendance_service_;

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

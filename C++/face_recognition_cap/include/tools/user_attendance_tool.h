/**
 * @file user_attendance_tool.h
 * @brief 单用户考勤查询工具 - 面向指定员工的考勤查询
 */

#pragma once

#include "tools/base_tool.h"
#include "service/attendance_service.h"
#include "service/user_service.h"

namespace agent {

class UserAttendanceTool : public BaseTool {
public:
    UserAttendanceTool(service::AttendanceService* attendance_service,
                       service::UserService* user_service);

    QString name() const override { return "lookup_user_attendance"; }

    QString description() const override {
        return "查询单个员工的考勤情况。"
               "适合回答某人今日/本周/本月/指定区间的出勤摘要、详细记录、最新打卡和异常打卡。"
               "用户可通过 user_id 或 name 指定。";
    }

    QJsonObject parametersSchema() const override;
    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;

private:
    service::AttendanceService* attendance_service_;
    service::UserService* user_service_;

    QString getTodayDate() const;
    QString getWeekStartDate() const;
    QString getMonthStartDate() const;
};

}  // namespace agent

/**
 * @file department_attendance_tool.h
 * @brief 部门考勤查询工具
 */

#pragma once

#include "tools/base_tool.h"
#include "service/attendance_service.h"
#include "service/user_service.h"

namespace agent {

class DepartmentAttendanceTool : public BaseTool {
public:
    DepartmentAttendanceTool(service::AttendanceService* attendance_service,
                             service::UserService* user_service);

    QString name() const override { return "lookup_department_attendance"; }

    QString description() const override {
        return "查询指定部门的考勤情况。"
               "适合回答部门今日/本周/本月/指定区间的出勤摘要、异常记录、缺勤名单和详细打卡记录。";
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

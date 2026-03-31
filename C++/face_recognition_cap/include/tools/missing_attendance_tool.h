/**
 * @file missing_attendance_tool.h
 * @brief 缺卡/缺勤查询工具
 */

#pragma once

#include "tools/base_tool.h"
#include "service/attendance_service.h"
#include "service/user_service.h"

namespace agent {

class MissingAttendanceTool : public BaseTool {
public:
    MissingAttendanceTool(service::AttendanceService* attendance_service,
                          service::UserService* user_service);

    QString name() const override { return "lookup_missing_attendance"; }

    QString description() const override {
        return "查询缺卡和缺勤情况。"
               "适合回答谁今天没签到、谁只签到没签退、谁连续多天没有打卡。";
    }

    QJsonObject parametersSchema() const override;
    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;

private:
    service::AttendanceService* attendance_service_;
    service::UserService* user_service_;

    QString getTodayDate() const;
};

}  // namespace agent

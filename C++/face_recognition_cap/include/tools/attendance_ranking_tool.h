/**
 * @file attendance_ranking_tool.h
 * @brief 考勤排名工具
 */

#pragma once

#include "tools/base_tool.h"
#include "service/attendance_service.h"
#include "service/user_service.h"

namespace agent {

class AttendanceRankingTool : public BaseTool {
public:
    AttendanceRankingTool(service::AttendanceService* attendance_service,
                          service::UserService* user_service);

    QString name() const override { return "lookup_attendance_ranking"; }

    QString description() const override {
        return "查询考勤排名。"
               "适合回答迟到最多、异常最多、出勤率最高/最低，以及部门异常率最高等排行问题。";
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

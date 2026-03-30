/**
 * @file system_tool.h
 * @brief 系统控制工具 - 提供日期时间、系统状态等查询
 */

#pragma once

#include "tools/base_tool.h"
#include <QDateTime>

namespace agent {

/**
 * @brief 系统工具 - 提供系统信息查询
 *
 * 功能:
 * - 获取当前日期时间
 * - 获取系统运行状态
 * - 获取考勤系统配置信息
 */
class SystemTool : public BaseTool {
public:
    SystemTool() = default;
    ~SystemTool() override = default;

    QString name() const override { return "system_info"; }

    QString description() const override {
        return "获取系统信息，包括当前日期时间、系统状态、考勤配置等";
    }

    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"query_type", QJsonObject{
                    {"type", "string"},
                    {"description", "查询类型: datetime(日期时间), status(系统状态), config(考勤配置), all(全部)"},
                    {"enum", QJsonArray{"datetime", "status", "config", "all"}}
                }}
            }},
            {"required", QJsonArray{"query_type"}}
        };
    }

    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;

private:
    QJsonObject buildDateTimeData();
    QJsonObject buildSystemStatusData();
    QJsonObject buildAttendanceConfigData();
};

/**
 * @brief 帮助工具 - 提供可用工具列表和使用说明
 */
class HelpTool : public BaseTool {
public:
    HelpTool() = default;
    ~HelpTool() override = default;

    QString name() const override { return "help"; }

    QString description() const override {
        return "获取帮助信息，列出可用的工具和功能说明";
    }

    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"topic", QJsonObject{
                    {"type", "string"},
                    {"description", "帮助主题: tools(工具列表), attendance(考勤功能), user(用户管理), all(全部)"},
                    {"enum", QJsonArray{"tools", "attendance", "user", "all"}}
                }}
            }},
            {"required", QJsonArray{}}
        };
    }

    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;
};

/**
 * @brief 计算器工具 - 提供简单的数学计算
 */
class CalculatorTool : public BaseTool {
public:
    CalculatorTool() = default;
    ~CalculatorTool() override = default;

    QString name() const override { return "calculator"; }

    QString description() const override {
        return "执行简单的数学计算，如加减乘除、百分比等";
    }

    QJsonObject parametersSchema() const override {
        return QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"expression", QJsonObject{
                    {"type", "string"},
                    {"description", "数学表达式，如: 100+200, 50*0.8, 1000/4"}
                }}
            }},
            {"required", QJsonArray{"expression"}}
        };
    }

    ToolExecutionResult executeWithResult(const ToolInvocation& invocation) override;

private:
    double evaluateSimple(const QString& expr);
};

} // namespace agent

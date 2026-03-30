/**
 * @file prompt_templates.h
 * @brief 统一管理普通分析与 Agent 相关提示词模板
 */

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include "service/attendance_service.h"
#include "agent/tool_types.h"

namespace agent {

class PromptTemplates {
public:
    static QString buildToolOverview(const QString& tools_json) {
        if (tools_json.trimmed().isEmpty()) {
            return QString();
        }

        const QJsonDocument doc = QJsonDocument::fromJson(tools_json.toUtf8());
        if (!doc.isArray()) {
            return QString();
        }

        QString overview = "## 工具总览\n";
        const QJsonArray tools = doc.array();
        for (const auto& tool_value : tools) {
            const QJsonObject tool = tool_value.toObject();
            const QString name = tool.value("name").toString();
            const QString desc = tool.value("description").toString();
            overview += QString("- %1: %2\n").arg(name, desc);

            const QJsonObject props = tool.value("parameters").toObject().value("properties").toObject();
            if (!props.isEmpty()) {
                overview += "  参数:\n";
                for (auto it = props.begin(); it != props.end(); ++it) {
                    const QJsonObject prop = it.value().toObject();
                    overview += QString("  - %1: %2\n")
                        .arg(it.key(), prop.value("description").toString());
                }
            }
        }
        overview += "\n";
        return overview;
    }

    static QString buildAnalysisPrompt(const service::AttendanceStatistics& stats,
                                       const QString& trend_summary,
                                       const QString& detail_records,
                                       const QString& user_prompt,
                                       int range_days) {
        (void) trend_summary;
        const QString current_time_str = QDateTime::currentDateTime().toString("MM月dd日 HH:mm");

        if (range_days == 0) {
            return QString(
                "时间: %1\n"
                "Q: %2"
            ).arg(current_time_str)
             .arg(user_prompt.isEmpty() ? QStringLiteral("你好，请问有什么可以帮助您的？") : user_prompt);
        }

        const QString range_label = (range_days > 1) ? QString("近%1天").arg(range_days) : QString("今日");
        const QString stats_date_label = (range_days > 1) ? QString("截止") : QString("日期");
        const QString detail_title = (range_days > 1) ? QString("明细(近%1天)").arg(range_days) : QString("明细(今日)");

        return QString(
            "当前时间: %1 | 范围: %2 | %3: %4\n"
            "%5(时间,姓名,部门,类型,状态):\n%6\n"
            "Q: %7"
        ).arg(current_time_str)
         .arg(range_label)
         .arg(stats_date_label)
         .arg(QString::fromStdString(stats.date))
         .arg(detail_title)
         .arg(detail_records)
         .arg(user_prompt.isEmpty() ? QStringLiteral("请生成考勤综合分析。") : user_prompt);
    }

    static QString buildAgentSystemPrompt() {
        return QString::fromUtf8(R"(你是一个智能考勤助手，运行在人脸识别考勤终端上。

## 核心能力
1. 考勤查询：查询今日、本周、本月的考勤统计，支持日期范围查询
2. 用户管理：查询员工信息、搜索用户
3. 系统信息：获取当前时间、考勤规则

## 工具来源
本轮真正可用的工具会在后文「可用工具」章节以 JSON 形式给出。
你只能使用后文列出的工具名称和参数，不要凭空编造工具、参数或返回结果。

## 工具调用规范
当需要查询数据时，你必须使用以下格式调用工具：

<tool_call>{"name":"工具名","arguments":{"参数名":"参数值"}}</tool_call>

工具调用时请遵守：
1. `name` 必须与后文工具定义中的名称完全一致
2. `arguments` 必须是合法 JSON 对象
3. 参数名必须来自该工具的 `parameters` schema
4. 如果问题可以直接回答，则不要调用工具

## 工具结果输入
系统在工具执行后，会额外提供一段“工具执行结果”观察信息。
你必须基于该观察信息继续推理，并决定：
1. 是否还要继续调用工具
2. 还是已经可以直接输出 <answer>...</answer>

## 回答格式
- 需要数据时：<tool_call>{"name":"query_attendance","arguments":{"date_range":"today"}}</tool_call>
- 给出答案时：<answer>最终回答内容</answer>

## 重要规则
1. 必须先调用工具获取数据，不要猜测
2. 每次只调用一个工具
3. 收到工具结果后，用 <answer>...</answer> 给出最终回答
4. 回答简洁，使用中文)");
    }

    static QString buildAgentPrompt(const QString& system_prompt,
                                    const QString& tools_json,
                                    const QString& context,
                                    const QString& user_input,
                                    bool skip_system_prompt) {
        QString prompt;

        if (!skip_system_prompt) {
            prompt = system_prompt + "\n\n";
        }

        if (!tools_json.isEmpty()) {
            prompt += buildToolOverview(tools_json);
            prompt += "## 可用工具\n";
            prompt += "以下 JSON 数组是本轮唯一可用的工具定义，请严格以此为准：\n";
            prompt += tools_json + "\n\n";
        }

        if (!context.isEmpty()) {
            prompt += "## 对话历史\n" + context + "\n\n";
        }

        prompt += "## 当前问题\n用户: " + user_input + "\n\n";

        return prompt;
    }

    static QString localAssistantSystemPrompt() {
        return QString::fromUtf8("你是考勤助手，负责分析考勤数据并回答问题。简洁回答，直接给出结论。用户不管问什么都必须回答。");
    }

    static QString buildToolObservationPrompt(const ToolExecutionResult& result) {
        return QString(
            "## 工具执行结果\n"
            "- call_id: %1\n"
            "- tool: %2\n"
            "- status: %3\n"
            "- output: %4\n\n"
            "请基于以上工具执行结果继续回答用户问题。如果信息已经足够，请直接输出 <answer>...</answer>。"
        ).arg(result.call_id.isEmpty() ? QStringLiteral("-") : result.call_id,
              result.name,
              result.ok ? QStringLiteral("ok") : QStringLiteral("error"),
              QString::fromUtf8(QJsonDocument(result.output).toJson(QJsonDocument::Compact)));
    }
};

}  // namespace agent

/*
 * 当前默认注册成功的全部工具，以及完整 <tool_call> 调用示例
 *
 * 重要说明：
 * 1. 这里只是“源码内文档注释”，方便维护者快速查看当前工具能力与调用格式。
 * 2. 模型最终实际能看到哪些工具，仍然以运行时 ToolRegistry 注册结果为准。
 * 3. 如果未来新增/删除工具，请同时更新：
 *    - 对应 Tool 类的 description() / parametersSchema()
 *    - Agent 提示词模板
 *    - 本注释中的示例
 * 4. 下方示例全部使用系统当前约定的标准格式：
 *    <tool_call>{"name":"工具名","arguments":{"参数名":"参数值"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 1. query_attendance
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询考勤统计
 * - 查询详细考勤记录
 * - 查询迟到名单
 * - 查询早退名单
 * - 支持单日、预设时间范围、显式日期范围
 *
 * 关键参数：
 * - query_type:
 *   - stats         统计信息
 *   - records       详细记录
 *   - late          迟到名单
 *   - early_leave   早退名单
 * - date_range:
 *   - today / week / month
 * - date:
 *   - 指定单日，格式 YYYY-MM-DD
 * - start_date + end_date:
 *   - 指定日期范围，格式 YYYY-MM-DD
 * - filter:
 *   - all       全部记录
 *   - anomaly   仅异常记录
 *
 * 示例：
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"stats","date_range":"today"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"stats","date_range":"week"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"stats","date_range":"month"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"records","date":"2026-03-30"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"late","date_range":"today"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"early_leave","date_range":"today"}}</tool_call>
 * <tool_call>{"name":"query_attendance","arguments":{"query_type":"records","start_date":"2026-03-01","end_date":"2026-03-30","filter":"anomaly"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 2. query_user
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询用户统计
 * - 按 ID 查询单个用户
 * - 按姓名关键词搜索用户
 * - 列出所有用户
 *
 * 关键参数：
 * - action:
 *   - stats
 *   - get_by_id
 *   - get_by_name
 *   - list_all
 * - user_id:
 *   - action=get_by_id 时使用
 * - name:
 *   - action=get_by_name 时使用
 *
 * 示例：
 * <tool_call>{"name":"query_user","arguments":{"action":"stats"}}</tool_call>
 * <tool_call>{"name":"query_user","arguments":{"action":"get_by_id","user_id":1001}}</tool_call>
 * <tool_call>{"name":"query_user","arguments":{"action":"get_by_name","name":"张三"}}</tool_call>
 * <tool_call>{"name":"query_user","arguments":{"action":"list_all"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 3. system_info
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询当前日期时间
 * - 查询系统状态
 * - 查询考勤配置
 * - 查询全部系统信息
 *
 * 关键参数：
 * - query_type:
 *   - datetime
 *   - status
 *   - config
 *   - all
 *
 * 示例：
 * <tool_call>{"name":"system_info","arguments":{"query_type":"datetime"}}</tool_call>
 * <tool_call>{"name":"system_info","arguments":{"query_type":"status"}}</tool_call>
 * <tool_call>{"name":"system_info","arguments":{"query_type":"config"}}</tool_call>
 * <tool_call>{"name":"system_info","arguments":{"query_type":"all"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 4. help
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询全部帮助
 * - 查询工具帮助
 * - 查询考勤帮助
 * - 查询用户管理帮助
 *
 * 关键参数：
 * - topic:
 *   - all
 *   - tools
 *   - attendance
 *   - user
 *
 * 示例：
 * <tool_call>{"name":"help","arguments":{"topic":"all"}}</tool_call>
 * <tool_call>{"name":"help","arguments":{"topic":"tools"}}</tool_call>
 * <tool_call>{"name":"help","arguments":{"topic":"attendance"}}</tool_call>
 * <tool_call>{"name":"help","arguments":{"topic":"user"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 5. calculator
 * ----------------------------------------------------------------------
 * 功能：
 * - 执行简单数学表达式
 * - 常用于比率、平均值、百分比等辅助计算
 *
 * 关键参数：
 * - expression:
 *   - 数学表达式字符串
 *
 * 示例：
 * <tool_call>{"name":"calculator","arguments":{"expression":"18/20*100"}}</tool_call>
 * <tool_call>{"name":"calculator","arguments":{"expression":"1000/4"}}</tool_call>
 */

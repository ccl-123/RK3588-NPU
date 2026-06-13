/**
 * @file prompt_templates.h
 * @brief 统一管理普通分析与 Agent 相关提示词模板
 */

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
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

            const QJsonObject parameters = tool.value("parameters").toObject();
            const QJsonObject props = parameters.value("properties").toObject();
            const QJsonArray required = parameters.value("required").toArray();
            if (!props.isEmpty()) {
                overview += "  参数:\n";
                for (auto it = props.begin(); it != props.end(); ++it) {
                    const QJsonObject prop = it.value().toObject();
                    bool is_required = false;
                    for (const auto& key : required) {
                        if (key.toString() == it.key()) {
                            is_required = true;
                            break;
                        }
                    }
                    overview += QString("  - %1%2: %3\n")
                        .arg(it.key(),
                             is_required ? QStringLiteral("（必填）") : QString(),
                             prop.value("description").toString());
                }
            }

            const QJsonArray any_of = parameters.value("anyOf").toArray();
            if (!any_of.isEmpty()) {
                QStringList alternatives;
                for (const auto& item : any_of) {
                    const QJsonArray alt_required = item.toObject().value("required").toArray();
                    QStringList keys;
                    for (const auto& key : alt_required) {
                        keys.append(key.toString());
                    }
                    if (!keys.isEmpty()) {
                        alternatives.append(keys.join("+"));
                    }
                }
                if (!alternatives.isEmpty()) {
                    overview += QString("  约束: %1 至少满足一项\n").arg(alternatives.join(" 或 "));
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
1. 考勤查询：查询今日、本周、本月、指定区间的考勤统计和详细记录
2. 深度分析：查询单个员工、指定部门、缺卡缺勤和各类排行
3. 用户管理：查询员工信息、搜索用户
4. 系统信息：获取当前时间、考勤规则

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
5. 调用工具时不要同时输出 <answer>，等工具返回后再回答

## 工具选择规则
1. 问全局考勤统计、全员打卡明细、迟到或早退名单：优先用 query_attendance
2. 问某一个员工的考勤、最近打卡、异常打卡：用 lookup_user_attendance
3. 问某个部门的考勤摘要、异常、缺勤或明细：用 lookup_department_attendance
4. 问谁没签到、谁只签到没签退、谁缺勤或连续缺勤：用 lookup_missing_attendance
5. 问排行、最多、最少、最高、最低：用 lookup_attendance_ranking
6. 问员工基础信息、员工列表、用户数量：用 query_user
7. 问当前时间、系统状态、考勤规则：用 system_info
8. 只有需要额外算术时才用 calculator

## 工具结果输入
系统在工具执行后，会额外提供一段"工具执行结果"观察信息。
你必须基于该观察信息继续判断：
1. 是否还需要继续调用其他工具
2. 还是已经可以直接输出 <answer>最终回答</answer>

## 回答格式
- 需要数据时：<tool_call>{"name":"query_attendance","arguments":{"query_type":"stats","date_range":"today"}}</tool_call>
- 给出答案时：<answer>最终回答内容</answer>

## 重要规则
1. 必须先调用工具获取数据，不要猜测
2. 每次只调用一个工具
3. 收到工具结果后，根据需要继续调用下一个工具，或输出 <answer>...</answer>
4. 回答简洁，使用中文
5. 思考过程尽量简短，快速做出决定
6. 只能回答工具结果中明确出现的信息；没有出现的字段不要补充或猜测
7. 不要输出内部字段或无关字段，例如记录ID、设备ID、相似度、地点、备注、图片路径)");
    }

    static QString buildAgentPrompt(const QString& system_prompt,
                                    const QString& tools_json,
                                    const QString& context,
                                    const QString& user_input,
                                    bool skip_system_prompt,
                                    bool include_tool_overview = true) {
        QString prompt;

        if (!skip_system_prompt) {
            prompt = system_prompt + "\n\n";
        }

        if (!tools_json.isEmpty()) {
            if (include_tool_overview) {
                prompt += buildToolOverview(tools_json);
            }
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

    static bool shouldDropObservationField(const QString& key) {
        const QString normalized = key.toLower();
        return normalized == "record_id" ||
               normalized == "device_id" ||
               normalized == "similarity" ||
               normalized == "location" ||
               normalized == "remark" ||
               normalized == "face_image" ||
               normalized == "face_image_path";
    }

    static QJsonValue sanitizeToolOutputValue(const QJsonValue& value) {
        if (value.isObject()) {
            QJsonObject sanitized;
            const QJsonObject obj = value.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                if (!shouldDropObservationField(it.key())) {
                    sanitized.insert(it.key(), sanitizeToolOutputValue(it.value()));
                }
            }
            return sanitized;
        }

        if (value.isArray()) {
            QJsonArray sanitized;
            const QJsonArray arr = value.toArray();
            for (const auto& item : arr) {
                sanitized.append(sanitizeToolOutputValue(item));
            }
            return sanitized;
        }

        return value;
    }

    static QJsonObject sanitizeToolOutputObject(const QJsonObject& output) {
        return sanitizeToolOutputValue(output).toObject();
    }

    static QString buildToolObservationPrompt(const ToolExecutionResult& result,
                                              bool include_structured_output = true) {
        const QJsonObject sanitized_output = sanitizeToolOutputObject(result.output);
        const QString display_text = result.promptText().trimmed();
        const QString error_line = result.error.isEmpty()
            ? QString()
            : QString("- error: %1\n").arg(result.error);
        const QString structured_line = include_structured_output
            ? QString("- structured_output: %1\n")
                  .arg(QString::fromUtf8(QJsonDocument(sanitized_output).toJson(QJsonDocument::Compact)))
            : QString();
        return QString(
            "## 工具执行结果\n"
            "- call_id: %1\n"
            "- tool: %2\n"
            "- status: %3\n"
            "- display_text:\n%4\n"
            "%5"
            "%6\n"
            "请基于 display_text 回答。"
            "如果还需要额外信息，可以继续调用一个合适的工具；"
            "如果信息已经足够，请输出 <answer>最终回答</answer>。"
        ).arg(result.call_id.isEmpty() ? QStringLiteral("-") : result.call_id,
              result.name,
              result.ok ? QStringLiteral("ok") : QStringLiteral("error"),
              display_text.isEmpty() ? QStringLiteral("-") : display_text,
              error_line,
              structured_line);
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
 * 2. lookup_user_attendance
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询单个员工在今日/本周/本月/指定区间内的考勤摘要
 * - 查询单个员工的详细打卡记录
 * - 查询单个员工最近一次打卡
 * - 查询单个员工的异常打卡
 *
 * 关键参数：
 * - query_type:
 *   - summary
 *   - records
 *   - latest
 *   - anomaly
 * - user_id / name:
 *   - 二选一，用于定位员工
 * - date_range / date / start_date + end_date:
 *   - 查询范围
 *
 * 示例：
 * <tool_call>{"name":"lookup_user_attendance","arguments":{"name":"张三","query_type":"summary","date_range":"week"}}</tool_call>
 * <tool_call>{"name":"lookup_user_attendance","arguments":{"user_id":1001,"query_type":"latest","date_range":"month"}}</tool_call>
 * <tool_call>{"name":"lookup_user_attendance","arguments":{"name":"李四","query_type":"anomaly","start_date":"2026-03-01","end_date":"2026-03-31"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 3. lookup_department_attendance
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询指定部门的考勤摘要
 * - 查询指定部门的异常记录
 * - 查询指定部门的缺勤人员
 * - 查询指定部门的全部打卡记录
 *
 * 关键参数：
 * - department:
 *   - 部门名称或关键词
 * - query_type:
 *   - summary / anomaly / missing / records
 * - date_range / date / start_date + end_date:
 *   - 查询范围
 *
 * 示例：
 * <tool_call>{"name":"lookup_department_attendance","arguments":{"department":"研发部","query_type":"summary","date_range":"today"}}</tool_call>
 * <tool_call>{"name":"lookup_department_attendance","arguments":{"department":"销售","query_type":"anomaly","date_range":"month"}}</tool_call>
 * <tool_call>{"name":"lookup_department_attendance","arguments":{"department":"行政","query_type":"missing","date":"2026-04-01"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 4. lookup_attendance_ranking
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询员工或部门的考勤排行
 * - 支持迟到、早退、异常、异常率、出勤覆盖率等指标
 *
 * 关键参数：
 * - scope:
 *   - user / department
 * - ranking_type:
 *   - late / early_leave / anomaly / anomaly_rate / attendance_rate
 * - limit:
 *   - 返回前 N 名
 * - order:
 *   - desc / asc
 * - department:
 *   - 可选，仅对 user 排行做部门过滤
 *
 * 示例：
 * <tool_call>{"name":"lookup_attendance_ranking","arguments":{"scope":"user","ranking_type":"late","date_range":"month","limit":5}}</tool_call>
 * <tool_call>{"name":"lookup_attendance_ranking","arguments":{"scope":"department","ranking_type":"anomaly_rate","date_range":"week","limit":3}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 5. lookup_missing_attendance
 * ----------------------------------------------------------------------
 * 功能：
 * - 查询完全缺勤人员
 * - 查询未签到人员
 * - 查询只签到未签退人员
 * - 查询连续多天未打卡人员
 *
 * 关键参数：
 * - query_type:
 *   - absent / missing_check_in / missing_check_out / consecutive_absent
 * - date:
 *   - 目标日期
 * - department:
 *   - 可选，部门过滤
 * - days:
 *   - consecutive_absent 时使用
 *
 * 示例：
 * <tool_call>{"name":"lookup_missing_attendance","arguments":{"query_type":"missing_check_in","date":"2026-04-01"}}</tool_call>
 * <tool_call>{"name":"lookup_missing_attendance","arguments":{"query_type":"missing_check_out","department":"研发部","date":"2026-04-01"}}</tool_call>
 * <tool_call>{"name":"lookup_missing_attendance","arguments":{"query_type":"consecutive_absent","days":2,"date":"2026-04-01"}}</tool_call>
 *
 * ----------------------------------------------------------------------
 * 6. query_user
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
 * 7. system_info
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
 * 8. help
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
 * 9. calculator
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

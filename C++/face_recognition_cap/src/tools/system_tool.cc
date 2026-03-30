/**
 * @file system_tool.cc
 * @brief 系统控制工具实现
 */

#include "tools/system_tool.h"

#include "config/config.h"
#include <QFileInfo>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace agent {

namespace {

QString format_datetime_text(const QJsonObject& data) {
    return QString("当前时间: %1 %2 %3\n时段: %4")
        .arg(data.value("date").toString())
        .arg(data.value("weekday").toString())
        .arg(data.value("time").toString())
        .arg(data.value("period").toString());
}

QString format_status_text(const QJsonObject& data) {
    return QString(
        "设备ID: %1\n"
        "位置: %2\n"
        "摄像头: /dev/video%3 (%4x%5)\n"
        "识别阈值: %6\n"
        "重复打卡间隔: %7秒\n"
        "LLM模型: %8"
    ).arg(data.value("device_id").toString())
     .arg(data.value("location").toString())
     .arg(data.value("camera_id").toInt())
     .arg(data.value("camera_width").toInt())
     .arg(data.value("camera_height").toInt())
     .arg(data.value("recognition_threshold").toDouble(), 0, 'f', 2)
     .arg(data.value("duplicate_check_interval").toInt())
     .arg(data.value("llm_model").toString());
}

QString format_config_text(const QJsonObject& data) {
    return QString(
        "上班时间: %1\n"
        "下班时间: %2\n"
        "迟到容忍: %3分钟\n"
        "早退容忍: %4分钟\n"
        "当前状态: %5"
    ).arg(data.value("work_start_time").toString())
     .arg(data.value("work_end_time").toString())
     .arg(data.value("late_threshold").toInt())
     .arg(data.value("early_leave_threshold").toInt())
     .arg(data.value("current_status").toString());
}

}  // namespace

ToolExecutionResult SystemTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    const QString query_type = invocation.arguments.value("query_type").toString("all");
    result.output["query_type"] = query_type;

    if (query_type == "datetime") {
        const auto data = buildDateTimeData();
        result.ok = true;
        result.output["datetime"] = data;
        result.display_text = format_datetime_text(data);
        return result;
    }

    if (query_type == "status") {
        const auto data = buildSystemStatusData();
        result.ok = true;
        result.output["status"] = data;
        result.display_text = format_status_text(data);
        return result;
    }

    if (query_type == "config") {
        const auto data = buildAttendanceConfigData();
        result.ok = true;
        result.output["config"] = data;
        result.display_text = format_config_text(data);
        return result;
    }

    const auto datetime = buildDateTimeData();
    const auto status = buildSystemStatusData();
    const auto config = buildAttendanceConfigData();
    result.ok = true;
    result.output["datetime"] = datetime;
    result.output["status"] = status;
    result.output["config"] = config;
    result.display_text =
        "【日期时间】\n" + format_datetime_text(datetime) + "\n\n" +
        "【系统状态】\n" + format_status_text(status) + "\n\n" +
        "【考勤配置】\n" + format_config_text(config);
    return result;
}

QJsonObject SystemTool::buildDateTimeData() {
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale(QLocale::Chinese, QLocale::China);

    const QString weekDay = locale.dayName(now.date().dayOfWeek());
    const QString dateStr = now.toString("yyyy年MM月dd日");
    const QString timeStr = now.toString("HH:mm:ss");

    const int hour = now.time().hour();
    QString period;
    if (hour < 6) period = "凌晨";
    else if (hour < 9) period = "早上";
    else if (hour < 12) period = "上午";
    else if (hour < 14) period = "中午";
    else if (hour < 18) period = "下午";
    else if (hour < 22) period = "晚上";
    else period = "深夜";

    return QJsonObject{
        {"date", dateStr},
        {"weekday", weekDay},
        {"time", timeStr},
        {"period", period},
    };
}

QJsonObject SystemTool::buildSystemStatusData() {
    const QString llm_model_path = QString::fromUtf8(Config::LocalLLM::getModelPath());
    return QJsonObject{
        {"device_id", QString(Config::Default::DEVICE_ID)},
        {"location", QString(Config::Default::LOCATION)},
        {"camera_id", Config::Default::CAMERA_ID},
        {"camera_width", Config::Camera::WIDTH},
        {"camera_height", Config::Camera::HEIGHT},
        {"recognition_threshold", Config::Default::RECOGNITION_THRESHOLD},
        {"duplicate_check_interval", Config::Default::DUPLICATE_CHECK_INTERVAL},
        {"llm_model", QFileInfo(llm_model_path).fileName()},
    };
}

QJsonObject SystemTool::buildAttendanceConfigData() {
    const QTime now = QTime::currentTime();
    const QTime workStart(Config::Default::WORK_START_HOUR, Config::Default::WORK_START_MINUTE);
    const QTime workEnd(Config::Default::WORK_END_HOUR, Config::Default::WORK_END_MINUTE);
    const bool inWorkTime = (now >= workStart && now <= workEnd);

    return QJsonObject{
        {"work_start_time", QString("%1:%2")
            .arg(Config::Default::WORK_START_HOUR, 2, 10, QChar('0'))
            .arg(Config::Default::WORK_START_MINUTE, 2, 10, QChar('0'))},
        {"work_end_time", QString("%1:%2")
            .arg(Config::Default::WORK_END_HOUR, 2, 10, QChar('0'))
            .arg(Config::Default::WORK_END_MINUTE, 2, 10, QChar('0'))},
        {"late_threshold", Config::Default::LATE_THRESHOLD},
        {"early_leave_threshold", Config::Default::EARLY_LEAVE_THRESHOLD},
        {"current_status", inWorkTime ? "工作时间" : "非工作时间"},
    };
}

ToolExecutionResult HelpTool::executeWithResult(const ToolInvocation& invocation) {
    const QString topic = invocation.arguments.value("topic").toString("all");
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();
    result.ok = true;
    result.output["topic"] = topic;

    QString help;
    if (topic == "tools" || topic == "all") {
        help += "【可用工具】\n";
        help += "1. query_attendance - 查询考勤数据\n";
        help += "2. query_user - 查询用户信息\n";
        help += "3. system_info - 获取系统信息\n";
        help += "4. calculator - 数学计算\n\n";
    }
    if (topic == "attendance" || topic == "all") {
        help += "【考勤功能说明】\n";
        help += "- 支持查询今日、本周、本月的考勤数据\n";
        help += "- 可以统计出勤率、迟到早退情况\n";
        help += "- 支持按日期范围查询历史记录\n\n";
    }
    if (topic == "user" || topic == "all") {
        help += "【用户管理说明】\n";
        help += "- 支持列出所有注册用户\n";
        help += "- 支持按姓名搜索用户\n";
        help += "- 支持统计用户总数\n";
    }

    result.display_text = help.trimmed();
    result.output["text"] = result.display_text;
    return result;
}

ToolExecutionResult CalculatorTool::executeWithResult(const ToolInvocation& invocation) {
    ToolExecutionResult result;
    result.call_id = invocation.call_id;
    result.name = name();

    const QString expression = invocation.arguments.value("expression").toString();
    result.output["expression"] = expression;
    if (expression.isEmpty()) {
        result.ok = false;
        result.error = "错误: 请提供数学表达式";
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }

    try {
        const double value = evaluateSimple(expression);
        result.ok = true;
        result.output["value"] = value;
        if (value == static_cast<int>(value)) {
            result.display_text = QString("%1 = %2").arg(expression).arg(static_cast<int>(value));
        } else {
            result.display_text = QString("%1 = %2").arg(expression).arg(value, 0, 'f', 2);
        }
        result.output["text"] = result.display_text;
        return result;
    } catch (...) {
        result.ok = false;
        result.error = QString("错误: 无法计算表达式 \"%1\"").arg(expression);
        result.display_text = result.error;
        result.output["error"] = result.error;
        return result;
    }
}

double CalculatorTool::evaluateSimple(const QString& expr) {
    QString cleaned = expr.simplified().remove(' ');
    cleaned.replace("%", "/100.0");

    QRegularExpression re(R"(^(-?\d+\.?\d*)\s*([+\-*/])\s*(-?\d+\.?\d*)$)");
    auto match = re.match(cleaned);
    if (match.hasMatch()) {
        const double a = match.captured(1).toDouble();
        const QString op = match.captured(2);
        const double b = match.captured(3).toDouble();

        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/") {
            if (b == 0) throw std::runtime_error("Division by zero");
            return a / b;
        }
    }

    bool ok = false;
    const double value = cleaned.toDouble(&ok);
    if (ok) return value;

    throw std::runtime_error("Invalid expression");
}

}  // namespace agent

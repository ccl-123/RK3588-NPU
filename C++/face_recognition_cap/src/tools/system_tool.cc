/**
 * @file system_tool.cc
 * @brief 系统控制工具实现
 */

#include "tools/system_tool.h"
#include "config/config.h"
#include <stdexcept>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <spdlog/spdlog.h>

namespace agent {

// ==================== SystemTool ====================

QString SystemTool::execute(const QJsonObject& args) {
    QString query_type = args["query_type"].toString("all");

    spdlog::debug("SystemTool::execute query_type={}", query_type.toStdString());

    if (query_type == "datetime") {
        return getDateTime();
    } else if (query_type == "status") {
        return getSystemStatus();
    } else if (query_type == "config") {
        return getAttendanceConfig();
    } else {
        // all
        QString result;
        result += "【日期时间】\n" + getDateTime() + "\n\n";
        result += "【系统状态】\n" + getSystemStatus() + "\n\n";
        result += "【考勤配置】\n" + getAttendanceConfig();
        return result;
    }
}

QString SystemTool::getDateTime() {
    QDateTime now = QDateTime::currentDateTime();
    QLocale locale(QLocale::Chinese, QLocale::China);

    QString weekDay = locale.dayName(now.date().dayOfWeek());
    QString dateStr = now.toString("yyyy年MM月dd日");
    QString timeStr = now.toString("HH:mm:ss");

    // 判断时段
    int hour = now.time().hour();
    QString period;
    if (hour < 6) period = "凌晨";
    else if (hour < 9) period = "早上";
    else if (hour < 12) period = "上午";
    else if (hour < 14) period = "中午";
    else if (hour < 18) period = "下午";
    else if (hour < 22) period = "晚上";
    else period = "深夜";

    return QString("当前时间: %1 %2 %3\n时段: %4")
        .arg(dateStr)
        .arg(weekDay)
        .arg(timeStr)
        .arg(period);
}

QString SystemTool::getSystemStatus() {
    QString status;

    // 系统基本信息
    status += "设备ID: " + QString(Config::Default::DEVICE_ID) + "\n";
    status += "位置: " + QString(Config::Default::LOCATION) + "\n";

    // 摄像头配置
    status += QString("摄像头: /dev/video%1 (%2x%3)\n")
        .arg(Config::Default::CAMERA_ID)
        .arg(Config::Camera::WIDTH)
        .arg(Config::Camera::HEIGHT);

    // 识别参数
    status += QString("识别阈值: %1\n").arg(Config::Default::RECOGNITION_THRESHOLD, 0, 'f', 2);
    status += QString("重复打卡间隔: %1秒\n").arg(Config::Default::DUPLICATE_CHECK_INTERVAL);

    // LLM 状态
    const QString llm_model_path = QString::fromUtf8(Config::LocalLLM::getModelPath());
    status += "LLM模型: " + QFileInfo(llm_model_path).fileName() + "\n";

    return status;
}

QString SystemTool::getAttendanceConfig() {
    QString config;

    config += QString("上班时间: %1:%2\n")
        .arg(Config::Default::WORK_START_HOUR, 2, 10, QChar('0'))
        .arg(Config::Default::WORK_START_MINUTE, 2, 10, QChar('0'));

    config += QString("下班时间: %1:%2\n")
        .arg(Config::Default::WORK_END_HOUR, 2, 10, QChar('0'))
        .arg(Config::Default::WORK_END_MINUTE, 2, 10, QChar('0'));

    config += QString("迟到容忍: %1分钟\n").arg(Config::Default::LATE_THRESHOLD);
    config += QString("早退容忍: %1分钟\n").arg(Config::Default::EARLY_LEAVE_THRESHOLD);

    // 判断当前是否在工作时间
    QTime now = QTime::currentTime();
    QTime workStart(Config::Default::WORK_START_HOUR, Config::Default::WORK_START_MINUTE);
    QTime workEnd(Config::Default::WORK_END_HOUR, Config::Default::WORK_END_MINUTE);

    bool inWorkTime = (now >= workStart && now <= workEnd);
    config += QString("当前状态: %1").arg(inWorkTime ? "工作时间" : "非工作时间");

    return config;
}

// ==================== HelpTool ====================

QString HelpTool::execute(const QJsonObject& args) {
    QString topic = args["topic"].toString("all");

    QString help;

    if (topic == "tools" || topic == "all") {
        help += "【可用工具】\n";
        help += "1. query_attendance - 查询考勤数据\n";
        help += "   参数: period (today/week/month/range)\n";
        help += "   示例: 查询今日考勤、本周考勤统计\n\n";
        help += "2. query_user - 查询用户信息\n";
        help += "   参数: query_type (list/search/count)\n";
        help += "   示例: 列出所有用户、搜索特定员工\n\n";
        help += "3. system_info - 获取系统信息\n";
        help += "   参数: query_type (datetime/status/config)\n";
        help += "   示例: 获取当前时间、系统状态\n\n";
        help += "4. calculator - 数学计算\n";
        help += "   参数: expression\n";
        help += "   示例: 计算 100*0.8\n\n";
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

    return help.trimmed();
}

// ==================== CalculatorTool ====================

QString CalculatorTool::execute(const QJsonObject& args) {
    QString expression = args["expression"].toString();

    if (expression.isEmpty()) {
        return "错误: 请提供数学表达式";
    }

    spdlog::debug("CalculatorTool::execute expr={}", expression.toStdString());

    try {
        double result = evaluateSimple(expression);

        // 格式化结果
        if (result == static_cast<int>(result)) {
            return QString("%1 = %2").arg(expression).arg(static_cast<int>(result));
        } else {
            return QString("%1 = %2").arg(expression).arg(result, 0, 'f', 2);
        }
    } catch (...) {
        return "错误: 无法计算表达式 \"" + expression + "\"";
    }
}

double CalculatorTool::evaluateSimple(const QString& expr) {
    // 简单的表达式解析器，支持 +, -, *, /, %
    QString cleaned = expr.simplified().remove(' ');

    // 处理百分比
    cleaned.replace("%", "/100.0");

    // 使用正则表达式解析简单的二元运算
    QRegularExpression re(R"(^(-?\d+\.?\d*)\s*([+\-*/])\s*(-?\d+\.?\d*)$)");
    auto match = re.match(cleaned);

    if (match.hasMatch()) {
        double a = match.captured(1).toDouble();
        QString op = match.captured(2);
        double b = match.captured(3).toDouble();

        if (op == "+") return a + b;
        if (op == "-") return a - b;
        if (op == "*") return a * b;
        if (op == "/") {
            if (b == 0) throw std::runtime_error("Division by zero");
            return a / b;
        }
    }

    // 尝试直接解析为数字
    bool ok;
    double value = cleaned.toDouble(&ok);
    if (ok) return value;

    throw std::runtime_error("Invalid expression");
}

} // namespace agent

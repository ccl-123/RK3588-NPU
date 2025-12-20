#include "ui/dashboard_page.h"
#include "services/ai_analysis_service.h"

#include "database/database_types.h"
#include "service/attendance_service.h"
#include "service/user_service.h"
#include "utils/config_manager.h"
#include "utils/svg_icon_manager.h"
#include "widgets/card_widget.h"
#include "widgets/toast_notification.h"

#include <spdlog/spdlog.h>
#include <QComboBox>
#include <QDate>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStringList>
#include <QTime>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct TrendSeries {
    std::vector<double> primary;
    std::vector<double> secondary;
    QStringList labels;
};

class TrendChartWidget : public QWidget {
public:
    explicit TrendChartWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setSeries(const TrendSeries& series) {
        series_ = series;
        update();
    }

    void setAxisTitles(const QString& y_title, const QString& x_title) {
        y_title_ = y_title;
        x_title_ = x_title;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF bounds = rect().adjusted(36, 16, -16, -40);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);

        if (series_.primary.empty()) {
            painter.setPen(QColor(140, 140, 140));
            painter.drawText(rect(), Qt::AlignCenter, tr("暂无趋势数据"));
            return;
        }

        double max_value = 0.0;
        for (double v : series_.primary) {
            max_value = std::max(max_value, v);
        }
        for (double v : series_.secondary) {
            max_value = std::max(max_value, v);
        }
        if (max_value <= 0.0) {
            max_value = 1.0;
        }

        QPen grid_pen(QColor(230, 230, 230));
        grid_pen.setStyle(Qt::DashLine);
        painter.setPen(grid_pen);
        const int grid_lines = 4;
        for (int i = 0; i <= grid_lines; ++i) {
            double y = bounds.top() + (bounds.height() / grid_lines) * i;
            painter.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
        }

        auto build_points = [&](const std::vector<double>& values) {
            QVector<QPointF> points;
            const int count = static_cast<int>(values.size());
            if (count == 0) {
                return points;
            }
            const double step = (count == 1) ? 0.0 : (bounds.width() / (count - 1));
            for (int i = 0; i < count; ++i) {
                const double x = bounds.left() + step * i;
                const double y = bounds.bottom() - (values[i] / max_value) * bounds.height();
                points.append(QPointF(x, y));
            }
            return points;
        };

        const QVector<QPointF> primary_points = build_points(series_.primary);
        const QVector<QPointF> secondary_points = build_points(series_.secondary);

        if (!primary_points.isEmpty()) {
            QPainterPath area;
            area.moveTo(primary_points.first().x(), bounds.bottom());
            for (const auto& pt : primary_points) {
                area.lineTo(pt);
            }
            area.lineTo(primary_points.last().x(), bounds.bottom());
            area.closeSubpath();

            QLinearGradient gradient(bounds.topLeft(), bounds.bottomLeft());
            gradient.setColorAt(0.0, QColor(22, 119, 255, 90));
            gradient.setColorAt(1.0, QColor(22, 119, 255, 0));
            painter.fillPath(area, gradient);

            QPainterPath line;
            line.moveTo(primary_points.first());
            for (const auto& pt : primary_points) {
                line.lineTo(pt);
            }
            painter.setPen(QPen(QColor(22, 119, 255), 2));
            painter.drawPath(line);

            painter.setBrush(QColor(22, 119, 255));
            painter.setPen(Qt::NoPen);
            for (const auto& pt : primary_points) {
                painter.drawEllipse(pt, 3.5, 3.5);
            }
        }

        if (!secondary_points.isEmpty()) {
            QPainterPath line;
            line.moveTo(secondary_points.first());
            for (const auto& pt : secondary_points) {
                line.lineTo(pt);
            }
            QPen secondary_pen(QColor(250, 140, 22), 2);
            secondary_pen.setStyle(Qt::DashLine);
            painter.setPen(secondary_pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(line);
        }

        painter.setPen(QColor(140, 140, 140));
        painter.setFont(QFont(painter.font().family(), 9));
        const int label_count = series_.labels.size();
        if (label_count > 0) {
            const int mid = label_count / 2;
            const QVector<int> indices = {0, mid, label_count - 1};
            for (int idx : indices) {
                if (idx < 0 || idx >= label_count) {
                    continue;
                }
                const double x = bounds.left() + (bounds.width() / std::max(1, label_count - 1)) * idx;
                const QString text = series_.labels.at(idx);
                const QRectF text_rect(x - 24, bounds.bottom() + 8, 48, 16);
                painter.drawText(text_rect, Qt::AlignCenter, text);
            }
        }

        painter.setPen(QColor(140, 140, 140));
        painter.setFont(QFont(painter.font().family(), 9));
        painter.drawText(QRectF(6, bounds.top() - 6, 28, 16), Qt::AlignLeft, "100%");
        painter.drawText(QRectF(6, bounds.center().y() - 8, 28, 16), Qt::AlignLeft, "50%");
        painter.drawText(QRectF(6, bounds.bottom() - 8, 28, 16), Qt::AlignLeft, "0%");

        if (!y_title_.isEmpty()) {
            painter.drawText(QRectF(bounds.left(), bounds.top() - 14, 160, 14),
                             Qt::AlignLeft, y_title_);
        }
        if (!x_title_.isEmpty()) {
            painter.drawText(QRectF(bounds.right() - 90, bounds.bottom() + 22, 90, 14),
                             Qt::AlignRight, x_title_);
        }
    }

private:
    TrendSeries series_;
    QString y_title_;
    QString x_title_;
};

struct DonutSegment {
    double value;
    QColor color;
    QString label;
};

class DonutChartWidget : public QWidget {
public:
    explicit DonutChartWidget(QWidget* parent = nullptr)
        : QWidget(parent)
        , thickness_(12) {
        setMinimumSize(160, 160);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    void setSegments(const std::vector<DonutSegment>& segments) {
        segments_ = segments;
        update();
    }

    void setCenterText(const QString& title, const QString& value) {
        center_title_ = title;
        center_value_ = value;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int diameter = std::min(width(), height()) - 32;
        if (diameter <= 0) {
            return;
        }
        const QRectF ring_rect(
            (width() - diameter) / 2.0,
            (height() - diameter) / 2.0,
            diameter,
            diameter);

        double total = 0.0;
        for (const auto& segment : segments_) {
            total += segment.value;
        }

        if (total <= 0.0) {
            painter.setPen(QColor(140, 140, 140));
            painter.drawText(rect(), Qt::AlignCenter, tr("暂无分布数据"));
            return;
        }

        double start_angle = 90.0;
        for (const auto& segment : segments_) {
            const double span = 360.0 * (segment.value / total);
            QPen pen(segment.color, thickness_, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawArc(ring_rect, static_cast<int>(start_angle * 16),
                           static_cast<int>(-span * 16));
            start_angle -= span;
        }

        painter.setPen(QColor(60, 60, 60));
        QFont value_font = painter.font();
        value_font.setPointSize(18);
        value_font.setBold(true);
        painter.setFont(value_font);
        painter.drawText(rect().adjusted(0, -6, 0, 0), Qt::AlignCenter, center_value_);

        painter.setPen(QColor(140, 140, 140));
        QFont title_font = painter.font();
        title_font.setPointSize(10);
        title_font.setBold(false);
        painter.setFont(title_font);
        painter.drawText(rect().adjusted(0, 22, 0, 0), Qt::AlignCenter, center_title_);
    }

private:
    std::vector<DonutSegment> segments_;
    int thickness_;
    QString center_title_;
    QString center_value_;
};

class BarChartWidget : public QWidget {
public:
    explicit BarChartWidget(QWidget* parent = nullptr)
        : QWidget(parent) {
        setMinimumHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setValues(const std::vector<double>& values, const QStringList& labels) {
        values_ = values;
        labels_ = labels;
        update();
    }

    void setAxisTitles(const QString& y_title, const QString& x_title) {
        y_title_ = y_title;
        x_title_ = x_title;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF bounds = rect().adjusted(32, 8, -12, -28);
        if (values_.empty()) {
            painter.setPen(QColor(140, 140, 140));
            painter.drawText(rect(), Qt::AlignCenter, tr("暂无时段数据"));
            return;
        }

        double max_value = 0.0;
        for (double v : values_) {
            max_value = std::max(max_value, v);
        }
        if (max_value <= 0.0) {
            max_value = 1.0;
        }

        const int count = static_cast<int>(values_.size());
        const double step = bounds.width() / std::max(1, count);
        const double bar_width = step * 0.55;

        painter.setPen(Qt::NoPen);
        for (int i = 0; i < count; ++i) {
            const double ratio = values_[i] / max_value;
            const double height = bounds.height() * ratio;
            const double x = bounds.left() + step * i + (step - bar_width) / 2;
            const double y = bounds.bottom() - height;

            QLinearGradient gradient(QPointF(x, y), QPointF(x, bounds.bottom()));
            gradient.setColorAt(0.0, QColor(54, 207, 201, 200));
            gradient.setColorAt(1.0, QColor(54, 207, 201, 80));
            painter.setBrush(gradient);
            painter.drawRoundedRect(QRectF(x, y, bar_width, height), 4, 4);
        }

        painter.setPen(QColor(140, 140, 140));
        painter.setFont(QFont(painter.font().family(), 9));
        for (int i = 0; i < count && i < labels_.size(); ++i) {
            const double x = bounds.left() + step * i + step / 2 - 18;
            painter.drawText(QRectF(x, bounds.bottom() + 6, 36, 14),
                             Qt::AlignCenter, labels_.at(i));
        }

        painter.drawText(QRectF(4, bounds.top() - 4, 26, 14), Qt::AlignLeft, tr("人"));
        painter.drawText(QRectF(bounds.right() - 90, bounds.bottom() + 20, 90, 14),
                         Qt::AlignRight, tr("时段"));
        if (!y_title_.isEmpty()) {
            painter.drawText(QRectF(bounds.left(), bounds.top() - 14, 160, 14),
                             Qt::AlignLeft, y_title_);
        }
        if (!x_title_.isEmpty()) {
            painter.drawText(QRectF(bounds.right() - 120, bounds.bottom() + 20, 120, 14),
                             Qt::AlignRight, x_title_);
        }
    }

private:
    std::vector<double> values_;
    QStringList labels_;
    QString y_title_;
    QString x_title_;
};

void clear_layout(QLayout* layout) {
    if (!layout) {
        return;
    }
    QLayoutItem* item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

}  // namespace

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent)
    , attendance_service_(nullptr)
    , user_service_(nullptr)
    , need_refresh_(true)
    , range_combo_(nullptr)
    , dept_combo_(nullptr)
    , last_sync_label_(nullptr)
    , data_coverage_label_(nullptr)
    , attendance_rate_label_(nullptr)
    , attendance_detail_label_(nullptr)
    , checkin_label_(nullptr)
    , late_label_(nullptr)
    , early_label_(nullptr)
    , missing_label_(nullptr)
    , similarity_label_(nullptr)
    , abnormal_rate_label_(nullptr)
    , checkout_label_(nullptr)
    , trend_chart_(nullptr)
    , donut_chart_(nullptr)
    , bar_chart_(nullptr)
    , insights_layout_(nullptr)
    , alerts_layout_(nullptr)
    , dept_rank_layout_(nullptr)
    , ai_analysis_btn_(nullptr)
    , is_analyzing_(false)
    , ai_result_label_(nullptr) {
    setup_ui();

    // 连接 AI 服务信号
    auto ai_service = AiAnalysisService::instance();
    connect(ai_service, &AiAnalysisService::analysisStarted, this, &DashboardPage::on_ai_analysis_started);
    connect(ai_service, &AiAnalysisService::analysisResultReady, this, &DashboardPage::on_ai_result_ready);
    connect(ai_service, &AiAnalysisService::analysisFinished, this, &DashboardPage::on_ai_analysis_finished);
    connect(ai_service, &AiAnalysisService::errorOccurred, this, &DashboardPage::on_ai_error);
    connect(ai_service, &AiAnalysisService::analysisCancelled, this, &DashboardPage::on_ai_analysis_cancelled);
}

void DashboardPage::setAttendanceService(service::AttendanceService* service) {
    attendance_service_ = service;
    need_refresh_ = true;
}

void DashboardPage::setUserService(service::UserService* service) {
    user_service_ = service;
    need_refresh_ = true;
}

void DashboardPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (need_refresh_) {
        refreshData();
    }
}

void DashboardPage::on_refresh_clicked() {
    refreshData();
}

void DashboardPage::on_range_changed(int) {
    refreshData();
}

void DashboardPage::on_filter_changed(int) {
    refreshData();
}

void DashboardPage::on_ai_analysis_clicked() {
    if (!attendance_service_ || !user_service_) return;

    // 如果正在分析，则取消
    if (is_analyzing_) {
        if (AiAnalysisService::instance()->isAnalyzing()) {
            AiAnalysisService::instance()->cancelAnalysis();
        } else {
            is_analyzing_ = false;
            ai_result_label_ = nullptr;
            if (ai_analysis_btn_) {
                ai_analysis_btn_->setText(tr("✨ 智能分析"));
                ai_analysis_btn_->setEnabled(true);
            }
        }
        return;
    }

    is_analyzing_ = true;
    
    // 1. 获取今日统计
    std::string today_str = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
    auto today_stats = attendance_service_->get_statistics(today_str);
    
    // 2. 获取趋势简报 (最近7天)
    QDate end_date = QDate::currentDate();
    QDate start_date = end_date.addDays(-6);
    auto range_stats = attendance_service_->get_statistics_range(
        start_date.toString("yyyy-MM-dd").toStdString(),
        end_date.toString("yyyy-MM-dd").toStdString()
    );
    
    QString trend_summary;
    for (const auto& s : range_stats) {
        trend_summary += QString("%1: 出勤%2人, 迟到%3人\n")
            .arg(QString::fromStdString(s.date).right(5))
            .arg(s.check_in_count)
            .arg(s.late_count);
    }
    
    // 3. 获取今日详细记录 (用于深度分析：姓名、部门、时间、状态)
    QString detail_records_str;
    auto today_records = attendance_service_->query_records_by_date(today_str);
    
    // 预加载所有用户部门信息以减少数据库查询
    std::unordered_map<int, std::string> user_depts;
    auto all_users = user_service_->get_all_users();
    for (const auto& u : all_users) {
        user_depts[u.user_id] = u.department;
    }

    // 按时间排序
    std::sort(today_records.begin(), today_records.end(), 
        [](const db::AttendanceRecord& a, const db::AttendanceRecord& b) {
            return a.check_time < b.check_time;
    });

    int count = 0;
    if (today_records.empty()) {
        detail_records_str = "暂无打卡记录\n";
    } else {
        for (const auto& r : today_records) {
            if (count++ >= 50) {
                detail_records_str += "...(更多记录已省略)\n";
                break;
            }
            
            QString dept = "未知部门";
            if (user_depts.find(r.user_id) != user_depts.end()) {
                dept = QString::fromStdString(user_depts[r.user_id]);
                if (dept.isEmpty()) dept = "未分组";
            }

            QString status_str;
            if (r.status == db::AttendanceStatus::STATUS_NORMAL) status_str = "正常";
            else if (r.status == db::AttendanceStatus::STATUS_LATE) status_str = "迟到";
            else if (r.status == db::AttendanceStatus::STATUS_EARLY_LEAVE) status_str = "早退";
            else status_str = "未知";
            
            QString type_str = (r.check_type == db::CheckType::CHECK_IN) ? "签到" : "签退";
            QString time_str = QDateTime::fromTime_t(r.check_time).toString("HH:mm");

            detail_records_str += QString("[%1] %2(%3): %4 %5\n")
                .arg(time_str)
                .arg(QString::fromStdString(r.user_name))
                .arg(dept)
                .arg(type_str)
                .arg(status_str);
        }
    }

    // 4. 发送请求
    spdlog::info("Sending AI analysis with {} records", today_records.size());
    AiAnalysisService::instance()->requestAnalysis(today_stats, trend_summary, detail_records_str);
}

void DashboardPage::on_ai_analysis_started() {
    // 清空之前的分析结果
    if (insights_layout_) {
        // ✅ 先清空指针，避免指向已删除的对象
        ai_result_label_ = nullptr;
        clear_layout(insights_layout_);

        // 创建AI分析结果容器 - 专业的AI回复面板
        auto item = new QFrame();
        item->setObjectName("DashboardInsightItemAI");
        // 深紫色背景，模拟AI助手风格
        item->setStyleSheet(R"(
            QFrame#DashboardInsightItemAI {
                background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(114, 46, 209, 0.15),
                    stop:1 rgba(235, 47, 150, 0.1));
                border: 2px solid rgba(114, 46, 209, 0.4);
                border-radius: 12px;
            }
        )");

        auto item_layout = new QVBoxLayout(item);
        item_layout->setContentsMargins(20, 16, 20, 16);
        item_layout->setSpacing(12);

        // AI 标题栏
        auto header = new QWidget(item);
        auto header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(0, 0, 0, 0);
        header_layout->setSpacing(8);

        auto icon = new QLabel(header);
        icon->setPixmap(SvgIconManager::icon(":/icons/status/info.svg", QSize(20, 20), QColor("#722ed1")).pixmap(20, 20));
        header_layout->addWidget(icon);

        auto title = new QLabel(tr("🤖 AI 智能分析"), header);
        title->setStyleSheet("font-weight: bold; font-size: 13px; color: #722ed1;");
        header_layout->addWidget(title);

        auto loading = new QLabel(tr("分析中..."), header);
        loading->setStyleSheet("font-size: 12px; color: #999;");
        header_layout->addWidget(loading, 1);

        item_layout->addWidget(header);

        // 分割线
        auto separator = new QFrame(item);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: rgba(114, 46, 209, 0.2);");
        item_layout->addWidget(separator);

        // AI 回复内容
        auto text_label = new QLabel(tr("正在分析您的出勤数据，请稍候..."), item);
        text_label->setObjectName("DashboardInsightTextAI");
        text_label->setWordWrap(true);
        text_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        text_label->setStyleSheet(R"(
            QLabel#DashboardInsightTextAI {
                font-size: 13px;
                line-height: 1.8;
                color: #333;
                background-color: transparent;
            }
        )");
        item_layout->addWidget(text_label, 1);

        insights_layout_->addWidget(item);

        // 保存引用
        ai_result_label_ = text_label;
    }

    // 更新按钮状态
    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("⏸ 取消分析"));
        ai_analysis_btn_->setEnabled(true);
    }
}

void DashboardPage::on_ai_result_ready(const QString& result) {
    spdlog::info("Received AI result: {} chars", result.length());

    // 更新AI分析结果（增量模式）
    if (ai_result_label_) {
        QString current_text = ai_result_label_->text();

        // 如果是第一次收到数据，清空"正在分析中"的提示
        if (current_text.contains(tr("正在分析您的出勤数据"))) {
            spdlog::info("First AI result, replacing placeholder");
            ai_result_label_->setText(result);
        } else {
            // 追加新内容（流式显示）
            spdlog::info("Appending AI result");
            ai_result_label_->setText(current_text + result);
        }

        // 确保标签可见并更新
        ai_result_label_->updateGeometry();
        ai_result_label_->update();
    } else {
        spdlog::warn("ai_result_label_ is null, cannot display result");
    }
}


void DashboardPage::on_ai_analysis_finished() {
    is_analyzing_ = false;

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("✨ 智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }

    // 在 AI 回复面板底部添加完成标记
    if (ai_result_label_ && insights_layout_) {
        QString current_text = ai_result_label_->text();
        ai_result_label_->setText(current_text + "\n\n✅ 分析完成");
        // ✅ 不要设置为 nullptr，让 Qt 管理生命周期
        // 下次点击"智能分析"时会创建新的 label
    }

    ToastNotification::showMessage(this, tr("AI 分析"), tr("分析完成"), ToastNotification::Level::Success);
    spdlog::info("AI analysis finished, button restored");
}


void DashboardPage::on_ai_error(const QString& error) {
    is_analyzing_ = false;
    ai_result_label_ = nullptr;

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("✨ 智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }

    // 显示错误信息面板
    if (insights_layout_) {
        clear_layout(insights_layout_);

        auto item = new QFrame();
        item->setObjectName("DashboardInsightItemError");
        item->setStyleSheet(R"(
            QFrame#DashboardInsightItemError {
                background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(255, 77, 79, 0.15),
                    stop:1 rgba(255, 77, 79, 0.1));
                border: 2px solid rgba(255, 77, 79, 0.4);
                border-radius: 12px;
            }
        )");

        auto item_layout = new QVBoxLayout(item);
        item_layout->setContentsMargins(20, 16, 20, 16);
        item_layout->setSpacing(12);

        // 错误标题栏
        auto header = new QWidget(item);
        auto header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(0, 0, 0, 0);
        header_layout->setSpacing(8);

        auto icon = new QLabel(header);
        icon->setPixmap(SvgIconManager::icon(":/icons/status/error.svg", QSize(20, 20),
                                             QColor("#ff4d4f")).pixmap(20, 20));
        header_layout->addWidget(icon);

        auto title = new QLabel(tr("❌ 分析失败"), header);
        title->setStyleSheet("font-weight: bold; font-size: 13px; color: #ff4d4f;");
        header_layout->addWidget(title);
        header_layout->addStretch();

        item_layout->addWidget(header);

        // 分割线
        auto separator = new QFrame(item);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: rgba(255, 77, 79, 0.2);");
        item_layout->addWidget(separator);

        // 错误信息
        auto text_label = new QLabel(error, item);
        text_label->setWordWrap(true);
        text_label->setStyleSheet("color: #ff4d4f; font-size: 12px;");
        item_layout->addWidget(text_label);

        insights_layout_->addWidget(item);
    }

    ToastNotification::showMessage(this, tr("AI 分析"), tr("分析失败: ") + error, ToastNotification::Level::Error);
}

void DashboardPage::on_ai_analysis_cancelled() {
    is_analyzing_ = false;
    ai_result_label_ = nullptr;

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("✨ 智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }

    // 显示取消提示
    if (insights_layout_) {
        clear_layout(insights_layout_);

        auto item = new QFrame();
        item->setObjectName("DashboardInsightItemCancelled");
        item->setStyleSheet(R"(
            QFrame#DashboardInsightItemCancelled {
                background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                    stop:0 rgba(22, 119, 255, 0.1),
                    stop:1 rgba(22, 119, 255, 0.05));
                border: 2px solid rgba(22, 119, 255, 0.3);
                border-radius: 12px;
            }
        )");

        auto item_layout = new QVBoxLayout(item);
        item_layout->setContentsMargins(20, 16, 20, 16);
        item_layout->setSpacing(12);

        // 取消标题栏
        auto header = new QWidget(item);
        auto header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(0, 0, 0, 0);
        header_layout->setSpacing(8);

        auto icon = new QLabel(header);
        icon->setPixmap(SvgIconManager::icon(":/icons/status/info.svg", QSize(20, 20),
                                             QColor("#1677ff")).pixmap(20, 20));
        header_layout->addWidget(icon);

        auto title = new QLabel(tr("⏹️ 分析已取消"), header);
        title->setStyleSheet("font-weight: bold; font-size: 13px; color: #1677ff;");
        header_layout->addWidget(title);
        header_layout->addStretch();

        item_layout->addWidget(header);

        // 分割线
        auto separator = new QFrame(item);
        separator->setFrameShape(QFrame::HLine);
        separator->setStyleSheet("color: rgba(22, 119, 255, 0.2);");
        item_layout->addWidget(separator);

        // 取消信息
        auto text_label = new QLabel(tr("您已取消本次分析，可重新点击按钮开始新的分析"), item);
        text_label->setWordWrap(true);
        text_label->setStyleSheet("color: #666; font-size: 12px;");
        item_layout->addWidget(text_label);

        insights_layout_->addWidget(item);
    }

    ToastNotification::showMessage(this, tr("AI 分析"), tr("分析已取消"), ToastNotification::Level::Info);
    spdlog::info("AI analysis cancelled by user");
}

void DashboardPage::setup_ui() {
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("DashboardScroll");

    auto content = new QWidget(scroll);
    content->setObjectName("DashboardContent");
    scroll->setWidget(content);

    auto root_layout = new QVBoxLayout(this);
    root_layout->setContentsMargins(0, 0, 0, 0);
    root_layout->addWidget(scroll);

    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    auto header = new QFrame(content);
    header->setObjectName("DashboardHeader");
    auto header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(20, 16, 20, 16);
    header_layout->setSpacing(16);

    auto title_group = new QWidget(header);
    auto title_layout = new QVBoxLayout(title_group);
    title_layout->setContentsMargins(0, 0, 0, 0);
    title_layout->setSpacing(4);
    auto title_label = new QLabel(tr("智能统计看板"), title_group);
    title_label->setObjectName("DashboardTitle");
    auto subtitle_label = new QLabel(tr("实时掌握出勤质量与风险变化"), title_group);
    subtitle_label->setObjectName("DashboardSubtitle");
    title_layout->addWidget(title_label);
    title_layout->addWidget(subtitle_label);
    header_layout->addWidget(title_group, 1);

    auto filters_group = new QWidget(header);
    auto filters_layout = new QHBoxLayout(filters_group);
    filters_layout->setContentsMargins(0, 0, 0, 0);
    filters_layout->setSpacing(12);

    range_combo_ = new QComboBox(filters_group);
    range_combo_->addItem(tr("近7天"), 7);
    range_combo_->addItem(tr("近30天"), 30);
    range_combo_->setObjectName("DashboardFilterCombo");
    connect(range_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DashboardPage::on_range_changed);
    filters_layout->addWidget(range_combo_);

    dept_combo_ = new QComboBox(filters_group);
    dept_combo_->addItem(tr("全部部门"));
    dept_combo_->setObjectName("DashboardFilterCombo");
    connect(dept_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DashboardPage::on_filter_changed);
    filters_layout->addWidget(dept_combo_);

    auto refresh_btn = new QPushButton(tr("刷新"), filters_group);
    refresh_btn->setObjectName("DashboardRefreshButton");
    refresh_btn->setMinimumWidth(90);
    connect(refresh_btn, &QPushButton::clicked, this, &DashboardPage::on_refresh_clicked);
    filters_layout->addWidget(refresh_btn);

    header_layout->addWidget(filters_group);

    auto meta_group = new QWidget(header);
    auto meta_layout = new QVBoxLayout(meta_group);
    meta_layout->setContentsMargins(0, 0, 0, 0);
    meta_layout->setSpacing(8);
    data_coverage_label_ = new QLabel(tr("数据覆盖率 0%"), meta_group);
    data_coverage_label_->setObjectName("DashboardMetaTag");
    data_coverage_label_->setProperty("tone", QVariant("primary"));
    last_sync_label_ = new QLabel(tr("最后同步 --:--"), meta_group);
    last_sync_label_->setObjectName("DashboardMetaTag");
    last_sync_label_->setProperty("tone", QVariant("neutral"));
    meta_layout->addWidget(data_coverage_label_);
    meta_layout->addWidget(last_sync_label_);
    header_layout->addWidget(meta_group);

    layout->addWidget(header);

    auto kpi_container = new QWidget(content);
    auto kpi_layout = new QGridLayout(kpi_container);
    kpi_layout->setContentsMargins(0, 0, 0, 0);
    kpi_layout->setHorizontalSpacing(16);
    kpi_layout->setVerticalSpacing(16);

    auto make_kpi = [kpi_container](const QString& title,
                                    const QString& icon_path,
                                    const QColor& color,
                                    QLabel** value_label,
                                    QLabel** sub_label,
                                    const QString& subtitle) {
        auto card = new QFrame(kpi_container);
        card->setObjectName("DashboardKpiCard");
        card->setProperty("tone", QVariant("primary"));

        auto card_layout = new QHBoxLayout(card);
        card_layout->setContentsMargins(16, 14, 16, 14);
        card_layout->setSpacing(12);

        auto icon = new QLabel(card);
        icon->setObjectName("DashboardKpiIcon");
        icon->setPixmap(SvgIconManager::icon(icon_path, QSize(20, 20), color).pixmap(20, 20));
        QColor icon_bg = color.lighter(190);
        icon->setStyleSheet(QString("background-color: %1; border-radius: 10px;").arg(icon_bg.name()));
        icon->setFixedSize(36, 36);
        card_layout->addWidget(icon);

        auto text_group = new QWidget(card);
        auto text_layout = new QVBoxLayout(text_group);
        text_layout->setContentsMargins(0, 0, 0, 0);
        text_layout->setSpacing(4);
        auto title_label = new QLabel(title, card);
        title_label->setObjectName("DashboardKpiTitle");
        *value_label = new QLabel("--", card);
        (*value_label)->setObjectName("DashboardKpiValue");
        (*value_label)->setStyleSheet(QString("color: %1;").arg(color.name()));
        auto sub_value = new QLabel(subtitle, card);
        sub_value->setObjectName("DashboardKpiSub");
        if (sub_label) {
            *sub_label = sub_value;
        }
        text_layout->addWidget(title_label);
        text_layout->addWidget(*value_label);
        text_layout->addWidget(sub_value);
        card_layout->addWidget(text_group, 1);
        return card;
    };

    kpi_layout->addWidget(make_kpi(tr("今日到岗率"), ":/icons/status/check-circle.svg",
                                   QColor("#1677ff"), &attendance_rate_label_,
                                   &attendance_detail_label_, tr("实到 0 / 应到 0")), 0, 0);
    kpi_layout->addWidget(make_kpi(tr("签到人数"), ":/icons/status/user-check.svg",
                                   QColor("#52c41a"), &checkin_label_,
                                   nullptr, tr("今日")), 0, 1);
    kpi_layout->addWidget(make_kpi(tr("迟到人数"), ":/icons/status/alert-circle.svg",
                                   QColor("#fa8c16"), &late_label_,
                                   nullptr, tr("今日")), 0, 2);
    kpi_layout->addWidget(make_kpi(tr("早退人数"), ":/icons/status/x-circle.svg",
                                   QColor("#f5222d"), &early_label_,
                                   nullptr, tr("今日")), 0, 3);
    kpi_layout->addWidget(make_kpi(tr("未打卡"), ":/icons/status/user-x.svg",
                                   QColor("#722ed1"), &missing_label_,
                                   nullptr, tr("今日")), 1, 0);
    kpi_layout->addWidget(make_kpi(tr("平均相似度"), ":/icons/status/info.svg",
                                   QColor("#13c2c2"), &similarity_label_,
                                   nullptr, tr("今日")), 1, 1);
    kpi_layout->addWidget(make_kpi(tr("异常识别率"), ":/icons/status/alert-circle.svg",
                                   QColor("#d46b08"), &abnormal_rate_label_,
                                   nullptr, tr("今日")), 1, 2);
    kpi_layout->addWidget(make_kpi(tr("签退人数"), ":/icons/status/user-check.svg",
                                   QColor("#9254de"), &checkout_label_,
                                   nullptr, tr("今日")), 1, 3);

    layout->addWidget(kpi_container);

    auto top_row = new QHBoxLayout();
    top_row->setSpacing(16);

    auto insight_card = new CardWidget(content);

    // 创建自定义标题栏（标题 + AI 分析按钮）
    auto insight_header = new QWidget(insight_card);
    auto insight_header_layout = new QHBoxLayout(insight_header);
    insight_header_layout->setContentsMargins(0, 0, 0, 0);
    insight_header_layout->setSpacing(12);

    auto insight_title = new QLabel(tr("智能分析"), insight_header);
    insight_title->setObjectName("CardTitle");
    insight_header_layout->addWidget(insight_title);

    insight_header_layout->addStretch();

    // AI 分析按钮（移到智能分析板块右侧）
    ai_analysis_btn_ = new QPushButton(tr("✨ 智能分析"), insight_header);
    ai_analysis_btn_->setObjectName("DashboardAiButton");
    ai_analysis_btn_->setCursor(Qt::PointingHandCursor);
    ai_analysis_btn_->setStyleSheet(R"(
        QPushButton {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #722ed1, stop:1 #eb2f96);
            color: white;
            border: none;
            border-radius: 16px;
            padding: 6px 16px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #854eca, stop:1 #f759ab);
        }
        QPushButton:pressed {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #531dab, stop:1 #c41d7f);
        }
        QPushButton:disabled {
            background-color: #444;
            color: #888;
        }
    )");
    connect(ai_analysis_btn_, &QPushButton::clicked, this, &DashboardPage::on_ai_analysis_clicked);
    insight_header_layout->addWidget(ai_analysis_btn_);

    insight_card->setHeaderWidget(insight_header);
    insight_card->setMinimumHeight(300);
    auto insight_layout = new QVBoxLayout(insight_card->bodyContainer());
    insight_layout->setContentsMargins(0, 0, 0, 0);
    insight_layout->setSpacing(12);
    insights_layout_ = insight_layout;
    top_row->addWidget(insight_card, 2);

    auto alert_card = new CardWidget(content);
    alert_card->setTitle(tr("异常列表"));
    alert_card->setMinimumHeight(300);
    auto alert_layout = new QVBoxLayout(alert_card->bodyContainer());
    alert_layout->setContentsMargins(0, 0, 0, 0);
    alert_layout->setSpacing(10);
    alerts_layout_ = alert_layout;
    top_row->addWidget(alert_card, 1);

    layout->addLayout(top_row);

    auto chart_row = new QHBoxLayout();
    chart_row->setSpacing(16);

    auto trend_card = new CardWidget(content);
    trend_card->setTitle(tr("出勤趋势"));
    auto trend_layout = new QVBoxLayout(trend_card->bodyContainer());
    trend_layout->setContentsMargins(0, 0, 0, 0);
    trend_layout->setSpacing(12);

    auto legend_row = new QHBoxLayout();
    legend_row->setSpacing(16);

    auto legend_primary = new QLabel(tr("到岗率（%）"), trend_card);
    legend_primary->setObjectName("DashboardLegendLabel");
    auto legend_secondary = new QLabel(tr("异常率（%）"), trend_card);
    legend_secondary->setObjectName("DashboardLegendLabel");

    auto legend_primary_dot = new QFrame(trend_card);
    legend_primary_dot->setObjectName("DashboardLegendDot");
    legend_primary_dot->setProperty("tone", QVariant("primary"));
    legend_primary_dot->setFixedSize(10, 10);

    auto legend_secondary_dot = new QFrame(trend_card);
    legend_secondary_dot->setObjectName("DashboardLegendDot");
    legend_secondary_dot->setProperty("tone", QVariant("warning"));
    legend_secondary_dot->setFixedSize(10, 10);

    auto legend_primary_wrap = new QWidget(trend_card);
    auto legend_primary_layout = new QHBoxLayout(legend_primary_wrap);
    legend_primary_layout->setContentsMargins(0, 0, 0, 0);
    legend_primary_layout->setSpacing(6);
    legend_primary_layout->addWidget(legend_primary_dot);
    legend_primary_layout->addWidget(legend_primary);

    auto legend_secondary_wrap = new QWidget(trend_card);
    auto legend_secondary_layout = new QHBoxLayout(legend_secondary_wrap);
    legend_secondary_layout->setContentsMargins(0, 0, 0, 0);
    legend_secondary_layout->setSpacing(6);
    legend_secondary_layout->addWidget(legend_secondary_dot);
    legend_secondary_layout->addWidget(legend_secondary);

    legend_row->addWidget(legend_primary_wrap);
    legend_row->addWidget(legend_secondary_wrap);
    legend_row->addStretch();
    trend_layout->addLayout(legend_row);

    auto trend_chart = new TrendChartWidget(trend_card);
    trend_chart->setObjectName("DashboardTrendChart");
    trend_chart->setAxisTitles(tr("到岗率/异常率（%）"), tr("日期"));
    trend_layout->addWidget(trend_chart, 1);
    trend_chart_ = trend_chart;

    chart_row->addWidget(trend_card, 3);

    auto dist_card = new CardWidget(content);
    dist_card->setTitle(tr("异常分布"));
    auto dist_layout = new QVBoxLayout(dist_card->bodyContainer());
    dist_layout->setContentsMargins(0, 0, 0, 0);
    dist_layout->setSpacing(16);

    auto dist_top = new QHBoxLayout();
    dist_top->setSpacing(16);

    auto donut = new DonutChartWidget(dist_card);
    donut->setObjectName("DashboardDonutChart");
    dist_top->addWidget(donut, 1);
    donut_chart_ = donut;

    auto dist_info = new QWidget(dist_card);
    auto dist_info_layout = new QVBoxLayout(dist_info);
    dist_info_layout->setContentsMargins(0, 0, 0, 0);
    dist_info_layout->setSpacing(10);
    auto dist_title = new QLabel(tr("异常明细（部门签到 Top 5）"), dist_info);
    dist_title->setObjectName("DashboardSectionTitle");
    dist_info_layout->addWidget(dist_title);
    dept_rank_layout_ = new QVBoxLayout();
    dept_rank_layout_->setSpacing(8);
    dist_info_layout->addLayout(dept_rank_layout_);
    dist_info_layout->addStretch();
    dist_top->addWidget(dist_info, 1);

    dist_layout->addLayout(dist_top);

    auto bar = new BarChartWidget(dist_card);
    bar->setObjectName("DashboardBarChart");
    bar->setAxisTitles(tr("签到人数（人）"), tr("签到时段"));
    dist_layout->addWidget(bar);
    bar_chart_ = bar;

    chart_row->addWidget(dist_card, 2);
    layout->addLayout(chart_row);
    layout->addStretch();
}

void DashboardPage::refreshData() {
    if (!attendance_service_ || !user_service_) {
        return;
    }

    const QString current_dept = dept_combo_ ? dept_combo_->currentText() : QString();

    std::vector<db::UserInfo> users = user_service_->get_all_users(db::UserStatus::USER_ENABLED);

    std::unordered_map<int, QString> user_departments;
    QMap<QString, int> dept_user_counts;
    for (const auto& user : users) {
        QString dept = QString::fromStdString(user.department);
        if (dept.trimmed().isEmpty()) {
            dept = tr("未分组");
        }
        user_departments[user.user_id] = dept;
        dept_user_counts[dept] += 1;
    }

    if (dept_combo_) {
        dept_combo_->blockSignals(true);
        dept_combo_->clear();
        dept_combo_->addItem(tr("全部部门"));
        const QStringList dept_names = dept_user_counts.keys();
        for (const auto& name : dept_names) {
            dept_combo_->addItem(name);
        }
        int idx = dept_combo_->findText(current_dept);
        if (idx >= 0) {
            dept_combo_->setCurrentIndex(idx);
        }
        dept_combo_->blockSignals(false);
    }

    const QString dept_filter = dept_combo_ ? dept_combo_->currentText() : QString();
    const bool filter_dept = !dept_filter.isEmpty() && dept_filter != tr("全部部门");

    int total_users = 0;
    for (const auto& user : users) {
        if (filter_dept) {
            auto it = user_departments.find(user.user_id);
            if (it != user_departments.end() && it->second == dept_filter) {
                total_users++;
            }
        } else {
            total_users++;
        }
    }

    const QString today = QDate::currentDate().toString("yyyy-MM-dd");
    auto records = attendance_service_->query_records_by_date(today.toStdString());

    std::unordered_set<int> checkin_users;
    std::unordered_set<int> checkout_users;
    std::unordered_set<int> late_users;
    std::unordered_set<int> early_users;
    QMap<QString, int> dept_checkin_counts;
    std::vector<db::AttendanceRecord> abnormal_records;
    int filtered_records = 0;

    double similarity_sum = 0.0;
    int similarity_count = 0;
    int low_similarity = 0;

    const float threshold = ConfigManager::instance()->getRecognitionThreshold();

    for (const auto& record : records) {
        if (filter_dept) {
            auto it = user_departments.find(record.user_id);
            if (it == user_departments.end() || it->second != dept_filter) {
                continue;
            }
        }
        filtered_records++;

        if (record.check_type == db::CheckType::CHECK_IN) {
            checkin_users.insert(record.user_id);
            auto it = user_departments.find(record.user_id);
            const QString dept = (it != user_departments.end()) ? it->second : tr("未分组");
            dept_checkin_counts[dept] += 1;
        } else if (record.check_type == db::CheckType::CHECK_OUT) {
            checkout_users.insert(record.user_id);
        }

        if (record.status == db::AttendanceStatus::STATUS_LATE) {
            late_users.insert(record.user_id);
            abnormal_records.push_back(record);
        } else if (record.status == db::AttendanceStatus::STATUS_EARLY_LEAVE) {
            early_users.insert(record.user_id);
            abnormal_records.push_back(record);
        }

        similarity_sum += record.similarity;
        similarity_count++;
        if (record.similarity < threshold) {
            low_similarity++;
        }
    }

    // 业务规则：有签退但无签到，判定为迟到
    for (const auto user_id : checkout_users) {
        if (checkin_users.find(user_id) == checkin_users.end()) {
            late_users.insert(user_id);
        }
    }

    std::unordered_set<int> present_users = checkin_users;
    present_users.insert(checkout_users.begin(), checkout_users.end());

    const int checked_in = static_cast<int>(checkin_users.size());
    const int checked_out = static_cast<int>(checkout_users.size());
    const int present_count = static_cast<int>(present_users.size());
    const int late_count = static_cast<int>(late_users.size());
    const int early_count = static_cast<int>(early_users.size());
    const int missing_count = std::max(0, total_users - present_count);
    const double attendance_rate = (total_users > 0) ? (static_cast<double>(present_count) / total_users) : 0.0;
    const double avg_similarity = (similarity_count > 0) ? (similarity_sum / similarity_count) : 0.0;
    const double abnormal_rate = filtered_records == 0
        ? 0.0
        : (static_cast<double>(low_similarity) / filtered_records);

    if (attendance_rate_label_) {
        attendance_rate_label_->setText(QString::number(attendance_rate * 100.0, 'f', 1) + "%");
    }
    if (attendance_detail_label_) {
        attendance_detail_label_->setText(tr("实到 %1 / 应到 %2").arg(present_count).arg(total_users));
    }
    if (checkin_label_) {
        checkin_label_->setText(QString::number(checked_in));
    }
    if (late_label_) {
        late_label_->setText(QString::number(late_count));
    }
    if (early_label_) {
        early_label_->setText(QString::number(early_count));
    }
    if (missing_label_) {
        missing_label_->setText(QString::number(missing_count));
    }
    if (similarity_label_) {
        similarity_label_->setText(QString::number(avg_similarity, 'f', 2));
    }
    if (abnormal_rate_label_) {
        abnormal_rate_label_->setText(QString::number(abnormal_rate * 100.0, 'f', 1) + "%");
    }
    if (checkout_label_) {
        checkout_label_->setText(QString::number(checked_out));
    }

    if (data_coverage_label_) {
        data_coverage_label_->setText(tr("数据覆盖率 %1%")
                                          .arg(QString::number(attendance_rate * 100.0, 'f', 1)));
    }
    if (last_sync_label_) {
        last_sync_label_->setText(tr("最后同步 %1").arg(QTime::currentTime().toString("HH:mm")));
    }

    int range_days = 7;
    if (range_combo_) {
        range_days = range_combo_->currentData().toInt();
        if (range_days <= 0) {
            range_days = 7;
        }
    }

    TrendSeries series;
    series.primary.reserve(range_days);
    series.secondary.reserve(range_days);

    // 计算日期范围
    QDate end_date = QDate::currentDate();
    QDate start_date = end_date.addDays(-(range_days - 1));
    
    // 使用优化的批量查询接口，一次性获取所有统计数据
    auto range_stats = attendance_service_->get_statistics_range(
        start_date.toString("yyyy-MM-dd").toStdString(),
        end_date.toString("yyyy-MM-dd").toStdString()
    );

    // 将统计结果转为 Map 方便按日期查找（因为数据库可能某天无数据，返回列表不连续）
    std::map<std::string, service::AttendanceStatistics> stats_map;
    for (const auto& s : range_stats) {
        stats_map[s.date] = s;
    }

    for (int i = 0; i < range_days; ++i) {
        QDate date = start_date.addDays(i);
        QString date_str = date.toString("yyyy-MM-dd");
        std::string date_std = date_str.toStdString();
        
        double day_rate = 0.0;
        double day_abnormal_rate = 0.0;

        auto it = stats_map.find(date_std);
        if (it != stats_map.end()) {
            const auto& stat = it->second;
            // 出勤率 = 出勤人数 / 总用户数
            day_rate = (total_users > 0) ? (static_cast<double>(stat.check_in_count) / total_users) : 0.0;
            
            // 异常率 = (迟到+早退) / 总记录数
            int abnormal_count = stat.late_count + stat.early_leave_count;
            day_abnormal_rate = (stat.total_count > 0) 
                ? (static_cast<double>(abnormal_count) / stat.total_count) 
                : 0.0;
        }

        series.primary.push_back(day_rate * 100.0);
        series.secondary.push_back(day_abnormal_rate * 100.0);
        series.labels << date.toString("MM/dd");
    }

    if (trend_chart_) {
        static_cast<TrendChartWidget*>(trend_chart_)->setSeries(series);
    }

    std::vector<DonutSegment> segments;
    segments.push_back({static_cast<double>(late_count), QColor("#fa8c16"), tr("迟到")});
    segments.push_back({static_cast<double>(early_count), QColor("#f5222d"), tr("早退")});
    segments.push_back({static_cast<double>(missing_count), QColor("#722ed1"), tr("未打卡")});
    segments.push_back({static_cast<double>(low_similarity), QColor("#13c2c2"), tr("低相似度")});
    if (donut_chart_) {
        auto donut = static_cast<DonutChartWidget*>(donut_chart_);
        donut->setSegments(segments);
        donut->setCenterText(tr("异常占比"),
                             QString::number((late_count + early_count + low_similarity) > 0
                                                 ? (static_cast<double>(late_count + early_count + low_similarity) /
                                                    std::max(1, checked_in + checked_out) * 100.0)
                                                 : 0.0,
                                             'f', 1) + "%");
    }

    if (dept_rank_layout_) {
        clear_layout(dept_rank_layout_);
        std::vector<std::pair<QString, int>> dept_sorted;
        dept_sorted.reserve(dept_checkin_counts.size());
        for (auto it = dept_checkin_counts.cbegin(); it != dept_checkin_counts.cend(); ++it) {
            dept_sorted.emplace_back(it.key(), it.value());
        }
        std::sort(dept_sorted.begin(), dept_sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        const int max_count = dept_sorted.empty() ? 1 : dept_sorted.front().second;
        const int show_count = std::min(5, static_cast<int>(dept_sorted.size()));
        for (int i = 0; i < show_count; ++i) {
            auto row = new QWidget();
            row->setObjectName("DashboardRankItem");
            auto row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            row_layout->setSpacing(8);

            auto name_label = new QLabel(dept_sorted[i].first, row);
            name_label->setObjectName("DashboardRankName");
            row_layout->addWidget(name_label);

            auto bar = new QProgressBar(row);
            bar->setObjectName("DashboardRankBar");
            bar->setRange(0, max_count);
            bar->setValue(dept_sorted[i].second);
            bar->setTextVisible(false);
            bar->setFixedHeight(8);
            row_layout->addWidget(bar, 1);

            auto value_label = new QLabel(QString::number(dept_sorted[i].second), row);
            value_label->setObjectName("DashboardRankValue");
            row_layout->addWidget(value_label);

            dept_rank_layout_->addWidget(row);
        }
        if (show_count == 0) {
            auto empty = new QLabel(tr("暂无部门签到数据"));
            empty->setObjectName("DashboardEmptyText");
            dept_rank_layout_->addWidget(empty);
        }
    }

    if (bar_chart_) {
        std::vector<double> bars(6, 0.0);
        QStringList labels = {QStringLiteral("07"), QStringLiteral("08"), QStringLiteral("09"),
                              QStringLiteral("10"), QStringLiteral("11"), QStringLiteral("12")};
        for (const auto& record : records) {
            if (record.check_type != db::CheckType::CHECK_IN) {
                continue;
            }
            if (filter_dept) {
                auto it = user_departments.find(record.user_id);
                if (it == user_departments.end() || it->second != dept_filter) {
                    continue;
                }
            }
            const QDateTime dt = QDateTime::fromTime_t(record.check_time);
            const int hour = dt.time().hour();
            if (hour >= 7 && hour <= 12) {
                bars[hour - 7] += 1.0;
            }
        }
        static_cast<BarChartWidget*>(bar_chart_)->setValues(bars, labels);
    }

    if (insights_layout_) {
        if (!is_analyzing_) {
            // 清空旧的分析结果，等待用户点击"智能分析"按钮
            clear_layout(insights_layout_);
            ai_result_label_ = nullptr;

            // 显示提示信息
            auto tip_item = new QFrame();
            tip_item->setObjectName("DashboardInsightItem");
            auto tip_layout = new QHBoxLayout(tip_item);
            tip_layout->setContentsMargins(12, 10, 12, 10);
            tip_layout->setSpacing(10);

            auto tip_icon = new QLabel(tip_item);
            tip_icon->setObjectName("DashboardInsightIcon");
            tip_icon->setPixmap(SvgIconManager::icon(":/icons/status/info.svg", QSize(16, 16),
                                                     QColor("#1677ff")).pixmap(16, 16));
            tip_layout->addWidget(tip_icon);

            auto tip_text = new QLabel(tr("点击右上角「智能分析」按钮获取 AI 驱动的深度分析报告"), tip_item);
            tip_text->setObjectName("DashboardInsightText");
            tip_text->setWordWrap(true);
            tip_layout->addWidget(tip_text, 1);

            insights_layout_->addWidget(tip_item);
        }
    }

    if (alerts_layout_) {
        clear_layout(alerts_layout_);
        std::sort(abnormal_records.begin(), abnormal_records.end(),
                  [](const db::AttendanceRecord& a, const db::AttendanceRecord& b) {
                      return a.check_time > b.check_time;
                  });

        const int show_count = std::min(5, static_cast<int>(abnormal_records.size()));
        for (int i = 0; i < show_count; ++i) {
            const auto& record = abnormal_records[i];
            auto row = new QFrame();
            row->setObjectName("DashboardAlertItem");
            auto row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(12, 10, 12, 10);
            row_layout->setSpacing(10);

            const QString name = QString::fromStdString(record.user_name);
            auto avatar = new QLabel(name.left(1), row);
            avatar->setObjectName("DashboardAlertAvatar");
            avatar->setFixedSize(36, 36);
            avatar->setAlignment(Qt::AlignCenter);
            row_layout->addWidget(avatar);

            auto info_group = new QWidget(row);
            auto info_layout = new QVBoxLayout(info_group);
            info_layout->setContentsMargins(0, 0, 0, 0);
            info_layout->setSpacing(2);
            auto name_label = new QLabel(name, info_group);
            name_label->setObjectName("DashboardAlertName");
            auto time_label = new QLabel(
                QDateTime::fromTime_t(record.check_time).toString("HH:mm"),
                info_group);
            time_label->setObjectName("DashboardAlertTime");
            info_layout->addWidget(name_label);
            info_layout->addWidget(time_label);
            row_layout->addWidget(info_group, 1);

            QString status_text;
            QString badge_type;
            if (record.status == db::AttendanceStatus::STATUS_LATE) {
                status_text = tr("迟到");
                badge_type = "late";
            } else {
                status_text = tr("早退");
                badge_type = "early";
            }

            auto badge = new QLabel(status_text, row);
            badge->setObjectName("DashboardBadge");
            badge->setProperty("badgeType", QVariant(badge_type));
            row_layout->addWidget(badge);

            alerts_layout_->addWidget(row);
        }

        if (show_count == 0) {
            auto empty = new QLabel(tr("暂无异常记录"));
            empty->setObjectName("DashboardEmptyText");
            alerts_layout_->addWidget(empty);
        }
    }

    need_refresh_ = false;
}

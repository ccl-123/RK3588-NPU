#include "ui/dashboard_page.h"
#include "gui_services/ai_analysis_service.h"
#include "gui_services/local_ai_analysis_service.h"
#include "gui_services/asr_service.h"
#include "app/local_llm_thread.h"
#include "config/config.h"

#include "database/database_types.h"
#include "service/attendance_service.h"
#include "service/user_service.h"
#include "gui_utils/config_manager.h"
#include "gui_utils/svg_icon_manager.h"
#include "widgets/card_widget.h"
#include "widgets/toast_notification.h"

#include <spdlog/spdlog.h>
#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QHideEvent>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSpacerItem>
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

constexpr int kMaxAiChatMessages = 200;

struct TrendSeries {
    std::vector<double> primary;
    std::vector<double> secondary;
    QStringList labels;
};

void apply_state_property(QWidget* widget, const char* name, const char* value) {
    if (!widget) {
        return;
    }
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QString resolve_local_llm_model_path() {
    const QString configured = QString::fromUtf8(Config::LocalLLM::getModelPath()).trimmed();
    if (configured.isEmpty()) {
        return QString();
    }

    const QFileInfo configured_info(configured);
    if (configured_info.isAbsolute()) {
        return configured_info.absoluteFilePath();
    }

    const QString app_dir = QCoreApplication::applicationDirPath();
    const QString current_dir = QDir::currentPath();

    QStringList candidate_paths = {
        QDir(app_dir).filePath(configured),
        QDir(app_dir).filePath("data/model/" + configured),
        QDir(app_dir).filePath("../data/model/" + configured),
        QDir(app_dir).filePath("../../data/model/" + configured),
        QDir(current_dir).filePath(configured),
        QDir(current_dir).filePath("data/model/" + configured),
        QDir(current_dir).filePath("C++/face_recognition_cap/data/model/" + configured),
        QDir(current_dir).filePath("install/face_recognition_cap/data/model/" + configured),
        QDir(current_dir).filePath("C++/face_recognition_cap/install/face_recognition_cap/data/model/" + configured),
    };

    for (const QString& candidate : candidate_paths) {
        QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists()) {
            return info.absoluteFilePath();
        }
    }

    return QDir(app_dir).filePath(configured);
}

bool has_custom_remote_backend() {
    return !QString::fromUtf8(Config::LlamaCpp::getBaseUrl()).trimmed().isEmpty();
}

QString remote_backend_display_name() {
    if (has_custom_remote_backend()) {
        const QString model = QString::fromUtf8(Config::LlamaCpp::getModel()).trimmed();
        return model.isEmpty() ? QStringLiteral("自定义模型") : model;
    }
    return QStringLiteral("OpenAI 兼容");
}

QString remote_backend_tooltip() {
    if (has_custom_remote_backend()) {
        return QObject::tr("点击切换到本地大模型");
    }
    return QObject::tr("点击切换到本地大模型");
}

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
    , checkin_sub_label_(nullptr)
    , late_label_(nullptr)
    , late_sub_label_(nullptr)
    , early_label_(nullptr)
    , early_sub_label_(nullptr)
    , missing_label_(nullptr)
    , missing_sub_label_(nullptr)
    , similarity_label_(nullptr)
    , similarity_sub_label_(nullptr)
    , abnormal_rate_label_(nullptr)
    , abnormal_rate_sub_label_(nullptr)
    , checkout_label_(nullptr)
    , checkout_sub_label_(nullptr)
    , trend_chart_(nullptr)
    , alerts_layout_(nullptr)
    , ai_analysis_btn_(nullptr)
    , is_analyzing_(false)
    , ai_scroll_(nullptr)
    , ai_chat_container_(nullptr)
    , ai_chat_layout_(nullptr)
    , ai_chat_spacer_(nullptr)
    , ai_input_(nullptr)
    , ai_send_btn_(nullptr)
    , asr_backend_toggle_btn_(nullptr)
    , ai_voice_btn_(nullptr)
    , asr_status_label_(nullptr)
    , backend_toggle_btn_(nullptr)
    , backend_status_label_(nullptr)
    , is_local_llm_(false)
    , agent_status_label_(nullptr) {
    setup_ui();

    // 连接 AI 服务信号
    auto cloud_service = AiAnalysisService::instance();
    connect(cloud_service, &AiAnalysisService::analysisStarted, this, &DashboardPage::on_ai_analysis_started);
    connect(cloud_service, &AiAnalysisService::streamEventReady, this, &DashboardPage::on_ai_stream_event);
    connect(cloud_service, &AiAnalysisService::analysisFinished, this, &DashboardPage::on_ai_analysis_finished);
    connect(cloud_service, &AiAnalysisService::errorOccurred, this, &DashboardPage::on_ai_error);
    connect(cloud_service, &AiAnalysisService::analysisCancelled, this, &DashboardPage::on_ai_analysis_cancelled);

    auto local_service = LocalAiAnalysisService::instance();
    connect(local_service, &LocalAiAnalysisService::analysisStarted, this, &DashboardPage::on_ai_analysis_started);
    connect(local_service, &LocalAiAnalysisService::streamEventReady, this, &DashboardPage::on_ai_stream_event);
    connect(local_service, &LocalAiAnalysisService::analysisFinished, this, &DashboardPage::on_ai_analysis_finished);
    connect(local_service, &LocalAiAnalysisService::errorOccurred, this, &DashboardPage::on_ai_error);
    connect(local_service, &LocalAiAnalysisService::analysisCancelled, this, &DashboardPage::on_ai_analysis_cancelled);
    connect(local_service, &LocalAiAnalysisService::localLLMReady, this, &DashboardPage::on_local_llm_ready);
    connect(local_service, &LocalAiAnalysisService::localLLMReleased, this, &DashboardPage::on_local_llm_released);

    // 连接 ASR 语音识别服务信号
    auto asr = AsrService::instance();
    connect(asr, &AsrService::transcriptionReady, this, &DashboardPage::on_asr_transcription);
    connect(asr, &AsrService::transcriptionFinished, this, &DashboardPage::on_asr_finished);
    connect(asr, &AsrService::asrError, this, &DashboardPage::on_asr_error);
    connect(asr, &AsrService::recordingStateChanged, this, &DashboardPage::on_asr_recording_state);
    connect(asr, &AsrService::transcribingStateChanged, this, &DashboardPage::on_asr_transcribing_state);
    connect(asr, &AsrService::recordingDurationChanged, this, &DashboardPage::on_asr_duration);
    connect(asr, &AsrService::backendModeChanged, this, &DashboardPage::on_asr_backend_changed);
    connect(asr, &AsrService::localReadyChanged, this, &DashboardPage::on_local_asr_ready_changed);
    connect(asr, &AsrService::localLoadingChanged, this, &DashboardPage::on_local_asr_loading_changed);
}

void DashboardPage::setAttendanceService(service::AttendanceService* service) {
    attendance_service_ = service;
    need_refresh_ = true;
}

void DashboardPage::setUserService(service::UserService* service) {
    user_service_ = service;
    need_refresh_ = true;

    // 初始化 Agent（当两个服务都设置后）
    if (attendance_service_ && user_service_) {
        // 初始化本地 Agent
        LocalAiAnalysisService::instance()->initializeAgent(attendance_service_, user_service_);
        // 初始化云端 Agent
        AiAnalysisService::instance()->initializeAgent(attendance_service_, user_service_);
    }
}

void DashboardPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (need_refresh_) {
        refreshData();
    }
    if (asr_backend_toggle_btn_ && asr_backend_toggle_btn_->isChecked()) {
        auto* asr = AsrService::instance();
        if (asr->backendMode() != AsrBackendMode::Local || !asr->isLocalReady()) {
            asr->setBackendMode(AsrBackendMode::Local);
        }
    }
}

void DashboardPage::hideEvent(QHideEvent* event) {
    AsrService::instance()->stopRecordingForPageLeave();
    QWidget::hideEvent(event);
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

void DashboardPage::appendChatMessage(const QString& role, const QString& text) {
    if (!ai_chat_layout_ || !ai_chat_container_) {
        return;
    }

    auto row = new QWidget(ai_chat_container_);
    auto row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);

    auto bubble = new QFrame(row);
    bubble->setObjectName("AiChatBubble");
    bubble->setProperty("role", role);
    bubble->setMaximumWidth(520);
    auto bubble_layout = new QVBoxLayout(bubble);
    bubble_layout->setContentsMargins(12, 10, 12, 10);
    bubble_layout->setSpacing(6);

    auto block = new QFrame(bubble);
    block->setObjectName("AiChatBlock");
    block->setProperty("blockType", "text");
    auto block_layout = new QVBoxLayout(block);
    block_layout->setContentsMargins(8, 6, 8, 6);
    block_layout->setSpacing(0);

    auto label = new QLabel(text, block);
    label->setObjectName("AiChatText");
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    block_layout->addWidget(label);
    bubble_layout->addWidget(block);

    if (role == "user") {
        row_layout->addStretch();
        row_layout->addWidget(bubble);
    } else {
        row_layout->addWidget(bubble);
        row_layout->addStretch();
    }

    int insert_pos = ai_chat_layout_->count();
    if (ai_chat_spacer_) {
        insert_pos = ai_chat_layout_->count() - 1;
    }
    ai_chat_layout_->insertWidget(insert_pos, row);
    trimChatHistory();
    scrollChatToBottom();
}

void DashboardPage::beginAssistantRenderMessage(const QString& placeholder_text) {
    if (!ai_chat_layout_ || !ai_chat_container_) {
        return;
    }

    ChatRenderMessage message;
    message.active = true;

    auto row = new QWidget(ai_chat_container_);
    auto row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(8);

    auto bubble = new QFrame(row);
    bubble->setObjectName("AiChatBubble");
    bubble->setProperty("role", "assistant");
    bubble->setMaximumWidth(520);
    auto bubble_layout = new QVBoxLayout(bubble);
    bubble_layout->setContentsMargins(12, 10, 12, 10);
    bubble_layout->setSpacing(6);

    row_layout->addWidget(bubble);
    row_layout->addStretch();

    int insert_pos = ai_chat_layout_->count();
    if (ai_chat_spacer_) {
        insert_pos = ai_chat_layout_->count() - 1;
    }
    ai_chat_layout_->insertWidget(insert_pos, row);

    message.row = row;
    message.bubble = bubble;
    message.bubble_layout = bubble_layout;
    current_assistant_message_ = message;

    if (!placeholder_text.isEmpty()) {
        current_assistant_message_.placeholder_label =
            appendRenderBlock(current_assistant_message_, "status", placeholder_text, false);
    }

    trimChatHistory();
    scrollChatToBottom();
}

void DashboardPage::trimChatHistory() {
    if (!ai_chat_layout_) {
        return;
    }

    int message_count = ai_chat_layout_->count();
    if (ai_chat_spacer_) {
        message_count -= 1;
    }

    while (message_count > kMaxAiChatMessages) {
        QLayoutItem* item = ai_chat_layout_->takeAt(0);
        if (!item) {
            break;
        }
        if (QWidget* widget = item->widget()) {
            if (current_assistant_message_.row == widget) {
                current_assistant_message_ = ChatRenderMessage{};
            }
            widget->deleteLater();
            --message_count;
        }
        delete item;
    }
}

QLabel* DashboardPage::appendRenderBlock(ChatRenderMessage& message,
                                         const QString& block_type,
                                         const QString& text,
                                         bool append_to_existing) {
    if (!message.bubble_layout) {
        return nullptr;
    }

    // 如果当前有占位符块（比如 "正在思考中..."），且当前准备添加实际内容块，
    // 则直接删除占位符块。从而保证后续添加的文本、思考、工具块都乖乖排在最下方。
    if (block_type != "status" && message.placeholder_label) {
        if (auto* p_block = qobject_cast<QWidget*>(message.placeholder_label->parentWidget())) {
            p_block->deleteLater();
        }
        message.placeholder_label = nullptr;
    }

    QLabel* existing_label = nullptr;
    if (append_to_existing) {
        if (block_type == "text") {
            existing_label = message.latest_text_block;
        } else if (block_type == "reasoning") {
            existing_label = message.latest_reasoning_block;
        }
    }

    if (existing_label) {
        existing_label->setText(existing_label->text() + text);
        existing_label->updateGeometry();
        scrollChatToBottom();
        return existing_label;
    }

    auto block = new QFrame(message.bubble);
    block->setObjectName("AiChatBlock");
    block->setProperty("blockType", block_type);
    auto block_layout = new QVBoxLayout(block);
    block_layout->setContentsMargins(8, 6, 8, 6);
    block_layout->setSpacing(0);

    auto label = new QLabel(text, block);
    label->setObjectName("AiChatText");
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    block_layout->addWidget(label);
    message.bubble_layout->addWidget(block);

    if (block_type == "text") {
        message.latest_text_block = label;
    } else if (block_type == "reasoning") {
        message.latest_reasoning_block = label;
    }

    scrollChatToBottom();
    return label;
}

void DashboardPage::appendAssistantTextBlock(const QString& text, bool append) {
    if (text.isEmpty()) {
        return;
    }
    if (!current_assistant_message_.active) {
        beginAssistantRenderMessage(QString());
    }

    appendRenderBlock(current_assistant_message_, "text", text, append);
}

void DashboardPage::appendAssistantReasoningBlock(const QString& text, bool append) {
    if (text.isEmpty()) {
        return;
    }
    if (!current_assistant_message_.active) {
        beginAssistantRenderMessage(QString());
    }

    appendRenderBlock(current_assistant_message_, "reasoning", text, append);
}

void DashboardPage::appendAssistantToolBlock(const QString& block_type,
                                             const QString& call_id,
                                             const QString& title,
                                             const QString& detail) {
    if (!current_assistant_message_.active) {
        beginAssistantRenderMessage(QString());
    }

    QString text = title;
    if (!detail.isEmpty()) {
        text += "\n" + detail;
    }

    if (!call_id.isEmpty() && current_assistant_message_.tool_blocks.contains(call_id)) {
        auto* label = current_assistant_message_.tool_blocks.value(call_id);
        label->setText(text);
        if (auto* block = qobject_cast<QWidget*>(label->parentWidget())) {
            apply_state_property(block, "blockType", block_type.toUtf8().constData());
        }
        scrollChatToBottom();
        return;
    }

    auto* label = appendRenderBlock(current_assistant_message_, block_type, text, false);
    if (!call_id.isEmpty() && label) {
        current_assistant_message_.tool_blocks.insert(call_id, label);
    }
}

void DashboardPage::finishAssistantRenderMessage() {
    current_assistant_message_.active = false;
    current_assistant_message_.placeholder_label = nullptr;
    current_assistant_message_.latest_text_block = nullptr;
    current_assistant_message_.latest_reasoning_block = nullptr;
    current_assistant_message_.tool_blocks.clear();
}

void DashboardPage::scrollChatToBottom() {
    if (!ai_scroll_) {
        return;
    }
    auto* bar = ai_scroll_->verticalScrollBar();
    if (bar) {
        bar->setValue(bar->maximum());
    }
}

void DashboardPage::on_ai_input_send() {
    if (!ai_input_) {
        return;
    }
    if (is_analyzing_) {
        if (is_local_llm_) {
            LocalAiAnalysisService::instance()->cancelAnalysis();
        } else {
            AiAnalysisService::instance()->cancelAnalysis();
        }
        return;
    }
    const QString user_text = ai_input_->text().trimmed();
    if (user_text.isEmpty()) {
        return;
    }
    ai_last_prompt_ = user_text;
    ai_input_->clear();
    on_ai_analysis_clicked();
}

void DashboardPage::on_ai_analysis_clicked() {
    // 如果正在分析，则取消
    if (is_analyzing_) {
        bool has_active = is_local_llm_
            ? LocalAiAnalysisService::instance()->isAnalyzing()
            : AiAnalysisService::instance()->isAnalyzing();
        if (has_active) {
            if (is_local_llm_) {
                LocalAiAnalysisService::instance()->cancelAnalysis();
            } else {
                AiAnalysisService::instance()->cancelAnalysis();
            }
        } else {
            is_analyzing_ = false;
            finishAssistantRenderMessage();
            if (ai_analysis_btn_) {
                ai_analysis_btn_->setText(tr("智能分析"));
                ai_analysis_btn_->setEnabled(true);
            }
        }
        return;
    }

    is_analyzing_ = true;

    QString user_prompt;
    if (!ai_last_prompt_.isEmpty()) {
        user_prompt = ai_last_prompt_;
        ai_last_prompt_.clear();
    } else if (ai_input_ && !ai_input_->text().trimmed().isEmpty()) {
        user_prompt = ai_input_->text().trimmed();
        ai_input_->clear();
    } else {
        user_prompt = tr("请分析今日考勤情况。");
    }

    appendChatMessage("user", user_prompt);
    beginAssistantRenderMessage(tr("正在思考中，请稍候..."));
    spdlog::info("Agent-only AI request (local={})", is_local_llm_);
    if (is_local_llm_) {
        LocalAiAnalysisService::instance()->requestAgentChat(user_prompt);
    } else {
        AiAnalysisService::instance()->requestAgentChat(user_prompt);
    }
}

void DashboardPage::on_ai_analysis_started() {
    is_analyzing_ = true;

    // 更新按钮状态
    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("取消分析"));
        ai_analysis_btn_->setEnabled(true);
    }
    if (ai_send_btn_) {
        ai_send_btn_->setText(tr("停止"));
        ai_send_btn_->setEnabled(true);
    }

    spdlog::debug("AI analysis started, button changed to '停止'");
}

void DashboardPage::on_ai_result_ready(const QString& result) {
    if (result.isEmpty()) {
        return;
    }
    appendAssistantTextBlock(result, true);
}

void DashboardPage::on_ai_stream_event(const agent::AgentStreamEvent& event) {
    if (event.channel == "assistant" && event.type == "delta") {
        on_ai_result_ready(event.text);
        return;
    }

    if (event.channel == "reasoning" && event.type == "reasoning") {
        appendAssistantReasoningBlock(event.text, true);
        return;
    }

    if (event.channel != "status" || !agent_status_label_) {
        return;
    }

    if (event.type == "thinking") {
        agent_status_label_->setText(tr("[思考中...]"));
        apply_state_property(agent_status_label_, "agentState", "thinking");
        agent_status_label_->show();
        return;
    }

    if (event.type == "tool_call") {
        const QString tool_name = event.data.value("tool_name").toString(event.text);
        const QString call_id = event.data.value("call_id").toString();
        agent_status_label_->setText(tr("[查询数据: %1]").arg(tool_name));
        apply_state_property(agent_status_label_, "agentState", "tool");
        agent_status_label_->show();
        const QString args_text = QString::fromUtf8(
            QJsonDocument(event.data.value("arguments").toObject()).toJson(QJsonDocument::Compact));
        appendAssistantToolBlock("tool_call", call_id, tr("[工具调用] %1").arg(tool_name), args_text);
        return;
    }

    if (event.type == "tool_result") {
        const QString tool_name = event.data.value("tool_name").toString(event.text);
        const QString call_id = event.data.value("call_id").toString();
        QString detail = event.data.value("result").toString();
        if (detail.length() > 240) {
            detail = detail.left(240) + tr("\n...(结果已截断)");
        }
        agent_status_label_->setText(tr("[%1 完成]").arg(tool_name));
        apply_state_property(agent_status_label_, "agentState", "success");
        agent_status_label_->show();
        appendAssistantToolBlock("tool_result", call_id, tr("[工具结果] %1").arg(tool_name), detail);
    }
}


void DashboardPage::on_ai_analysis_finished() {
    spdlog::info("on_ai_analysis_finished() called");
    is_analyzing_ = false;
    finishAssistantRenderMessage();

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }
    if (ai_send_btn_) {
        ai_send_btn_->setText(tr("发送"));
        ai_send_btn_->setEnabled(true);
    }

    // 隐藏 Agent 状态标签
    if (agent_status_label_) {
        apply_state_property(agent_status_label_, "agentState", "idle");
        agent_status_label_->hide();
    }

    ToastNotification::showMessage(this, tr("AI 分析"), tr("分析完成"), ToastNotification::Level::Success);
    spdlog::info("AI analysis finished, button restored");
}


void DashboardPage::on_ai_error(const QString& error) {
    spdlog::warn("on_ai_error() called: {}", error.toStdString());
    is_analyzing_ = false;
    finishAssistantRenderMessage();

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }
    if (ai_send_btn_) {
        ai_send_btn_->setText(tr("发送"));
        ai_send_btn_->setEnabled(true);
    }

    // 隐藏 Agent 状态标签
    if (agent_status_label_) {
        apply_state_property(agent_status_label_, "agentState", "idle");
        agent_status_label_->hide();
    }

    // 显示错误信息面板
    appendChatMessage("assistant", tr("分析失败: ") + error);

    ToastNotification::showMessage(this, tr("AI 分析"), tr("分析失败: ") + error, ToastNotification::Level::Error);
}

void DashboardPage::on_ai_analysis_cancelled() {
    is_analyzing_ = false;
    finishAssistantRenderMessage();

    if (ai_analysis_btn_) {
        ai_analysis_btn_->setText(tr("智能分析"));
        ai_analysis_btn_->setEnabled(true);
    }
    if (ai_send_btn_) {
        ai_send_btn_->setText(tr("发送"));
        ai_send_btn_->setEnabled(true);
    }

    // 隐藏 Agent 状态标签
    if (agent_status_label_) {
        apply_state_property(agent_status_label_, "agentState", "idle");
        agent_status_label_->hide();
    }

    appendChatMessage("assistant", tr("分析已取消"));

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
    range_combo_->addItem(tr("今日"), 1);
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
                                    const char* tone,
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
        icon->setProperty("tone", QVariant(tone));
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
        (*value_label)->setProperty("tone", QVariant(tone));
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

    kpi_layout->addWidget(make_kpi(tr("到岗率"), ":/icons/status/check-circle.svg",
                                   QColor("#1677ff"), "blue", &attendance_rate_label_,
                                   &attendance_detail_label_, tr("实到 0 / 应到 0")), 0, 0);
    kpi_layout->addWidget(make_kpi(tr("签到人数"), ":/icons/status/user-check.svg",
                                   QColor("#52c41a"), "green", &checkin_label_,
                                   &checkin_sub_label_, tr("今日")), 0, 1);
    kpi_layout->addWidget(make_kpi(tr("迟到人数"), ":/icons/status/alert-circle.svg",
                                   QColor("#fa8c16"), "orange", &late_label_,
                                   &late_sub_label_, tr("今日")), 0, 2);
    kpi_layout->addWidget(make_kpi(tr("早退人数"), ":/icons/status/x-circle.svg",
                                   QColor("#f5222d"), "red", &early_label_,
                                   &early_sub_label_, tr("今日")), 0, 3);
    kpi_layout->addWidget(make_kpi(tr("未打卡"), ":/icons/status/user-x.svg",
                                   QColor("#722ed1"), "purple", &missing_label_,
                                   &missing_sub_label_, tr("今日")), 1, 0);
    kpi_layout->addWidget(make_kpi(tr("平均相似度"), ":/icons/status/info.svg",
                                   QColor("#13c2c2"), "cyan", &similarity_label_,
                                   &similarity_sub_label_, tr("今日")), 1, 1);
    // 将“异常识别率”替换为“异常记录数”，统计迟到/早退的记录条数
    kpi_layout->addWidget(make_kpi(tr("异常记录数"), ":/icons/status/alert-circle.svg",
                                   QColor("#d46b08"), "brown", &abnormal_rate_label_,
                                   &abnormal_rate_sub_label_, tr("迟到+早退条数")), 1, 2);
    kpi_layout->addWidget(make_kpi(tr("签退人数"), ":/icons/status/user-check.svg",
                                   QColor("#9254de"), "violet", &checkout_label_,
                                   &checkout_sub_label_, tr("今日")), 1, 3);

    layout->addWidget(kpi_container);

    auto top_row = new QHBoxLayout();
    top_row->setSpacing(16);

    auto insight_card = new CardWidget(content);

    // 创建自定义标题栏（标题 + AI 分析按钮）
    auto insight_header = new QWidget(insight_card);
    auto insight_header_layout = new QHBoxLayout(insight_header);
    insight_header_layout->setContentsMargins(0, 0, 0, 0);
    insight_header_layout->setSpacing(12);

    auto ai_title_layout = new QHBoxLayout();
    ai_title_layout->setSpacing(8);
    ai_title_layout->setContentsMargins(0,0,0,0);
    
    auto icon_label = new QLabel(insight_header);
    icon_label->setPixmap(SvgIconManager::icon(":/icons/status/info.svg", QSize(24, 24), QColor("#722ed1")).pixmap(24, 24));
    ai_title_layout->addWidget(icon_label);

    auto insight_title = new QLabel(tr("智能考勤助手 AI Agent"), insight_header);
    insight_title->setObjectName("CardTitle");
    // 样式由 QSS #CardTitle 定义
    ai_title_layout->addWidget(insight_title);
    
    insight_header_layout->addLayout(ai_title_layout);
    
    insight_header_layout->addStretch();

    // AI 分析按钮（智能分析板块右侧）
    ai_analysis_btn_ = new QPushButton(tr("开始分析"), insight_header);
    ai_analysis_btn_->setObjectName("DashboardAiButton");
    ai_analysis_btn_->setCursor(Qt::PointingHandCursor);
    ai_analysis_btn_->setIcon(SvgIconManager::icon(":/icons/status/info.svg", QSize(16, 16), QColor("#ffffff")));
    ai_analysis_btn_->setIconSize(QSize(16, 16));
    connect(ai_analysis_btn_, &QPushButton::clicked, this, &DashboardPage::on_ai_analysis_clicked);
    insight_header_layout->addWidget(ai_analysis_btn_);

    insight_card->setHeaderWidget(insight_header);
    insight_card->setMinimumHeight(560);
    auto insight_layout = new QVBoxLayout(insight_card->bodyContainer());
    insight_layout->setContentsMargins(0, 0, 0, 0);
    insight_layout->setSpacing(10);

    ai_scroll_ = new QScrollArea(insight_card);
    ai_scroll_->setWidgetResizable(true);
    ai_scroll_->setFrameShape(QFrame::NoFrame);
    ai_scroll_->setObjectName("AiChatScroll");
    ai_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ai_scroll_->setMinimumHeight(280);

    ai_chat_container_ = new QWidget(ai_scroll_);
    ai_chat_container_->setObjectName("AiChatContainer");
    ai_scroll_->setWidget(ai_chat_container_);

    ai_chat_layout_ = new QVBoxLayout(ai_chat_container_);
    ai_chat_layout_->setContentsMargins(0, 0, 0, 0);
    ai_chat_layout_->setSpacing(10);
    ai_chat_spacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    ai_chat_layout_->addItem(ai_chat_spacer_);

    insight_layout->addWidget(ai_scroll_, 1);

    auto quick_row = new QWidget(insight_card);
    quick_row->setObjectName("AiQuickRow");
    auto quick_layout = new QHBoxLayout(quick_row);
    quick_layout->setContentsMargins(0, 0, 0, 0);
    quick_layout->setSpacing(8);

    auto template_btn = new QPushButton(tr("提示词模板"), quick_row);
    template_btn->setObjectName("AiQuickButton");
    
    QMenu* template_menu = new QMenu(template_btn);
    auto add_prompt_template = [this, template_menu](const QString& title, const QString& prompt) {
        template_menu->addAction(title, [this, prompt]() {
            if (ai_input_) {
                ai_input_->setText(prompt);
                ai_input_->setFocus();
            }
        });
    };

    add_prompt_template(tr("今日考勤统计"),
        tr("请查询今日的考勤数据统计。"));
    add_prompt_template(tr("今日异常打卡"),
        tr("请查询今日考勤中的异常打卡记录。"));
    add_prompt_template(tr("本周考勤数据"),
        tr("请查询本周的考勤统计数据。"));

    template_menu->addSeparator();
    add_prompt_template(tr("查询员工信息"),
        tr("请查询张三的员工个人基本信息。"));
    add_prompt_template(tr("员工今日打卡"),
        tr("请查询张三今日的打卡记录。"));
    add_prompt_template(tr("员工本周考勤"),
        tr("请查询张三本周的考勤摘要和异常记录。"));

    template_menu->addSeparator();
    add_prompt_template(tr("部门今日出勤"),
        tr("请查询技术部今日的考勤统计摘要。"));
    add_prompt_template(tr("部门本周异常"),
        tr("请查询技术部本周的异常打卡记录。"));
    add_prompt_template(tr("系统当前时间"),
        tr("请查询系统当前的日期和时间。"));
    template_btn->setMenu(template_menu);
    quick_layout->addWidget(template_btn);

    auto clear_btn = new QPushButton(tr("清空"), quick_row);
    clear_btn->setObjectName("AiQuickButton");
    connect(clear_btn, &QPushButton::clicked, this, [this]() {
        if (!ai_chat_layout_) {
            return;
        }
        QLayoutItem* item = nullptr;
        while ((item = ai_chat_layout_->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
        finishAssistantRenderMessage();
        ai_chat_spacer_ = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
        ai_chat_layout_->addItem(ai_chat_spacer_);

        // ★ 清除 Agent 对话记忆
        if (auto* local_svc = LocalAiAnalysisService::instance()) {
            local_svc->clearAgentHistory();
        }
        // ★ 清除 RKLLM KV Cache，避免残留上下文干扰下一次对话
        LocalLLMThread::instance()->resetContext();

        appendChatMessage("assistant", tr("对话已清空，输入问题即可开始新的分析。"));
    });
    quick_layout->addWidget(clear_btn);

    quick_layout->addStretch();

    insight_layout->addWidget(quick_row);

    auto input_row = new QWidget(insight_card);
    input_row->setObjectName("AiChatInputRow");
    auto input_layout = new QHBoxLayout(input_row);
    input_layout->setContentsMargins(0, 0, 0, 0);
    input_layout->setSpacing(10);

    ai_input_ = new QLineEdit(input_row);
    ai_input_->setObjectName("AiChatInput");
    ai_input_->setPlaceholderText(tr("输入问题，获取 AI 考勤分析"));
    // 确保输入法支持
    ai_input_->setAttribute(Qt::WA_InputMethodEnabled, true);
    ai_input_->setInputMethodHints(Qt::ImhNone); // 允许所有输入
    connect(ai_input_, &QLineEdit::returnPressed, this, &DashboardPage::on_ai_input_send);
    input_layout->addWidget(ai_input_, 1);

    // ASR 本地/云端切换。默认设为本地 ASR (checked=true)
    asr_backend_toggle_btn_ = new QPushButton(tr("本地ASR"), input_row);
    asr_backend_toggle_btn_->setObjectName("AsrBackendToggle");
    asr_backend_toggle_btn_->setCheckable(true);
    asr_backend_toggle_btn_->setChecked(true);
    asr_backend_toggle_btn_->setMinimumWidth(82);
    asr_backend_toggle_btn_->setCursor(Qt::PointingHandCursor);
    asr_backend_toggle_btn_->setToolTip(tr("点击切换到云端 ASR"));
    connect(asr_backend_toggle_btn_, &QPushButton::toggled,
            this, &DashboardPage::on_asr_backend_toggled);
    input_layout->addWidget(asr_backend_toggle_btn_);

    // 语音输入按钮（点击开始/点击停止）
    ai_voice_btn_ = new QPushButton(input_row);
    ai_voice_btn_->setObjectName("AiVoiceButton");
    ai_voice_btn_->setCheckable(true);
    ai_voice_btn_->setFixedSize(40, 40);
    ai_voice_btn_->setCursor(Qt::PointingHandCursor);
    ai_voice_btn_->setToolTip(tr("点击开始语音输入，再次点击停止"));
    ai_voice_btn_->setIcon(SvgIconManager::icon(":/icons/actions/mic.svg", QSize(20, 20), QColor("#595959")));
    ai_voice_btn_->setIconSize(QSize(20, 20));
    connect(ai_voice_btn_, &QPushButton::clicked, this, &DashboardPage::on_voice_btn_clicked);
    input_layout->addWidget(ai_voice_btn_);

    // ASR 状态标签（录音中/识别中）
    asr_status_label_ = new QLabel(input_row);
    asr_status_label_->setObjectName("AsrStatusLabel");
    asr_status_label_->hide();
    input_layout->addWidget(asr_status_label_);

    // 本地/云端 LLM 切换按钮（浅色风格，显示当前模式）
    backend_toggle_btn_ = new QPushButton(remote_backend_display_name(), input_row);
    backend_toggle_btn_->setObjectName("BackendToggle");
    backend_toggle_btn_->setCheckable(true);
    backend_toggle_btn_->setMinimumWidth(90);
    backend_toggle_btn_->setCursor(Qt::PointingHandCursor);
    backend_toggle_btn_->setToolTip(remote_backend_tooltip());
    connect(backend_toggle_btn_, &QPushButton::toggled, this, &DashboardPage::on_backend_toggled);
    input_layout->addWidget(backend_toggle_btn_);

    // Agent 状态标签（显示思考中/调用工具等状态）
    agent_status_label_ = new QLabel(input_row);
    agent_status_label_->setObjectName("AgentStatusLabel");
    apply_state_property(agent_status_label_, "agentState", "idle");
    agent_status_label_->setMinimumWidth(100);
    agent_status_label_->hide();  // 默认隐藏，有状态时显示
    input_layout->addWidget(agent_status_label_);

    // 后端状态标签（显示就绪/加载中）
    backend_status_label_ = new QLabel(input_row);
    backend_status_label_->setObjectName("BackendStatusLabel");
    apply_state_property(backend_status_label_, "backendState", "idle");
    input_layout->addWidget(backend_status_label_);

    ai_send_btn_ = new QPushButton(tr("发送"), input_row);
    ai_send_btn_->setObjectName("AiChatSendButton");
    connect(ai_send_btn_, &QPushButton::clicked, this, &DashboardPage::on_ai_input_send);
    input_layout->addWidget(ai_send_btn_);

    insight_layout->addWidget(input_row);

    appendChatMessage("assistant", tr("点击右上角「智能分析」或输入问题，获取 AI 驱动的深度分析报告。"));
    top_row->addWidget(insight_card, 2);

    auto alert_card = new CardWidget(content);
    alert_card->setTitle(tr("异常列表"));
    alert_card->setMinimumHeight(300);

    // 异常列表卡片内部使用独立的滚动区域，避免数据多时拉长整个 Dashboard 页面
    auto alert_card_layout = new QVBoxLayout(alert_card->bodyContainer());
    alert_card_layout->setContentsMargins(0, 0, 0, 0);
    alert_card_layout->setSpacing(0);

    auto alert_scroll = new QScrollArea(alert_card);
    alert_scroll->setWidgetResizable(true);
    alert_scroll->setFrameShape(QFrame::NoFrame);
    alert_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto alert_container = new QWidget(alert_scroll);
    alert_scroll->setWidget(alert_container);

    auto alert_layout = new QVBoxLayout(alert_container);
    alert_layout->setContentsMargins(0, 0, 0, 0);
    alert_layout->setSpacing(10);
    alerts_layout_ = alert_layout;

    alert_card_layout->addWidget(alert_scroll);
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

    chart_row->addWidget(trend_card, 1);

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

    int range_days = 1;
    if (range_combo_) {
        range_days = range_combo_->currentData().toInt();
        if (range_days <= 0) range_days = 1;
    }

    QDate end_date = QDate::currentDate();
    QDate start_date = end_date.addDays(-(range_days - 1));
    std::string start_str = start_date.toString("yyyy-MM-dd").toStdString();
    std::string end_str = end_date.toString("yyyy-MM-dd").toStdString();

    auto records = attendance_service_->query_records_range(start_str, end_str);

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
    // 注意：这里不仅要计入迟到人数统计，还要补充到异常记录列表中
    for (const auto user_id : checkout_users) {
        if (checkin_users.find(user_id) == checkin_users.end()) {
            late_users.insert(user_id);

            // 从打卡记录中找到该用户最近的一条签退记录，将其视为“推断迟到”记录
            auto it = std::find_if(records.rbegin(), records.rend(),
                                   [user_id](const db::AttendanceRecord& r) {
                                       return r.user_id == user_id &&
                                              r.check_type == db::CheckType::CHECK_OUT;
                                   });
            if (it != records.rend()) {
                db::AttendanceRecord inferred = *it;
                inferred.status = db::AttendanceStatus::STATUS_LATE;
                abnormal_records.push_back(inferred);
            }
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
    const int abnormal_record_count = static_cast<int>(abnormal_records.size());

    QString range_str;
    if (range_days == 1) range_str = tr("今日");
    else if (range_days == 7) range_str = tr("近7日");
    else if (range_days == 30) range_str = tr("近30日");
    else range_str = tr("近 %1 天").arg(range_days);

    if (attendance_rate_label_) {
        attendance_rate_label_->setText(QString::number(attendance_rate * 100.0, 'f', 1) + "%");
    }
    if (attendance_detail_label_) {
        // For range view, "Real/Expected" might be ambiguous. 
        // Maybe change to "Active / Total"
        if (range_days == 1) {
            attendance_detail_label_->setText(tr("实到 %1 / 应到 %2").arg(present_count).arg(total_users));
        } else {
            attendance_detail_label_->setText(tr("活跃 %1 / 总数 %2").arg(present_count).arg(total_users));
        }
    }
    if (checkin_label_) {
        checkin_label_->setText(QString::number(checked_in));
    }
    if (checkin_sub_label_) checkin_sub_label_->setText(range_str);

    if (late_label_) {
        late_label_->setText(QString::number(late_count));
    }
    if (late_sub_label_) late_sub_label_->setText(range_str);

    if (early_label_) {
        early_label_->setText(QString::number(early_count));
    }
    if (early_sub_label_) early_sub_label_->setText(range_str);

    if (missing_label_) {
        missing_label_->setText(QString::number(missing_count));
    }
    if (missing_sub_label_) missing_sub_label_->setText(range_str);

    if (similarity_label_) {
        similarity_label_->setText(QString::number(avg_similarity, 'f', 2));
    }
    if (similarity_sub_label_) similarity_sub_label_->setText(range_str);

    // “异常记录数”卡片：展示迟到/早退记录条数，帮助快速掌握异常事件量
    if (abnormal_rate_label_) {
        abnormal_rate_label_->setText(QString::number(abnormal_record_count));
    }
    if (abnormal_rate_sub_label_) abnormal_rate_sub_label_->setText(range_str);

    if (checkout_label_) {
        checkout_label_->setText(QString::number(checked_out));
    }
    if (checkout_sub_label_) checkout_sub_label_->setText(range_str);

    if (data_coverage_label_) {
        data_coverage_label_->setText(tr("数据覆盖率 %1%")
                                          .arg(QString::number(attendance_rate * 100.0, 'f', 1)));
    }
    if (last_sync_label_) {
        last_sync_label_->setText(tr("最后同步 %1").arg(QTime::currentTime().toString("HH:mm")));
    }

    TrendSeries series;
    series.primary.reserve(range_days);
    series.secondary.reserve(range_days);
    
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

    // 智能分析区域由 AI 对话面板负责渲染，不在刷新中重置

    if (alerts_layout_) {
        clear_layout(alerts_layout_);
        std::sort(abnormal_records.begin(), abnormal_records.end(),
                  [](const db::AttendanceRecord& a, const db::AttendanceRecord& b) {
                      return a.check_time > b.check_time;
                  });

        // 这里不再限制只显示前 5 条，由外层滚动容器控制整体高度，确保近7日/近30日异常记录完整可见
        const int show_count = static_cast<int>(abnormal_records.size());
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
            
            QString time_format = (range_days > 1) ? "MM-dd HH:mm" : "HH:mm";
            auto time_label = new QLabel(
                QDateTime::fromTime_t(record.check_time).toString(time_format),
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
        
        alerts_layout_->addStretch();
    }

    need_refresh_ = false;
}

void DashboardPage::on_backend_toggled(bool checked) {
    is_local_llm_ = checked;

    // 更新按钮文字和提示
    if (backend_toggle_btn_) {
        backend_toggle_btn_->setText(checked ? tr("本地大模型") : remote_backend_display_name());
        backend_toggle_btn_->setToolTip(checked
            ? tr("点击切换到%1").arg(remote_backend_display_name())
            : remote_backend_tooltip());
    }

    if (backend_status_label_) {
        backend_status_label_->setText(checked ? tr("加载中...") : tr(""));
        apply_state_property(backend_status_label_, "backendState", checked ? "loading" : "idle");
    }

    if (checked) {
        if (AiAnalysisService::instance()->isAnalyzing()) {
            AiAnalysisService::instance()->cancelAnalysis();
        }
        // 如果切换到本地且模型未加载，则初始化
        QString model_path = resolve_local_llm_model_path();
        if (!QFileInfo::exists(model_path)) {
            spdlog::error("Local LLM model not found: {}", model_path.toStdString());
            ToastNotification::showMessage(this,
                tr("AI 模型"),
                tr("未找到本地模型文件:\n%1\n请设置环境变量 LOCAL_LLM_MODEL_PATH").arg(model_path),
                ToastNotification::Level::Error);
            if (backend_status_label_) {
                backend_status_label_->setText(tr("模型缺失"));
                apply_state_property(backend_status_label_, "backendState", "idle");
            }
            if (backend_toggle_btn_) {
                const QSignalBlocker blocker(backend_toggle_btn_);
                backend_toggle_btn_->setChecked(false);
                backend_toggle_btn_->setText(remote_backend_display_name());
                backend_toggle_btn_->setToolTip(remote_backend_tooltip());
            }
            is_local_llm_ = false;
            return;
        }
        if (LocalAiAnalysisService::instance()->isLocalLLMReady()) {
            if (backend_status_label_) {
                backend_status_label_->setText(tr("就绪"));
                apply_state_property(backend_status_label_, "backendState", "ready");
            }
        } else if (backend_status_label_) {
            backend_status_label_->setText(tr("等待 NPU..."));
            apply_state_property(backend_status_label_, "backendState", "loading");
        }
        emit backendPreferenceChanged(true, model_path);
    } else {
        if (LocalAiAnalysisService::instance()->isAnalyzing()) {
            LocalAiAnalysisService::instance()->cancelAnalysis();
        }
        ToastNotification::showMessage(this, tr("AI 模型"),
            tr("已切换至%1").arg(remote_backend_display_name()),
            ToastNotification::Level::Info);

        // 初始化云端 Agent（如果尚未初始化）
        if (attendance_service_ && user_service_) {
            AiAnalysisService::instance()->initializeAgent(attendance_service_, user_service_);
        }
        emit backendPreferenceChanged(false, QString());
    }
}

void DashboardPage::on_local_llm_ready() {
    if (is_local_llm_ && backend_status_label_) {
        backend_status_label_->setText(tr("就绪"));
        apply_state_property(backend_status_label_, "backendState", "ready");
        ToastNotification::showMessage(this, tr("AI 模型"), tr("本地模型加载完成"), ToastNotification::Level::Success);
    }

}

void DashboardPage::on_local_llm_progress(int percent) {
    if (is_local_llm_ && backend_status_label_) {
        backend_status_label_->setText(tr("加载中 %1%").arg(percent));
        apply_state_property(backend_status_label_, "backendState", "loading");
    }
}

void DashboardPage::on_local_llm_released() {
    // 模型被释放（通常是因为回到识别页面，NPU 资源需要给人脸识别使用）
    // 将按钮状态切回云端模式
    is_local_llm_ = false;

    if (backend_toggle_btn_) {
        // 阻止信号触发 on_backend_toggled
        backend_toggle_btn_->blockSignals(true);
        backend_toggle_btn_->setChecked(false);
        backend_toggle_btn_->setText(remote_backend_display_name());
        backend_toggle_btn_->setToolTip(remote_backend_tooltip());
        backend_toggle_btn_->blockSignals(false);
    }

    if (backend_status_label_) {
        backend_status_label_->setText(tr(""));
        apply_state_property(backend_status_label_, "backendState", "idle");
    }

    spdlog::info("Dashboard: Local LLM released, button switched to cloud mode");
}

// ==================== ASR 语音识别 ====================

void DashboardPage::on_voice_btn_clicked() {
    auto asr = AsrService::instance();

    if (asr->isRecording()) {
        // 正在录音 → 停止
        asr->stopRecording();
    } else if (asr->isTranscribing()) {
        // 正在识别 → 取消
        asr->cancel();
    } else {
        // 开始录音
        if (is_analyzing_) {
            ToastNotification::showMessage(this, tr("语音识别"), tr("请等待当前分析完成"), ToastNotification::Level::Warning, 2000);
            if (ai_voice_btn_) ai_voice_btn_->setChecked(false);
            return;
        }
        if (asr->backendMode() == AsrBackendMode::Cloud && !asr->isApiKeyConfigured()) {
            ToastNotification::showMessage(this, tr("语音识别"), tr("未配置 MIMO_ASR_API_KEY"), ToastNotification::Level::Error, 3000);
            if (ai_voice_btn_) ai_voice_btn_->setChecked(false);
            return;
        }
        if (asr->backendMode() == AsrBackendMode::Local && !asr->isLocalReady()) {
            ToastNotification::showMessage(this, tr("语音识别"), tr("本地 ASR 尚未就绪"), ToastNotification::Level::Warning, 2000);
            if (ai_voice_btn_) ai_voice_btn_->setChecked(false);
            return;
        }
        asr->startRecording();
    }
}

void DashboardPage::on_asr_transcription(const QString& text) {
    // 流式填入输入框
    if (ai_input_) {
        ai_input_->setText(text);
        ai_input_->setFocus();
    }
}

void DashboardPage::on_asr_finished(const QString& fullText) {
    Q_UNUSED(fullText);
    spdlog::info("ASR: transcription finished, text in input box");
    // 文本已通过 transcriptionReady 填入，用户可以确认后手动发送
}

void DashboardPage::on_asr_error(const QString& error) {
    spdlog::error("ASR error: {}", error.toStdString());
    ToastNotification::showMessage(this, tr("语音识别"), error, ToastNotification::Level::Error, 3000);

    // 重置按钮状态
    if (ai_voice_btn_) {
        ai_voice_btn_->setChecked(false);
    }
}

void DashboardPage::on_asr_recording_state(bool recording) {
    if (!ai_voice_btn_) return;

    if (recording) {
        // 录音中：红色图标，checked 状态
        ai_voice_btn_->setChecked(true);
        ai_voice_btn_->setIcon(SvgIconManager::icon(
            ":/icons/actions/mic.svg", QSize(20, 20), QColor("#f5222d")));
        ai_voice_btn_->setToolTip(tr("录音中...点击停止"));

        if (asr_status_label_) {
            asr_status_label_->setText(tr("录音中 0s"));
            asr_status_label_->show();
        }
        if (asr_backend_toggle_btn_) {
            asr_backend_toggle_btn_->setEnabled(false);
        }
    } else {
        // 停止录音
        ai_voice_btn_->setChecked(false);
        ai_voice_btn_->setIcon(SvgIconManager::icon(
            ":/icons/actions/mic.svg", QSize(20, 20), QColor("#595959")));
        ai_voice_btn_->setToolTip(tr("点击开始语音输入"));
        if (asr_backend_toggle_btn_) {
            asr_backend_toggle_btn_->setEnabled(true);
        }
    }
}

void DashboardPage::on_asr_transcribing_state(bool transcribing) {
    if (transcribing) {
        if (asr_status_label_) {
            asr_status_label_->setText(tr("识别中..."));
            asr_status_label_->show();
        }
        if (ai_voice_btn_) {
            ai_voice_btn_->setEnabled(false);  // 识别中禁止操作
        }
        if (asr_backend_toggle_btn_) {
            asr_backend_toggle_btn_->setEnabled(false);
        }
    } else {
        if (asr_status_label_) {
            asr_status_label_->hide();
        }
        if (ai_voice_btn_) {
            ai_voice_btn_->setEnabled(true);
        }
        if (asr_backend_toggle_btn_) {
            asr_backend_toggle_btn_->setEnabled(true);
        }
    }
}

void DashboardPage::on_asr_duration(int seconds) {
    if (asr_status_label_ && asr_status_label_->isVisible()) {
        asr_status_label_->setText(tr("录音中 %1s").arg(seconds));
    }
}

void DashboardPage::on_asr_backend_toggled(bool checked) {
    auto* asr = AsrService::instance();
    if (asr->isRecording() || asr->isTranscribing()) {
        ToastNotification::showMessage(this, tr("语音识别"),
            tr("请先停止当前语音识别"), ToastNotification::Level::Warning, 2000);
        if (asr_backend_toggle_btn_) {
            const QSignalBlocker blocker(asr_backend_toggle_btn_);
            asr_backend_toggle_btn_->setChecked(asr->backendMode() == AsrBackendMode::Local);
        }
        return;
    }

    asr->setBackendMode(checked ? AsrBackendMode::Local : AsrBackendMode::Cloud);
}

void DashboardPage::on_asr_backend_changed(AsrBackendMode mode) {
    const bool is_local = mode == AsrBackendMode::Local;
    if (asr_backend_toggle_btn_) {
        const QSignalBlocker blocker(asr_backend_toggle_btn_);
        asr_backend_toggle_btn_->setChecked(is_local);
        asr_backend_toggle_btn_->setText(is_local ? tr("本地ASR") : tr("云端ASR"));
        asr_backend_toggle_btn_->setToolTip(is_local ? tr("点击切换到云端 ASR") : tr("点击切换到本地 ASR"));
    }
    if (asr_status_label_) {
        asr_status_label_->setText(is_local ? tr("本地ASR") : tr(""));
        asr_status_label_->setVisible(is_local);
    }
}

void DashboardPage::on_local_asr_ready_changed(bool ready) {
    auto* asr = AsrService::instance();
    if (asr->backendMode() != AsrBackendMode::Local) {
        return;
    }
    if (ai_voice_btn_) {
        ai_voice_btn_->setEnabled(ready);
    }
    if (asr_status_label_) {
        asr_status_label_->setText(ready ? tr("本地ASR就绪") : tr("本地ASR已释放"));
        asr_status_label_->show();
    }
    if (ready) {
        ToastNotification::showMessage(this, tr("语音识别"), tr("本地 ASR 加载完成"), ToastNotification::Level::Success, 2000);
    }
}

void DashboardPage::on_local_asr_loading_changed(bool loading) {
    if (ai_voice_btn_) {
        ai_voice_btn_->setEnabled(!loading);
    }
    if (asr_backend_toggle_btn_) {
        asr_backend_toggle_btn_->setEnabled(!loading);
    }
    if (asr_status_label_) {
        if (loading) {
            asr_status_label_->setText(tr("本地ASR加载中..."));
            asr_status_label_->show();
        }
    }
}

#pragma once

#include <QString>
#include <QMap>
#include <QWidget>
#include "agent/stream_event.h"

class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QFrame;
class QScrollArea;
class QScrollBar;
class QVBoxLayout;
class QShowEvent;
class QHideEvent;
class QSpacerItem;

enum class AsrBackendMode;

namespace service {
class AttendanceService;
class UserService;
}  // namespace service

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget* parent = nullptr);

    void setAttendanceService(service::AttendanceService* service);
    void setUserService(service::UserService* service);

    void refreshData();
    bool isLocalBackendEnabled() const { return is_local_llm_; }

signals:
    void backendPreferenceChanged(bool use_local, const QString& model_path);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void on_refresh_clicked();
    void on_range_changed(int index);
    void on_filter_changed(int index);
    void on_ai_input_send();

private:
    struct ChatRenderMessage {
        QWidget* row = nullptr;
        QFrame* bubble = nullptr;
        QVBoxLayout* bubble_layout = nullptr;
        QLabel* placeholder_label = nullptr;
        QLabel* latest_text_block = nullptr;
        QLabel* latest_reasoning_block = nullptr;
        QMap<QString, QLabel*> tool_blocks;
        bool active = false;
    };

    void setup_ui();
    void appendChatMessage(const QString& role, const QString& text);
    void beginAssistantRenderMessage(const QString& placeholder_text);
    QLabel* appendRenderBlock(ChatRenderMessage& message,
                              const QString& block_type,
                              const QString& text,
                              bool append_to_existing = false);
    void appendAssistantTextBlock(const QString& text, bool append);
    void appendAssistantReasoningBlock(const QString& text, bool append);
    void appendAssistantToolBlock(const QString& block_type,
                                  const QString& call_id,
                                  const QString& title,
                                  const QString& detail = QString());
    void finishAssistantRenderMessage();
    void trimChatHistory();
    void scrollChatToBottom();

    service::AttendanceService* attendance_service_;
    service::UserService* user_service_;
    bool need_refresh_;

    QComboBox* range_combo_;
    QComboBox* dept_combo_;
    QLabel* last_sync_label_;
    QLabel* data_coverage_label_;

    QLabel* attendance_rate_label_;
    QLabel* attendance_detail_label_;
    QLabel* checkin_label_;
    QLabel* checkin_sub_label_;
    QLabel* late_label_;
    QLabel* late_sub_label_;
    QLabel* early_label_;
    QLabel* early_sub_label_;
    QLabel* missing_label_;
    QLabel* missing_sub_label_;
    QLabel* similarity_label_;
    QLabel* similarity_sub_label_;
    QLabel* abnormal_rate_label_;
    QLabel* abnormal_rate_sub_label_;
    QLabel* checkout_label_;
    QLabel* checkout_sub_label_;

    QWidget* trend_chart_;
    QVBoxLayout* alerts_layout_;

    // AI 分析相关
    QPushButton* ai_analysis_btn_;
    bool is_analyzing_;
    QScrollArea* ai_scroll_;
    QWidget* ai_chat_container_;
    QVBoxLayout* ai_chat_layout_;
    QSpacerItem* ai_chat_spacer_;
    QLineEdit* ai_input_;
    QPushButton* ai_send_btn_;
    QPushButton* asr_backend_toggle_btn_; // ASR 本地/云端切换
    QPushButton* ai_voice_btn_;   // 语音识别按钮
    QLabel* asr_status_label_;   // ASR 状态标签
    QString ai_last_prompt_;
    ChatRenderMessage current_assistant_message_;

private slots:
    void on_ai_analysis_clicked();
    void on_ai_result_ready(const QString& result);
    void on_ai_stream_event(const agent::AgentStreamEvent& event);
    void on_ai_analysis_finished();
    void on_ai_error(const QString& error);
    void on_ai_analysis_started();
    void on_ai_analysis_cancelled();

    // ASR 语音识别
    void on_voice_btn_clicked();
    void on_asr_transcription(const QString& text);
    void on_asr_finished(const QString& fullText);
    void on_asr_error(const QString& error);
    void on_asr_recording_state(bool recording);
    void on_asr_transcribing_state(bool transcribing);
    void on_asr_duration(int seconds);
    void on_asr_backend_toggled(bool checked);
    void on_asr_backend_changed(AsrBackendMode mode);
    void on_local_asr_ready_changed(bool ready);
    void on_local_asr_loading_changed(bool loading);
    
    // 后端切换相关
    void on_backend_toggled(bool checked);
    void on_local_llm_ready();
    void on_local_llm_progress(int percent);
    void on_local_llm_released();

private:
    // LLM 后端切换
    QPushButton* backend_toggle_btn_;
    QLabel* backend_status_label_;
    bool is_local_llm_;

    QLabel* agent_status_label_;
};

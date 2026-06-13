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
class QSpacerItem;

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
    QPushButton* ai_voice_btn_;   // 语音识别按钮
    QLabel* asr_status_label_;   // ASR 状态标签
    QString ai_last_prompt_;
    QString ai_skip_prefix_;
    int ai_data_range_days_;

    // 数据范围选择按钮
    QPushButton* ai_data_today_btn_;
    QPushButton* ai_data_7day_btn_;
    QPushButton* ai_data_30day_btn_;
    QPushButton* ai_data_qa_btn_;  // 纯问答模式按钮（不附带考勤数据）
    QLabel* ai_data_range_label_;  // 显示当前选中的数据范围
    ChatRenderMessage current_assistant_message_;

    void update_data_range_buttons();  // 更新按钮选中状态

private slots:
    void on_ai_analysis_clicked();
    void on_ai_result_ready(const QString& result);
    void on_ai_stream_event(const agent::AgentStreamEvent& event);
    void on_ai_analysis_finished();
    void on_ai_error(const QString& error);
    void on_ai_analysis_started();
    void on_ai_analysis_cancelled();
    void on_data_range_changed(int days);

    // ASR 语音识别
    void on_voice_btn_clicked();
    void on_asr_transcription(const QString& text);
    void on_asr_finished(const QString& fullText);
    void on_asr_error(const QString& error);
    void on_asr_recording_state(bool recording);
    void on_asr_transcribing_state(bool transcribing);
    void on_asr_duration(int seconds);
    
    // 后端切换相关
    void on_backend_toggled(bool checked);
    void on_local_llm_ready();
    void on_local_llm_progress(int percent);
    void on_local_llm_released();

    void on_agent_mode_toggled(bool checked);

private:
    // LLM 后端切换
    QPushButton* backend_toggle_btn_;
    QLabel* backend_status_label_;
    bool is_local_llm_;

    // Agent 模式切换（仅本地 LLM 可用）
    QPushButton* agent_mode_btn_;
    QLabel* agent_status_label_;
    bool is_agent_mode_;
};

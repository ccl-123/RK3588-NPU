#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QLineEdit;
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

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void on_refresh_clicked();
    void on_range_changed(int index);
    void on_filter_changed(int index);
    void on_ai_input_send();

private:
    void setup_ui();
    void appendChatMessage(const QString& role, const QString& text);
    void updateAssistantMessage(const QString& text, bool append);
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
    QLabel* ai_result_label_;  // AI分析结果显示标签
    QScrollArea* ai_scroll_;
    QWidget* ai_chat_container_;
    QVBoxLayout* ai_chat_layout_;
    QSpacerItem* ai_chat_spacer_;
    QLineEdit* ai_input_;
    QPushButton* ai_send_btn_;
    QString ai_last_prompt_;
    int ai_data_range_days_;

    // 数据范围选择按钮
    QPushButton* ai_data_today_btn_;
    QPushButton* ai_data_7day_btn_;
    QPushButton* ai_data_30day_btn_;
    QPushButton* ai_data_qa_btn_;  // 纯问答模式按钮（不附带考勤数据）
    QLabel* ai_data_range_label_;  // 显示当前选中的数据范围

    void update_data_range_buttons();  // 更新按钮选中状态

private slots:
    void on_ai_analysis_clicked();
    void on_ai_result_ready(const QString& result);
    void on_ai_analysis_finished();
    void on_ai_error(const QString& error);
    void on_ai_analysis_started();
    void on_ai_analysis_cancelled();
    void on_data_range_changed(int days);
    
    // 后端切换相关
    void on_backend_toggled(bool checked);
    void on_local_llm_ready();
    void on_local_llm_progress(int percent);

private:
    // LLM 后端切换
    QPushButton* backend_toggle_btn_;
    QLabel* backend_status_label_;
    bool is_local_llm_;
};

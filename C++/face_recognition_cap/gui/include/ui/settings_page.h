#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimeEdit>

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    void themeToggleRequested();
    void settingsChanged();

private slots:
    void on_theme_toggle_clicked();
    void on_save_clicked();
    void on_reset_clicked();
    void on_clear_cache_clicked();

private:
    void setup_ui();
    void load_settings();
    void save_settings();
    
    // UI 组件 - 系统信息
    QLabel* version_label_;
    QLabel* db_size_label_;
    
    // 显示设置
    QCheckBox* show_fps_check_;
    QCheckBox* show_confidence_check_;
    QCheckBox* auto_start_check_;
    
    // 识别设置
    QDoubleSpinBox* recognition_threshold_spin_;
    QSpinBox* duplicate_check_interval_spin_;
    QSpinBox* recognition_confirm_count_spin_;
    
    // 考勤设置
    QTimeEdit* work_start_time_edit_;
    QTimeEdit* work_end_time_edit_;
    QSpinBox* late_threshold_spin_;
    QSpinBox* early_leave_threshold_spin_;
    QCheckBox* allow_multiple_checkin_check_;
    QCheckBox* checkin_sound_check_;
    QCheckBox* show_checkin_reminder_check_;
};


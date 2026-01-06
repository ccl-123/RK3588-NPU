#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QTimeEdit>
#include <QSlider>
#include <QComboBox>
#include <QPushButton>
#include <QLineEdit>
#include "config/config.h"
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void activate();

signals:
    void themeToggleRequested();
    void settingsChanged();
    void weatherSettingsChanged();  // 天气设置变更，需要刷新天气
    void cameraSettingsChanged(int deviceId);  // 摄像头设置变更，需要重新初始化摄像头

private slots:
    void on_theme_toggle_clicked();
    void on_save_clicked();
    void on_reset_clicked();
    void on_clear_cache_clicked();
    void on_apply_camera_clicked();  // 应用摄像头设置
    void onAudioDevicesRefreshed(const QStringList& devices);  // 音频设备异步刷新完成

private:
    void setup_ui();
    void load_settings();
    void save_settings();
    void update_db_size();
    bool activated_{false};
    
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
    QDoubleSpinBox* user_confirm_duration_spin_;  // 用户识别确认时间（秒）
    
public:
    // 公开访问识别阈值（供 MainWindow 使用）
    float getRecognitionThreshold() const {
        return recognition_threshold_spin_ ? recognition_threshold_spin_->value() : 0.60f;
    }
    
    // 获取用户识别确认时间（毫秒）
    int getUserConfirmDuration() const {
        return user_confirm_duration_spin_ ? 
            static_cast<int>(user_confirm_duration_spin_->value() * 1000) : 1000;
    }
    
private:
    
    // 考勤设置
    QTimeEdit* work_start_time_edit_;
    QTimeEdit* work_end_time_edit_;
    QSpinBox* late_threshold_spin_;
    QSpinBox* early_leave_threshold_spin_;
    QCheckBox* allow_multiple_checkin_check_;
    QCheckBox* checkin_sound_check_;
    QCheckBox* show_checkin_reminder_check_;
    
    // 音频设置
    QCheckBox* audio_enabled_check_;
    QSlider* audio_volume_slider_;
    QLabel* audio_volume_label_;
    QComboBox* audio_device_combo_;
    QPushButton* test_audio_btn_;
    
    // 摄像头设置
    QComboBox* camera_device_combo_;
    QPushButton* refresh_camera_btn_;
    QPushButton* apply_camera_btn_;  // 应用摄像头设置按钮
    
    // 天气/城市设置
    QCheckBox* auto_location_check_;
    QComboBox* city_preset_combo_;
    QLineEdit* manual_city_edit_;
    QDoubleSpinBox* manual_lat_spin_;
    QDoubleSpinBox* manual_lon_spin_;
    
private:
    void scan_usb_cameras();
    void on_auto_location_changed(bool checked);
};

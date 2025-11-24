#pragma once

#include <QWidget>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>

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
    
    // UI 组件
    QLabel* version_label_;
    QLabel* db_size_label_;
    QLabel* cache_size_label_;
    QCheckBox* auto_start_check_;
    QCheckBox* show_fps_check_;
    QCheckBox* show_confidence_check_;
    QSpinBox* duplicate_check_interval_spin_;
    QDoubleSpinBox* recognition_threshold_spin_;
};


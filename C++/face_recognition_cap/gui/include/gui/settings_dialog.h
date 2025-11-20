/**
 * @file settings_dialog.h
 * @brief 系统设置对话框
 * @author Augment Agent
 * @date 2025-11-20
 */

/**
 * @file settings_dialog.h
 * @brief 系统设置对话框类定义
 * @author CL
 * @date 2025-11-20
 *
 * 提供系统参数配置界面，包括摄像头设置、识别参数、性能设置等。
 */

#ifndef SETTINGS_DIALOG_H
#define SETTINGS_DIALOG_H

#include <QDialog>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include "app/face_recognition_app.h"

/**
 * @brief 系统设置对话框
 */
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(AppConfig* config, QWidget* parent = nullptr);
    ~SettingsDialog();

private slots:
    void on_ok_clicked();
    void on_cancel_clicked();
    void on_apply_clicked();
    void on_reset_clicked();

private:
    void setup_ui();
    void load_settings();
    void save_settings();
    void reset_to_defaults();

private:
    AppConfig* config_;
    AppConfig original_config_;
    
    // 摄像头设置
    QComboBox* camera_type_combo_;
    QLineEdit* device_number_edit_;
    QSpinBox* camera_width_spin_;
    QSpinBox* camera_height_spin_;
    QCheckBox* async_usb_check_;
    
    // 识别参数
    QDoubleSpinBox* box_conf_spin_;
    QDoubleSpinBox* nms_threshold_spin_;
    QDoubleSpinBox* facenet_threshold_spin_;
    
    // 性能设置
    QSpinBox* perf_report_interval_spin_;
    
    // 按钮
    QPushButton* ok_btn_;
    QPushButton* cancel_btn_;
    QPushButton* apply_btn_;
    QPushButton* reset_btn_;
};

#endif // SETTINGS_DIALOG_H


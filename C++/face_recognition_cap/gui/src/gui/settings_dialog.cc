/**
 * @file settings_dialog.cc
 * @brief 系统设置对话框类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/settings_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>

SettingsDialog::SettingsDialog(AppConfig* config, QWidget* parent)
    : QDialog(parent)
    , config_(config)
    , original_config_(*config)
{
    setup_ui();
    load_settings();
}

SettingsDialog::~SettingsDialog() {
}

void SettingsDialog::setup_ui() {
    setWindowTitle("系统设置");
    resize(500, 600);
    
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    // 摄像头设置组
    QGroupBox* camera_group = new QGroupBox("摄像头设置");
    QFormLayout* camera_layout = new QFormLayout();
    
    camera_type_combo_ = new QComboBox();
    camera_type_combo_->addItem("USB 摄像头", "usb");
    camera_type_combo_->addItem("MIPI 摄像头", "mipi");
    
    device_number_edit_ = new QLineEdit();
    
    camera_width_spin_ = new QSpinBox();
    camera_width_spin_->setRange(320, 1920);
    camera_width_spin_->setSingleStep(160);
    
    camera_height_spin_ = new QSpinBox();
    camera_height_spin_->setRange(240, 1080);
    camera_height_spin_->setSingleStep(120);
    
    async_usb_check_ = new QCheckBox("启用异步读取（仅USB）");
    
    camera_layout->addRow("摄像头类型:", camera_type_combo_);
    camera_layout->addRow("设备编号:", device_number_edit_);
    camera_layout->addRow("分辨率宽度:", camera_width_spin_);
    camera_layout->addRow("分辨率高度:", camera_height_spin_);
    camera_layout->addRow("", async_usb_check_);
    
    camera_group->setLayout(camera_layout);
    
    // 识别参数组
    QGroupBox* recognition_group = new QGroupBox("识别参数");
    QFormLayout* recognition_layout = new QFormLayout();
    
    box_conf_spin_ = new QDoubleSpinBox();
    box_conf_spin_->setRange(0.1, 1.0);
    box_conf_spin_->setSingleStep(0.05);
    box_conf_spin_->setDecimals(2);
    
    nms_threshold_spin_ = new QDoubleSpinBox();
    nms_threshold_spin_->setRange(0.1, 1.0);
    nms_threshold_spin_->setSingleStep(0.05);
    nms_threshold_spin_->setDecimals(2);
    
    facenet_threshold_spin_ = new QDoubleSpinBox();
    facenet_threshold_spin_->setRange(0.1, 1.0);
    facenet_threshold_spin_->setSingleStep(0.05);
    facenet_threshold_spin_->setDecimals(2);
    
    recognition_layout->addRow("人脸检测置信度:", box_conf_spin_);
    recognition_layout->addRow("NMS 阈值:", nms_threshold_spin_);
    recognition_layout->addRow("人脸识别阈值:", facenet_threshold_spin_);
    
    recognition_group->setLayout(recognition_layout);
    
    // 性能设置组
    QGroupBox* performance_group = new QGroupBox("性能设置");
    QFormLayout* performance_layout = new QFormLayout();
    
    perf_report_interval_spin_ = new QSpinBox();
    perf_report_interval_spin_->setRange(1, 100);
    perf_report_interval_spin_->setSuffix(" 帧");
    
    performance_layout->addRow("性能报告间隔:", perf_report_interval_spin_);
    
    performance_group->setLayout(performance_layout);
    
    // 按钮
    QHBoxLayout* button_layout = new QHBoxLayout();
    
    ok_btn_ = new QPushButton("确定");
    connect(ok_btn_, &QPushButton::clicked, this, &SettingsDialog::on_ok_clicked);
    
    cancel_btn_ = new QPushButton("取消");
    connect(cancel_btn_, &QPushButton::clicked, this, &SettingsDialog::on_cancel_clicked);
    
    apply_btn_ = new QPushButton("应用");
    connect(apply_btn_, &QPushButton::clicked, this, &SettingsDialog::on_apply_clicked);
    
    reset_btn_ = new QPushButton("恢复默认");
    connect(reset_btn_, &QPushButton::clicked, this, &SettingsDialog::on_reset_clicked);
    
    button_layout->addWidget(reset_btn_);
    button_layout->addStretch();
    button_layout->addWidget(ok_btn_);
    button_layout->addWidget(cancel_btn_);
    button_layout->addWidget(apply_btn_);
    
    main_layout->addWidget(camera_group);
    main_layout->addWidget(recognition_group);
    main_layout->addWidget(performance_group);
    main_layout->addStretch();
    main_layout->addLayout(button_layout);
}

void SettingsDialog::load_settings() {
    // 摄像头设置
    int camera_index = camera_type_combo_->findData(QString::fromStdString(config_->camera_type));
    if (camera_index >= 0) {
        camera_type_combo_->setCurrentIndex(camera_index);
    }
    
    device_number_edit_->setText(QString::fromStdString(config_->device_number));
    camera_width_spin_->setValue(config_->camera_width);
    camera_height_spin_->setValue(config_->camera_height);
    async_usb_check_->setChecked(config_->use_async_usb);
    
    // 识别参数
    box_conf_spin_->setValue(config_->box_conf_threshold);
    nms_threshold_spin_->setValue(config_->nms_threshold);
    facenet_threshold_spin_->setValue(config_->facenet_threshold);
    
    // 性能设置
    perf_report_interval_spin_->setValue(config_->perf_report_interval);
}

void SettingsDialog::save_settings() {
    // 摄像头设置
    config_->camera_type = camera_type_combo_->currentData().toString().toStdString();
    config_->device_number = device_number_edit_->text().toStdString();
    config_->camera_width = camera_width_spin_->value();
    config_->camera_height = camera_height_spin_->value();
    config_->use_async_usb = async_usb_check_->isChecked();
    
    // 识别参数
    config_->box_conf_threshold = box_conf_spin_->value();
    config_->nms_threshold = nms_threshold_spin_->value();
    config_->facenet_threshold = facenet_threshold_spin_->value();
    
    // 性能设置
    config_->perf_report_interval = perf_report_interval_spin_->value();
}

void SettingsDialog::reset_to_defaults() {
    AppConfig defaults;
    camera_width_spin_->setValue(defaults.camera_width);
    camera_height_spin_->setValue(defaults.camera_height);
    box_conf_spin_->setValue(defaults.box_conf_threshold);
    nms_threshold_spin_->setValue(defaults.nms_threshold);
    facenet_threshold_spin_->setValue(defaults.facenet_threshold);
    async_usb_check_->setChecked(defaults.use_async_usb);
    perf_report_interval_spin_->setValue(defaults.perf_report_interval);
}

void SettingsDialog::on_ok_clicked() {
    save_settings();
    QMessageBox::information(this, "提示", "设置已保存\n部分设置需要重启程序后生效");
    accept();
}

void SettingsDialog::on_cancel_clicked() {
    *config_ = original_config_;
    reject();
}

void SettingsDialog::on_apply_clicked() {
    save_settings();
    QMessageBox::information(this, "提示", "设置已应用\n部分设置需要重启程序后生效");
}

void SettingsDialog::on_reset_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(this, "确认",
        "确定要恢复默认设置吗？",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        reset_to_defaults();
    }
}


/**
 * @file settings_dialog.cc
 * @brief 系统设置对话框类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/settings_dialog.h"

#include "widgets/card_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <QVariant>

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
    setWindowTitle(tr("系统设置"));
    resize(560, 680);

    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(24);

    auto build_form = [](CardWidget* card) {
        auto layout = new QFormLayout(card->bodyContainer());
        layout->setLabelAlignment(Qt::AlignRight);
        layout->setHorizontalSpacing(24);
        return layout;
    };

    auto camera_card = new CardWidget(this);
    camera_card->setTitle(tr("摄像头设置"));
    camera_card->setSubtitle(tr("配置设备类型、分辨率等基础参数"));
    auto camera_layout = build_form(camera_card);

    camera_type_combo_ = new QComboBox(camera_card);
    camera_type_combo_->addItem(tr("USB 摄像头"), "usb");
    camera_type_combo_->addItem(tr("MIPI 摄像头"), "mipi");

    device_number_edit_ = new QLineEdit(camera_card);

    camera_width_spin_ = new QSpinBox(camera_card);
    camera_width_spin_->setRange(320, 1920);
    camera_width_spin_->setSingleStep(160);

    camera_height_spin_ = new QSpinBox(camera_card);
    camera_height_spin_->setRange(240, 1080);
    camera_height_spin_->setSingleStep(120);

    async_usb_check_ = new QCheckBox(tr("启用异步读取（仅 USB）"), camera_card);

    camera_layout->addRow(tr("摄像头类型"), camera_type_combo_);
    camera_layout->addRow(tr("设备编号"), device_number_edit_);
    camera_layout->addRow(tr("分辨率宽度"), camera_width_spin_);
    camera_layout->addRow(tr("分辨率高度"), camera_height_spin_);
    camera_layout->addRow(QString(), async_usb_check_);

    auto recognition_card = new CardWidget(this);
    recognition_card->setTitle(tr("识别参数"));
    recognition_card->setSubtitle(tr("控制检测与识别模型灵敏度"));
    auto recognition_layout = build_form(recognition_card);

    box_conf_spin_ = new QDoubleSpinBox(recognition_card);
    box_conf_spin_->setRange(0.1, 1.0);
    box_conf_spin_->setSingleStep(0.05);
    box_conf_spin_->setDecimals(2);

    nms_threshold_spin_ = new QDoubleSpinBox(recognition_card);
    nms_threshold_spin_->setRange(0.1, 1.0);
    nms_threshold_spin_->setSingleStep(0.05);
    nms_threshold_spin_->setDecimals(2);

    facenet_threshold_spin_ = new QDoubleSpinBox(recognition_card);
    facenet_threshold_spin_->setRange(0.1, 1.0);
    facenet_threshold_spin_->setSingleStep(0.05);
    facenet_threshold_spin_->setDecimals(2);

    recognition_layout->addRow(tr("检测置信度"), box_conf_spin_);
    recognition_layout->addRow(tr("NMS 阈值"), nms_threshold_spin_);
    recognition_layout->addRow(tr("识别阈值"), facenet_threshold_spin_);

    auto performance_card = new CardWidget(this);
    performance_card->setTitle(tr("性能与监控"));
    performance_card->setSubtitle(tr("调整性能上报、监控频率"));
    auto performance_layout = build_form(performance_card);

    perf_report_interval_spin_ = new QSpinBox(performance_card);
    perf_report_interval_spin_->setRange(1, 100);
    perf_report_interval_spin_->setSuffix(tr(" 帧"));
    performance_layout->addRow(tr("性能报告间隔"), perf_report_interval_spin_);

    auto button_layout = new QHBoxLayout();
    button_layout->setContentsMargins(0, 0, 0, 0);

    reset_btn_ = new QPushButton(tr("恢复默认"), this);
    connect(reset_btn_, &QPushButton::clicked, this, &SettingsDialog::on_reset_clicked);

    ok_btn_ = new QPushButton(tr("确定"), this);
    ok_btn_->setProperty("primary", QVariant(true));
    connect(ok_btn_, &QPushButton::clicked, this, &SettingsDialog::on_ok_clicked);

    cancel_btn_ = new QPushButton(tr("取消"), this);
    connect(cancel_btn_, &QPushButton::clicked, this, &SettingsDialog::on_cancel_clicked);

    apply_btn_ = new QPushButton(tr("应用"), this);
    connect(apply_btn_, &QPushButton::clicked, this, &SettingsDialog::on_apply_clicked);

    button_layout->addWidget(reset_btn_);
    button_layout->addStretch();
    button_layout->addWidget(ok_btn_);
    button_layout->addWidget(cancel_btn_);
    button_layout->addWidget(apply_btn_);

    main_layout->addWidget(camera_card);
    main_layout->addWidget(recognition_card);
    main_layout->addWidget(performance_card);
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


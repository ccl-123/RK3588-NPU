#include "ui/settings_page.h"

#include "widgets/card_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QVariant>
#include <spdlog/spdlog.h>

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
    , version_label_(nullptr)
    , db_size_label_(nullptr)
    , cache_size_label_(nullptr)
    , auto_start_check_(nullptr)
    , show_fps_check_(nullptr)
    , show_confidence_check_(nullptr)
    , duplicate_check_interval_spin_(nullptr)
    , recognition_threshold_spin_(nullptr) {
    setup_ui();
    load_settings();
}

void SettingsPage::setup_ui() {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    // 系统信息卡片
    auto info_card = new CardWidget(this);
    info_card->setTitle(tr("系统信息"));

    auto info_layout = new QFormLayout(info_card->bodyContainer());
    info_layout->setLabelAlignment(Qt::AlignRight);
    info_layout->setHorizontalSpacing(24);
    info_layout->setVerticalSpacing(12);

    version_label_ = new QLabel("v1.0.0");
    info_layout->addRow(tr("版本号:"), version_label_);

    auto build_date_label = new QLabel("2025-11-24");
    info_layout->addRow(tr("构建日期:"), build_date_label);

    db_size_label_ = new QLabel("0 KB");
    info_layout->addRow(tr("数据库大小:"), db_size_label_);

    cache_size_label_ = new QLabel("0 KB");
    info_layout->addRow(tr("缓存大小:"), cache_size_label_);

    // 显示设置卡片
    auto display_card = new CardWidget(this);
    display_card->setTitle(tr("显示设置"));
    
    auto display_layout = new QVBoxLayout(display_card->bodyContainer());
    display_layout->setSpacing(12);

    auto theme_row = new QHBoxLayout();
    theme_row->addWidget(new QLabel(tr("主题:")));
    auto theme_btn = new QPushButton(tr("切换主题"));
    connect(theme_btn, &QPushButton::clicked, this, &SettingsPage::on_theme_toggle_clicked);
    theme_row->addWidget(theme_btn);
    theme_row->addStretch();
    display_layout->addLayout(theme_row);

    show_fps_check_ = new QCheckBox(tr("显示 FPS 信息"));
    show_fps_check_->setChecked(true);
    display_layout->addWidget(show_fps_check_);

    show_confidence_check_ = new QCheckBox(tr("显示识别置信度"));
    show_confidence_check_->setChecked(true);
    display_layout->addWidget(show_confidence_check_);

    // 识别设置卡片
    auto recognition_card = new CardWidget(this);
    recognition_card->setTitle(tr("识别设置"));
    
    auto recognition_layout = new QFormLayout(recognition_card->bodyContainer());
    recognition_layout->setLabelAlignment(Qt::AlignRight);
    recognition_layout->setHorizontalSpacing(24);
    recognition_layout->setVerticalSpacing(12);

    recognition_threshold_spin_ = new QDoubleSpinBox();
    recognition_threshold_spin_->setRange(0.1, 1.0);
    recognition_threshold_spin_->setSingleStep(0.05);
    recognition_threshold_spin_->setDecimals(2);
    recognition_threshold_spin_->setValue(0.60);
    recognition_threshold_spin_->setSuffix("");
    recognition_layout->addRow(tr("识别阈值:"), recognition_threshold_spin_);

    duplicate_check_interval_spin_ = new QSpinBox();
    duplicate_check_interval_spin_->setRange(60, 3600);
    duplicate_check_interval_spin_->setSingleStep(60);
    duplicate_check_interval_spin_->setValue(300);
    duplicate_check_interval_spin_->setSuffix(" 秒");
    recognition_layout->addRow(tr("重复检测间隔:"), duplicate_check_interval_spin_);

    // 系统设置卡片
    auto system_card = new CardWidget(this);
    system_card->setTitle(tr("系统设置"));
    
    auto system_layout = new QVBoxLayout(system_card->bodyContainer());
    system_layout->setSpacing(12);

    auto_start_check_ = new QCheckBox(tr("系统启动时自动运行"));
    auto_start_check_->setChecked(false);
    system_layout->addWidget(auto_start_check_);

    // 数据管理
    auto data_row = new QHBoxLayout();
    data_row->addWidget(new QLabel(tr("数据管理:")));
    auto clear_cache_btn = new QPushButton(tr("清理缓存"));
    connect(clear_cache_btn, &QPushButton::clicked, this, &SettingsPage::on_clear_cache_clicked);
    data_row->addWidget(clear_cache_btn);
    data_row->addStretch();
    system_layout->addLayout(data_row);

    // 按钮栏
    auto button_layout = new QHBoxLayout();
    button_layout->setSpacing(12);
    button_layout->addStretch();

    auto reset_btn = new QPushButton(tr("恢复默认"));
    connect(reset_btn, &QPushButton::clicked, this, &SettingsPage::on_reset_clicked);
    button_layout->addWidget(reset_btn);

    auto save_btn = new QPushButton(tr("保存设置"));
    save_btn->setProperty("primary", QVariant(true));
    connect(save_btn, &QPushButton::clicked, this, &SettingsPage::on_save_clicked);
    button_layout->addWidget(save_btn);

    layout->addWidget(info_card);
    layout->addWidget(display_card);
    layout->addWidget(recognition_card);
    layout->addWidget(system_card);
    layout->addLayout(button_layout);
    layout->addStretch();
}

void SettingsPage::load_settings() {
    // 这里可以从配置文件或数据库加载设置
    // 目前使用默认值
    
    // 更新数据库大小信息
    QFileInfo db_file("data/database/attendance.db");
    if (db_file.exists() && db_size_label_) {
        qint64 size_kb = db_file.size() / 1024;
        db_size_label_->setText(QString("%1 KB").arg(size_kb));
    }
    
    spdlog::info("Settings loaded");
}

void SettingsPage::save_settings() {
    // 这里可以保存设置到配置文件或数据库
    // 目前只是记录日志
    
    spdlog::info("Settings saved: recognition_threshold={}, duplicate_check_interval={}, show_fps={}, auto_start={}",
                 recognition_threshold_spin_ ? recognition_threshold_spin_->value() : 0.6,
                 duplicate_check_interval_spin_ ? duplicate_check_interval_spin_->value() : 300,
                 show_fps_check_ ? show_fps_check_->isChecked() : true,
                 auto_start_check_ ? auto_start_check_->isChecked() : false);
    
    emit settingsChanged();
}

void SettingsPage::on_theme_toggle_clicked() {
    emit themeToggleRequested();
}

void SettingsPage::on_save_clicked() {
    save_settings();
    QMessageBox::information(this, tr("成功"), tr("设置已保存"));
}

void SettingsPage::on_reset_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认"), tr("确定要恢复默认设置吗？"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        if (recognition_threshold_spin_) recognition_threshold_spin_->setValue(0.60);
        if (duplicate_check_interval_spin_) duplicate_check_interval_spin_->setValue(300);
        if (show_fps_check_) show_fps_check_->setChecked(true);
        if (show_confidence_check_) show_confidence_check_->setChecked(true);
        if (auto_start_check_) auto_start_check_->setChecked(false);
        
        QMessageBox::information(this, tr("成功"), tr("已恢复默认设置"));
        spdlog::info("Settings reset to defaults");
    }
}

void SettingsPage::on_clear_cache_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认"), tr("确定要清理缓存吗？"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // 这里可以实现实际的缓存清理逻辑
        if (cache_size_label_) {
            cache_size_label_->setText("0 KB");
        }
        QMessageBox::information(this, tr("成功"), tr("缓存已清理"));
        spdlog::info("Cache cleared");
    }
}



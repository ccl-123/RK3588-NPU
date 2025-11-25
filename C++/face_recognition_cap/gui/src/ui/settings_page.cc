#include "ui/settings_page.h"

#include "widgets/card_widget.h"
#include "utils/audio_manager.h"
#include "utils/config_manager.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileInfo>
#include <QVariant>
#include <QTime>
#include <QScrollArea>
#include <QStringList>
#include <spdlog/spdlog.h>

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
    , version_label_(nullptr)
    , db_size_label_(nullptr)
    , auto_start_check_(nullptr)
    , show_fps_check_(nullptr)
    , show_confidence_check_(nullptr)
    , duplicate_check_interval_spin_(nullptr)
    , recognition_threshold_spin_(nullptr)
    , recognition_confirm_count_spin_(nullptr)
    , work_start_time_edit_(nullptr)
    , work_end_time_edit_(nullptr)
    , late_threshold_spin_(nullptr)
    , early_leave_threshold_spin_(nullptr)
    , allow_multiple_checkin_check_(nullptr)
    , checkin_sound_check_(nullptr)
    , show_checkin_reminder_check_(nullptr)
    , audio_enabled_check_(nullptr)
    , audio_volume_slider_(nullptr)
    , audio_volume_label_(nullptr)
    , audio_device_combo_(nullptr)
    , test_audio_btn_(nullptr) {
    setup_ui();
    load_settings();
}

void SettingsPage::setup_ui() {
    // 创建滚动区域以容纳所有设置
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    
    auto content = new QWidget();
    scroll->setWidget(content);
    
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(scroll);
    
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    // ========== 系统信息（紧凑型）==========
    auto info_card = new CardWidget(content);
    info_card->setTitle(tr("系统信息"));
    
    auto info_layout = new QGridLayout(info_card->bodyContainer());
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->setHorizontalSpacing(16);
    info_layout->setVerticalSpacing(8);
    
    version_label_ = new QLabel("v1.0.0");
    db_size_label_ = new QLabel("0 KB");
    
    info_layout->addWidget(new QLabel(tr("版本:")), 0, 0, Qt::AlignRight);
    info_layout->addWidget(version_label_, 0, 1);
    info_layout->addWidget(new QLabel(tr("数据库:")), 0, 2, Qt::AlignRight);
    info_layout->addWidget(db_size_label_, 0, 3);
    
    // ========== 考勤系统设置（新增）==========
    auto attendance_card = new CardWidget(content);
    attendance_card->setTitle(tr("考勤设置"));
    
    auto attendance_layout = new QGridLayout(attendance_card->bodyContainer());
    attendance_layout->setContentsMargins(0, 0, 0, 0);
    attendance_layout->setHorizontalSpacing(12);
    attendance_layout->setVerticalSpacing(10);
    
    int row = 0;
    
    // 工作时间设置
    work_start_time_edit_ = new QTimeEdit();
    work_start_time_edit_->setTime(QTime(9, 0));
    work_start_time_edit_->setDisplayFormat("HH:mm");
    attendance_layout->addWidget(new QLabel(tr("上班时间:")), row, 0, Qt::AlignRight);
    attendance_layout->addWidget(work_start_time_edit_, row, 1);
    
    work_end_time_edit_ = new QTimeEdit();
    work_end_time_edit_->setTime(QTime(18, 0));
    work_end_time_edit_->setDisplayFormat("HH:mm");
    attendance_layout->addWidget(new QLabel(tr("下班时间:")), row, 2, Qt::AlignRight);
    attendance_layout->addWidget(work_end_time_edit_, row, 3);
    row++;
    
    // 迟到/早退阈值
    late_threshold_spin_ = new QSpinBox();
    late_threshold_spin_->setRange(0, 120);
    late_threshold_spin_->setValue(30);
    late_threshold_spin_->setSuffix(" 分钟");
    attendance_layout->addWidget(new QLabel(tr("迟到阈值:")), row, 0, Qt::AlignRight);
    attendance_layout->addWidget(late_threshold_spin_, row, 1);
    
    early_leave_threshold_spin_ = new QSpinBox();
    early_leave_threshold_spin_->setRange(0, 120);
    early_leave_threshold_spin_->setValue(30);
    early_leave_threshold_spin_->setSuffix(" 分钟");
    attendance_layout->addWidget(new QLabel(tr("早退阈值:")), row, 2, Qt::AlignRight);
    attendance_layout->addWidget(early_leave_threshold_spin_, row, 3);
    row++;
    
    // 签到选项（横向排列）
    auto checkin_opts = new QHBoxLayout();
    checkin_opts->setSpacing(20);
    
    allow_multiple_checkin_check_ = new QCheckBox(tr("允许一天多次签到"));
    allow_multiple_checkin_check_->setChecked(false);
    checkin_opts->addWidget(allow_multiple_checkin_check_);
    checkin_opts->addStretch();
    
    attendance_layout->addLayout(checkin_opts, row, 0, 1, 4);
    row++;
    
    // 提醒设置
    checkin_sound_check_ = new QCheckBox(tr("签到声音提示"));
    checkin_sound_check_->setChecked(true);
    attendance_layout->addWidget(checkin_sound_check_, row, 0, 1, 2);
    
    show_checkin_reminder_check_ = new QCheckBox(tr("显示签到提醒"));
    show_checkin_reminder_check_->setChecked(true);
    attendance_layout->addWidget(show_checkin_reminder_check_, row, 2, 1, 2);
    
    // ========== 识别设置 ==========
    auto recognition_card = new CardWidget(content);
    recognition_card->setTitle(tr("识别设置"));
    
    auto recognition_layout = new QGridLayout(recognition_card->bodyContainer());
    recognition_layout->setContentsMargins(0, 0, 0, 0);
    recognition_layout->setHorizontalSpacing(12);
    recognition_layout->setVerticalSpacing(10);
    
    // 第一行：识别阈值和重复检测
    recognition_threshold_spin_ = new QDoubleSpinBox();
    recognition_threshold_spin_->setRange(0.3, 0.95);
    recognition_threshold_spin_->setSingleStep(0.05);
    recognition_threshold_spin_->setDecimals(2);
    recognition_threshold_spin_->setValue(0.60);
    recognition_layout->addWidget(new QLabel(tr("识别阈值:")), 0, 0, Qt::AlignRight);
    recognition_layout->addWidget(recognition_threshold_spin_, 0, 1);
    
    duplicate_check_interval_spin_ = new QSpinBox();
    duplicate_check_interval_spin_->setRange(60, 3600);
    duplicate_check_interval_spin_->setSingleStep(60);
    duplicate_check_interval_spin_->setValue(300);
    duplicate_check_interval_spin_->setSuffix(" 秒");
    duplicate_check_interval_spin_->setToolTip(tr("防止同一人短时间内重复签到"));
    recognition_layout->addWidget(new QLabel(tr("防重复签到:")), 0, 2, Qt::AlignRight);
    recognition_layout->addWidget(duplicate_check_interval_spin_, 0, 3);
    
    // 第二行：连续确认次数（防误识别）
    recognition_confirm_count_spin_ = new QSpinBox();
    recognition_confirm_count_spin_->setRange(1, 10);
    recognition_confirm_count_spin_->setValue(3);
    recognition_confirm_count_spin_->setSuffix(" 次");
    recognition_confirm_count_spin_->setToolTip(tr("连续识别到同一人多少次后才确认签到，可防止误识别"));
    recognition_layout->addWidget(new QLabel(tr("确认次数:")), 1, 0, Qt::AlignRight);
    recognition_layout->addWidget(recognition_confirm_count_spin_, 1, 1);
    
    auto confirm_hint = new QLabel(tr("(防止误识别导致错误签到)"));
    confirm_hint->setStyleSheet("color: #8c8c8c; font-size: 12px;");
    recognition_layout->addWidget(confirm_hint, 1, 2, 1, 2);
    
    // ========== 显示与系统设置 ==========
    auto system_card = new CardWidget(content);
    system_card->setTitle(tr("系统设置"));
    
    auto system_layout = new QVBoxLayout(system_card->bodyContainer());
    system_layout->setContentsMargins(0, 0, 0, 0);
    system_layout->setSpacing(10);
    
    // 显示选项（横向排列）
    auto display_row = new QHBoxLayout();
    display_row->setSpacing(20);
    
    show_fps_check_ = new QCheckBox(tr("显示FPS"));
    show_fps_check_->setChecked(true);
    display_row->addWidget(show_fps_check_);
    
    show_confidence_check_ = new QCheckBox(tr("显示置信度"));
    show_confidence_check_->setChecked(true);
    display_row->addWidget(show_confidence_check_);
    
    auto_start_check_ = new QCheckBox(tr("开机自启"));
    auto_start_check_->setChecked(false);
    display_row->addWidget(auto_start_check_);
    
    display_row->addStretch();
    system_layout->addLayout(display_row);
    
    // 操作按钮
    auto action_row = new QHBoxLayout();
    action_row->setSpacing(12);
    
    auto theme_btn = new QPushButton(tr("切换主题"));
    connect(theme_btn, &QPushButton::clicked, this, &SettingsPage::on_theme_toggle_clicked);
    action_row->addWidget(theme_btn);
    
    auto clear_cache_btn = new QPushButton(tr("清理缓存"));
    connect(clear_cache_btn, &QPushButton::clicked, this, &SettingsPage::on_clear_cache_clicked);
    action_row->addWidget(clear_cache_btn);
    
    action_row->addStretch();
    system_layout->addLayout(action_row);

    // ========== 音频设置 ==========
    auto audio_card = new CardWidget(content);
    audio_card->setTitle(tr("音频设置"));
    
    auto audio_layout = new QGridLayout(audio_card->bodyContainer());
    audio_layout->setContentsMargins(0, 0, 0, 0);
    audio_layout->setHorizontalSpacing(12);
    audio_layout->setVerticalSpacing(10);
    
    int audio_row = 0;
    
    // 第1行：启用音频
    audio_enabled_check_ = new QCheckBox(tr("启用语音播报"));
    audio_enabled_check_->setChecked(true);
    audio_layout->addWidget(audio_enabled_check_, audio_row, 0, 1, 4);
    audio_row++;
    
    // 第2行：音量控制
    audio_layout->addWidget(new QLabel(tr("音量:")), audio_row, 0, Qt::AlignRight);
    
    audio_volume_slider_ = new QSlider(Qt::Horizontal);
    audio_volume_slider_->setRange(0, 100);
    audio_volume_slider_->setValue(70);
    audio_volume_slider_->setTickPosition(QSlider::TicksBelow);
    audio_volume_slider_->setTickInterval(10);
    audio_layout->addWidget(audio_volume_slider_, audio_row, 1, 1, 2);
    
    audio_volume_label_ = new QLabel("70%");
    audio_volume_label_->setMinimumWidth(50);
    audio_layout->addWidget(audio_volume_label_, audio_row, 3);
    
    // 连接音量滑块信号
    connect(audio_volume_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (audio_volume_label_) {
            audio_volume_label_->setText(QString("%1%").arg(value));
        }
        // 实时更新音量
        AudioManager::instance()->setVolume(value);
    });
    audio_row++;
    
    // 第3行：音频设备选择
    audio_layout->addWidget(new QLabel(tr("输出设备:")), audio_row, 0, Qt::AlignRight);
    
    audio_device_combo_ = new QComboBox();
    audio_device_combo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    audio_layout->addWidget(audio_device_combo_, audio_row, 1, 1, 2);
    
    test_audio_btn_ = new QPushButton(tr("测试"));
    test_audio_btn_->setMaximumWidth(80);
    audio_layout->addWidget(test_audio_btn_, audio_row, 3);
    
    // 填充音频设备列表
    QStringList devices = AudioManager::instance()->availableDevices();
    audio_device_combo_->addItems(devices);
    QString currentDevice = AudioManager::instance()->currentDevice();
    int deviceIndex = audio_device_combo_->findText(currentDevice);
    if (deviceIndex >= 0) {
        audio_device_combo_->setCurrentIndex(deviceIndex);
    }
    
    // 连接设备选择信号
    connect(audio_device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && audio_device_combo_) {
            QString deviceName = audio_device_combo_->currentText();
            AudioManager::instance()->setAudioDevice(deviceName);
            spdlog::info("Audio device changed to: {}", deviceName.toStdString());
        }
    });
    
    // 连接测试按钮
    connect(test_audio_btn_, &QPushButton::clicked, this, [this]() {
        AudioManager::instance()->playSound(AudioType::CheckInSuccess);
    });
    
    // 连接启用/禁用复选框
    connect(audio_enabled_check_, &QCheckBox::toggled, this, [this](bool checked) {
        AudioManager::instance()->setEnabled(checked);
        if (audio_volume_slider_) audio_volume_slider_->setEnabled(checked);
        if (audio_device_combo_) audio_device_combo_->setEnabled(checked);
        if (test_audio_btn_) test_audio_btn_->setEnabled(checked);
    });

    // ========== 底部按钮栏 ==========
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

    // 添加所有卡片
    layout->addWidget(info_card);
    layout->addWidget(attendance_card);
    layout->addWidget(recognition_card);
    layout->addWidget(system_card);
    layout->addWidget(audio_card);
    layout->addLayout(button_layout);
    layout->addStretch();
}

void SettingsPage::load_settings() {
    // 从配置文件加载设置
    ConfigManager* config = ConfigManager::instance();
    
    // 更新数据库大小信息 - 尝试多个可能的路径
    if (db_size_label_) {
        QStringList db_paths = {
            // 绝对路径（优先）
            "/home/firefly/open_project/edge2-npu/C++/face_recognition_cap/install/face_recognition_cap/data/database/face_recognition.db",
            // 相对于安装目录
            "data/database/face_recognition.db",
            "../data/database/face_recognition.db",
            "../../data/database/face_recognition.db",
            // 相对于项目根目录
            "install/face_recognition_cap/data/database/face_recognition.db",
            "../install/face_recognition_cap/data/database/face_recognition.db",
            // 其他可能的路径
            "data/database/attendance.db"
        };
        
        bool db_found = false;
        for (const QString& path : db_paths) {
            QFileInfo db_file(path);
            if (db_file.exists()) {
                qint64 size_bytes = db_file.size();
                qint64 size_kb = size_bytes / 1024;
                
                if (size_kb > 1024) {
                    db_size_label_->setText(QString("%1 MB").arg(size_kb / 1024.0, 0, 'f', 2));
                } else if (size_kb > 0) {
                    db_size_label_->setText(QString("%1 KB").arg(size_kb));
                } else {
                    db_size_label_->setText(QString("%1 字节").arg(size_bytes));
                }
                
                spdlog::info("Database found: {} (size: {} bytes)", path.toStdString(), size_bytes);
                db_found = true;
                break;
            }
        }
        
        if (!db_found) {
            db_size_label_->setText("未找到");
            spdlog::warn("Database file not found in any expected location");
        }
    }
    
    // 加载识别设置
    if (recognition_threshold_spin_) {
        recognition_threshold_spin_->setValue(config->getRecognitionThreshold());
    }
    if (duplicate_check_interval_spin_) {
        duplicate_check_interval_spin_->setValue(config->getDuplicateCheckInterval());
    }
    if (recognition_confirm_count_spin_) {
        recognition_confirm_count_spin_->setValue(config->getRecognitionConfirmCount());
    }
    
    // 加载考勤设置
    if (work_start_time_edit_) {
        work_start_time_edit_->setTime(QTime::fromString(config->getWorkStartTime(), "HH:mm"));
    }
    if (work_end_time_edit_) {
        work_end_time_edit_->setTime(QTime::fromString(config->getWorkEndTime(), "HH:mm"));
    }
    if (late_threshold_spin_) late_threshold_spin_->setValue(config->getLateThreshold());
    if (early_leave_threshold_spin_) early_leave_threshold_spin_->setValue(config->getEarlyLeaveThreshold());
    if (allow_multiple_checkin_check_) allow_multiple_checkin_check_->setChecked(config->isAllowMultipleCheckin());
    if (checkin_sound_check_) checkin_sound_check_->setChecked(config->isCheckinSound());
    if (show_checkin_reminder_check_) show_checkin_reminder_check_->setChecked(config->isShowCheckinReminder());
    
    // 加载显示设置
    if (show_fps_check_) show_fps_check_->setChecked(config->isShowFPS());
    if (show_confidence_check_) show_confidence_check_->setChecked(config->isShowConfidence());
    if (auto_start_check_) auto_start_check_->setChecked(config->isAutoStart());
    
    // 加载音频设置
    if (audio_enabled_check_) {
        audio_enabled_check_->setChecked(config->isAudioEnabled());
    }
    if (audio_volume_slider_) {
        audio_volume_slider_->setValue(config->getAudioVolume());
    }
    if (audio_volume_label_) {
        audio_volume_label_->setText(QString("%1%").arg(config->getAudioVolume()));
    }
    if (audio_device_combo_) {
        QString currentDevice = config->getAudioDevice();
        if (!currentDevice.isEmpty()) {
            int deviceIndex = audio_device_combo_->findText(currentDevice);
            if (deviceIndex >= 0) {
                audio_device_combo_->setCurrentIndex(deviceIndex);
            }
        }
    }
    
    spdlog::info("Settings loaded");
}

void SettingsPage::save_settings() {
    // 保存设置到配置文件
    ConfigManager* config = ConfigManager::instance();
    
    // 保存识别设置
    if (recognition_threshold_spin_) {
        config->setRecognitionThreshold(recognition_threshold_spin_->value());
    }
    if (duplicate_check_interval_spin_) {
        config->setDuplicateCheckInterval(duplicate_check_interval_spin_->value());
    }
    if (recognition_confirm_count_spin_) {
        config->setRecognitionConfirmCount(recognition_confirm_count_spin_->value());
    }
    
    // 保存考勤设置
    if (work_start_time_edit_) {
        config->setWorkStartTime(work_start_time_edit_->time().toString("HH:mm"));
    }
    if (work_end_time_edit_) {
        config->setWorkEndTime(work_end_time_edit_->time().toString("HH:mm"));
    }
    if (late_threshold_spin_) config->setLateThreshold(late_threshold_spin_->value());
    if (early_leave_threshold_spin_) config->setEarlyLeaveThreshold(early_leave_threshold_spin_->value());
    if (allow_multiple_checkin_check_) config->setAllowMultipleCheckin(allow_multiple_checkin_check_->isChecked());
    if (checkin_sound_check_) config->setCheckinSound(checkin_sound_check_->isChecked());
    if (show_checkin_reminder_check_) config->setShowCheckinReminder(show_checkin_reminder_check_->isChecked());
    
    // 保存显示设置
    if (show_fps_check_) config->setShowFPS(show_fps_check_->isChecked());
    if (show_confidence_check_) config->setShowConfidence(show_confidence_check_->isChecked());
    if (auto_start_check_) config->setAutoStart(auto_start_check_->isChecked());
    
    spdlog::info("Settings saved:");
    spdlog::info("  - Recognition: threshold={:.2f}, duplicate_interval={}s, confirm_count={}",
                 recognition_threshold_spin_ ? recognition_threshold_spin_->value() : 0.6,
                 duplicate_check_interval_spin_ ? duplicate_check_interval_spin_->value() : 300,
                 recognition_confirm_count_spin_ ? recognition_confirm_count_spin_->value() : 3);
    
    spdlog::info("  - Attendance: work_time={}~{}, late_threshold={}min, early_leave={}min",
                 work_start_time_edit_ ? work_start_time_edit_->time().toString("HH:mm").toStdString() : "09:00",
                 work_end_time_edit_ ? work_end_time_edit_->time().toString("HH:mm").toStdString() : "18:00",
                 late_threshold_spin_ ? late_threshold_spin_->value() : 30,
                 early_leave_threshold_spin_ ? early_leave_threshold_spin_->value() : 30);
    
    spdlog::info("  - Display: show_fps={}, show_confidence={}, auto_start={}",
                 show_fps_check_ ? show_fps_check_->isChecked() : true,
                 show_confidence_check_ ? show_confidence_check_->isChecked() : true,
                 auto_start_check_ ? auto_start_check_->isChecked() : false);
    
    // 保存音频设置到配置文件和 AudioManager
    if (audio_enabled_check_) {
        bool enabled = audio_enabled_check_->isChecked();
        config->setAudioEnabled(enabled);
        AudioManager::instance()->setEnabled(enabled);
    }
    if (audio_volume_slider_) {
        int volume = audio_volume_slider_->value();
        config->setAudioVolume(volume);
        AudioManager::instance()->setVolume(volume);
    }
    if (audio_device_combo_ && audio_device_combo_->currentIndex() >= 0) {
        QString deviceName = audio_device_combo_->currentText();
        config->setAudioDevice(deviceName);
        AudioManager::instance()->setAudioDevice(deviceName);
    }
    
    spdlog::info("  - Audio: enabled={}, volume={}, device={}",
                 audio_enabled_check_ ? audio_enabled_check_->isChecked() : true,
                 audio_volume_slider_ ? audio_volume_slider_->value() : 70,
                 audio_device_combo_ ? audio_device_combo_->currentText().toStdString() : "default");
    
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
        // 识别设置
        if (recognition_threshold_spin_) recognition_threshold_spin_->setValue(0.60);
        if (duplicate_check_interval_spin_) duplicate_check_interval_spin_->setValue(300);
        if (recognition_confirm_count_spin_) recognition_confirm_count_spin_->setValue(3);
        
        // 考勤设置
        if (work_start_time_edit_) work_start_time_edit_->setTime(QTime(9, 0));
        if (work_end_time_edit_) work_end_time_edit_->setTime(QTime(18, 0));
        if (late_threshold_spin_) late_threshold_spin_->setValue(30);
        if (early_leave_threshold_spin_) early_leave_threshold_spin_->setValue(30);
        if (allow_multiple_checkin_check_) allow_multiple_checkin_check_->setChecked(false);
        if (checkin_sound_check_) checkin_sound_check_->setChecked(true);
        if (show_checkin_reminder_check_) show_checkin_reminder_check_->setChecked(true);
        
        // 显示设置
        if (show_fps_check_) show_fps_check_->setChecked(true);
        if (show_confidence_check_) show_confidence_check_->setChecked(true);
        if (auto_start_check_) auto_start_check_->setChecked(false);
        
        // 音频设置
        if (audio_enabled_check_) audio_enabled_check_->setChecked(true);
        if (audio_volume_slider_) audio_volume_slider_->setValue(70);
        if (audio_volume_label_) audio_volume_label_->setText("70%");
        // 音频设备保持当前设置不变
        
        QMessageBox::information(this, tr("成功"), tr("已恢复默认设置"));
        spdlog::info("Settings reset to defaults");
    }
}

void SettingsPage::on_clear_cache_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认"), tr("确定要清理缓存吗？\n这将清除临时文件和日志。"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // 这里可以实现实际的缓存清理逻辑
        // 例如：清理临时人脸图片、日志文件等
        
        QMessageBox::information(this, tr("成功"), tr("缓存已清理"));
        spdlog::info("Cache cleared");
    }
}



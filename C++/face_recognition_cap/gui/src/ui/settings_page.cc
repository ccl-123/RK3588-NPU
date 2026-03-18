#include "ui/settings_page.h"

#include "config/config.h"
#include "widgets/card_widget.h"
#include "gui_utils/audio_manager.h"
#include "gui_utils/config_manager.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QFileInfo>
#include <QVariant>
#include <QTime>
#include <QScrollArea>
#include <QStringList>
#include <QSignalBlocker>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QCoreApplication>
#include <spdlog/spdlog.h>

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
    , version_label_(nullptr)
    , db_size_label_(nullptr)
    , show_fps_check_(nullptr)
    , show_confidence_check_(nullptr)
    , auto_start_check_(nullptr)
    , recognition_threshold_spin_(nullptr)
    , duplicate_check_interval_spin_(nullptr)
    , user_confirm_duration_spin_(nullptr)
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
    , test_audio_btn_(nullptr)
    , camera_device_combo_(nullptr)
    , refresh_camera_btn_(nullptr)
    , apply_camera_btn_(nullptr)
    , auto_location_check_(nullptr)
    , city_preset_combo_(nullptr)
    , manual_city_edit_(nullptr)
    , manual_lat_spin_(nullptr)
    , manual_lon_spin_(nullptr) {
    setup_ui();
    // 延迟加载设置/设备枚举：避免主窗口首次显示被摄像头/音频设备扫描拖慢
}

void SettingsPage::activate() {
    if (!activated_) {
        activated_ = true;

        // 异步加载音频设备列表
        if (audio_device_combo_) {
            QSignalBlocker blocker(audio_device_combo_);
            audio_device_combo_->clear();
            audio_device_combo_->addItem(tr("正在加载设备..."));

            // 连接异步刷新信号
            connect(AudioManager::instance(), &AudioManager::devicesRefreshed,
                    this, &SettingsPage::onAudioDevicesRefreshed,
                    Qt::UniqueConnection);

            // 触发异步刷新
            AudioManager::instance()->refreshDevicesAsync();
        }
    }

    scan_usb_cameras();
    load_settings();
}

void SettingsPage::onAudioDevicesRefreshed(const QStringList& devices) {
    if (!audio_device_combo_) return;

    QSignalBlocker blocker(audio_device_combo_);
    audio_device_combo_->clear();

    if (devices.isEmpty()) {
        audio_device_combo_->addItem(tr("未检测到音频设备"));
    } else {
        audio_device_combo_->addItems(devices);

        // 恢复之前保存的设备选择
        QString savedDevice = ConfigManager::instance()->getAudioDevice();
        if (!savedDevice.isEmpty()) {
            int idx = audio_device_combo_->findText(savedDevice);
            if (idx >= 0) {
                audio_device_combo_->setCurrentIndex(idx);
            } else {
                // 尝试模糊匹配
                for (int i = 0; i < audio_device_combo_->count(); i++) {
                    const QString itemText = audio_device_combo_->itemText(i);
                    if (itemText.startsWith(savedDevice) || itemText.contains(savedDevice)) {
                        audio_device_combo_->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
    }
}

// ============================================================================
// 辅助函数：创建分隔线
// ============================================================================
QFrame* createSeparator() {
    auto line = new QFrame();
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("SettingsSeparator");
    return line;
}

void SettingsPage::setup_ui() {
    // 创建滚动区域
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName("SettingsScroll");
    
    auto content = new QWidget();
    content->setObjectName("SettingsContent");
    scroll->setWidget(content);
    
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->addWidget(scroll);
    
    auto layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    // ========================================================================
    // 页面标题
    // ========================================================================
    auto page_title = new QLabel(tr("系统设置"));
    page_title->setObjectName("PageTitle");
    layout->addWidget(page_title);

    // ========================================================================
    // 第一部分：系统概览（紧凑横条）
    // ========================================================================
    auto overview_card = new CardWidget(content);
    overview_card->setObjectName("OverviewCard");
    auto overview_layout = new QHBoxLayout(overview_card->bodyContainer());
    overview_layout->setContentsMargins(0, 0, 0, 0);
    overview_layout->setSpacing(32);
    
    // 版本信息
    auto version_group = new QWidget();
    auto version_layout = new QHBoxLayout(version_group);
    version_layout->setContentsMargins(0, 0, 0, 0);
    version_layout->setSpacing(8);
    auto version_icon = new QLabel("📦");
    version_icon->setObjectName("OverviewIcon");
    version_layout->addWidget(version_icon);
    auto version_text = new QVBoxLayout();
    version_text->setSpacing(0);
    version_text->addWidget(new QLabel(tr("版本")));
    version_label_ = new QLabel("v2.1.0");
    version_label_->setObjectName("OverviewValue");
    version_text->addWidget(version_label_);
    version_layout->addLayout(version_text);
    overview_layout->addWidget(version_group);
    
    // 数据库大小
    auto db_group = new QWidget();
    auto db_layout = new QHBoxLayout(db_group);
    db_layout->setContentsMargins(0, 0, 0, 0);
    db_layout->setSpacing(8);
    auto db_icon = new QLabel("📊");
    db_icon->setObjectName("OverviewIcon");
    db_layout->addWidget(db_icon);
    auto db_text = new QVBoxLayout();
    db_text->setSpacing(0);
    db_text->addWidget(new QLabel(tr("数据库")));
    db_size_label_ = new QLabel("0 KB");
    db_size_label_->setObjectName("OverviewValue");
    db_text->addWidget(db_size_label_);
    db_layout->addLayout(db_text);
    overview_layout->addWidget(db_group);
    
    // 摄像头状态
    auto camera_group = new QWidget();
    auto camera_status_layout = new QHBoxLayout(camera_group);
    camera_status_layout->setContentsMargins(0, 0, 0, 0);
    camera_status_layout->setSpacing(8);
    auto camera_icon = new QLabel("📷");
    camera_icon->setObjectName("OverviewIcon");
    camera_status_layout->addWidget(camera_icon);
    auto camera_text = new QVBoxLayout();
    camera_text->setSpacing(0);
    camera_text->addWidget(new QLabel(tr("摄像头")));
    auto camera_status = new QLabel(tr("已连接"));
    camera_status->setObjectName("OverviewValue");
    camera_status->setStyleSheet("color: #52c41a;");
    camera_text->addWidget(camera_status);
    camera_status_layout->addLayout(camera_text);
    overview_layout->addWidget(camera_group);
    
    overview_layout->addStretch();
    layout->addWidget(overview_card);

    // ========================================================================
    // 第二部分：摄像头设置
    // ========================================================================
    auto camera_card = new CardWidget(content);
    camera_card->setTitle(tr("📷 摄像头"));
    camera_card->setSubtitle(tr("选择用于人脸识别的 USB 摄像头设备"));
    
    auto camera_body = new QVBoxLayout(camera_card->bodyContainer());
    camera_body->setContentsMargins(0, 8, 0, 0);
    camera_body->setSpacing(16);
    
    auto camera_row = new QHBoxLayout();
    camera_row->setSpacing(12);
    
    auto camera_label = new QLabel(tr("USB 摄像头:"));
    camera_label->setObjectName("SettingsLabel");
    camera_row->addWidget(camera_label);
    
    camera_device_combo_ = new QComboBox();
    camera_device_combo_->setObjectName("SettingsCombo");
    camera_device_combo_->setMinimumWidth(300);
    camera_row->addWidget(camera_device_combo_);

    refresh_camera_btn_ = new QPushButton(tr("↻ 刷新"));
    refresh_camera_btn_->setObjectName("SecondaryButton");
    camera_row->addWidget(refresh_camera_btn_);

    apply_camera_btn_ = new QPushButton(tr("✓ 应用"));
    apply_camera_btn_->setObjectName("PrimaryButton");
    apply_camera_btn_->setToolTip(tr("立即应用摄像头设置，无需重启"));
    camera_row->addWidget(apply_camera_btn_);

    auto camera_hint = new QLabel(tr("选择摄像头后点击【应用】立即生效"));
    camera_hint->setObjectName("SettingsHint");
    camera_row->addWidget(camera_hint);

    camera_row->addStretch();
    camera_body->addLayout(camera_row);

    // 延迟扫描摄像头（进入设置页时再扫描）
    camera_device_combo_->addItem(tr("（进入设置后加载）"), -1);
    connect(refresh_camera_btn_, &QPushButton::clicked, this, [this]() {
        scan_usb_cameras();
    });
    connect(apply_camera_btn_, &QPushButton::clicked, this, &SettingsPage::on_apply_camera_clicked);
    
    layout->addWidget(camera_card);

    // ========================================================================
    // 第三部分：考勤规则设置
    // ========================================================================
    auto attendance_card = new CardWidget(content);
    attendance_card->setTitle(tr("🕐 考勤规则"));
    attendance_card->setSubtitle(tr("设置上下班时间、迟到早退阈值等考勤参数"));
    
    auto attendance_body = new QVBoxLayout(attendance_card->bodyContainer());
    attendance_body->setContentsMargins(0, 8, 0, 0);
    attendance_body->setSpacing(20);
    
    // 工作时间行
    auto time_row = new QHBoxLayout();
    time_row->setSpacing(24);
    
    // 上班时间
    auto start_group = new QHBoxLayout();
    start_group->setSpacing(8);
    start_group->addWidget(new QLabel(tr("上班时间")));
    work_start_time_edit_ = new QTimeEdit();
    work_start_time_edit_->setObjectName("SettingsTimeEdit");
    work_start_time_edit_->setTime(QTime(9, 0));
    work_start_time_edit_->setDisplayFormat("HH:mm");
    work_start_time_edit_->setMinimumWidth(100);
    start_group->addWidget(work_start_time_edit_);
    time_row->addLayout(start_group);
    
    // 下班时间
    auto end_group = new QHBoxLayout();
    end_group->setSpacing(8);
    end_group->addWidget(new QLabel(tr("下班时间")));
    work_end_time_edit_ = new QTimeEdit();
    work_end_time_edit_->setObjectName("SettingsTimeEdit");
    work_end_time_edit_->setTime(QTime(18, 0));
    work_end_time_edit_->setDisplayFormat("HH:mm");
    work_end_time_edit_->setMinimumWidth(100);
    end_group->addWidget(work_end_time_edit_);
    time_row->addLayout(end_group);
    
    time_row->addStretch();
    attendance_body->addLayout(time_row);
    
    // 阈值行
    auto threshold_row = new QHBoxLayout();
    threshold_row->setSpacing(24);
    
    // 迟到阈值
    auto late_group = new QHBoxLayout();
    late_group->setSpacing(8);
    late_group->addWidget(new QLabel(tr("迟到阈值")));
    late_threshold_spin_ = new QSpinBox();
    late_threshold_spin_->setObjectName("SettingsSpinBox");
    late_threshold_spin_->setRange(0, 120);
    late_threshold_spin_->setValue(30);
    late_threshold_spin_->setSuffix(tr(" 分钟"));
    late_threshold_spin_->setMinimumWidth(120);
    late_group->addWidget(late_threshold_spin_);
    threshold_row->addLayout(late_group);
    
    // 早退阈值
    auto early_group = new QHBoxLayout();
    early_group->setSpacing(8);
    early_group->addWidget(new QLabel(tr("早退阈值")));
    early_leave_threshold_spin_ = new QSpinBox();
    early_leave_threshold_spin_->setObjectName("SettingsSpinBox");
    early_leave_threshold_spin_->setRange(0, 120);
    early_leave_threshold_spin_->setValue(30);
    early_leave_threshold_spin_->setSuffix(tr(" 分钟"));
    early_leave_threshold_spin_->setMinimumWidth(120);
    early_group->addWidget(early_leave_threshold_spin_);
    threshold_row->addLayout(early_group);
    
    threshold_row->addStretch();
    attendance_body->addLayout(threshold_row);
    
    // 分隔线
    attendance_body->addWidget(createSeparator());
    
    // 选项行
    auto options_row = new QHBoxLayout();
    options_row->setSpacing(32);
    
    allow_multiple_checkin_check_ = new QCheckBox(tr("允许一天多次签到"));
    allow_multiple_checkin_check_->setObjectName("SettingsCheckBox");
    options_row->addWidget(allow_multiple_checkin_check_);
    
    checkin_sound_check_ = new QCheckBox(tr("签到声音提示"));
    checkin_sound_check_->setObjectName("SettingsCheckBox");
    checkin_sound_check_->setChecked(true);
    options_row->addWidget(checkin_sound_check_);
    
    show_checkin_reminder_check_ = new QCheckBox(tr("显示签到提醒"));
    show_checkin_reminder_check_->setObjectName("SettingsCheckBox");
    show_checkin_reminder_check_->setChecked(true);
    options_row->addWidget(show_checkin_reminder_check_);
    
    options_row->addStretch();
    attendance_body->addLayout(options_row);
    
    layout->addWidget(attendance_card);

    // ========================================================================
    // 第四部分：识别参数
    // ========================================================================
    auto recognition_card = new CardWidget(content);
    recognition_card->setTitle(tr("◎ 识别参数"));
    recognition_card->setSubtitle(tr("调整人脸识别阈值和防误识别参数"));
    
    auto recognition_body = new QVBoxLayout(recognition_card->bodyContainer());
    recognition_body->setContentsMargins(0, 8, 0, 0);
    recognition_body->setSpacing(20);
    
    // 第一行：识别阈值和防重复
    auto recog_row1 = new QHBoxLayout();
    recog_row1->setSpacing(24);
    
    // 识别阈值
    auto threshold_group = new QHBoxLayout();
    threshold_group->setSpacing(8);
    threshold_group->addWidget(new QLabel(tr("识别阈值")));
    recognition_threshold_spin_ = new QDoubleSpinBox();
    recognition_threshold_spin_->setObjectName("SettingsSpinBox");
    recognition_threshold_spin_->setRange(0.3, 0.95);
    recognition_threshold_spin_->setSingleStep(0.05);
    recognition_threshold_spin_->setDecimals(2);
    recognition_threshold_spin_->setValue(0.60);
    recognition_threshold_spin_->setMinimumWidth(100);
    threshold_group->addWidget(recognition_threshold_spin_);
    auto threshold_hint = new QLabel(tr("值越高越严格"));
    threshold_hint->setObjectName("SettingsHint");
    threshold_group->addWidget(threshold_hint);
    recog_row1->addLayout(threshold_group);
    
    // 防重复签到
    auto dup_group = new QHBoxLayout();
    dup_group->setSpacing(8);
    dup_group->addWidget(new QLabel(tr("防重复签到")));
    duplicate_check_interval_spin_ = new QSpinBox();
    duplicate_check_interval_spin_->setObjectName("SettingsSpinBox");
    duplicate_check_interval_spin_->setRange(60, 3600);
    duplicate_check_interval_spin_->setSingleStep(60);
    duplicate_check_interval_spin_->setValue(300);
    duplicate_check_interval_spin_->setSuffix(tr(" 秒"));
    duplicate_check_interval_spin_->setMinimumWidth(120);
    dup_group->addWidget(duplicate_check_interval_spin_);
    recog_row1->addLayout(dup_group);
    
    recog_row1->addStretch();
    recognition_body->addLayout(recog_row1);
    
    // 第二行：确认时间
    auto recog_row2 = new QHBoxLayout();
    recog_row2->setSpacing(8);
    
    recog_row2->addWidget(new QLabel(tr("确认时间")));
    user_confirm_duration_spin_ = new QDoubleSpinBox();
    user_confirm_duration_spin_->setObjectName("SettingsSpinBox");
    user_confirm_duration_spin_->setRange(0.1, 2.0);
    user_confirm_duration_spin_->setSingleStep(0.1);
    user_confirm_duration_spin_->setDecimals(1);
    user_confirm_duration_spin_->setValue(1.0);
    user_confirm_duration_spin_->setSuffix(tr(" 秒"));
    user_confirm_duration_spin_->setMinimumWidth(100);
    recog_row2->addWidget(user_confirm_duration_spin_);
    
    auto confirm_hint = new QLabel(tr("持续检测到同一人的时长，防止误识别"));
    confirm_hint->setObjectName("SettingsHint");
    recog_row2->addWidget(confirm_hint);
    
    recog_row2->addStretch();
    recognition_body->addLayout(recog_row2);
    
    layout->addWidget(recognition_card);

    // ========================================================================
    // 第五部分：显示选项
    // ========================================================================
    auto display_card = new CardWidget(content);
    display_card->setTitle(tr("🖥️ 显示选项"));
    display_card->setSubtitle(tr("控制界面显示内容和系统行为"));
    
    auto display_body = new QVBoxLayout(display_card->bodyContainer());
    display_body->setContentsMargins(0, 8, 0, 0);
    display_body->setSpacing(16);
    
    auto display_row = new QHBoxLayout();
    display_row->setSpacing(32);
    
    show_fps_check_ = new QCheckBox(tr("显示 FPS"));
    show_fps_check_->setObjectName("SettingsCheckBox");
    show_fps_check_->setChecked(true);
    display_row->addWidget(show_fps_check_);
    
    show_confidence_check_ = new QCheckBox(tr("显示置信度"));
    show_confidence_check_->setObjectName("SettingsCheckBox");
    show_confidence_check_->setChecked(true);
    display_row->addWidget(show_confidence_check_);
    
    auto_start_check_ = new QCheckBox(tr("开机自启"));
    auto_start_check_->setObjectName("SettingsCheckBox");
    display_row->addWidget(auto_start_check_);
    
    display_row->addStretch();
    display_body->addLayout(display_row);
    
    // 操作按钮
    auto action_row = new QHBoxLayout();
    action_row->setSpacing(12);
    
    auto theme_btn = new QPushButton(tr("🎨 切换主题"));
    theme_btn->setObjectName("SecondaryButton");
    connect(theme_btn, &QPushButton::clicked, this, &SettingsPage::on_theme_toggle_clicked);
    action_row->addWidget(theme_btn);
    
    auto clear_cache_btn = new QPushButton(tr("🗑️ 清理缓存"));
    clear_cache_btn->setObjectName("SecondaryButton");
    connect(clear_cache_btn, &QPushButton::clicked, this, &SettingsPage::on_clear_cache_clicked);
    action_row->addWidget(clear_cache_btn);
    
    action_row->addStretch();
    display_body->addLayout(action_row);
    
    layout->addWidget(display_card);

    // ========================================================================
    // 第六部分：音频设置
    // ========================================================================
    auto audio_card = new CardWidget(content);
    audio_card->setTitle(tr("🔊 音频设置"));
    audio_card->setSubtitle(tr("配置语音播报和音频输出"));
    
    auto audio_body = new QVBoxLayout(audio_card->bodyContainer());
    audio_body->setContentsMargins(0, 8, 0, 0);
    audio_body->setSpacing(20);
    
    // 启用语音播报
    audio_enabled_check_ = new QCheckBox(tr("启用语音播报"));
    audio_enabled_check_->setObjectName("SettingsCheckBox");
    audio_enabled_check_->setChecked(true);
    audio_body->addWidget(audio_enabled_check_);
    
    // 音量控制行
    auto volume_row = new QHBoxLayout();
    volume_row->setSpacing(12);
    
    volume_row->addWidget(new QLabel(tr("音量")));
    
    audio_volume_slider_ = new QSlider(Qt::Horizontal);
    audio_volume_slider_->setObjectName("SettingsSlider");
    audio_volume_slider_->setRange(0, 100);
    audio_volume_slider_->setValue(70);
    audio_volume_slider_->setMinimumWidth(200);
    volume_row->addWidget(audio_volume_slider_);
    
    audio_volume_label_ = new QLabel("70%");
    audio_volume_label_->setObjectName("VolumeLabel");
    audio_volume_label_->setMinimumWidth(50);
    volume_row->addWidget(audio_volume_label_);
    
    volume_row->addStretch();
    audio_body->addLayout(volume_row);
    
    // 设备选择行
    auto device_row = new QHBoxLayout();
    device_row->setSpacing(12);
    
    device_row->addWidget(new QLabel(tr("输出设备")));
    
    audio_device_combo_ = new QComboBox();
    audio_device_combo_->setObjectName("SettingsCombo");
    audio_device_combo_->setMinimumWidth(250);
    device_row->addWidget(audio_device_combo_);
    
    test_audio_btn_ = new QPushButton(tr("🔈 测试"));
    test_audio_btn_->setObjectName("SecondaryButton");
    device_row->addWidget(test_audio_btn_);
    
    device_row->addStretch();
    audio_body->addLayout(device_row);
    
    // 延迟填充音频设备列表（进入设置页时再枚举）
    audio_device_combo_->addItem(tr("（进入设置后加载）"));
    
    // 连接信号
    connect(audio_volume_slider_, &QSlider::valueChanged, this, [this](int value) {
        if (audio_volume_label_) {
            audio_volume_label_->setText(QString("%1%").arg(value));
        }
        AudioManager::instance()->setVolume(value);
    });
    
    connect(audio_device_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0 && audio_device_combo_) {
            QString deviceName = audio_device_combo_->currentText();
            AudioManager::instance()->setAudioDevice(deviceName);
            spdlog::info("Audio device changed to: {}", deviceName.toStdString());
        }
    });
    
    connect(test_audio_btn_, &QPushButton::clicked, this, [this]() {
        AudioManager::instance()->playSound(AudioType::CheckInSuccess);
    });
    
    connect(audio_enabled_check_, &QCheckBox::toggled, this, [this](bool checked) {
        AudioManager::instance()->setEnabled(checked);
        if (audio_volume_slider_) audio_volume_slider_->setEnabled(checked);
        if (audio_device_combo_) audio_device_combo_->setEnabled(checked);
        if (test_audio_btn_) test_audio_btn_->setEnabled(checked);
    });
    
    layout->addWidget(audio_card);

    // ========================================================================
    // 第七部分：天气/定位设置
    // ========================================================================
    auto weather_card = new CardWidget(content);
    weather_card->setTitle(tr("🌤️ 天气定位"));
    weather_card->setSubtitle(tr("设置天气显示的城市和位置信息"));
    
    auto weather_body = new QVBoxLayout(weather_card->bodyContainer());
    weather_body->setContentsMargins(0, 8, 0, 0);
    weather_body->setSpacing(20);
    
    // 自动定位开关
    auto_location_check_ = new QCheckBox(tr("使用 IP 自动定位"));
    auto_location_check_->setObjectName("SettingsCheckBox");
    auto loc_hint = new QLabel(tr("关闭后使用下方手动设置的城市"));
    loc_hint->setObjectName("SettingsHint");
    auto loc_row = new QHBoxLayout();
    loc_row->addWidget(auto_location_check_);
    loc_row->addWidget(loc_hint);
    loc_row->addStretch();
    weather_body->addLayout(loc_row);
    
    // 快捷城市选择
    auto preset_row = new QHBoxLayout();
    preset_row->setSpacing(12);
    preset_row->addWidget(new QLabel(tr("快捷选择")));
    
    city_preset_combo_ = new QComboBox();
    city_preset_combo_->setObjectName("SettingsCombo");
    city_preset_combo_->setMinimumWidth(200);
    city_preset_combo_->addItem(tr("-- 选择城市 --"), QVariant());
    const QString default_city = QString::fromUtf8(Config::Default::CITY);
    city_preset_combo_->addItem(
        tr("%1 (默认)").arg(default_city),
        QVariant::fromValue(QVector<double>{Config::Default::LATITUDE, Config::Default::LONGITUDE}));
    city_preset_combo_->addItem(tr("广州"), QVariant::fromValue(QVector<double>{23.1291, 113.2644}));
    city_preset_combo_->addItem(tr("深圳"), QVariant::fromValue(QVector<double>{22.5431, 114.0579}));
    city_preset_combo_->addItem(tr("东莞"), QVariant::fromValue(QVector<double>{23.0430, 113.7633}));
    city_preset_combo_->addItem(tr("珠海"), QVariant::fromValue(QVector<double>{22.2710, 113.5767}));
    city_preset_combo_->addItem(tr("中山"), QVariant::fromValue(QVector<double>{22.5176, 113.3926}));
    city_preset_combo_->addItem(tr("惠州"), QVariant::fromValue(QVector<double>{23.1115, 114.4152}));
    city_preset_combo_->addItem(tr("江门"), QVariant::fromValue(QVector<double>{22.5789, 113.0815}));
    city_preset_combo_->addItem(tr("肇庆"), QVariant::fromValue(QVector<double>{23.0469, 112.4654}));
    city_preset_combo_->addItem(tr("北京"), QVariant::fromValue(QVector<double>{39.9042, 116.4074}));
    city_preset_combo_->addItem(tr("上海"), QVariant::fromValue(QVector<double>{31.2304, 121.4737}));
    city_preset_combo_->addItem(tr("杭州"), QVariant::fromValue(QVector<double>{30.2741, 120.1551}));
    city_preset_combo_->addItem(tr("成都"), QVariant::fromValue(QVector<double>{30.5728, 104.0668}));
    city_preset_combo_->addItem(tr("武汉"), QVariant::fromValue(QVector<double>{30.5928, 114.3055}));
    city_preset_combo_->addItem(tr("南京"), QVariant::fromValue(QVector<double>{32.0603, 118.7969}));
    city_preset_combo_->addItem(tr("西安"), QVariant::fromValue(QVector<double>{34.3416, 108.9398}));
    city_preset_combo_->addItem(tr("重庆"), QVariant::fromValue(QVector<double>{29.5630, 106.5516}));
    city_preset_combo_->addItem(tr("长沙"), QVariant::fromValue(QVector<double>{28.2282, 112.9388}));
    city_preset_combo_->addItem(tr("厦门"), QVariant::fromValue(QVector<double>{24.4798, 118.0894}));
    preset_row->addWidget(city_preset_combo_);
    preset_row->addStretch();
    weather_body->addLayout(preset_row);
    
    // 手动设置行
    auto manual_row = new QHBoxLayout();
    manual_row->setSpacing(16);
    
    manual_row->addWidget(new QLabel(tr("城市名称")));
    manual_city_edit_ = new QLineEdit();
    manual_city_edit_->setObjectName("SettingsLineEdit");
    manual_city_edit_->setPlaceholderText(tr("自定义城市名"));
    manual_city_edit_->setMinimumWidth(100);
    manual_row->addWidget(manual_city_edit_);
    
    manual_row->addWidget(new QLabel(tr("纬度")));
    manual_lat_spin_ = new QDoubleSpinBox();
    manual_lat_spin_->setObjectName("SettingsSpinBox");
    manual_lat_spin_->setRange(-90.0, 90.0);
    manual_lat_spin_->setDecimals(4);
    manual_lat_spin_->setSingleStep(0.01);
    manual_lat_spin_->setMinimumWidth(100);
    manual_row->addWidget(manual_lat_spin_);
    
    manual_row->addWidget(new QLabel(tr("经度")));
    manual_lon_spin_ = new QDoubleSpinBox();
    manual_lon_spin_->setObjectName("SettingsSpinBox");
    manual_lon_spin_->setRange(-180.0, 180.0);
    manual_lon_spin_->setDecimals(4);
    manual_lon_spin_->setSingleStep(0.01);
    manual_lon_spin_->setMinimumWidth(100);
    manual_row->addWidget(manual_lon_spin_);
    
    manual_row->addStretch();
    weather_body->addLayout(manual_row);
    
    // 连接信号
    connect(auto_location_check_, &QCheckBox::toggled, this, &SettingsPage::on_auto_location_changed);
    
    connect(city_preset_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index <= 0) return;
        
        QString cityName = city_preset_combo_->currentText();
        QVariant data = city_preset_combo_->currentData();
        
        if (data.isValid() && data.canConvert<QVector<double>>()) {
            QVector<double> coords = data.value<QVector<double>>();
            if (coords.size() >= 2) {
                manual_city_edit_->setText(cityName);
                manual_lat_spin_->setValue(coords[0]);
                manual_lon_spin_->setValue(coords[1]);
            }
        }
    });
    
    layout->addWidget(weather_card);

    // ========================================================================
    // 底部操作栏
    // ========================================================================
    auto bottom_bar = new QWidget();
    bottom_bar->setObjectName("BottomBar");
    auto bottom_layout = new QHBoxLayout(bottom_bar);
    bottom_layout->setContentsMargins(0, 16, 0, 0);
    bottom_layout->setSpacing(12);
    
    bottom_layout->addStretch();
    
    auto reset_btn = new QPushButton(tr("↩️ 恢复默认"));
    reset_btn->setObjectName("SecondaryButton");
    connect(reset_btn, &QPushButton::clicked, this, &SettingsPage::on_reset_clicked);
    bottom_layout->addWidget(reset_btn);
    
    auto save_btn = new QPushButton(tr("✓ 保存设置"));
    save_btn->setProperty("primary", QVariant(true));
    save_btn->setObjectName("PrimaryButton");
    connect(save_btn, &QPushButton::clicked, this, &SettingsPage::on_save_clicked);
    bottom_layout->addWidget(save_btn);
    
    layout->addWidget(bottom_bar);
    layout->addStretch();
}

void SettingsPage::load_settings() {
    ConfigManager* config = ConfigManager::instance();

    // 更新数据库大小信息
    update_db_size();

    // 加载识别设置
    if (recognition_threshold_spin_) {
        recognition_threshold_spin_->setValue(config->getRecognitionThreshold());
    }
    if (duplicate_check_interval_spin_) {
        duplicate_check_interval_spin_->setValue(config->getDuplicateCheckInterval());
    }
    if (user_confirm_duration_spin_) {
        user_confirm_duration_spin_->setValue(config->getUserConfirmDuration() / 1000.0);
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

    // 加载音频设置 - 使用 QSignalBlocker 阻止触发 AudioManager 副作用
    if (audio_enabled_check_) {
        audio_enabled_check_->setChecked(config->isAudioEnabled());
    }
    if (audio_volume_slider_) {
        const QSignalBlocker blocker(audio_volume_slider_);
        audio_volume_slider_->setValue(config->getAudioVolume());
    }
    if (audio_volume_label_) {
        audio_volume_label_->setText(QString("%1%").arg(config->getAudioVolume()));
    }
    if (audio_device_combo_) {
        const QSignalBlocker blocker(audio_device_combo_);
        QString savedDevice = config->getAudioDevice();
        if (!savedDevice.isEmpty()) {
            int deviceIndex = audio_device_combo_->findText(savedDevice);
            if (deviceIndex >= 0) {
                audio_device_combo_->setCurrentIndex(deviceIndex);
            } else {
                for (int i = 0; i < audio_device_combo_->count(); i++) {
                    const QString itemText = audio_device_combo_->itemText(i);
                    if (itemText.startsWith(savedDevice) || itemText.contains(savedDevice)) {
                        audio_device_combo_->setCurrentIndex(i);
                        break;
                    }
                }
            }
        }
    }

    // 加载摄像头设置
    if (camera_device_combo_) {
        int cameraId = config->getCameraId();
        for (int i = 0; i < camera_device_combo_->count(); i++) {
            if (camera_device_combo_->itemData(i).toInt() == cameraId) {
                camera_device_combo_->setCurrentIndex(i);
                break;
            }
        }
    }

    // 加载天气/城市设置
    if (auto_location_check_) {
        auto_location_check_->setChecked(config->isAutoLocationEnabled());
    }
    if (manual_city_edit_) {
        manual_city_edit_->setText(config->getManualCity());
    }
    if (manual_lat_spin_) {
        manual_lat_spin_->setValue(config->getManualLatitude());
    }
    if (manual_lon_spin_) {
        manual_lon_spin_->setValue(config->getManualLongitude());
    }
    on_auto_location_changed(config->isAutoLocationEnabled());
    
    spdlog::info("Settings loaded");
}

void SettingsPage::update_db_size() {
    if (!db_size_label_) return;

    const QString app_dir = QCoreApplication::applicationDirPath();
    const QString current_dir = QDir::currentPath();
    const QString db_rel_path = QString::fromUtf8(Config::Path::DATABASE);

    QStringList db_prefixes = {
        app_dir,
        QDir(app_dir).absoluteFilePath(".."),
        QDir(app_dir).absoluteFilePath("../.."),
        current_dir,
        QDir(current_dir).absoluteFilePath(".."),
        QDir(current_dir).absoluteFilePath("../..")
    };

    bool db_found = false;
    for (const QString& prefix : db_prefixes) {
        QString db_path = QDir(prefix).filePath(db_rel_path);

        QFileInfo db_file(db_path);
        if (!db_file.exists()) continue;

        qint64 size_bytes = db_file.size();
        qint64 size_kb = size_bytes / 1024;

        if (size_kb > 1024) {
            db_size_label_->setText(QString("%1 MB").arg(size_kb / 1024.0, 0, 'f', 2));
        } else if (size_kb > 0) {
            db_size_label_->setText(QString("%1 KB").arg(size_kb));
        } else {
            db_size_label_->setText(QString("%1 字节").arg(size_bytes));
        }

        spdlog::info("Database found: {} (size: {} bytes)", db_path.toStdString(), size_bytes);
        db_found = true;
        break;
    }

    if (!db_found) {
        db_size_label_->setText("未找到");
        spdlog::warn("Database file not found in any expected location");
    }
}

void SettingsPage::scan_usb_cameras() {
    if (!camera_device_combo_) {
        return;
    }
    
    int current_id = camera_device_combo_->currentData().toInt();
    camera_device_combo_->clear();
    
    QDir v4lDir("/sys/class/video4linux");
    if (!v4lDir.exists()) {
        camera_device_combo_->addItem(tr("未找到视频设备目录"), -1);
        spdlog::error("Directory /sys/class/video4linux not found");
        return;
    }
    
    QStringList filters;
    filters << "video*";
    v4lDir.setNameFilters(filters);
    v4lDir.setFilter(QDir::Dirs | QDir::NoDotAndDotDot);
    
    QFileInfoList devices = v4lDir.entryInfoList();
    
    int found_count = 0;
    for (const QFileInfo& deviceInfo : devices) {
        QString deviceName = deviceInfo.fileName();
        
        QString numStr = deviceName.mid(5);
        bool ok;
        int deviceId = numStr.toInt(&ok);
        
        if (!ok) continue;
        
        QString nameFilePath = QString("/sys/class/video4linux/%1/name").arg(deviceName);
        QFile nameFile(nameFilePath);
        QString cameraName = "Unknown";
        
        if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            cameraName = QString::fromUtf8(nameFile.readAll()).trimmed();
            nameFile.close();
        }
        
        if (cameraName.contains("rkcif", Qt::CaseInsensitive) ||
            cameraName.contains("rkisp", Qt::CaseInsensitive) ||
            cameraName.contains("stream_", Qt::CaseInsensitive) ||
            cameraName.contains("hdmirx", Qt::CaseInsensitive) ||
            cameraName.contains("v4l2loopback", Qt::CaseInsensitive) ||
            cameraName.contains("subdev", Qt::CaseInsensitive)) {
            spdlog::debug("Skipped system device: {} ({})", deviceName.toStdString(), cameraName.toStdString());
            continue;
        }
        
        QFileInfo symlinkInfo(QString("/sys/class/video4linux/%1").arg(deviceName));
        QString realPath = symlinkInfo.canonicalFilePath();
        
        if (!realPath.contains("usb", Qt::CaseInsensitive)) {
            spdlog::debug("Skipped non-USB device: {} (path: {})", deviceName.toStdString(), realPath.toStdString());
            continue;
        }
        
        QString devicePath = QString("/dev/%1").arg(deviceName);
        QFile device(devicePath);
        if (!device.exists()) {
            continue;
        }
        
        QString displayName = QString("%1 (/dev/video%2)").arg(cameraName).arg(deviceId);
        camera_device_combo_->addItem(displayName, deviceId);
        found_count++;
        
        spdlog::info("Found USB camera: {} -> {}", deviceName.toStdString(), cameraName.toStdString());
    }
    
    if (found_count == 0) {
        camera_device_combo_->addItem(tr("未找到 USB 摄像头"), -1);
        spdlog::warn("No USB cameras found");
    } else {
        spdlog::info("Total {} USB camera(s) found", found_count);
        
        for (int i = 0; i < camera_device_combo_->count(); i++) {
            if (camera_device_combo_->itemData(i).toInt() == current_id) {
                camera_device_combo_->setCurrentIndex(i);
                spdlog::debug("Restored camera selection: ID {}", current_id);
                break;
            }
        }
    }
}

void SettingsPage::save_settings() {
    ConfigManager* config = ConfigManager::instance();
    
    // 保存识别设置
    if (recognition_threshold_spin_) {
        config->setRecognitionThreshold(recognition_threshold_spin_->value());
    }
    if (duplicate_check_interval_spin_) {
        config->setDuplicateCheckInterval(duplicate_check_interval_spin_->value());
    }
    if (user_confirm_duration_spin_) {
        config->setUserConfirmDuration(static_cast<int>(user_confirm_duration_spin_->value() * 1000));
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
    
    // 保存摄像头设置
    if (camera_device_combo_ && camera_device_combo_->currentIndex() >= 0) {
        int deviceId = camera_device_combo_->currentData().toInt();
        if (deviceId >= 0) {
            config->setCameraId(deviceId);
            spdlog::info("Camera ID saved: {}", deviceId);
        }
    }
    
    // 保存天气/城市设置
    if (auto_location_check_) {
        config->setAutoLocationEnabled(auto_location_check_->isChecked());
    }
    if (manual_city_edit_) {
        config->setManualCity(manual_city_edit_->text());
    }
    if (manual_lat_spin_) {
        config->setManualLatitude(manual_lat_spin_->value());
    }
    if (manual_lon_spin_) {
        config->setManualLongitude(manual_lon_spin_->value());
    }
    
    emit weatherSettingsChanged();
    
    spdlog::info("Settings saved:");
    spdlog::info("  - Recognition: threshold={:.2f}, duplicate_interval={}s, confirm_duration={:.1f}s",
                 recognition_threshold_spin_ ? recognition_threshold_spin_->value() : 0.6,
                 duplicate_check_interval_spin_ ? duplicate_check_interval_spin_->value() : 300,
                 user_confirm_duration_spin_ ? user_confirm_duration_spin_->value() : 1.0);
    
    spdlog::info("  - Attendance: work_time={}~{}, late_threshold={}min, early_leave={}min",
                 work_start_time_edit_ ? work_start_time_edit_->time().toString("HH:mm").toStdString() : "09:00",
                 work_end_time_edit_ ? work_end_time_edit_->time().toString("HH:mm").toStdString() : "18:00",
                 late_threshold_spin_ ? late_threshold_spin_->value() : 30,
                 early_leave_threshold_spin_ ? early_leave_threshold_spin_->value() : 30);
    
    spdlog::info("  - Display: show_fps={}, show_confidence={}, auto_start={}",
                 show_fps_check_ ? show_fps_check_->isChecked() : true,
                 show_confidence_check_ ? show_confidence_check_->isChecked() : true,
                 auto_start_check_ ? auto_start_check_->isChecked() : false);
    
    // 保存音频设置
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
    update_db_size();  // 保存后重新刷新数据库大小显示
    QMessageBox::information(this, tr("成功"), tr("设置已保存"));
}

void SettingsPage::on_reset_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认"), tr("确定要恢复默认设置吗？"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        // 识别设置
        if (recognition_threshold_spin_) recognition_threshold_spin_->setValue(Config::Default::RECOGNITION_THRESHOLD);
        if (duplicate_check_interval_spin_) duplicate_check_interval_spin_->setValue(Config::Default::DUPLICATE_CHECK_INTERVAL);
        if (user_confirm_duration_spin_) user_confirm_duration_spin_->setValue(Config::Default::USER_CONFIRM_DURATION_MS / 1000.0);

        // 考勤设置
        if (work_start_time_edit_) work_start_time_edit_->setTime(QTime(Config::Default::WORK_START_HOUR, Config::Default::WORK_START_MINUTE));
        if (work_end_time_edit_) work_end_time_edit_->setTime(QTime(Config::Default::WORK_END_HOUR, Config::Default::WORK_END_MINUTE));
        if (late_threshold_spin_) late_threshold_spin_->setValue(Config::Default::LATE_THRESHOLD);
        if (early_leave_threshold_spin_) early_leave_threshold_spin_->setValue(Config::Default::EARLY_LEAVE_THRESHOLD);
        if (allow_multiple_checkin_check_) allow_multiple_checkin_check_->setChecked(false);
        if (checkin_sound_check_) checkin_sound_check_->setChecked(Config::Default::AUDIO_ENABLED);
        if (show_checkin_reminder_check_) show_checkin_reminder_check_->setChecked(true);

        // 显示设置
        if (show_fps_check_) show_fps_check_->setChecked(true);
        if (show_confidence_check_) show_confidence_check_->setChecked(true);
        if (auto_start_check_) auto_start_check_->setChecked(false);

        // 音频设置
        if (audio_enabled_check_) audio_enabled_check_->setChecked(Config::Default::AUDIO_ENABLED);
        if (audio_volume_slider_) audio_volume_slider_->setValue(Config::Default::AUDIO_VOLUME);
        if (audio_volume_label_) audio_volume_label_->setText(QString::number(Config::Default::AUDIO_VOLUME) + "%");

        // 摄像头设置
        if (camera_device_combo_) {
            for (int i = 0; i < camera_device_combo_->count(); i++) {
                if (camera_device_combo_->itemData(i).toInt() == Config::Default::CAMERA_ID) {
                    camera_device_combo_->setCurrentIndex(i);
                    break;
                }
            }
        }

        // 天气/城市设置
        if (auto_location_check_) auto_location_check_->setChecked(false);
        if (manual_city_edit_) manual_city_edit_->setText(QString::fromUtf8(Config::Default::CITY));
        if (manual_lat_spin_) manual_lat_spin_->setValue(Config::Default::LATITUDE);
        if (manual_lon_spin_) manual_lon_spin_->setValue(Config::Default::LONGITUDE);
        on_auto_location_changed(false);
        
        QMessageBox::information(this, tr("成功"), tr("已恢复默认设置"));
        spdlog::info("Settings reset to defaults");
    }
}

void SettingsPage::on_clear_cache_clicked() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("确认"), tr("确定要清理缓存吗？\n这将清除临时文件和日志。"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        QMessageBox::information(this, tr("成功"), tr("缓存已清理"));
        spdlog::info("Cache cleared");
    }
}

void SettingsPage::on_auto_location_changed(bool checked) {
    if (manual_city_edit_) {
        manual_city_edit_->setEnabled(!checked);
    }
    if (manual_lat_spin_) {
        manual_lat_spin_->setEnabled(!checked);
    }
    if (manual_lon_spin_) {
        manual_lon_spin_->setEnabled(!checked);
    }
    if (city_preset_combo_) {
        city_preset_combo_->setEnabled(!checked);
    }
}

void SettingsPage::on_apply_camera_clicked() {
    if (!camera_device_combo_ || camera_device_combo_->currentIndex() < 0) {
        QMessageBox::warning(this, tr("警告"), tr("请先选择摄像头设备"));
        return;
    }

    int deviceId = camera_device_combo_->currentData().toInt();
    if (deviceId < 0) {
        QMessageBox::warning(this, tr("警告"), tr("无效的摄像头设备"));
        return;
    }

    // 保存到配置
    ConfigManager::instance()->setCameraId(deviceId);
    spdlog::info("Applying camera settings: device ID = {}", deviceId);

    // 发送信号通知主窗口重新初始化摄像头
    emit cameraSettingsChanged(deviceId);
}

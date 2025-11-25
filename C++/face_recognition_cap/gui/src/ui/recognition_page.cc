/**
 * @file recognition_page.cc
 * @brief 实时识别页面 - 现代化设计
 * @author CL
 * @date 2025-11-25
 */

#include "ui/recognition_page.h"

#include "gui/video_display_widget.h"
#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QSizePolicy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <spdlog/spdlog.h>

RecognitionPage::RecognitionPage(QWidget* parent)
    : QWidget(parent)
    , video_widget_(nullptr)
    , attendance_table_(nullptr)
    , status_label_(nullptr)
    , fps_label_(nullptr)
    , recognition_label_(nullptr)
    , attendance_status_label_(nullptr)
    , user_name_label_(nullptr)
    , user_id_label_(nullptr)
    , user_dept_label_(nullptr)
    , user_similarity_label_(nullptr)
    , check_type_label_(nullptr)
    , clock_label_(nullptr)
    , date_label_(nullptr)
    , face_count_label_(nullptr)
    , detection_status_label_(nullptr)
    , detection_progress_bar_(nullptr)
    , weather_label_(nullptr)
    , temp_label_(nullptr)
    , checkin_count_label_(nullptr)
    , checkout_count_label_(nullptr)
    , late_count_label_(nullptr)
    , check_mode_label_(nullptr)
    , network_manager_(nullptr) {
    
    setObjectName("RecognitionPage");
    setAttribute(Qt::WA_StyledBackground, true);
    
    // 不设置内联样式，让全局 QSS 控制背景色

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    // 左侧：视频区域
    auto video_card = createVideoCard();
    
    // 右侧：控制面板 + 签到列表
    auto right_column = new QVBoxLayout();
    right_column->setSpacing(20);

    auto status_card = createStatusCard();
    auto attendance_card = createAttendanceCard();

    layout->addWidget(video_card, 3);  // 视频占更大比例
    layout->addLayout(right_column, 2);
    
    right_column->addWidget(status_card);
    right_column->addWidget(attendance_card, 1);
}

VideoDisplayWidget* RecognitionPage::videoWidget() const {
    return video_widget_;
}

ModernTableView* RecognitionPage::attendanceTable() const {
    return attendance_table_;
}

QLabel* RecognitionPage::statusLabel() const {
    return status_label_;
}

QLabel* RecognitionPage::fpsLabel() const {
    return fps_label_;
}

QLabel* RecognitionPage::recognitionLabel() const {
    return recognition_label_;
}

QLabel* RecognitionPage::attendanceStatusLabel() const {
    return attendance_status_label_;
}

QLabel* RecognitionPage::userNameLabel() const {
    return user_name_label_;
}

QLabel* RecognitionPage::userIdLabel() const {
    return user_id_label_;
}

QLabel* RecognitionPage::userDeptLabel() const {
    return user_dept_label_;
}

QLabel* RecognitionPage::userSimilarityLabel() const {
    return user_similarity_label_;
}

QLabel* RecognitionPage::checkTypeLabel() const {
    return check_type_label_;
}

QLabel* RecognitionPage::clockLabel() const {
    return clock_label_;
}

QLabel* RecognitionPage::dateLabel() const {
    return date_label_;
}

QLabel* RecognitionPage::faceCountLabel() const {
    return face_count_label_;
}

QLabel* RecognitionPage::detectionStatusLabel() const {
    return detection_status_label_;
}

QProgressBar* RecognitionPage::detectionProgressBar() const {
    return detection_progress_bar_;
}

void RecognitionPage::updateClock(const QString& time) {
    if (clock_label_) {
        clock_label_->setText(time);
    }
}

void RecognitionPage::updateDate(const QString& date) {
    if (date_label_) {
        date_label_->setText(date);
    }
}

void RecognitionPage::updateFaceCount(int count) {
    if (face_count_label_) {
        // 使用固定格式文字，避免长度变化导致布局抖动
        face_count_label_->setText(tr("人脸: %1").arg(count));
        face_count_label_->setProperty("status", count > 0 ? "active" : "inactive");
        face_count_label_->style()->unpolish(face_count_label_);
        face_count_label_->style()->polish(face_count_label_);
    }
}

void RecognitionPage::updateDetectionStatus(const QString& status, int progress) {
    if (detection_status_label_) {
        detection_status_label_->setText(status);
    }
    if (detection_progress_bar_) {
        // 始终显示进度条，防止布局抖动
        if (progress < 0) {
            detection_progress_bar_->setValue(0);  // 重置为0，但不隐藏
        } else {
            detection_progress_bar_->setValue(progress);
        }
    }
}

void RecognitionPage::updateWeather(const QString& weather, const QString& temp) {
    if (weather_label_) {
        weather_label_->setText(weather);
    }
    if (temp_label_) {
        temp_label_->setText(temp);
    }
}

void RecognitionPage::updateAttendanceStats(int checkin_count, int checkout_count, int late_count, int early_leave_count) {
    if (checkin_count_label_) {
        checkin_count_label_->setText(QString::number(checkin_count));
    }
    if (checkout_count_label_) {
        checkout_count_label_->setText(QString::number(checkout_count));
    }
    if (late_count_label_) {
        // 迟到 + 早退
        int abnormal = late_count + early_leave_count;
        late_count_label_->setText(QString::number(abnormal));
        // 有异常时变红
        late_count_label_->setProperty("hasAlert", abnormal > 0);
        late_count_label_->style()->unpolish(late_count_label_);
        late_count_label_->style()->polish(late_count_label_);
    }
}

void RecognitionPage::updateCheckMode(bool is_checkout_mode) {
    if (check_mode_label_) {
        if (is_checkout_mode) {
            check_mode_label_->setText(tr("签退模式"));
            check_mode_label_->setStyleSheet("color: #fa8c16; font-weight: 600; background: rgba(250,140,22,0.1); padding: 4px 12px; border-radius: 12px;");
        } else {
            check_mode_label_->setText(tr("签到模式"));
            check_mode_label_->setStyleSheet("color: #52c41a; font-weight: 600; background: rgba(82,196,26,0.1); padding: 4px 12px; border-radius: 12px;");
        }
    }
}

void RecognitionPage::refreshWeather() {
    if (!network_manager_) {
        network_manager_ = new QNetworkAccessManager(this);
        connect(network_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onWeatherReplyFinished);
    }
    
    // 使用 Open-Meteo API（完全免费，无需 API Key）
    // 佛山坐标：纬度 23.0215，经度 113.1214
    QUrl url("https://api.open-meteo.com/v1/forecast?latitude=23.0215&longitude=113.1214&current_weather=true");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    network_manager_->get(request);
    
    spdlog::debug("Weather request sent to Open-Meteo for Foshan");
}

void RecognitionPage::onWeatherReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current_weather"].toObject();
            
            double temp = current["temperature"].toDouble();
            int weatherCode = current["weathercode"].toInt();
            
            // 将天气代码转换为中文描述
            QString weatherDesc = weatherCodeToString(weatherCode);
            QString tempStr = QString("%1°C").arg(temp, 0, 'f', 0);
            
            updateWeather(weatherDesc, tempStr);
            spdlog::info("Weather updated: {} {}", weatherDesc.toStdString(), tempStr.toStdString());
        } else {
            spdlog::warn("Weather JSON parse failed");
            updateWeather(tr("--"), tr("--"));
        }
    } else {
        spdlog::warn("Weather request failed: {}", reply->errorString().toStdString());
        updateWeather(tr("--"), tr("--"));
    }
    reply->deleteLater();
}

QString RecognitionPage::weatherCodeToString(int code) {
    // Open-Meteo WMO Weather interpretation codes
    // https://open-meteo.com/en/docs
    switch (code) {
        case 0: return tr("晴");
        case 1: return tr("晴");
        case 2: return tr("多云");
        case 3: return tr("阴");
        case 45:
        case 48: return tr("雾");
        case 51:
        case 53:
        case 55: return tr("小雨");
        case 56:
        case 57: return tr("冻雨");
        case 61:
        case 63: return tr("中雨");
        case 65: return tr("大雨");
        case 66:
        case 67: return tr("冻雨");
        case 71:
        case 73: return tr("小雪");
        case 75: return tr("大雪");
        case 77: return tr("雪粒");
        case 80:
        case 81:
        case 82: return tr("阵雨");
        case 85:
        case 86: return tr("阵雪");
        case 95: return tr("雷雨");
        case 96:
        case 99: return tr("冰雹");
        default: return tr("未知");
    }
}

CardWidget* RecognitionPage::createVideoCard() {
    auto card = new CardWidget();
    card->setVariant("dark");
    card->setTitle("");
    
    auto container = new QWidget();
    container->setObjectName("VideoContainer");
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    auto main_layout = new QVBoxLayout(container);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    
    // ========== 视频显示区域 ==========
    video_widget_ = new VideoDisplayWidget(container);
    video_widget_->set_show_fps(true);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    main_layout->addWidget(video_widget_, 1);
    
    // ========== 信息栏（天气 + 考勤统计） ==========
    auto info_bar = createInfoBar();
    main_layout->addWidget(info_bar, 0);
    
    // ========== 综合状态栏（时钟 + 状态指示 + 进度条） ==========
    auto status_bar = createStatusBar();
    main_layout->addWidget(status_bar, 0);
    
    // ========== 用户信息面板（现代化设计） ==========
    auto info_panel = new QWidget(container);
    info_panel->setObjectName("UserInfoPanel");
    info_panel->setAttribute(Qt::WA_StyledBackground, true);
    info_panel->setFixedHeight(100);

    auto info_layout = new QHBoxLayout(info_panel);
    info_layout->setContentsMargins(20, 16, 20, 16);
    info_layout->setSpacing(20);
    
    // ===== 左侧：用户头像占位符 =====
    auto avatar_container = new QWidget(info_panel);
    avatar_container->setObjectName("AvatarContainer");
    avatar_container->setAttribute(Qt::WA_StyledBackground, true);
    avatar_container->setFixedSize(68, 68);
    
    auto avatar_layout = new QVBoxLayout(avatar_container);
    avatar_layout->setContentsMargins(0, 0, 0, 0);
    avatar_layout->setAlignment(Qt::AlignCenter);
    
    auto avatar_icon = new QLabel("", avatar_container);
    avatar_icon->setObjectName("AvatarIcon");
    avatar_icon->setAlignment(Qt::AlignCenter);
    avatar_icon->setStyleSheet("font-size: 32px; color: #8c8c8c;");
    avatar_icon->setText("◉");  // 使用简单的圆形符号作为头像占位符
    avatar_layout->addWidget(avatar_icon);
    
    info_layout->addWidget(avatar_container);
    
    // ===== 中间：用户基本信息 =====
    auto user_info_column = new QVBoxLayout();
    user_info_column->setSpacing(6);
    
    // 用户名 + 状态标签
    auto name_row = new QHBoxLayout();
    name_row->setSpacing(10);
    
    user_name_label_ = new QLabel(tr("等待识别..."), info_panel);
    user_name_label_->setObjectName("UserNameLabel");
    
    attendance_status_label_ = new QLabel(info_panel);
    attendance_status_label_->setObjectName("AttendanceStatusLabel");
    attendance_status_label_->setVisible(false);
    
    name_row->addWidget(user_name_label_);
    name_row->addWidget(attendance_status_label_);
    name_row->addStretch();
    
    // 工号 | 部门
    auto detail_row = new QHBoxLayout();
    detail_row->setSpacing(0);
    
    user_id_label_ = new QLabel(tr("工号: --"), info_panel);
    user_id_label_->setObjectName("UserMetaLabel");
    
    auto separator1 = new QLabel("  •  ", info_panel);
    separator1->setObjectName("MetaSeparator");
    
    user_dept_label_ = new QLabel(tr("部门: --"), info_panel);
    user_dept_label_->setObjectName("UserMetaLabel");
    
    detail_row->addWidget(user_id_label_);
    detail_row->addWidget(separator1);
    detail_row->addWidget(user_dept_label_);
    detail_row->addStretch();
    
    // 识别状态（隐藏，内部使用）
    recognition_label_ = new QLabel(tr("状态: 未识别"), info_panel);
    recognition_label_->setObjectName("UserDetailLabel");
    recognition_label_->setVisible(false);
    
    user_info_column->addLayout(name_row);
    user_info_column->addLayout(detail_row);
    user_info_column->addStretch();
    
    info_layout->addLayout(user_info_column, 1);
    
    // ===== 右侧：识别指标卡片 =====
    auto metrics_container = new QWidget(info_panel);
    metrics_container->setObjectName("MetricsContainer");
    metrics_container->setAttribute(Qt::WA_StyledBackground, true);
    
    auto metrics_layout = new QHBoxLayout(metrics_container);
    metrics_layout->setContentsMargins(0, 0, 0, 0);
    metrics_layout->setSpacing(16);
    
    // 相似度指标
    auto similarity_card = new QWidget(metrics_container);
    similarity_card->setObjectName("MetricCard");
    similarity_card->setAttribute(Qt::WA_StyledBackground, true);
    similarity_card->setFixedWidth(90);
    
    auto sim_layout = new QVBoxLayout(similarity_card);
    sim_layout->setContentsMargins(12, 8, 12, 8);
    sim_layout->setSpacing(2);
    sim_layout->setAlignment(Qt::AlignCenter);
    
    user_similarity_label_ = new QLabel(tr("--"), similarity_card);
    user_similarity_label_->setObjectName("MetricValue");
    user_similarity_label_->setAlignment(Qt::AlignCenter);
    
    auto sim_title = new QLabel(tr("相似度"), similarity_card);
    sim_title->setObjectName("MetricTitle");
    sim_title->setAlignment(Qt::AlignCenter);
    
    sim_layout->addWidget(user_similarity_label_);
    sim_layout->addWidget(sim_title);
    
    // 打卡类型指标
    auto type_card = new QWidget(metrics_container);
    type_card->setObjectName("MetricCard");
    type_card->setAttribute(Qt::WA_StyledBackground, true);
    type_card->setFixedWidth(90);
    
    auto type_layout = new QVBoxLayout(type_card);
    type_layout->setContentsMargins(12, 8, 12, 8);
    type_layout->setSpacing(2);
    type_layout->setAlignment(Qt::AlignCenter);
    
    check_type_label_ = new QLabel(tr("--"), type_card);
    check_type_label_->setObjectName("MetricValue");
    check_type_label_->setAlignment(Qt::AlignCenter);
    
    auto type_title = new QLabel(tr("打卡类型"), type_card);
    type_title->setObjectName("MetricTitle");
    type_title->setAlignment(Qt::AlignCenter);
    
    type_layout->addWidget(check_type_label_);
    type_layout->addWidget(type_title);
    
    metrics_layout->addWidget(similarity_card);
    metrics_layout->addWidget(type_card);
    
    info_layout->addWidget(metrics_container);
    
    main_layout->addWidget(info_panel, 0);
    
    auto card_layout = new QVBoxLayout(card->bodyContainer());
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);
    card_layout->addWidget(container);
    
    return card;
}

QWidget* RecognitionPage::createInfoBar() {
    auto info_bar = new QWidget();
    info_bar->setObjectName("InfoBar");
    info_bar->setAttribute(Qt::WA_StyledBackground, true);
    info_bar->setFixedHeight(56);  // 增加高度
    
    auto layout = new QHBoxLayout(info_bar);
    layout->setContentsMargins(20, 12, 20, 12);  // 增加上下内边距
    layout->setSpacing(20);
    
    // ===== 左侧：天气信息 =====
    auto weather_container = new QWidget(info_bar);
    weather_container->setObjectName("WeatherContainer");
    auto weather_layout = new QHBoxLayout(weather_container);
    weather_layout->setContentsMargins(0, 0, 0, 0);
    weather_layout->setSpacing(6);
    
    auto weather_icon = new QLabel("☀", weather_container);
    weather_icon->setObjectName("WeatherIcon");
    weather_icon->setStyleSheet("font-size: 18px;");
    
    weather_label_ = new QLabel(tr("--"), weather_container);
    weather_label_->setObjectName("WeatherLabel");
    
    temp_label_ = new QLabel(tr("--"), weather_container);
    temp_label_->setObjectName("TempLabel");
    
    weather_layout->addWidget(weather_icon);
    weather_layout->addWidget(weather_label_);
    weather_layout->addWidget(temp_label_);
    
    layout->addWidget(weather_container);
    
    // ===== 分隔符 =====
    auto sep1 = new QWidget(info_bar);
    sep1->setObjectName("InfoBarSeparator");
    sep1->setFixedWidth(1);
    sep1->setMinimumHeight(20);
    layout->addWidget(sep1);
    
    // ===== 中间：今日考勤统计 =====
    auto stats_container = new QWidget(info_bar);
    stats_container->setObjectName("StatsContainer");
    auto stats_layout = new QHBoxLayout(stats_container);
    stats_layout->setContentsMargins(0, 0, 0, 0);
    stats_layout->setSpacing(16);
    
    auto stats_title = new QLabel(tr("今日:"), stats_container);
    stats_title->setObjectName("StatsTitle");
    
    // 签到人数
    auto checkin_label = new QLabel(tr("签到"), stats_container);
    checkin_label->setObjectName("StatsItemLabel");
    checkin_count_label_ = new QLabel("0", stats_container);
    checkin_count_label_->setObjectName("StatsItemValue");
    checkin_count_label_->setStyleSheet("color: #52c41a; font-weight: 600;");
    
    // 签退人数
    auto checkout_label = new QLabel(tr("签退"), stats_container);
    checkout_label->setObjectName("StatsItemLabel");
    checkout_count_label_ = new QLabel("0", stats_container);
    checkout_count_label_->setObjectName("StatsItemValue");
    checkout_count_label_->setStyleSheet("color: #1890ff; font-weight: 600;");
    
    // 异常（迟到+早退）
    auto late_label = new QLabel(tr("异常"), stats_container);
    late_label->setObjectName("StatsItemLabel");
    late_count_label_ = new QLabel("0", stats_container);
    late_count_label_->setObjectName("StatsItemValue");
    late_count_label_->setStyleSheet("color: #8c8c8c; font-weight: 600;");
    
    stats_layout->addWidget(stats_title);
    stats_layout->addWidget(checkin_label);
    stats_layout->addWidget(checkin_count_label_);
    stats_layout->addWidget(checkout_label);
    stats_layout->addWidget(checkout_count_label_);
    stats_layout->addWidget(late_label);
    stats_layout->addWidget(late_count_label_);
    
    layout->addWidget(stats_container);
    
    layout->addStretch();
    
    // ===== 右侧：当前模式 =====
    check_mode_label_ = new QLabel(tr("签到模式"), info_bar);
    check_mode_label_->setObjectName("CheckModeLabel");
    check_mode_label_->setStyleSheet("color: #52c41a; font-weight: 600; background: rgba(82,196,26,0.1); padding: 4px 12px; border-radius: 12px;");
    
    layout->addWidget(check_mode_label_);
    
    return info_bar;
}

QWidget* RecognitionPage::createStatusBar() {
    auto status_bar = new QWidget();
    status_bar->setObjectName("DetectionStatusBar");
    status_bar->setAttribute(Qt::WA_StyledBackground, true);
    status_bar->setFixedHeight(56);
    
    auto layout = new QHBoxLayout(status_bar);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(24);
    
    // ===== 左侧：实时时钟 =====
    auto clock_container = new QWidget(status_bar);
    clock_container->setObjectName("ClockContainer");
    auto clock_layout = new QHBoxLayout(clock_container);
    clock_layout->setContentsMargins(0, 0, 0, 0);
    clock_layout->setSpacing(8);
    
    auto clock_icon = new QLabel("⏱", clock_container);  // 使用简单字符
    clock_icon->setObjectName("ClockIcon");
    
    clock_label_ = new QLabel("--:--:--", clock_container);
    clock_label_->setObjectName("ClockLabel");
    
    clock_layout->addWidget(clock_icon);
    clock_layout->addWidget(clock_label_);
    
    layout->addWidget(clock_container);
    
    // ===== 分隔符 =====
    auto separator1 = new QWidget(status_bar);
    separator1->setObjectName("StatusBarSeparator");
    separator1->setFixedWidth(1);
    separator1->setMinimumHeight(24);
    layout->addWidget(separator1);
    
    // ===== 日期 =====
    auto date_container = new QWidget(status_bar);
    date_container->setObjectName("DateContainer");
    auto date_layout = new QHBoxLayout(date_container);
    date_layout->setContentsMargins(0, 0, 0, 0);
    date_layout->setSpacing(8);
    
    date_label_ = new QLabel("----年--月--日", date_container);
    date_label_->setObjectName("DateLabel");
    
    date_layout->addWidget(date_label_);
    
    layout->addWidget(date_container);
    
    // ===== 分隔符 =====
    auto separator2 = new QWidget(status_bar);
    separator2->setObjectName("StatusBarSeparator");
    separator2->setFixedWidth(1);
    separator2->setMinimumHeight(24);
    layout->addWidget(separator2);
    
    // ===== 人脸检测状态 =====
    auto face_container = new QWidget(status_bar);
    face_container->setObjectName("FaceCountContainer");
    face_container->setFixedWidth(140);  // 固定宽度防止抖动
    auto face_layout = new QHBoxLayout(face_container);
    face_layout->setContentsMargins(0, 0, 0, 0);
    face_layout->setSpacing(8);
    
    face_count_label_ = new QLabel(tr("人脸: 0"), face_container);
    face_count_label_->setObjectName("FaceCountLabel");
    face_count_label_->setProperty("status", "inactive");
    
    face_layout->addWidget(face_count_label_);
    
    layout->addWidget(face_container);
    
    layout->addStretch();
    
    // ===== 右侧：识别进度 =====
    auto progress_container = new QWidget(status_bar);
    progress_container->setObjectName("ProgressContainer");
    auto progress_layout = new QHBoxLayout(progress_container);
    progress_layout->setContentsMargins(0, 0, 0, 0);
    progress_layout->setSpacing(12);
    
    detection_status_label_ = new QLabel(tr("等待识别"), progress_container);
    detection_status_label_->setObjectName("DetectionStatusLabel");
    
    detection_progress_bar_ = new QProgressBar(progress_container);
    detection_progress_bar_->setObjectName("DetectionProgressBar");
    detection_progress_bar_->setRange(0, 100);
    detection_progress_bar_->setValue(0);
    detection_progress_bar_->setTextVisible(false);
    detection_progress_bar_->setFixedSize(120, 6);
    // 始终显示进度条，防止布局抖动
    detection_progress_bar_->setVisible(true);
    
    progress_layout->addWidget(detection_status_label_);
    progress_layout->addWidget(detection_progress_bar_);
    
    layout->addWidget(progress_container);
    
    return status_bar;
}

CardWidget* RecognitionPage::createStatusCard() {
    auto card = new CardWidget();
    card->setTitle(tr("系统控制"));

    // 状态指示器
    auto status_container = new QWidget();
    status_container->setObjectName("StatusContainer");
    status_container->setAttribute(Qt::WA_StyledBackground, true);
    
    auto status_layout = new QHBoxLayout(status_container);
    status_layout->setContentsMargins(0, 0, 0, 0);
    status_layout->setSpacing(24);
    
    // 运行状态
    auto status_item = new QWidget();
    status_item->setAttribute(Qt::WA_StyledBackground, true);
    auto status_item_layout = new QVBoxLayout(status_item);
    status_item_layout->setContentsMargins(0, 0, 0, 0);
    status_item_layout->setSpacing(4);
    
    auto status_title = new QLabel(tr("运行状态"));
    status_title->setObjectName("StatusTitle");

    status_label_ = new QLabel(tr("就绪"));
    status_label_->setObjectName("StatusValue");
    status_label_->setProperty("status", "success");
    
    status_item_layout->addWidget(status_title);
    status_item_layout->addWidget(status_label_);
    
    // 帧率
    auto fps_item = new QWidget();
    fps_item->setAttribute(Qt::WA_StyledBackground, true);
    auto fps_item_layout = new QVBoxLayout(fps_item);
    fps_item_layout->setContentsMargins(0, 0, 0, 0);
    fps_item_layout->setSpacing(4);
    
    auto fps_title = new QLabel(tr("实时帧率"));
    fps_title->setObjectName("StatusTitle");

    fps_label_ = new QLabel(tr("0 FPS"));
    fps_label_->setObjectName("FpsValue");
    
    fps_item_layout->addWidget(fps_title);
    fps_item_layout->addWidget(fps_label_);
    
    status_layout->addWidget(status_item);
    status_layout->addWidget(fps_item);
    status_layout->addStretch();

    // 按钮组
    auto button_row = new QHBoxLayout();
    button_row->setSpacing(12);

    auto start_btn = new QPushButton(tr("▶ 开始识别"), card);
    start_btn->setProperty("buttonType", "primary");
    start_btn->setMinimumHeight(40);
    start_btn->setCursor(Qt::PointingHandCursor);
    connect(start_btn, &QPushButton::clicked, this, &RecognitionPage::startRecognitionRequested);

    auto stop_btn = new QPushButton(tr("■ 停止识别"), card);
    stop_btn->setMinimumHeight(40);
    stop_btn->setCursor(Qt::PointingHandCursor);
    connect(stop_btn, &QPushButton::clicked, this, &RecognitionPage::stopRecognitionRequested);

    auto register_btn = new QPushButton(tr("+ 注册人脸"), card);
    register_btn->setObjectName("GhostButton");
    register_btn->setMinimumHeight(40);
    register_btn->setCursor(Qt::PointingHandCursor);
    connect(register_btn, &QPushButton::clicked, this, &RecognitionPage::registerFaceRequested);

    button_row->addWidget(start_btn);
    button_row->addWidget(stop_btn);
    button_row->addWidget(register_btn);
    button_row->addStretch();

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(20);
    body_layout->addWidget(status_container);
    body_layout->addLayout(button_row);

    return card;
}

CardWidget* RecognitionPage::createAttendanceCard() {
    auto card = new CardWidget();
    card->setTitle(tr("今日签到记录"));

    attendance_table_ = new ModernTableView(card);
    attendance_table_->setColumnCount(4);
    attendance_table_->setHorizontalHeaderLabels({tr("姓名"), tr("时间"), tr("类型"), tr("相似度")});
    
    // 不设置内联样式，让全局 QSS 控制

    auto layout = new QVBoxLayout(card->bodyContainer());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(attendance_table_);

    return card;
}

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
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QSizePolicy>

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
    , check_type_label_(nullptr) {
    
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
    
    auto avatar_icon = new QLabel("👤", avatar_container);
    avatar_icon->setObjectName("AvatarIcon");
    avatar_icon->setAlignment(Qt::AlignCenter);
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

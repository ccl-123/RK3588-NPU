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
    
    // ========== 用户信息面板 ==========
    auto info_panel = new QWidget(container);
    info_panel->setObjectName("UserInfoPanel");
    info_panel->setAttribute(Qt::WA_StyledBackground, true);
    info_panel->setFixedHeight(130);

    auto info_layout = new QVBoxLayout(info_panel);
    info_layout->setContentsMargins(24, 16, 24, 16);
    info_layout->setSpacing(10);
    
    // 第一行：用户名 + 状态标签
    auto name_row = new QHBoxLayout();
    name_row->setSpacing(12);
    
    user_name_label_ = new QLabel(tr("等待识别..."), info_panel);
    user_name_label_->setObjectName("UserNameLabel");
    
    attendance_status_label_ = new QLabel(info_panel);
    attendance_status_label_->setObjectName("AttendanceStatusLabel");
    attendance_status_label_->setVisible(false);
    
    name_row->addWidget(user_name_label_);
    name_row->addWidget(attendance_status_label_);
    name_row->addStretch();
    
    // 第二行：工号、部门
    auto basic_row = new QHBoxLayout();
    basic_row->setSpacing(32);
    
    user_id_label_ = new QLabel(tr("工号: --"), info_panel);
    user_id_label_->setObjectName("UserInfoLabel");
    
    user_dept_label_ = new QLabel(tr("部门: --"), info_panel);
    user_dept_label_->setObjectName("UserInfoLabel");
    
    basic_row->addWidget(user_id_label_);
    basic_row->addWidget(user_dept_label_);
    basic_row->addStretch();
    
    // 第三行：相似度、识别状态、打卡类型
    auto recog_row = new QHBoxLayout();
    recog_row->setSpacing(32);
    
    user_similarity_label_ = new QLabel(tr("相似度: --"), info_panel);
    user_similarity_label_->setObjectName("UserDetailLabel");
    
    recognition_label_ = new QLabel(tr("状态: 未识别"), info_panel);
    recognition_label_->setObjectName("UserDetailLabel");
    
    check_type_label_ = new QLabel(tr("类型: --"), info_panel);
    check_type_label_->setObjectName("UserDetailLabel");
    
    recog_row->addWidget(user_similarity_label_);
    recog_row->addWidget(recognition_label_);
    recog_row->addWidget(check_type_label_);
    recog_row->addStretch();
    
    info_layout->addLayout(name_row);
    info_layout->addLayout(basic_row);
    info_layout->addLayout(recog_row);
    
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

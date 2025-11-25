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
    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    auto video_card = createVideoCard();
    auto right_column = new QVBoxLayout();
    right_column->setSpacing(24);

    auto status_card = createStatusCard();
    auto attendance_card = createAttendanceCard();

    layout->addWidget(video_card, 2);
    layout->addLayout(right_column, 1);
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
    card->setTitle("");  // 清空标题，隐藏 header
    
    // 创建一个容器 widget 来包含视频和用户信息面板
    auto container = new QWidget();
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 主垂直布局（视频在上，信息面板在下）
    auto main_layout = new QVBoxLayout(container);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    
    // ========== 视频显示区域 ==========
    video_widget_ = new VideoDisplayWidget(container);
    video_widget_->set_show_fps(true);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // 视频占据主要空间（拉伸因子为1）
    main_layout->addWidget(video_widget_, 1);
    
    // ========== 用户信息面板（底部固定区域）==========
    auto info_panel = new QWidget(container);
    info_panel->setObjectName("UserInfoPanel");
    info_panel->setAttribute(Qt::WA_StyledBackground, true);
    info_panel->setAutoFillBackground(true);
    
    // 设置样式：深色半透明背景 + 蓝色顶部边框
    info_panel->setStyleSheet(R"(
        QWidget#UserInfoPanel {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(0,0,0,0.92), stop:1 rgba(0,0,0,0.85));
            border-top: 2px solid #1677ff;
        }
        QWidget#UserInfoPanel QLabel {
            background: transparent;
        }
    )");
    
    // 增加高度以容纳更多信息
    info_panel->setFixedHeight(140);
    info_panel->setMinimumHeight(140);
    info_panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    // 信息面板布局
    auto info_layout = new QVBoxLayout(info_panel);
    info_layout->setContentsMargins(24, 16, 24, 16);
    info_layout->setSpacing(12);
    
    // 第一行：用户名 + 考勤状态标签
    auto name_row = new QHBoxLayout();
    name_row->setSpacing(12);
    
    user_name_label_ = new QLabel(tr("等待识别..."), info_panel);
    user_name_label_->setStyleSheet("color: #ffffff; font-size: 18px; font-weight: bold; background: transparent;");
    
    attendance_status_label_ = new QLabel(info_panel);
    attendance_status_label_->setObjectName("AttendanceStatusInline");
    attendance_status_label_->setStyleSheet(
        "background: #52c41a; color: white; padding: 4px 16px; "
        "border-radius: 12px; font-size: 13px; font-weight: bold;");
    attendance_status_label_->setVisible(false);
    
    name_row->addWidget(user_name_label_);
    name_row->addWidget(attendance_status_label_);
    name_row->addStretch();
    
    // 第二行：基本信息（工号、部门）
    auto basic_row = new QHBoxLayout();
    basic_row->setSpacing(40);
    
    user_id_label_ = new QLabel(tr("工号: --"), info_panel);
    user_id_label_->setStyleSheet("color: #d9d9d9; font-size: 14px; background: transparent;");
    
    user_dept_label_ = new QLabel(tr("部门: --"), info_panel);
    user_dept_label_->setStyleSheet("color: #d9d9d9; font-size: 14px; background: transparent;");
    
    basic_row->addWidget(user_id_label_);
    basic_row->addWidget(user_dept_label_);
    basic_row->addStretch();
    
    // 第三行：识别信息（相似度、识别时间、打卡类型）
    auto recog_row = new QHBoxLayout();
    recog_row->setSpacing(40);
    
    user_similarity_label_ = new QLabel(tr("相似度: --"), info_panel);
    user_similarity_label_->setStyleSheet("color: #bfbfbf; font-size: 13px; background: transparent;");
    
    recognition_label_ = new QLabel(tr("识别: 未识别"), info_panel);
    recognition_label_->setStyleSheet("color: #bfbfbf; font-size: 13px; background: transparent;");
    
    check_type_label_ = new QLabel(tr("打卡类型: --"), info_panel);
    check_type_label_->setObjectName("CheckTypeLabel");
    check_type_label_->setStyleSheet("color: #bfbfbf; font-size: 13px; background: transparent;");
    
    recog_row->addWidget(user_similarity_label_);
    recog_row->addWidget(recognition_label_);
    recog_row->addWidget(check_type_label_);
    recog_row->addStretch();
    
    info_layout->addLayout(name_row);
    info_layout->addLayout(basic_row);
    info_layout->addLayout(recog_row);
    
    // 添加信息面板到主布局（拉伸因子为0，固定在底部）
    main_layout->addWidget(info_panel, 0);
    
    // 将容器添加到卡片的 body
    auto card_layout = new QVBoxLayout(card->bodyContainer());
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);
    card_layout->addWidget(container);
    
    return card;
}

CardWidget* RecognitionPage::createStatusCard() {
    auto card = new CardWidget();
    card->setTitle(tr("系统控制"));

    status_label_ = new QLabel(tr("就绪"));
    fps_label_ = new QLabel(tr("FPS: 0"));

    auto grid = new QGridLayout(card->bodyContainer());
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(12);

    grid->addWidget(new QLabel(tr("系统状态")), 0, 0);
    grid->addWidget(status_label_, 0, 1);
    grid->addWidget(new QLabel(tr("帧率")), 1, 0);
    grid->addWidget(fps_label_, 1, 1);

    auto button_row = new QHBoxLayout();
    button_row->setSpacing(12);

    auto start_btn = new QPushButton(tr("开始识别"), card);
    start_btn->setProperty("primary", QVariant(true));
    connect(start_btn, &QPushButton::clicked, this, &RecognitionPage::startRecognitionRequested);

    auto stop_btn = new QPushButton(tr("停止识别"), card);
    connect(stop_btn, &QPushButton::clicked, this, &RecognitionPage::stopRecognitionRequested);

    auto register_btn = new QPushButton(tr("注册人脸"), card);
    connect(register_btn, &QPushButton::clicked, this, &RecognitionPage::registerFaceRequested);

    button_row->addWidget(start_btn);
    button_row->addWidget(stop_btn);
    button_row->addWidget(register_btn);
    button_row->addStretch();

    grid->addLayout(button_row, 2, 0, 1, 2);

    return card;
}

CardWidget* RecognitionPage::createAttendanceCard() {
    auto card = new CardWidget();
    card->setTitle(tr("今日签到"));

    attendance_table_ = new ModernTableView(card);
    attendance_table_->setColumnCount(4);
    attendance_table_->setHorizontalHeaderLabels({tr("姓名"), tr("时间"), tr("类型"), tr("相似度")});

    auto layout = new QVBoxLayout(card->bodyContainer());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(attendance_table_);

    return card;
}


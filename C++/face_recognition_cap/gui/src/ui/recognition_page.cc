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

RecognitionPage::RecognitionPage(QWidget* parent)
    : QWidget(parent)
    , video_widget_(nullptr)
    , attendance_table_(nullptr)
    , status_label_(nullptr)
    , fps_label_(nullptr)
    , recognition_label_(nullptr)
    , attendance_status_label_(nullptr) {
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

CardWidget* RecognitionPage::createVideoCard() {
    auto card = new CardWidget();
    card->setTitle(tr("实时识别"));
    card->setVariant("dark");

    video_widget_ = new VideoDisplayWidget(card);
    video_widget_->set_show_fps(true);

    auto layout = new QVBoxLayout(card->bodyContainer());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(video_widget_, 1);

    return card;
}

CardWidget* RecognitionPage::createStatusCard() {
    auto card = new CardWidget();
    card->setTitle(tr("识别状态"));

    status_label_ = new QLabel(tr("就绪"));
    recognition_label_ = new QLabel(tr("未识别"));
    fps_label_ = new QLabel(tr("FPS: 0"));
    attendance_status_label_ = new QLabel();
    attendance_status_label_->setObjectName("AttendanceStatus");
    attendance_status_label_->setStyleSheet(
        "background: #52c41a; color: white; padding: 6px 12px; border-radius: 12px;");
    attendance_status_label_->setVisible(false);

    auto grid = new QGridLayout(card->bodyContainer());
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(12);

    grid->addWidget(new QLabel(tr("系统状态")), 0, 0);
    grid->addWidget(status_label_, 0, 1);
    grid->addWidget(new QLabel(tr("识别结果")), 1, 0);
    grid->addWidget(recognition_label_, 1, 1);
    grid->addWidget(new QLabel(tr("帧率")), 2, 0);
    grid->addWidget(fps_label_, 2, 1);
    grid->addWidget(new QLabel(tr("提示")), 3, 0);
    grid->addWidget(attendance_status_label_, 3, 1);

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

    grid->addLayout(button_row, 4, 0, 1, 2);

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


#include "ui/attendance_page.h"

#include "widgets/card_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

AttendancePage::AttendancePage(QWidget* parent)
    : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    auto card = new CardWidget(this);
    card->setTitle(tr("考勤记录"));

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(16);

    auto description = new QLabel(tr("在此页面可以查看考勤统计、导出报表，"
                                     "点击下方按钮打开完整的考勤查询窗口。"), card);
    description->setWordWrap(true);

    auto button = new QPushButton(tr("打开考勤查询"), card);
    button->setProperty("primary", QVariant(true));
    connect(button, &QPushButton::clicked, this, &AttendancePage::openAttendanceQueryRequested);

    body_layout->addWidget(description);
    body_layout->addWidget(button, 0, Qt::AlignLeft);

    layout->addWidget(card);
}


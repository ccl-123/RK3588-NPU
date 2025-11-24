#include "ui/dashboard_page.h"

#include "widgets/card_widget.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

DashboardPage::DashboardPage(QWidget* parent)
    : QWidget(parent) {
    auto layout = new QGridLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setHorizontalSpacing(24);
    layout->setVerticalSpacing(24);

    for (int i = 0; i < 4; ++i) {
        auto card = new CardWidget(this);
        card->setTitle(tr("指标 %1").arg(i + 1));

        auto value = new QLabel(QString::number(100 * (i + 1)), card);
        value->setStyleSheet("font-size: 32px; font-weight: 600;");

        auto body_layout = new QVBoxLayout(card->bodyContainer());
        body_layout->setContentsMargins(0, 0, 0, 0);
        body_layout->addWidget(value, 0, Qt::AlignCenter);

        layout->addWidget(card, 0, i);
    }

    auto trend_card = new CardWidget(this);
    trend_card->setTitle(tr("趋势分析"));
    auto placeholder = new QLabel(tr("图表占位..."), trend_card);
    placeholder->setAlignment(Qt::AlignCenter);
    auto body_layout = new QVBoxLayout(trend_card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->addWidget(placeholder);
    layout->addWidget(trend_card, 1, 0, 1, 2);

    auto info_card = new CardWidget(this);
    info_card->setTitle(tr("系统提示"));
    auto info = new QLabel(tr("请连接摄像头并启动识别流程。"), info_card);
    info->setWordWrap(true);
    body_layout = new QVBoxLayout(info_card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->addWidget(info);
    layout->addWidget(info_card, 1, 2, 1, 2);
}


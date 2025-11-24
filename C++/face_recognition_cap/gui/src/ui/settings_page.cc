#include "ui/settings_page.h"

#include "widgets/card_widget.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    auto card = new CardWidget(this);
    card->setTitle(tr("系统设置"));

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(12);

    auto label = new QLabel(tr("统一的设置界面即将上线。可点击按钮打开旧版设置对话框。"), card);
    label->setWordWrap(true);

    auto button = new QPushButton(tr("打开设置"), card);
    button->setProperty("primary", QVariant(true));
    connect(button, &QPushButton::clicked, this, &SettingsPage::openSettingsDialogRequested);

    body_layout->addWidget(label);
    body_layout->addWidget(button, 0, Qt::AlignLeft);

    layout->addWidget(card);
}



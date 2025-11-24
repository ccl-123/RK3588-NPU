#include "widgets/title_bar.h"

#include "utils/svg_icon_manager.h"
#include "widgets/icon_button.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSpacerItem>

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
    , title_label_(new QLabel(this))
    , breadcrumb_label_(new QLabel(this))
    , theme_button_(new IconButton(this))
    , user_button_(new IconButton(this))
    , minimize_button_(new IconButton(this))
    , close_button_(new IconButton(this)) {
    setObjectName("TitleBar");
    setFixedHeight(64);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 0, 24, 0);
    layout->setSpacing(16);

    title_label_->setText(tr("人脸识别考勤系统"));
    title_label_->setStyleSheet("font-size: 20px; font-weight: 600;");

    breadcrumb_label_->setObjectName("Breadcrumb");
    breadcrumb_label_->setStyleSheet("color: #8c8c8c;");

    theme_button_->setSvg(":/icons/actions/theme.svg", QSize(20, 20));
    theme_button_->setText(tr("主题"));
    theme_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    theme_button_->setToolTip(tr("切换深色/浅色主题"));

    user_button_->setSvg(":/icons/status/user.svg", QSize(20, 20));
    user_button_->setText(tr("用户"));
    user_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    user_button_->setToolTip(tr("用户菜单"));

    minimize_button_->setSvg(":/icons/actions/minimize.svg", QSize(16, 16));
    minimize_button_->setText(tr("最小化"));
    minimize_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    minimize_button_->setToolTip(tr("最小化窗口"));
    
    close_button_->setSvg(":/icons/actions/close.svg", QSize(16, 16));
    close_button_->setText(tr("关闭"));
    close_button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    close_button_->setToolTip(tr("关闭程序"));

    layout->addWidget(title_label_);
    layout->addSpacing(12);
    layout->addWidget(breadcrumb_label_);
    layout->addStretch();
    layout->addWidget(theme_button_);
    layout->addWidget(user_button_);
    layout->addWidget(minimize_button_);
    layout->addWidget(close_button_);

    connect(theme_button_, &QToolButton::clicked, this, &TitleBar::requestToggleTheme);
    connect(minimize_button_, &QToolButton::clicked, this, &TitleBar::requestMinimize);
    connect(close_button_, &QToolButton::clicked, this, &TitleBar::requestClose);
}

void TitleBar::setTitle(const QString& title) {
    title_label_->setText(title);
}

void TitleBar::setBreadcrumb(const QStringList& crumbs) {
    breadcrumb_label_->setText(crumbs.join(" / "));
}

void TitleBar::setUserMenu(QMenu* menu) {
    if (!menu) {
        return;
    }
    user_button_->setMenu(menu);
    user_button_->setPopupMode(QToolButton::InstantPopup);
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        drag_pos_ = event->globalPos() - parentWidget()->frameGeometry().topLeft();
        event->accept();
    }
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        parentWidget()->move(event->globalPos() - drag_pos_);
        event->accept();
    }
}


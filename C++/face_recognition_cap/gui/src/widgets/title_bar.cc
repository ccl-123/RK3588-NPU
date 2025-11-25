/**
 * @file title_bar.cc
 * @brief 现代化标题栏
 * @author CL
 * @date 2025-11-25
 */

#include "widgets/title_bar.h"
#include "utils/svg_icon_manager.h"
#include "widgets/icon_button.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOption>

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
    , title_label_(new QLabel(this))
    , breadcrumb_label_(new QLabel(this))
    , theme_button_(new IconButton(this))
    , user_button_(new IconButton(this))
    , minimize_button_(new IconButton(this))
    , close_button_(new IconButton(this)) {
    setObjectName("TitleBar");
    setFixedHeight(56);
    setAttribute(Qt::WA_StyledBackground, true);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(8);

    // 左侧占位区域（与侧边栏宽度对齐：220px）
    auto left_spacer = new QWidget(this);
    left_spacer->setFixedWidth(220);
    left_spacer->setObjectName("TitleBarSpacer");
    layout->addWidget(left_spacer);

    // 面包屑导航（带左边距）
    breadcrumb_label_->setObjectName("Breadcrumb");
    breadcrumb_label_->setContentsMargins(24, 0, 0, 0);

    // 主题切换按钮
    theme_button_->setObjectName("ThemeButton");
    theme_button_->setSvg(":/icons/ui/sun.svg", QSize(18, 18));
    theme_button_->setToolTip(tr("切换主题"));
    theme_button_->setFixedSize(36, 36);

    // 用户菜单按钮
    user_button_->setObjectName("UserButton");
    user_button_->setSvg(":/icons/status/user.svg", QSize(18, 18));
    user_button_->setToolTip(tr("用户菜单"));
    user_button_->setFixedSize(36, 36);

    // 最小化按钮
    minimize_button_->setObjectName("MinimizeButton");
    minimize_button_->setSvg(":/icons/ui/minimize.svg", QSize(16, 16));
    minimize_button_->setToolTip(tr("最小化"));
    minimize_button_->setFixedSize(36, 36);

    // 关闭按钮
    close_button_->setObjectName("CloseButton");
    close_button_->setSvg(":/icons/actions/close.svg", QSize(16, 16));
    close_button_->setToolTip(tr("关闭"));
    close_button_->setFixedSize(36, 36);

    layout->addWidget(breadcrumb_label_);
    layout->addStretch();
    layout->addWidget(theme_button_);
    layout->addWidget(user_button_);
    layout->addSpacing(8);
    layout->addWidget(minimize_button_);
    layout->addWidget(close_button_);

    connect(theme_button_, &QToolButton::clicked, this, &TitleBar::requestToggleTheme);
    connect(minimize_button_, &QToolButton::clicked, this, &TitleBar::requestMinimize);
    connect(close_button_, &QToolButton::clicked, this, &TitleBar::requestClose);
    
    // 不使用内联样式，让全局 QSS 控制主题
}

void TitleBar::setTitle(const QString& title) {
    title_label_->setText(title);
}

void TitleBar::setBreadcrumb(const QStringList& crumbs) {
    QString html;
    for (int i = 0; i < crumbs.size(); ++i) {
        if (i > 0) {
            html += " <span style='color:#bfbfbf;'>/</span> ";
        }
        if (i == crumbs.size() - 1) {
            // 最后一项高亮
            html += QString("<span style='color:#262626; font-weight:600;'>%1</span>").arg(crumbs[i]);
        } else {
            html += QString("<span style='color:#8c8c8c;'>%1</span>").arg(crumbs[i]);
        }
    }
    breadcrumb_label_->setText(html);
}

void TitleBar::setUserMenu(QMenu* menu) {
    if (!menu) {
        return;
    }
    // 设置菜单样式
    menu->setStyleSheet(R"(
        QMenu {
            background-color: #ffffff;
            border: 1px solid #e8e8e8;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 8px 24px 8px 16px;
            border-radius: 4px;
            color: #262626;
        }
        QMenu::item:selected {
            background-color: #f5f5f5;
        }
        QMenu::separator {
            height: 1px;
            background-color: #f0f0f0;
            margin: 4px 8px;
        }
    )");
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

void TitleBar::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

/**
 * @file title_bar.cc
 * @brief 现代化标题栏
 * @author CL
 * @date 2025-11-25
 */

#include "widgets/title_bar.h"
#include "gui_utils/svg_icon_manager.h"
#include "widgets/icon_button.h"
#include "widgets/news_ticker.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QPainter>
#include <QStyleOption>

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
    , left_spacer_(new QWidget(this))
    , title_label_(new QLabel(this))
    , breadcrumb_label_(new QLabel(this))
    , news_ticker_(new NewsTicker(this))
    , theme_button_(new IconButton(this))
    , user_button_(new IconButton(this))
    , minimize_button_(new IconButton(this))
    , maximize_button_(new IconButton(this))
    , close_button_(new IconButton(this))
    , dragging_(false) {
    setObjectName("TitleBar");
    setFixedHeight(56);
    setAttribute(Qt::WA_StyledBackground, true);

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(8);

    left_spacer_->setFixedWidth(220);
    left_spacer_->setObjectName("TitleBarSpacer");
    layout->addWidget(left_spacer_);

    // 面包屑导航（带左边距）
    breadcrumb_label_->setObjectName("Breadcrumb");
    breadcrumb_label_->setContentsMargins(24, 0, 0, 0);

    // 让跑马灯在视觉上更突出
    news_ticker_->setContentsMargins(12, 4, 12, 4);

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

    // 最大化按钮
    maximize_button_->setObjectName("MaximizeButton");
    maximize_button_->setSvg(":/icons/ui/maximize.svg", QSize(16, 16));
    maximize_button_->setToolTip(tr("最大化"));
    maximize_button_->setFixedSize(36, 36);

    // 关闭按钮
    close_button_->setObjectName("CloseButton");
    close_button_->setSvg(":/icons/actions/close.svg", QSize(16, 16));
    close_button_->setToolTip(tr("关闭"));
    close_button_->setFixedSize(36, 36);

    // 标题与面包屑
    layout->addWidget(title_label_);
    layout->addWidget(breadcrumb_label_);

    // 热点跑马灯，占据中部空白区域
    news_ticker_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(news_ticker_, 1);

    layout->addStretch();
    layout->addWidget(theme_button_);
    layout->addWidget(user_button_);
    layout->addSpacing(8);
    layout->addWidget(minimize_button_);
    layout->addWidget(maximize_button_);
    layout->addWidget(close_button_);

    connect(theme_button_, &QToolButton::clicked, this, &TitleBar::requestToggleTheme);
    connect(minimize_button_, &QToolButton::clicked, this, &TitleBar::requestMinimize);
    connect(maximize_button_, &QToolButton::clicked, this, &TitleBar::requestMaximize);
    connect(close_button_, &QToolButton::clicked, this, &TitleBar::requestClose);

    // 不使用内联样式，让全局 QSS 控制主题
}

void TitleBar::setTitle(const QString& title) {
    title_label_->setText(title);
}

void TitleBar::setBreadcrumb(const QStringList& crumbs) {
    QString html;
    const qsizetype crumb_count = crumbs.size();
    for (qsizetype i = 0; i < crumb_count; ++i) {
        if (i > 0) {
            html += " <span style='color:#bfbfbf;'>/</span> ";
        }
        if (i == crumb_count - 1) {
            // 最后一项高亮
            html += QString("<span style='color:#262626; font-weight:600;'>%1</span>").arg(crumbs[i]);
        } else {
            html += QString("<span style='color:#8c8c8c;'>%1</span>").arg(crumbs[i]);
        }
    }
    breadcrumb_label_->setText(html);
}

void TitleBar::setHeadlines(const QStringList& headlines) {
    if (news_ticker_) {
        news_ticker_->setHeadlines(headlines);
    }
}

void TitleBar::setSideBarWidth(int width) {
    if (!left_spacer_) {
        return;
    }
    left_spacer_->setFixedWidth(qMax(0, width));
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
    dragging_ = false;
    QWidget* parent = parentWidget();
    if (event->button() == Qt::LeftButton && parent &&
        !parent->isMaximized() && !parent->isFullScreen() &&
        isDragArea(event->pos())) {
        drag_pos_ = event->globalPos() - parent->frameGeometry().topLeft();
        dragging_ = true;
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
    QWidget* parent = parentWidget();
    if (dragging_ && (event->buttons() & Qt::LeftButton) && parent &&
        !parent->isMaximized() && !parent->isFullScreen()) {
        parent->move(event->globalPos() - drag_pos_);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isDragArea(event->pos())) {
        emit requestMaximize();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::updateMaximizeIcon() {
    QWidget* parent = parentWidget();
    if (!parent || !maximize_button_) {
        return;
    }

    if (parent->isMaximized() || parent->isFullScreen()) {
        maximize_button_->setSvg(":/icons/ui/restore.svg", QSize(16, 16));
        maximize_button_->setToolTip(tr("还原"));
    } else {
        maximize_button_->setSvg(":/icons/ui/maximize.svg", QSize(16, 16));
        maximize_button_->setToolTip(tr("最大化"));
    }
}

bool TitleBar::isDragArea(const QPoint& pos) const {
    if (!rect().contains(pos)) {
        return false;
    }

    QWidget* child = childAt(pos);
    if (!child || child == this) {
        return true;
    }

    for (QWidget* current = child; current && current != this; current = current->parentWidget()) {
        if (qobject_cast<QAbstractButton*>(current) || current == news_ticker_) {
            return false;
        }
    }

    return child == left_spacer_ || child == title_label_ || child == breadcrumb_label_;
}

void TitleBar::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}

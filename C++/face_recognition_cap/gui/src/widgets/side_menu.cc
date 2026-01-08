/**
 * @file side_menu.cc
 * @brief 现代化侧边导航菜单
 * @author CL
 * @date 2025-11-25
 */

#include "widgets/side_menu.h"
#include "gui_utils/svg_icon_manager.h"

#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyleOption>

SideMenu::SideMenu(QWidget* parent)
    : QWidget(parent)
    , logo_label_(nullptr)
    , list_widget_(new QListWidget(this)) {
    setObjectName("SideMenu");
    setFixedWidth(220);
    setAttribute(Qt::WA_StyledBackground, true);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ========== Logo 区域 ==========
    auto logo_container = new QWidget(this);
    logo_container->setObjectName("LogoContainer");
    logo_container->setFixedHeight(56);  // 与标题栏高度一致
    
    auto logo_layout = new QHBoxLayout(logo_container);
    logo_layout->setContentsMargins(16, 0, 16, 0);
    
    // Logo 图标 - 使用 SVG 图标替代 emoji
    auto logo_icon = new QLabel(logo_container);
    logo_icon->setPixmap(SvgIconManager::icon(":/icons/ui/app-logo.svg", QSize(28, 28)).pixmap(28, 28));
    logo_icon->setFixedSize(28, 28);
    
    logo_label_ = new QLabel(tr("考勤系统"), logo_container);
    logo_label_->setObjectName("LogoLabel");
    
    logo_layout->addWidget(logo_icon);
    logo_layout->addSpacing(8);
    logo_layout->addWidget(logo_label_);
    logo_layout->addStretch();
    
    layout->addWidget(logo_container);

    // ========== 分割线 ==========
    auto separator = new QWidget(this);
    separator->setObjectName("MenuSeparator");
    separator->setFixedHeight(1);
    layout->addWidget(separator);

    // ========== 菜单列表 ==========
    auto menu_container = new QWidget(this);
    auto menu_layout = new QVBoxLayout(menu_container);
    menu_layout->setContentsMargins(12, 16, 12, 16);
    menu_layout->setSpacing(0);

    list_widget_->setObjectName("MenuList");
    list_widget_->setSpacing(4);
    list_widget_->setFrameStyle(QFrame::NoFrame);
    list_widget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_widget_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    list_widget_->setIconSize(QSize(20, 20));

    menu_layout->addWidget(list_widget_);
    layout->addWidget(menu_container, 1);

    // ========== 底部区域 ==========
    auto footer = new QWidget(this);
    footer->setObjectName("MenuFooter");
    footer->setFixedHeight(48);
    
    auto footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(20, 0, 20, 0);
    
    auto version_label = new QLabel("v2.1.0", footer);
    version_label->setObjectName("VersionLabel");
    footer_layout->addWidget(version_label);
    footer_layout->addStretch();
    
    layout->addWidget(footer);

    connect(list_widget_, &QListWidget::itemSelectionChanged,
            this, &SideMenu::handleSelectionChanged);
    
    // 不使用内联样式，让全局 QSS 控制主题
}

void SideMenu::setItems(const QList<SideMenu::Item>& items) {
    list_widget_->clear();
    for (const auto& item : items) {
        auto list_item = new QListWidgetItem(item.text);
        list_item->setData(Qt::UserRole, item.key);
        list_item->setSizeHint(QSize(-1, 48));  // 固定高度
        if (!item.icon.isEmpty()) {
            // 使用浅色图标以便在深色背景上可见
            list_item->setIcon(SvgIconManager::icon(item.icon, QSize(20, 20), QColor("#8c8c8c")));
        }
        list_widget_->addItem(list_item);
    }
    if (list_widget_->count() > 0) {
        list_widget_->setCurrentRow(0);
    }
}

void SideMenu::setActiveKey(const QString& key) {
    for (int i = 0; i < list_widget_->count(); ++i) {
        auto item = list_widget_->item(i);
        if (item->data(Qt::UserRole).toString() == key) {
            list_widget_->setCurrentRow(i);
            break;
        }
    }
}

void SideMenu::handleSelectionChanged() {
    auto item = list_widget_->currentItem();
    if (!item) {
        return;
    }
    emit routeChanged(item->data(Qt::UserRole).toString());
}

void SideMenu::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.init(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QWidget::paintEvent(event);
}


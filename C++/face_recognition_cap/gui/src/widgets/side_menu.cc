#include "widgets/side_menu.h"

#include "utils/svg_icon_manager.h"

#include <QListWidget>
#include <QVBoxLayout>

SideMenu::SideMenu(QWidget* parent)
    : QWidget(parent)
    , list_widget_(new QListWidget(this)) {
    setObjectName("SideMenu");
    setFixedWidth(240);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 24, 16, 24);
    layout->addWidget(list_widget_);

    list_widget_->setSpacing(4);
    list_widget_->setFrameStyle(QFrame::NoFrame);
    list_widget_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_widget_->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(list_widget_, &QListWidget::itemSelectionChanged,
            this, &SideMenu::handleSelectionChanged);
}

void SideMenu::setItems(const QList<SideMenu::Item>& items) {
    list_widget_->clear();
    for (const auto& item : items) {
        auto list_item = new QListWidgetItem(item.text);
        list_item->setData(Qt::UserRole, item.key);
        if (!item.icon.isEmpty()) {
            list_item->setIcon(SvgIconManager::icon(item.icon, QSize(20, 20), QColor("#ffffff")));
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


#include "widgets/icon_button.h"

#include "gui_utils/svg_icon_manager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>

IconButton::IconButton(QWidget* parent)
    : QToolButton(parent)
    , badge_(new QLabel(this)) {
    setObjectName("IconButton");
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setAutoRaise(true);
    setCursor(Qt::PointingHandCursor);

    auto badge_label = qobject_cast<QLabel*>(badge_);
    badge_label->setStyleSheet(
        "background: #ff4d4f; color: white; font-size: 10px; "
        "border-radius: 8px; padding: 0 4px;");
    badge_label->setAlignment(Qt::AlignCenter);
    badge_label->hide();
}

void IconButton::setSvg(const QString& resource_path, const QSize& size) {
    current_icon_path_ = resource_path;
    current_icon_size_ = size;
    setIcon(SvgIconManager::icon(resource_path, size));
    setIconSize(size);
}

void IconButton::setBadgeCount(int count) {
    badge_count_ = count;
    auto badge_label = qobject_cast<QLabel*>(badge_);
    if (count > 0) {
        badge_label->setText(QString::number(count));
        badge_label->adjustSize();
        badge_label->show();
    } else {
        badge_label->hide();
    }
    updateBadgePosition();
}

void IconButton::resizeEvent(QResizeEvent* event) {
    QToolButton::resizeEvent(event);
    updateBadgePosition();
}

void IconButton::updateBadgePosition() {
    if (!badge_ || !badge_->isVisible()) {
        return;
    }
    constexpr int offset = 4;
    badge_->adjustSize();
    const int x = width() - badge_->width() + offset;
    const int y = -offset;
    badge_->move(x, y);
}


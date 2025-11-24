#include "widgets/card_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>

CardWidget::CardWidget(QWidget* parent)
    : QFrame(parent)
    , header_container_(new QWidget(this))
    , body_container_(new QWidget(this))
    , footer_container_(new QWidget(this))
    , title_label_(new QLabel(this))
    , subtitle_label_(new QLabel(this))
    , main_layout_(new QVBoxLayout(this)) {
    setObjectName("CardWidget");
    setFrameStyle(QFrame::NoFrame);

    header_container_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    footer_container_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    title_label_->setObjectName("CardTitle");
    title_label_->setStyleSheet("font-size: 18px; font-weight: 600;");
    subtitle_label_->setObjectName("CardSubtitle");
    subtitle_label_->setStyleSheet("color: #8c8c8c;");

    auto header_layout = new QVBoxLayout(header_container_);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(4);
    header_layout->addWidget(title_label_);
    header_layout->addWidget(subtitle_label_);
    subtitle_label_->setVisible(false);

    auto footer_layout = new QVBoxLayout(footer_container_);
    footer_layout->setContentsMargins(0, 0, 0, 0);

    main_layout_->setContentsMargins(24, 24, 24, 24);
    main_layout_->setSpacing(16);
    main_layout_->addWidget(header_container_);
    main_layout_->addWidget(body_container_);
    main_layout_->addWidget(footer_container_);

    footer_container_->setVisible(false);
}

void CardWidget::setTitle(const QString& title) {
    title_label_->setText(title);
    header_container_->setVisible(!title.isEmpty() || subtitle_label_->isVisible());
}

void CardWidget::setSubtitle(const QString& subtitle) {
    subtitle_label_->setText(subtitle);
    subtitle_label_->setVisible(!subtitle.isEmpty());
}

void CardWidget::setHeaderWidget(QWidget* widget) {
    if (!widget) {
        return;
    }

    delete header_container_->layout();
    auto layout = new QHBoxLayout(header_container_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(widget);
}

QWidget* CardWidget::bodyContainer() const {
    return body_container_;
}

void CardWidget::setFooterWidget(QWidget* widget) {
    auto layout = static_cast<QVBoxLayout*>(footer_container_->layout());
    QLayoutItem* child;
    while ((child = layout->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    if (widget) {
        layout->addWidget(widget);
        footer_container_->setVisible(true);
    } else {
        footer_container_->setVisible(false);
    }
}

void CardWidget::setVariant(const QString& variant) {
    variant_ = variant;
    setProperty("cardVariant", QVariant(variant_));
    style()->unpolish(this);
    style()->polish(this);
    update();
}

QString CardWidget::variant() const {
    return variant_;
}


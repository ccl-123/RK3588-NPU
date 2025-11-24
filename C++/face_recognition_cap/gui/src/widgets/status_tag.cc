#include "widgets/status_tag.h"

#include <QStyle>
#include <QVariant>

StatusTag::StatusTag(QWidget* parent)
    : QLabel(parent) {
    setObjectName("StatusTag");
    setMargin(6);
    setAlignment(Qt::AlignCenter);
    updateAppearance();
}

void StatusTag::setType(StatusTag::Type type) {
    type_ = type;
    updateAppearance();
}

StatusTag::Type StatusTag::type() const {
    return type_;
}

void StatusTag::updateAppearance() {
    switch (type_) {
    case Type::Success:
        setProperty("type", QVariant(QStringLiteral("success")));
        break;
    case Type::Warning:
        setProperty("type", QVariant(QStringLiteral("warning")));
        break;
    case Type::Error:
        setProperty("type", QVariant(QStringLiteral("error")));
        break;
    case Type::Processing:
        setProperty("type", QVariant(QStringLiteral("processing")));
        break;
    case Type::Default:
    default:
        setProperty("type", QVariant(QStringLiteral("default")));
        break;
    }
    style()->unpolish(this);
    style()->polish(this);
}


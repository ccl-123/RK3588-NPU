#include "utils/stack_router.h"

#include <QDebug>

UiRouter::UiRouter(QStackedWidget* stack, QObject* parent)
    : QObject(parent)
    , stack_(stack) {
}

void UiRouter::registerPage(const QString& key, QWidget* widget) {
    if (!stack_ || !widget) {
        return;
    }

    if (indices_.contains(key)) {
        int index = indices_.value(key);
        stack_->removeWidget(stack_->widget(index));
    }

    int index = stack_->addWidget(widget);
    indices_.insert(key, index);
}

void UiRouter::navigateTo(const QString& key) {
    if (!stack_ || !indices_.contains(key)) {
        qWarning() << "UiRouter: key not found" << key;
        return;
    }

    int index = indices_.value(key);
    stack_->setCurrentIndex(index);
    current_key_ = key;
    emit routeChanged(key, stack_->currentWidget());
}

QString UiRouter::currentKey() const {
    return current_key_;
}


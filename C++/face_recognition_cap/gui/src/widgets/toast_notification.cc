#include "widgets/toast_notification.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QStyle>
#include <QVariant>
#include <QVBoxLayout>
#include <QtGlobal>

namespace {
constexpr int kMargin = 24;
constexpr int kSpacing = 12;

QHash<QWidget*, QList<ToastNotification*>> g_toasts;

void repositionToasts(QWidget* root) {
    if (!root) {
        return;
    }
    const int width = root->width();
    int y = kMargin;
    auto list = g_toasts.value(root);
    for (ToastNotification* toast : qAsConst(list)) {
        if (!toast) {
            continue;
        }
        toast->adjustSize();
        const int x = width - toast->width() - kMargin;
        toast->move(x, y);
        toast->raise();
        y += toast->height() + kSpacing;
    }
}

void registerToast(QWidget* root, ToastNotification* toast) {
    auto list = g_toasts.value(root);
    list.prepend(toast);
    while (list.size() > 4) {
        if (auto tail = list.takeLast()) {
            tail->deleteLater();
        }
    }
    g_toasts.insert(root, list);
    repositionToasts(root);
}

void unregisterToast(QWidget* root, ToastNotification* toast) {
    auto list = g_toasts.value(root);
    list.removeAll(toast);
    g_toasts.insert(root, list);
    repositionToasts(root);
}
}  // namespace

ToastNotification::ToastNotification(QWidget* parent)
    : QFrame(parent)
    , title_label_(new QLabel(this))
    , description_label_(new QLabel(this)) {
    setObjectName("ToastItem");
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(8);

    title_label_->setStyleSheet("font-size: 16px; font-weight: 600;");
    description_label_->setStyleSheet("color: #595959;");
    description_label_->setWordWrap(true);

    layout->addWidget(title_label_);
    layout->addWidget(description_label_);
}

void ToastNotification::showMessage(QWidget* anchor,
                                    const QString& title,
                                    const QString& description,
                                    ToastNotification::Level level,
                                    int duration_ms) {
    QWidget* root = anchor ? anchor->window() : QApplication::activeWindow();
    if (!root) {
        const auto widgets = QApplication::topLevelWidgets();
        if (!widgets.isEmpty()) {
            root = widgets.first();
        }
    }
    if (!root) {
        return;
    }

    auto toast = new ToastNotification(root);
    toast->title_label_->setText(title);
    toast->description_label_->setText(description);
    toast->setLevel(level);
    toast->start(duration_ms);
    registerToast(root, toast);
}

void ToastNotification::start(int duration_ms) {
    auto effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);

    auto fade_in = new QPropertyAnimation(effect, "opacity", this);
    fade_in->setDuration(180);
    fade_in->setStartValue(0.0);
    fade_in->setEndValue(1.0);
    fade_in->start(QAbstractAnimation::DeleteWhenStopped);

    show();

    QTimer::singleShot(duration_ms, this, [this, effect]() {
        auto fade_out = new QPropertyAnimation(effect, "opacity", this);
        fade_out->setDuration(220);
        fade_out->setStartValue(1.0);
        fade_out->setEndValue(0.0);
        connect(fade_out, &QPropertyAnimation::finished, this, [this]() {
            unregisterToast(parentWidget(), this);
            deleteLater();
        });
        fade_out->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void ToastNotification::setLevel(ToastNotification::Level level) {
    QString property_value;
    switch (level) {
    case Level::Success:
        property_value = QStringLiteral("success");
        break;
    case Level::Warning:
        property_value = QStringLiteral("warning");
        break;
    case Level::Error:
        property_value = QStringLiteral("error");
        break;
    case Level::Info:
    default:
        property_value = QStringLiteral("info");
        break;
    }
    setProperty("level", QVariant(property_value));
    style()->unpolish(this);
    style()->polish(this);
}


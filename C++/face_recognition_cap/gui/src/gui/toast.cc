#include "gui/toast.h"
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QGraphicsOpacityEffect>

Toast::Toast(QWidget* parent) : QWidget(parent), opacity_(0.0) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(10);

    icon_label_ = new QLabel(this);
    text_label_ = new QLabel(this);
    text_label_->setStyleSheet("color: white; font-weight: bold; font-size: 14px;");

    layout->addWidget(icon_label_);
    layout->addWidget(text_label_);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, &Toast::close);
}

void Toast::show(QWidget* parent, const QString& message, Type type) {
    Toast* toast = new Toast(parent);
    toast->text_label_->setText(message);

    // 设置样式
    QString icon;
    switch (type) {
        case Info:
            toast->bg_color_ = QColor(45, 45, 45, 230); // 深灰
            icon = "ℹ️";
            break;
        case Success:
            toast->bg_color_ = QColor(40, 167, 69, 230); // 绿色
            icon = "✅";
            break;
        case Warning:
            toast->bg_color_ = QColor(255, 193, 7, 230); // 黄色
            toast->text_label_->setStyleSheet("color: #333; font-weight: bold; font-size: 14px;");
            icon = "⚠️";
            break;
        case Error:
            toast->bg_color_ = QColor(220, 53, 69, 230); // 红色
            icon = "❌";
            break;
    }
    toast->icon_label_->setText(icon);

    // 计算位置 (居中偏下)
    toast->adjustSize();
    QWidget* window = parent ? parent->window() : QApplication::activeWindow();
    if (window) {
        QPoint center = window->geometry().center();
        toast->move(center.x() - toast->width() / 2, 
                   window->geometry().bottom() - 100);
    }

    toast->QWidget::show();
    
    // 动画效果
    QPropertyAnimation* anim = new QPropertyAnimation(toast, "opacity");
    anim->setDuration(300);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    // 3秒后消失
    toast->timer_->start(3000);
}

void Toast::setOpacity(double opacity) {
    opacity_ = opacity;
    setWindowOpacity(opacity);
    update();
}

void Toast::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 绘制圆角背景
    painter.setBrush(bg_color_);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), 8, 8);
}

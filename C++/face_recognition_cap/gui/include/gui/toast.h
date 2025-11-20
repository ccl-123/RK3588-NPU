#ifndef GUI_TOAST_H
#define GUI_TOAST_H

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QPropertyAnimation>
#include <QHBoxLayout>

class Toast : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity)

public:
    enum Type {
        Info,
        Success,
        Warning,
        Error
    };

    explicit Toast(QWidget* parent = nullptr);

    static void show(QWidget* parent, const QString& message, Type type = Info);

    double opacity() const { return opacity_; }
    void setOpacity(double opacity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* icon_label_;
    QLabel* text_label_;
    QTimer* timer_;
    double opacity_;
    QColor bg_color_;
    QColor text_color_;
};

#endif // GUI_TOAST_H

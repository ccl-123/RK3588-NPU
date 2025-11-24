#pragma once

#include <QFrame>
#include <QString>

class QLabel;

class ToastNotification : public QFrame {
    Q_OBJECT
public:
    enum class Level {
        Info,
        Success,
        Warning,
        Error
    };

    static void showMessage(QWidget* anchor,
                            const QString& title,
                            const QString& description,
                            Level level = Level::Info,
                            int duration_ms = 3000);

private:
    explicit ToastNotification(QWidget* parent = nullptr);
    void start(int duration_ms);
    void setLevel(Level level);

    QLabel* title_label_;
    QLabel* description_label_;
};


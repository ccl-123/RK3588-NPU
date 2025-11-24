#pragma once

#include <QWidget>

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(QWidget* parent = nullptr);

signals:
    void openAttendanceQueryRequested();
};


#pragma once

#include <QWidget>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <vector>

#include "service/attendance_service.h"
#include "database/database_types.h"

class ModernTableView;

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(QWidget* parent = nullptr);

    void setAttendanceService(service::AttendanceService* service);

private slots:
    void on_query_clicked();
    void on_export_clicked();
    void on_refresh_clicked();
    void on_date_changed(const QDate& date);

private:
    void setup_ui();
    void load_attendance_records(const std::string& date);
    void update_statistics(const std::string& date);
    void export_to_csv(const QString& filename);
    
    service::AttendanceService* attendance_service_;
    QDateEdit* date_edit_;
    QComboBox* user_combo_;
    QPushButton* query_btn_;
    QPushButton* export_btn_;
    QPushButton* refresh_btn_;
    ModernTableView* records_table_;
    QLabel* total_label_;
    QLabel* on_time_label_;
    QLabel* late_label_;
    QLabel* absent_label_;
    std::vector<db::AttendanceRecord> current_records_;
};


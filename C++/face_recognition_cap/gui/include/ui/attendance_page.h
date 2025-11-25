#pragma once

#include <QWidget>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVBoxLayout>
#include <vector>
#include <unordered_map>

#include "service/attendance_service.h"
#include "database/database_types.h"

class ModernTableView;
class CardWidget;

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(QWidget* parent = nullptr);

    void setAttendanceService(service::AttendanceService* service);

private slots:
    void on_query_clicked();
    void on_export_clicked();
    void on_refresh_clicked();
    void on_start_date_changed(const QDate& date);
    void on_end_date_changed(const QDate& date);
    void on_user_combo_changed(int index);
    void on_date_range_toggled(bool checked);

private:
    // UI Setup methods
    void setup_ui();
    CardWidget* create_filter_card();
    CardWidget* create_statistics_card();
    void create_records_table();
    
    // Data methods
    void load_attendance_records();
    void load_user_list();
    void update_statistics();
    void filter_records();
    void export_to_csv(const QString& filename);
    
    // Helper methods
    QString format_timestamp(time_t timestamp) const;
    QString get_check_type_text(int check_type) const;
    QString get_status_text(int status) const;
    
    // Service
    service::AttendanceService* attendance_service_;
    
    // Filter widgets
    QCheckBox* date_range_checkbox_;
    QDateEdit* start_date_edit_;
    QDateEdit* end_date_edit_;
    QComboBox* user_combo_;
    QPushButton* query_btn_;
    QPushButton* export_btn_;
    QPushButton* refresh_btn_;
    
    // Statistics widgets
    QLabel* total_label_;
    QLabel* check_in_label_;
    QLabel* check_out_label_;
    QLabel* late_label_;
    QLabel* early_leave_label_;
    
    // Table
    ModernTableView* records_table_;
    
    // Data
    std::vector<db::AttendanceRecord> all_records_;
    std::vector<db::AttendanceRecord> filtered_records_;
    std::unordered_map<int, std::string> user_id_to_name_;
    
    // Loading state
    bool is_loading_;
};


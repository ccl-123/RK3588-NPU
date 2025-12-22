#pragma once

#include <QWidget>
#include <QDateEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QRadioButton>
#include <vector>
#include <unordered_map>

#include "service/attendance_service.h"
#include "service/user_service.h"
#include "database/database_types.h"

class ModernTableView;
class CardWidget;

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(QWidget* parent = nullptr);

    void setAttendanceService(service::AttendanceService* service);
    void setUserService(service::UserService* service);

private slots:
    void on_query_clicked();
    void on_export_clicked();
    void on_refresh_clicked();
    void on_filter_mode_changed(int id);
    void on_date_changed();
    void on_user_combo_changed(int index);

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
    void batch_export_by_user();
    void export_specific_user();
    void write_csv(const std::vector<db::AttendanceRecord>& records, const QString& filename);
    
    // Helper methods
    QString format_timestamp(time_t timestamp) const;
    QString get_check_type_text(int check_type) const;
    QString get_status_text(int status) const;
    void update_ui_state();

    enum FilterMode {
        Mode_SingleDay = 0,
        Mode_DateRange = 1
    };
    
    // Service
    service::AttendanceService* attendance_service_;
    service::UserService* user_service_;
    
    // Filter widgets
    QButtonGroup* mode_group_;
    QRadioButton* radio_single_;
    QRadioButton* radio_range_;
    
    QDateEdit* start_date_edit_;
    QDateEdit* end_date_edit_;
    QLabel* range_separator_label_;
    
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
    
    // State
    FilterMode current_mode_;
    bool is_loading_;
};


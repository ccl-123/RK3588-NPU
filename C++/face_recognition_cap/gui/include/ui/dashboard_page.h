#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QVBoxLayout;
class QShowEvent;

namespace service {
class AttendanceService;
class UserService;
}  // namespace service

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(QWidget* parent = nullptr);

    void setAttendanceService(service::AttendanceService* service);
    void setUserService(service::UserService* service);

    void refreshData();

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void on_refresh_clicked();
    void on_range_changed(int index);
    void on_filter_changed(int index);

private:
    void setup_ui();

    service::AttendanceService* attendance_service_;
    service::UserService* user_service_;
    bool need_refresh_;

    QComboBox* range_combo_;
    QComboBox* dept_combo_;
    QLabel* last_sync_label_;
    QLabel* data_coverage_label_;

    QLabel* attendance_rate_label_;
    QLabel* attendance_detail_label_;
    QLabel* checkin_label_;
    QLabel* late_label_;
    QLabel* early_label_;
    QLabel* missing_label_;
    QLabel* similarity_label_;
    QLabel* abnormal_rate_label_;
    QLabel* checkout_label_;

    QWidget* trend_chart_;
    QWidget* donut_chart_;
    QWidget* bar_chart_;
    QVBoxLayout* insights_layout_;
    QVBoxLayout* alerts_layout_;
    QVBoxLayout* dept_rank_layout_;
};

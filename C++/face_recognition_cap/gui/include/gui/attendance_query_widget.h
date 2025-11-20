/**
 * @file attendance_query_widget.h
 * @brief 考勤查询组件类定义
 * @author CL
 * @date 2025-11-20
 *
 * 提供考勤记录查询、统计和导出功能的 Qt 组件。
 * 支持按日期和用户查询，支持导出为 CSV 格式。
 */

#ifndef ATTENDANCE_QUERY_WIDGET_H
#define ATTENDANCE_QUERY_WIDGET_H

#include <QWidget>
#include <QDateEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QComboBox>
#include <memory>

#include "service/attendance_service.h"
#include "database/database_types.h"

/**
 * @brief 考勤查询组件
 * 
 * 功能：
 * - 按日期查询考勤记录
 * - 按用户查询考勤记录
 * - 考勤统计
 * - 导出考勤数据（CSV）
 */
class AttendanceQueryWidget : public QWidget {
    Q_OBJECT

public:
    explicit AttendanceQueryWidget(service::AttendanceService* attendance_service,
                                  QWidget* parent = nullptr);
    ~AttendanceQueryWidget();

private slots:
    // 查询操作
    void on_query_clicked();
    void on_export_clicked();
    void on_refresh_clicked();
    
    // 日期改变
    void on_date_changed(const QDate& date);

private:
    // UI 初始化
    void setup_ui();
    
    // 加载考勤记录
    void load_attendance_records(const std::string& date);
    
    // 更新统计信息
    void update_statistics(const std::string& date);
    
    // 导出到 CSV
    void export_to_csv(const QString& filename);
    
    // 查询条件
    QDateEdit* date_edit_;
    QComboBox* user_combo_;
    QPushButton* query_btn_;
    QPushButton* export_btn_;
    QPushButton* refresh_btn_;
    
    // 考勤记录表格
    QTableWidget* records_table_;
    
    // 统计信息
    QLabel* total_label_;
    QLabel* on_time_label_;
    QLabel* late_label_;
    QLabel* absent_label_;
    
    // 系统组件
    service::AttendanceService* attendance_service_;
    
    // 当前查询结果
    std::vector<db::AttendanceRecord> current_records_;
};

#endif // ATTENDANCE_QUERY_WIDGET_H


/**
 * @file attendance_query_widget.cc
 * @brief 考勤查询组件类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/attendance_query_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <ctime>
#include <spdlog/spdlog.h>

AttendanceQueryWidget::AttendanceQueryWidget(service::AttendanceService* attendance_service,
                                           QWidget* parent)
    : QWidget(parent)
    , attendance_service_(attendance_service)
{
    setup_ui();
    
    // 加载今日考勤
    QDate today = QDate::currentDate();
    date_edit_->setDate(today);
    load_attendance_records(today.toString("yyyy-MM-dd").toStdString());
}

AttendanceQueryWidget::~AttendanceQueryWidget() {
}

void AttendanceQueryWidget::setup_ui() {
    setWindowTitle("考勤查询");
    resize(800, 600);
    
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    // 查询条件组
    QGroupBox* query_group = new QGroupBox("查询条件");
    QHBoxLayout* query_layout = new QHBoxLayout();
    
    query_layout->addWidget(new QLabel("日期:"));
    date_edit_ = new QDateEdit();
    date_edit_->setCalendarPopup(true);
    date_edit_->setDate(QDate::currentDate());
    connect(date_edit_, &QDateEdit::dateChanged, this, &AttendanceQueryWidget::on_date_changed);
    query_layout->addWidget(date_edit_);
    
    query_layout->addWidget(new QLabel("用户:"));
    user_combo_ = new QComboBox();
    user_combo_->addItem("全部");
    query_layout->addWidget(user_combo_);
    
    query_btn_ = new QPushButton("查询");
    connect(query_btn_, &QPushButton::clicked, this, &AttendanceQueryWidget::on_query_clicked);
    query_layout->addWidget(query_btn_);
    
    export_btn_ = new QPushButton("导出");
    connect(export_btn_, &QPushButton::clicked, this, &AttendanceQueryWidget::on_export_clicked);
    query_layout->addWidget(export_btn_);
    
    refresh_btn_ = new QPushButton("刷新");
    connect(refresh_btn_, &QPushButton::clicked, this, &AttendanceQueryWidget::on_refresh_clicked);
    query_layout->addWidget(refresh_btn_);

    query_layout->addStretch();

    QPushButton* close_btn = new QPushButton("关闭");
    connect(close_btn, &QPushButton::clicked, this, &QWidget::close);
    query_layout->addWidget(close_btn);

    query_group->setLayout(query_layout);
    main_layout->addWidget(query_group);
    
    // 统计信息组
    QGroupBox* stats_group = new QGroupBox("统计信息");
    QHBoxLayout* stats_layout = new QHBoxLayout();
    
    total_label_ = new QLabel("总人数: 0");
    on_time_label_ = new QLabel("正常: 0");
    late_label_ = new QLabel("迟到: 0");
    absent_label_ = new QLabel("缺勤: 0");
    
    stats_layout->addWidget(total_label_);
    stats_layout->addWidget(on_time_label_);
    stats_layout->addWidget(late_label_);
    stats_layout->addWidget(absent_label_);
    stats_layout->addStretch();
    
    stats_group->setLayout(stats_layout);
    main_layout->addWidget(stats_group);
    
    // 考勤记录表格
    records_table_ = new QTableWidget();
    records_table_->setColumnCount(6);
    records_table_->setHorizontalHeaderLabels({
        "记录ID", "姓名", "打卡时间", "类型", "状态", "相似度"
    });
    records_table_->horizontalHeader()->setStretchLastSection(true);
    records_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    records_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    main_layout->addWidget(records_table_);
}

void AttendanceQueryWidget::load_attendance_records(const std::string& date) {
    if (!attendance_service_) {
        return;
    }
    
    // 清空表格
    records_table_->setRowCount(0);
    
    // 查询考勤记录
    current_records_ = attendance_service_->query_records_by_date(date);
    
    // 填充表格
    for (const auto& record : current_records_) {
        int row = records_table_->rowCount();
        records_table_->insertRow(row);

        // 转换时间戳为字符串
        char time_str[64];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&record.check_time));

        records_table_->setItem(row, 0, new QTableWidgetItem(QString::number(record.record_id)));
        records_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(record.user_name)));
        records_table_->setItem(row, 2, new QTableWidgetItem(QString::fromLatin1(time_str)));
        records_table_->setItem(row, 3, new QTableWidgetItem(record.check_type == 1 ? "签到" : "签退"));

        QString status_text;
        if (record.status == 1) status_text = "正常";
        else if (record.status == 2) status_text = "迟到";
        else if (record.status == 3) status_text = "早退";
        else status_text = "异常";
        records_table_->setItem(row, 4, new QTableWidgetItem(status_text));

        records_table_->setItem(row, 5, new QTableWidgetItem(QString::number(record.similarity, 'f', 2)));
    }
    
    // 更新统计信息
    update_statistics(date);
    
    spdlog::info("Loaded {} attendance records for date: {}", current_records_.size(), date);
}

void AttendanceQueryWidget::update_statistics(const std::string& date) {
    if (!attendance_service_) {
        return;
    }

    auto stats = attendance_service_->get_statistics(date);

    total_label_->setText(QString("总人数: %1").arg(stats.total_count));
    on_time_label_->setText(QString("签到: %1").arg(stats.check_in_count));
    late_label_->setText(QString("迟到: %1").arg(stats.late_count));
    absent_label_->setText(QString("签退: %1").arg(stats.check_out_count));
}

void AttendanceQueryWidget::export_to_csv(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法创建文件");
        return;
    }
    
    QTextStream out(&file);
    out.setCodec("UTF-8");
    
    // 写入表头
    out << "记录ID,姓名,打卡时间,类型,状态,相似度\n";
    
    // 写入数据
    for (const auto& record : current_records_) {
        // 转换时间戳为字符串
        char time_str[64];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&record.check_time));

        QString status_text;
        if (record.status == 1) status_text = "正常";
        else if (record.status == 2) status_text = "迟到";
        else if (record.status == 3) status_text = "早退";
        else status_text = "异常";

        out << record.record_id << ","
            << QString::fromStdString(record.user_name) << ","
            << QString::fromLatin1(time_str) << ","
            << (record.check_type == 1 ? "签到" : "签退") << ","
            << status_text << ","
            << QString::number(record.similarity, 'f', 2) << "\n";
    }
    
    file.close();
    QMessageBox::information(this, "成功", "导出成功！");
}

void AttendanceQueryWidget::on_query_clicked() {
    QString date = date_edit_->date().toString("yyyy-MM-dd");
    load_attendance_records(date.toStdString());
}

void AttendanceQueryWidget::on_export_clicked() {
    QString filename = QFileDialog::getSaveFileName(this, "导出考勤记录", 
        "", "CSV 文件 (*.csv)");
    if (!filename.isEmpty()) {
        export_to_csv(filename);
    }
}

void AttendanceQueryWidget::on_refresh_clicked() {
    on_query_clicked();
}

void AttendanceQueryWidget::on_date_changed(const QDate& date) {
    // 日期改变时自动查询
    load_attendance_records(date.toString("yyyy-MM-dd").toStdString());
}


#include "ui/attendance_page.h"

#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QVariant>
#include <ctime>
#include <utility>
#include <spdlog/spdlog.h>

AttendancePage::AttendancePage(QWidget* parent)
    : QWidget(parent)
    , attendance_service_(nullptr)
    , date_edit_(nullptr)
    , user_combo_(nullptr)
    , query_btn_(nullptr)
    , export_btn_(nullptr)
    , refresh_btn_(nullptr)
    , records_table_(nullptr)
    , total_label_(nullptr)
    , on_time_label_(nullptr)
    , late_label_(nullptr)
    , absent_label_(nullptr) {
    setup_ui();
}

void AttendancePage::setAttendanceService(service::AttendanceService* service) {
    attendance_service_ = service;
    if (attendance_service_ && date_edit_) {
        QDate today = QDate::currentDate();
        date_edit_->setDate(today);
        load_attendance_records(today.toString("yyyy-MM-dd").toStdString());
    }
}

void AttendancePage::setup_ui() {
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(32, 24, 32, 24);
    main_layout->setSpacing(24);

    auto filter_card = new CardWidget(this);
    filter_card->setTitle(tr("筛选条件"));
    auto filter_layout = new QHBoxLayout(filter_card->bodyContainer());
    filter_layout->setContentsMargins(0, 0, 0, 0);
    filter_layout->setSpacing(12);

    auto date_layout = new QVBoxLayout();
    auto date_label = new QLabel(tr("日期"));
    date_label->setObjectName("Caption");
    date_layout->addWidget(date_label);
    date_edit_ = new QDateEdit(filter_card);
    date_edit_->setCalendarPopup(true);
    date_edit_->setDate(QDate::currentDate());
    connect(date_edit_, &QDateEdit::dateChanged, this, &AttendancePage::on_date_changed);
    date_layout->addWidget(date_edit_);
    filter_layout->addLayout(date_layout);

    auto user_layout = new QVBoxLayout();
    auto user_label = new QLabel(tr("用户"));
    user_label->setObjectName("Caption");
    user_layout->addWidget(user_label);
    user_combo_ = new QComboBox(filter_card);
    user_combo_->addItem(tr("全部"));
    user_layout->addWidget(user_combo_);
    filter_layout->addLayout(user_layout);

    filter_layout->addStretch();
    
    query_btn_ = new QPushButton(tr("查询"), filter_card);
    query_btn_->setProperty("primary", QVariant(true));
    connect(query_btn_, &QPushButton::clicked, this, &AttendancePage::on_query_clicked);
    export_btn_ = new QPushButton(tr("导出"), filter_card);
    connect(export_btn_, &QPushButton::clicked, this, &AttendancePage::on_export_clicked);
    refresh_btn_ = new QPushButton(tr("刷新"), filter_card);
    connect(refresh_btn_, &QPushButton::clicked, this, &AttendancePage::on_refresh_clicked);

    filter_layout->addWidget(query_btn_);
    filter_layout->addWidget(export_btn_);
    filter_layout->addWidget(refresh_btn_);

    auto stats_card = new CardWidget(this);
    stats_card->setTitle(tr("统计概览"));
    auto stats_layout = new QGridLayout(stats_card->bodyContainer());
    stats_layout->setContentsMargins(0, 0, 0, 0);
    stats_layout->setHorizontalSpacing(24);
    stats_layout->setVerticalSpacing(8);

    auto make_stat = [](const QString& title) {
        auto wrapper = new QVBoxLayout();
        wrapper->setSpacing(4);
        auto caption = new QLabel(title);
        caption->setObjectName("Caption");
        auto value = new QLabel("0");
        value->setStyleSheet("font-size: 24px; font-weight: 600;");
        wrapper->addWidget(caption);
        wrapper->addWidget(value);
        return std::make_pair(wrapper, value);
    };

    auto total_stat = make_stat(tr("总记录"));
    total_label_ = total_stat.second;
    stats_layout->addLayout(total_stat.first, 0, 0);

    auto on_time_stat = make_stat(tr("签到"));
    on_time_label_ = on_time_stat.second;
    stats_layout->addLayout(on_time_stat.first, 0, 1);

    auto late_stat = make_stat(tr("迟到"));
    late_label_ = late_stat.second;
    stats_layout->addLayout(late_stat.first, 0, 2);

    auto absent_stat = make_stat(tr("签退"));
    absent_label_ = absent_stat.second;
    stats_layout->addLayout(absent_stat.first, 0, 3);

    records_table_ = new ModernTableView(this);
    records_table_->setColumnCount(6);
    records_table_->setHorizontalHeaderLabels({
        tr("记录ID"), tr("姓名"), tr("打卡时间"), tr("类型"), tr("状态"), tr("相似度")
    });
    records_table_->horizontalHeader()->setStretchLastSection(true);
    records_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    records_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    records_table_->setEmptyText(tr("暂无考勤记录"));
    
    main_layout->addWidget(filter_card);
    main_layout->addWidget(stats_card);
    main_layout->addWidget(records_table_, 1);
}

void AttendancePage::load_attendance_records(const std::string& date) {
    if (!attendance_service_ || !records_table_) {
        return;
    }
    
    records_table_->setRowCount(0);
    current_records_ = attendance_service_->query_records_by_date(date);
    
    for (const auto& record : current_records_) {
        int row = records_table_->rowCount();
        records_table_->insertRow(row);

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
    
    update_statistics(date);
    spdlog::info("Loaded {} attendance records for date: {}", current_records_.size(), date);
}

void AttendancePage::update_statistics(const std::string& date) {
    if (!attendance_service_) {
        return;
    }

    auto stats = attendance_service_->get_statistics(date);

    if (total_label_) total_label_->setText(QString::number(stats.total_count));
    if (on_time_label_) on_time_label_->setText(QString::number(stats.check_in_count));
    if (late_label_) late_label_->setText(QString::number(stats.late_count));
    if (absent_label_) absent_label_->setText(QString::number(stats.check_out_count));
}

void AttendancePage::export_to_csv(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "错误", "无法创建文件");
        return;
    }
    
    QTextStream out(&file);
    out.setCodec("UTF-8");
    
    out << "记录ID,姓名,打卡时间,类型,状态,相似度\n";
    
    for (const auto& record : current_records_) {
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

void AttendancePage::on_query_clicked() {
    if (date_edit_) {
        QString date = date_edit_->date().toString("yyyy-MM-dd");
        load_attendance_records(date.toStdString());
    }
}

void AttendancePage::on_export_clicked() {
    QString filename = QFileDialog::getSaveFileName(this, "导出考勤记录", 
        "", "CSV 文件 (*.csv)");
    if (!filename.isEmpty()) {
        export_to_csv(filename);
    }
}

void AttendancePage::on_refresh_clicked() {
    on_query_clicked();
}

void AttendancePage::on_date_changed(const QDate& date) {
    load_attendance_records(date.toString("yyyy-MM-dd").toStdString());
}


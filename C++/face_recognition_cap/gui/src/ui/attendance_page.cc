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
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QBrush>
#include <ctime>
#include <spdlog/spdlog.h>

AttendancePage::AttendancePage(QWidget* parent)
    : QWidget(parent)
    , attendance_service_(nullptr)
    , date_range_checkbox_(nullptr)
    , start_date_edit_(nullptr)
    , end_date_edit_(nullptr)
    , user_combo_(nullptr)
    , query_btn_(nullptr)
    , export_btn_(nullptr)
    , refresh_btn_(nullptr)
    , records_table_(nullptr)
    , total_label_(nullptr)
    , check_in_label_(nullptr)
    , check_out_label_(nullptr)
    , late_label_(nullptr)
    , is_loading_(false) {
    setup_ui();
}

void AttendancePage::setAttendanceService(service::AttendanceService* service) {
    attendance_service_ = service;
    if (attendance_service_) {
        load_user_list();
        if (start_date_edit_) {
            QDate today = QDate::currentDate();
            start_date_edit_->setDate(today);
            end_date_edit_->setDate(today);
            load_attendance_records();
        }
    }
}

void AttendancePage::setup_ui() {
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(32, 24, 32, 24);
    main_layout->setSpacing(24);

    // Create UI components
    auto filter_card = create_filter_card();
    auto statistics_card = create_statistics_card();
    create_records_table();

    main_layout->addWidget(filter_card);
    main_layout->addWidget(statistics_card);
    main_layout->addWidget(records_table_, 1);
}

CardWidget* AttendancePage::create_filter_card() {
    auto filter_card = new CardWidget(this);
    filter_card->setTitle(tr("筛选条件"));
    
    auto filter_layout = new QVBoxLayout(filter_card->bodyContainer());
    filter_layout->setContentsMargins(0, 0, 0, 0);
    filter_layout->setSpacing(16);

    // First row: Date filters
    auto date_row = new QHBoxLayout();
    date_row->setSpacing(12);

    // Date range checkbox
    date_range_checkbox_ = new QCheckBox(tr("日期范围查询"), filter_card);
    connect(date_range_checkbox_, &QCheckBox::toggled, this, &AttendancePage::on_date_range_toggled);
    date_row->addWidget(date_range_checkbox_);

    // Start date
    auto start_date_layout = new QVBoxLayout();
    auto start_date_label = new QLabel(tr("开始日期"));
    start_date_label->setObjectName("Caption");
    start_date_layout->addWidget(start_date_label);
    start_date_edit_ = new QDateEdit(filter_card);
    start_date_edit_->setCalendarPopup(true);
    start_date_edit_->setDate(QDate::currentDate());
    start_date_edit_->setDisplayFormat("yyyy-MM-dd");
    connect(start_date_edit_, &QDateEdit::dateChanged, this, &AttendancePage::on_start_date_changed);
    start_date_layout->addWidget(start_date_edit_);
    date_row->addLayout(start_date_layout);

    // End date
    auto end_date_layout = new QVBoxLayout();
    auto end_date_label = new QLabel(tr("结束日期"));
    end_date_label->setObjectName("Caption");
    end_date_layout->addWidget(end_date_label);
    end_date_edit_ = new QDateEdit(filter_card);
    end_date_edit_->setCalendarPopup(true);
    end_date_edit_->setDate(QDate::currentDate());
    end_date_edit_->setDisplayFormat("yyyy-MM-dd");
    end_date_edit_->setEnabled(false);
    connect(end_date_edit_, &QDateEdit::dateChanged, this, &AttendancePage::on_end_date_changed);
    end_date_layout->addWidget(end_date_edit_);
    date_row->addLayout(end_date_layout);

    // User filter
    auto user_layout = new QVBoxLayout();
    auto user_label = new QLabel(tr("用户筛选"));
    user_label->setObjectName("Caption");
    user_layout->addWidget(user_label);
    user_combo_ = new QComboBox(filter_card);
    user_combo_->addItem(tr("全部用户"), -1);
    user_combo_->setMinimumWidth(180);
    connect(user_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &AttendancePage::on_user_combo_changed);
    user_layout->addWidget(user_combo_);
    date_row->addLayout(user_layout);

    date_row->addStretch();

    filter_layout->addLayout(date_row);

    // Second row: Action buttons
    auto button_row = new QHBoxLayout();
    button_row->setSpacing(12);

    query_btn_ = new QPushButton(tr("查询"), filter_card);
    query_btn_->setProperty("primary", QVariant(true));
    query_btn_->setMinimumWidth(100);
    connect(query_btn_, &QPushButton::clicked, this, &AttendancePage::on_query_clicked);
    
    refresh_btn_ = new QPushButton(tr("刷新"), filter_card);
    refresh_btn_->setMinimumWidth(100);
    connect(refresh_btn_, &QPushButton::clicked, this, &AttendancePage::on_refresh_clicked);
    
    export_btn_ = new QPushButton(tr("导出 CSV"), filter_card);
    export_btn_->setMinimumWidth(100);
    connect(export_btn_, &QPushButton::clicked, this, &AttendancePage::on_export_clicked);

    button_row->addStretch();
    button_row->addWidget(query_btn_);
    button_row->addWidget(refresh_btn_);
    button_row->addWidget(export_btn_);

    filter_layout->addLayout(button_row);

    return filter_card;
}

CardWidget* AttendancePage::create_statistics_card() {
    auto stats_card = new CardWidget(this);
    stats_card->setTitle(tr("统计概览"));
    
    auto stats_layout = new QGridLayout(stats_card->bodyContainer());
    stats_layout->setContentsMargins(0, 0, 0, 0);
    stats_layout->setHorizontalSpacing(32);
    stats_layout->setVerticalSpacing(12);

    // Helper function to create stat item with icon
    auto make_stat = [](const QString& title, const QString& icon_text, const QString& color) {
        auto wrapper = new QVBoxLayout();
        wrapper->setSpacing(8);
        
        auto header_row = new QHBoxLayout();
        header_row->setSpacing(8);
        
        // Icon
        auto icon = new QLabel(icon_text);
        icon->setStyleSheet(QString("font-size: 20px; color: %1;").arg(color));
        header_row->addWidget(icon);
        
        // Caption
        auto caption = new QLabel(title);
        caption->setObjectName("Caption");
        header_row->addWidget(caption);
        header_row->addStretch();
        
        wrapper->addLayout(header_row);
        
        // Value
        auto value = new QLabel("0");
        value->setStyleSheet(QString("font-size: 28px; font-weight: 700; color: %1;").arg(color));
        wrapper->addWidget(value);
        
        return std::make_pair(wrapper, value);
    };

    // Total records - Blue
    auto total_stat = make_stat(tr("总记录"), "📊", "#1677ff");
    total_label_ = total_stat.second;
    stats_layout->addLayout(total_stat.first, 0, 0);

    // Check-in - Green
    auto check_in_stat = make_stat(tr("签到次数"), "✅", "#52c41a");
    check_in_label_ = check_in_stat.second;
    stats_layout->addLayout(check_in_stat.first, 0, 1);

    // Check-out - Purple
    auto check_out_stat = make_stat(tr("签退次数"), "👋", "#722ed1");
    check_out_label_ = check_out_stat.second;
    stats_layout->addLayout(check_out_stat.first, 0, 2);

    // Late - Orange
    auto late_stat = make_stat(tr("迟到次数"), "⚠️", "#fa8c16");
    late_label_ = late_stat.second;
    stats_layout->addLayout(late_stat.first, 0, 3);

    return stats_card;
}

void AttendancePage::create_records_table() {
    records_table_ = new ModernTableView(this);
    records_table_->setColumnCount(6);
    records_table_->setHorizontalHeaderLabels({
        tr("记录ID"), tr("姓名"), tr("打卡时间"), tr("类型"), tr("状态"), tr("相似度")
    });
    
    // Set column widths
    records_table_->setColumnWidth(0, 100);  // 记录ID
    records_table_->setColumnWidth(1, 120);  // 姓名
    records_table_->setColumnWidth(2, 180);  // 打卡时间
    records_table_->setColumnWidth(3, 100);  // 类型
    records_table_->setColumnWidth(4, 120);  // 状态（增加宽度）
    records_table_->setColumnWidth(5, 100);  // 相似度
    
    records_table_->horizontalHeader()->setStretchLastSection(true);
    records_table_->verticalHeader()->setDefaultSectionSize(48);  // 设置行高
    records_table_->verticalHeader()->setMinimumSectionSize(48);
    records_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    records_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    records_table_->setAlternatingRowColors(true);
    records_table_->setEmptyText(tr("暂无考勤记录"));
}

void AttendancePage::load_attendance_records() {
    if (!attendance_service_ || !records_table_) {
        return;
    }
    
    if (is_loading_) {
        return;
    }
    
    is_loading_ = true;
    
    // Disable controls during loading
    if (query_btn_) query_btn_->setEnabled(false);
    if (refresh_btn_) refresh_btn_->setEnabled(false);
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    try {
        all_records_.clear();
        
        bool use_date_range = date_range_checkbox_ && date_range_checkbox_->isChecked();
        
        if (use_date_range && start_date_edit_ && end_date_edit_) {
            // Date range query
            QString start_date = start_date_edit_->date().toString("yyyy-MM-dd");
            QString end_date = end_date_edit_->date().toString("yyyy-MM-dd");
            
            // Query each day in the range
            QDate current = start_date_edit_->date();
            QDate end = end_date_edit_->date();
            
            while (current <= end) {
                auto daily_records = attendance_service_->query_records_by_date(
                    current.toString("yyyy-MM-dd").toStdString()
                );
                all_records_.insert(all_records_.end(), daily_records.begin(), daily_records.end());
                current = current.addDays(1);
            }
            
            spdlog::info("Loaded {} attendance records from {} to {}", 
                        all_records_.size(), start_date.toStdString(), end_date.toStdString());
        } else if (start_date_edit_) {
            // Single day query
            QString date = start_date_edit_->date().toString("yyyy-MM-dd");
            all_records_ = attendance_service_->query_records_by_date(date.toStdString());
            
            spdlog::info("Loaded {} attendance records for date: {}", 
                        all_records_.size(), date.toStdString());
        }
        
        filter_records();
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load attendance records: {}", e.what());
        QMessageBox::warning(this, tr("错误"), tr("加载考勤记录失败：%1").arg(e.what()));
    }
    
    QApplication::restoreOverrideCursor();
    
    // Re-enable controls
    if (query_btn_) query_btn_->setEnabled(true);
    if (refresh_btn_) refresh_btn_->setEnabled(true);
    
    is_loading_ = false;
}

void AttendancePage::load_user_list() {
    if (!attendance_service_ || !user_combo_) {
        return;
    }
    
    try {
        // Get all users from attendance service (you may need to add this method)
        // For now, we'll extract unique users from records
        user_id_to_name_.clear();
        
        // Clear existing items except "All Users"
        while (user_combo_->count() > 1) {
            user_combo_->removeItem(1);
        }
        
        spdlog::info("User list loaded");
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load user list: {}", e.what());
    }
}

void AttendancePage::filter_records() {
    if (!records_table_) {
        return;
    }
    
    filtered_records_.clear();
    
    int selected_user_id = -1;
    if (user_combo_ && user_combo_->currentIndex() > 0) {
        selected_user_id = user_combo_->currentData().toInt();
    }
    
    // Filter by user if selected
    for (const auto& record : all_records_) {
        if (selected_user_id == -1 || record.user_id == selected_user_id) {
            filtered_records_.push_back(record);
        }
    }
    
    // Update table
    records_table_->setRowCount(0);
    
    for (const auto& record : filtered_records_) {
        int row = records_table_->rowCount();
        records_table_->insertRow(row);

        // Record ID
        auto id_item = new QTableWidgetItem(QString::number(record.record_id));
        id_item->setTextAlignment(Qt::AlignCenter);
        records_table_->setItem(row, 0, id_item);

        // Name
        auto name_item = new QTableWidgetItem(QString::fromStdString(record.user_name));
        records_table_->setItem(row, 1, name_item);
        
        // Update user map
        if (user_id_to_name_.find(record.user_id) == user_id_to_name_.end()) {
            user_id_to_name_[record.user_id] = record.user_name;
            if (user_combo_ && user_combo_->findData(record.user_id) == -1) {
                user_combo_->addItem(QString::fromStdString(record.user_name), record.user_id);
            }
        }

        // Check time
        auto time_item = new QTableWidgetItem(format_timestamp(record.check_time));
        time_item->setTextAlignment(Qt::AlignCenter);
        records_table_->setItem(row, 2, time_item);

        // Check type - with icon and color
        QString type_text = get_check_type_text(record.check_type);
        QString type_with_icon;
        QColor type_color;
        QColor type_bg_color;
        
        if (record.check_type == 1) {  // Check-in
            type_with_icon = "✅ " + type_text;
            type_color = QColor("#237804");
            type_bg_color = QColor("#f6ffed");
        } else if (record.check_type == 2) {  // Check-out
            type_with_icon = "👋 " + type_text;
            type_color = QColor("#531dab");
            type_bg_color = QColor("#f9f0ff");
        } else {
            type_with_icon = type_text;
            type_color = QColor("#595959");
            type_bg_color = QColor("#fafafa");
        }
        
        auto type_item = new QTableWidgetItem(type_with_icon);
        type_item->setTextAlignment(Qt::AlignCenter);
        
        QFont type_font = type_item->font();
        type_font.setBold(true);
        type_font.setPointSize(11);
        type_item->setFont(type_font);
        type_item->setForeground(type_color);
        type_item->setBackground(type_bg_color);
        
        records_table_->setItem(row, 3, type_item);

        // Status - Use colored item with background
        auto status_item = new QTableWidgetItem(get_status_text(record.status));
        status_item->setTextAlignment(Qt::AlignCenter);
        
        // Set background color and text color based on status
        QFont status_font = status_item->font();
        status_font.setBold(true);
        status_font.setPointSize(11);
        status_item->setFont(status_font);
        
        switch (record.status) {
            case 1:  // Normal
                status_item->setBackground(QColor("#f6ffed"));
                status_item->setForeground(QColor("#237804"));
                break;
            case 2:  // Late
                status_item->setBackground(QColor("#fff7e6"));
                status_item->setForeground(QColor("#ad4e00"));
                break;
            case 3:  // Early leave
                status_item->setBackground(QColor("#fff7e6"));
                status_item->setForeground(QColor("#ad4e00"));
                break;
            default:  // Abnormal
                status_item->setBackground(QColor("#fff1f0"));
                status_item->setForeground(QColor("#a8071a"));
                break;
        }
        
        records_table_->setItem(row, 4, status_item);

        // Similarity - Color coded based on value
        double similarity_percent = record.similarity * 100;
        auto similarity_item = new QTableWidgetItem(
            QString::number(similarity_percent, 'f', 1) + "%"
        );
        similarity_item->setTextAlignment(Qt::AlignCenter);
        
        // Set color based on similarity value
        QFont sim_font = similarity_item->font();
        sim_font.setBold(true);
        sim_font.setPointSize(11);
        similarity_item->setFont(sim_font);
        
        if (similarity_percent >= 85.0) {
            // High similarity - green
            similarity_item->setForeground(QColor("#237804"));
            similarity_item->setBackground(QColor("#f6ffed"));
        } else if (similarity_percent >= 70.0) {
            // Medium similarity - orange
            similarity_item->setForeground(QColor("#ad4e00"));
            similarity_item->setBackground(QColor("#fff7e6"));
        } else {
            // Low similarity - red
            similarity_item->setForeground(QColor("#a8071a"));
            similarity_item->setBackground(QColor("#fff1f0"));
        }
        
        records_table_->setItem(row, 5, similarity_item);
    }
    
    update_statistics();
}

void AttendancePage::update_statistics() {
    if (filtered_records_.empty()) {
        if (total_label_) total_label_->setText("0");
        if (check_in_label_) check_in_label_->setText("0");
        if (check_out_label_) check_out_label_->setText("0");
        if (late_label_) late_label_->setText("0");
        return;
    }
    
    int total = filtered_records_.size();
    int check_in_count = 0;
    int check_out_count = 0;
    int late_count = 0;
    
    for (const auto& record : filtered_records_) {
        if (record.check_type == 1) {
            check_in_count++;
        } else if (record.check_type == 2) {
            check_out_count++;
        }
        
        if (record.status == 2) {  // Late
            late_count++;
        }
    }
    
    if (total_label_) total_label_->setText(QString::number(total));
    if (check_in_label_) check_in_label_->setText(QString::number(check_in_count));
    if (check_out_label_) check_out_label_->setText(QString::number(check_out_count));
    if (late_label_) late_label_->setText(QString::number(late_count));
}

void AttendancePage::export_to_csv(const QString& filename) {
    if (filtered_records_.empty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可导出的记录"));
        return;
    }
    
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("错误"), tr("无法创建文件：%1").arg(file.errorString()));
        return;
    }
    
    try {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        
        // Write BOM for Excel compatibility
        out << "\xEF\xBB\xBF";
        
        // Header
        out << "记录ID,用户ID,姓名,打卡时间,类型,状态,相似度\n";
        
        // Data
        for (const auto& record : filtered_records_) {
            out << record.record_id << ","
                << record.user_id << ","
                << QString::fromStdString(record.user_name) << ","
                << format_timestamp(record.check_time) << ","
                << get_check_type_text(record.check_type) << ","
                << get_status_text(record.status) << ","
                << QString::number(record.similarity * 100, 'f', 2) << "%\n";
        }
        
        file.close();
        
        QMessageBox::information(this, tr("成功"), 
            tr("成功导出 %1 条记录到：\n%2").arg(filtered_records_.size()).arg(filename));
        
        spdlog::info("Exported {} records to: {}", filtered_records_.size(), filename.toStdString());
        
    } catch (const std::exception& e) {
        file.close();
        spdlog::error("Failed to export CSV: {}", e.what());
        QMessageBox::warning(this, tr("错误"), tr("导出失败：%1").arg(e.what()));
    }
}

// Helper methods
QString AttendancePage::format_timestamp(time_t timestamp) const {
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", std::localtime(&timestamp));
    return QString::fromLatin1(time_str);
}

QString AttendancePage::get_check_type_text(int check_type) const {
    switch (check_type) {
        case 1: return tr("签到");
        case 2: return tr("签退");
        default: return tr("未知");
    }
}

QString AttendancePage::get_status_text(int status) const {
    switch (status) {
        case 1: return tr("正常");
        case 2: return tr("迟到");
        case 3: return tr("早退");
        default: return tr("异常");
    }
}


// Slots
void AttendancePage::on_query_clicked() {
    load_attendance_records();
}

void AttendancePage::on_export_clicked() {
    QString default_filename = QString("attendance_%1.csv")
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    
    QString filename = QFileDialog::getSaveFileName(
        this, 
        tr("导出考勤记录"), 
        default_filename,
        tr("CSV 文件 (*.csv)")
    );
    
    if (!filename.isEmpty()) {
        export_to_csv(filename);
    }
}

void AttendancePage::on_refresh_clicked() {
    load_attendance_records();
}

void AttendancePage::on_start_date_changed(const QDate& date) {
    // Ensure end date is not before start date
    if (end_date_edit_ && end_date_edit_->date() < date) {
        end_date_edit_->setDate(date);
    }
}

void AttendancePage::on_end_date_changed(const QDate& date) {
    // Ensure end date is not before start date
    if (start_date_edit_ && date < start_date_edit_->date()) {
        end_date_edit_->setDate(start_date_edit_->date());
    }
}

void AttendancePage::on_user_combo_changed(int index) {
    filter_records();
}

void AttendancePage::on_date_range_toggled(bool checked) {
    if (end_date_edit_) {
        end_date_edit_->setEnabled(checked);
    }
}

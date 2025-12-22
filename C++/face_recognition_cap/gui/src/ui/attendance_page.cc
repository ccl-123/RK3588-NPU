#include "ui/attendance_page.h"

#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "utils/svg_icon_manager.h"

#include <QMenu>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QDir>
#include <QMessageBox>
#include <QInputDialog>
#include <QTextStream>
#include <QVariant>
#include <QTableWidgetItem>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QBrush>
#include <ctime>
#include <spdlog/spdlog.h>

AttendancePage::AttendancePage(QWidget* parent)
    : QWidget(parent)
    , attendance_service_(nullptr)
    , user_service_(nullptr)
    , mode_group_(nullptr)
    , radio_single_(nullptr)
    , radio_range_(nullptr)
    , start_date_edit_(nullptr)
    , end_date_edit_(nullptr)
    , range_separator_label_(nullptr)
    , user_combo_(nullptr)
    , query_btn_(nullptr)
    , export_btn_(nullptr)
    , refresh_btn_(nullptr)
    , records_table_(nullptr)
    , total_label_(nullptr)
    , check_in_label_(nullptr)
    , check_out_label_(nullptr)
    , late_label_(nullptr)
    , early_leave_label_(nullptr)
    , current_mode_(Mode_SingleDay)
    , is_loading_(false) {
    setup_ui();
}

void AttendancePage::setAttendanceService(service::AttendanceService* service) {
    attendance_service_ = service;
    if (attendance_service_) {
        // Default to today
        QDate today = QDate::currentDate();
        if (start_date_edit_) start_date_edit_->setDate(today);
        if (end_date_edit_) end_date_edit_->setDate(today);

        load_attendance_records();
    }
}

void AttendancePage::setUserService(service::UserService* service) {
    user_service_ = service;
    if (user_service_) {
        load_user_list();
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
    
    // Initialize state
    update_ui_state();
}

CardWidget* AttendancePage::create_filter_card() {
    auto filter_card = new CardWidget(this);
    filter_card->setTitle(tr("筛选条件"));
    
    auto filter_layout = new QVBoxLayout(filter_card->bodyContainer());
    filter_layout->setContentsMargins(0, 0, 0, 0);
    filter_layout->setSpacing(16);

    // --- Mode Selection Row ---
    auto mode_layout = new QHBoxLayout();
    mode_layout->setSpacing(24);
    
    auto mode_label = new QLabel(tr("查询模式:"));
    mode_label->setObjectName("Caption");
    mode_layout->addWidget(mode_label);

    mode_group_ = new QButtonGroup(this);
    radio_single_ = new QRadioButton(tr("按单日查询"));
    radio_range_ = new QRadioButton(tr("按日期范围查询"));
    
    mode_group_->addButton(radio_single_, Mode_SingleDay);
    mode_group_->addButton(radio_range_, Mode_DateRange);
    radio_single_->setChecked(true); // Default
    
    connect(mode_group_, &QButtonGroup::idClicked, 
            this, &AttendancePage::on_filter_mode_changed);

    mode_layout->addWidget(radio_single_);
    mode_layout->addWidget(radio_range_);
    mode_layout->addStretch();
    filter_layout->addLayout(mode_layout);

    // --- Date & User Filter Row ---
    auto filter_row = new QHBoxLayout();
    filter_row->setSpacing(16);

    // Date Picker Area
    auto date_picker_layout = new QHBoxLayout();
    date_picker_layout->setSpacing(8);
    
    start_date_edit_ = new QDateEdit(filter_card);
    start_date_edit_->setCalendarPopup(true);
    start_date_edit_->setDisplayFormat("yyyy-MM-dd");
    start_date_edit_->setMinimumWidth(120);
    connect(start_date_edit_, &QDateEdit::dateChanged, this, &AttendancePage::on_date_changed);
    
    range_separator_label_ = new QLabel(tr("至"));
    range_separator_label_->setAlignment(Qt::AlignCenter);
    range_separator_label_->setFixedWidth(20);
    
    end_date_edit_ = new QDateEdit(filter_card);
    end_date_edit_->setCalendarPopup(true);
    end_date_edit_->setDisplayFormat("yyyy-MM-dd");
    end_date_edit_->setMinimumWidth(120);
    connect(end_date_edit_, &QDateEdit::dateChanged, this, &AttendancePage::on_date_changed);

    date_picker_layout->addWidget(start_date_edit_);
    date_picker_layout->addWidget(range_separator_label_);
    date_picker_layout->addWidget(end_date_edit_);
    
    filter_row->addLayout(date_picker_layout);
    
    // Spacer
    filter_row->addSpacing(24);

    // User Filter
    auto user_label = new QLabel(tr("用户:"));
    user_label->setObjectName("Caption");
    filter_row->addWidget(user_label);
    
    user_combo_ = new QComboBox(filter_card);
    user_combo_->addItem(tr("全部用户"), -1);
    user_combo_->setMinimumWidth(150);
    connect(user_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &AttendancePage::on_user_combo_changed);
    filter_row->addWidget(user_combo_);

    filter_row->addStretch();

    // Action Buttons
    query_btn_ = new QPushButton(tr("查询"), filter_card);
    query_btn_->setProperty("primary", QVariant(true));
    query_btn_->setMinimumWidth(90);
    connect(query_btn_, &QPushButton::clicked, this, &AttendancePage::on_query_clicked);
    
    refresh_btn_ = new QPushButton(tr("刷新"), filter_card);
    refresh_btn_->setMinimumWidth(90);
    connect(refresh_btn_, &QPushButton::clicked, this, &AttendancePage::on_refresh_clicked);
    
    export_btn_ = new QPushButton(tr("导出"), filter_card);
    export_btn_->setMinimumWidth(90);
    
    QMenu* export_menu = new QMenu(export_btn_);
    export_menu->addAction(tr("导出当前列表"), this, &AttendancePage::on_export_clicked);
    export_menu->addAction(tr("导出指定人员"), this, &AttendancePage::export_specific_user);
    export_menu->addSeparator();
    export_menu->addAction(tr("按人员批量导出"), this, &AttendancePage::batch_export_by_user);
    export_btn_->setMenu(export_menu);

    filter_row->addWidget(query_btn_);
    filter_row->addWidget(refresh_btn_);
    filter_row->addWidget(export_btn_);

    filter_layout->addLayout(filter_row);

    return filter_card;
}

CardWidget* AttendancePage::create_statistics_card() {
    auto stats_card = new CardWidget(this);
    stats_card->setTitle(tr("统计概览"));
    
    auto stats_layout = new QGridLayout(stats_card->bodyContainer());
    stats_layout->setContentsMargins(0, 0, 0, 0);
    stats_layout->setHorizontalSpacing(32);
    stats_layout->setVerticalSpacing(12);

    auto make_stat = [](const QString& title, const QString& icon_path, const QString& color) {
        auto wrapper = new QVBoxLayout();
        wrapper->setSpacing(8);
        
        auto header_row = new QHBoxLayout();
        header_row->setSpacing(8);
        
        auto icon = new QLabel();
        icon->setPixmap(SvgIconManager::icon(icon_path, QSize(20, 20), QColor(color)).pixmap(20, 20));
        icon->setFixedSize(20, 20);
        header_row->addWidget(icon);
        
        auto caption = new QLabel(title);
        caption->setObjectName("Caption");
        header_row->addWidget(caption);
        header_row->addStretch();
        
        wrapper->addLayout(header_row);
        
        auto value = new QLabel("0");
        value->setStyleSheet(QString("font-size: 28px; font-weight: 700; color: %1;").arg(color));
        wrapper->addWidget(value);
        
        return std::make_pair(wrapper, value);
    };

    auto total_stat = make_stat(tr("总记录"), ":/icons/status/info.svg", "#1677ff");
    total_label_ = total_stat.second;
    stats_layout->addLayout(total_stat.first, 0, 0);

    auto check_in_stat = make_stat(tr("签到次数"), ":/icons/status/check-circle.svg", "#52c41a");
    check_in_label_ = check_in_stat.second;
    stats_layout->addLayout(check_in_stat.first, 0, 1);

    auto check_out_stat = make_stat(tr("签退次数"), ":/icons/status/user-check.svg", "#722ed1");
    check_out_label_ = check_out_stat.second;
    stats_layout->addLayout(check_out_stat.first, 0, 2);

    auto late_stat = make_stat(tr("迟到次数"), ":/icons/status/alert-circle.svg", "#fa8c16");
    late_label_ = late_stat.second;
    stats_layout->addLayout(late_stat.first, 0, 3);

    auto early_leave_stat = make_stat(tr("早退次数"), ":/icons/status/x-circle.svg", "#f5222d");
    early_leave_label_ = early_leave_stat.second;
    stats_layout->addLayout(early_leave_stat.first, 0, 4);

    return stats_card;
}

void AttendancePage::create_records_table() {
    records_table_ = new ModernTableView(this);
    records_table_->setColumnCount(6);
    records_table_->setHorizontalHeaderLabels({
        tr("记录ID"), tr("姓名"), tr("打卡时间"), tr("类型"), tr("状态"), tr("相似度")
    });
    
    records_table_->setColumnWidth(0, 100);
    records_table_->setColumnWidth(1, 120);
    records_table_->setColumnWidth(2, 180);
    records_table_->setColumnWidth(3, 100);
    records_table_->setColumnWidth(4, 120);
    records_table_->setColumnWidth(5, 100);
    
    records_table_->horizontalHeader()->setStretchLastSection(true);
    records_table_->verticalHeader()->setDefaultSectionSize(48);
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
    if (query_btn_) query_btn_->setEnabled(false);
    if (refresh_btn_) refresh_btn_->setEnabled(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    try {
        all_records_.clear();
        
        QString start_date_str;
        QString end_date_str;

        if (current_mode_ == Mode_SingleDay) {
            start_date_str = start_date_edit_->date().toString("yyyy-MM-dd");
            end_date_str = start_date_edit_->date().toString("yyyy-MM-dd"); // Same day
        } else {
            start_date_str = start_date_edit_->date().toString("yyyy-MM-dd");
            end_date_str = end_date_edit_->date().toString("yyyy-MM-dd");
        }
        
        // Use optimized range query
        all_records_ = attendance_service_->query_records_range(
            start_date_str.toStdString(), 
            end_date_str.toStdString()
        );
        
        spdlog::info("Loaded {} attendance records from {} to {}", 
                    all_records_.size(), start_date_str.toStdString(), end_date_str.toStdString());
        
        filter_records();
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load attendance records: {}", e.what());
        QMessageBox::warning(this, tr("错误"), tr("加载考勤记录失败：%1").arg(e.what()));
    }
    
    QApplication::restoreOverrideCursor();
    if (query_btn_) query_btn_->setEnabled(true);
    if (refresh_btn_) refresh_btn_->setEnabled(true);
    is_loading_ = false;
}

void AttendancePage::load_user_list() {
    if (!user_service_ || !user_combo_) {
        return;
    }

    // 保存当前选中项
    int current_user_id = user_combo_->currentData().toInt();

    // 清空并重新加载
    user_combo_->blockSignals(true);
    user_combo_->clear();
    user_combo_->addItem(tr("全部用户"), -1);

    // 加载所有启用的用户
    auto users = user_service_->get_all_users(db::UserStatus::USER_ENABLED);
    for (const auto& user : users) {
        QString display_name = QString::fromStdString(user.user_name);
        if (!user.department.empty()) {
            display_name += QString(" (%1)").arg(QString::fromStdString(user.department));
        }
        user_combo_->addItem(display_name, user.user_id);
    }

    // 恢复之前的选择（如果存在）
    int index = user_combo_->findData(current_user_id);
    if (index >= 0) {
        user_combo_->setCurrentIndex(index);
    }

    user_combo_->blockSignals(false);

    spdlog::info("Loaded {} users into combo box", users.size());
}

void AttendancePage::filter_records() {
    if (!records_table_) return;
    
    filtered_records_.clear();
    int selected_user_id = -1;
    
    if (user_combo_ && user_combo_->currentIndex() > 0) {
        selected_user_id = user_combo_->currentData().toInt();
    }
    
    for (const auto& record : all_records_) {
        if (selected_user_id == -1 || record.user_id == selected_user_id) {
            filtered_records_.push_back(record);
        }
    }
    
    // Update table
    records_table_->setRowCount(0);
    // 数据已按 check_time DESC 排序（新的在前），直接正向迭代即可
    for (const auto& record : filtered_records_) {
        int row = records_table_->rowCount();
        records_table_->insertRow(row);

        auto id_item = new QTableWidgetItem(QString::number(record.record_id));
        id_item->setTextAlignment(Qt::AlignCenter);
        records_table_->setItem(row, 0, id_item);

        auto name_item = new QTableWidgetItem(QString::fromStdString(record.user_name));
        records_table_->setItem(row, 1, name_item);

        auto time_item = new QTableWidgetItem(format_timestamp(record.check_time));
        time_item->setTextAlignment(Qt::AlignCenter);
        records_table_->setItem(row, 2, time_item);

        // Styling
        QString type_text = get_check_type_text(record.check_type);
        QColor type_fg, type_bg;
        if (record.check_type == 1) { type_fg = QColor("#237804"); type_bg = QColor("#f6ffed"); }
        else if (record.check_type == 2) { type_fg = QColor("#531dab"); type_bg = QColor("#f9f0ff"); }
        else { type_fg = QColor("#595959"); type_bg = QColor("#fafafa"); }
        
        auto type_item = new QTableWidgetItem(type_text);
        type_item->setTextAlignment(Qt::AlignCenter);
        type_item->setForeground(type_fg);
        type_item->setBackground(type_bg);
        records_table_->setItem(row, 3, type_item);

        auto status_item = new QTableWidgetItem(get_status_text(record.status));
        status_item->setTextAlignment(Qt::AlignCenter);
        QColor st_fg, st_bg;
        switch(record.status) {
            case 1: st_fg = QColor("#237804"); st_bg = QColor("#f6ffed"); break;
            case 2: // Late
            case 3: // Early
                st_fg = QColor("#ad4e00"); st_bg = QColor("#fff7e6"); break;
            default: st_fg = QColor("#a8071a"); st_bg = QColor("#fff1f0"); break;
        }
        status_item->setForeground(st_fg);
        status_item->setBackground(st_bg);
        records_table_->setItem(row, 4, status_item);

        double sim_val = record.similarity * 100.0;
        auto sim_item = new QTableWidgetItem(QString::number(sim_val, 'f', 1) + "% ");
        sim_item->setTextAlignment(Qt::AlignCenter);
        if (sim_val >= 85) sim_item->setForeground(QColor("#237804"));
        else if (sim_val >= 70) sim_item->setForeground(QColor("#ad4e00"));
        else sim_item->setForeground(QColor("#a8071a"));
        records_table_->setItem(row, 5, sim_item);
    }
    
    update_statistics();
}

void AttendancePage::update_statistics() {
    if (filtered_records_.empty()) {
        if (total_label_) total_label_->setText("0");
        if (check_in_label_) check_in_label_->setText("0");
        if (check_out_label_) check_out_label_->setText("0");
        if (late_label_) late_label_->setText("0");
        if (early_leave_label_) early_leave_label_->setText("0");
        return;
    }
    
    int total = filtered_records_.size();
    int check_in_count = 0;
    int check_out_count = 0;
    int late_count = 0;
    int early_leave_count = 0;
    
    for (const auto& record : filtered_records_) {
        if (record.check_type == 1) check_in_count++;
        else if (record.check_type == 2) check_out_count++;
        
        if (record.status == 2) late_count++;
        else if (record.status == 3) early_leave_count++;
    }
    
    if (total_label_) total_label_->setText(QString::number(total));
    if (check_in_label_) check_in_label_->setText(QString::number(check_in_count));
    if (check_out_label_) check_out_label_->setText(QString::number(check_out_count));
    if (late_label_) late_label_->setText(QString::number(late_count));
    if (early_leave_label_) early_leave_label_->setText(QString::number(early_leave_count));
}

void AttendancePage::export_to_csv(const QString& filename) {
    if (filtered_records_.empty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可导出的记录"));
        return;
    }
    write_csv(filtered_records_, filename);
}

void AttendancePage::write_csv(const std::vector<db::AttendanceRecord>& records, const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("错误"), tr("无法创建文件：%1").arg(file.errorString()));
        return;
    }
    
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << "\xEF\xBB\xBF"; // BOM
    out << "记录ID,用户ID,姓名,打卡时间,类型,状态,相似度\n";
    
    for (const auto& record : records) {
        out << record.record_id << ","
            << record.user_id << ","
            << QString::fromStdString(record.user_name) << ","
            << format_timestamp(record.check_time) << ","
            << get_check_type_text(record.check_type) << ","
            << get_status_text(record.status) << ","
            << QString::number(record.similarity * 100, 'f', 2) << "%\n";
    }
    file.close();
    QMessageBox::information(this, tr("成功"), tr("导出完成"));
}

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

void AttendancePage::export_specific_user() {
    if (all_records_.empty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可导出的记录"));
        return;
    }

    // Collect unique users
    std::map<QString, int> user_map;
    QStringList items;
    for (const auto& record : all_records_) {
        QString name = QString::fromStdString(record.user_name);
        QString label = QString("%1 (%2)").arg(name).arg(record.user_id);
        if (user_map.find(label) == user_map.end()) {
            user_map[label] = record.user_id;
            items << label;
        }
    }
    items.sort();

    bool ok;
    QString item = QInputDialog::getItem(this, tr("导出指定人员"),
                                         tr("请选择人员:"), items, 0, false, &ok);
    if (ok && !item.isEmpty()) {
        int user_id = user_map[item];
        
        // Filter records
        std::vector<db::AttendanceRecord> user_records;
        for (const auto& record : all_records_) {
            if (record.user_id == user_id) {
                user_records.push_back(record);
            }
        }

        if (user_records.empty()) return;

        QString user_name = QString::fromStdString(user_records[0].user_name);
        QString default_filename = QString("%1_%2_attendance.csv")
                .arg(user_name)
                .arg(QDateTime::currentDateTime().toString("yyyyMMdd"));
        
        QString filename = QFileDialog::getSaveFileName(this, tr("导出人员考勤"), 
                                                        default_filename, 
                                                        tr("CSV 文件 (*.csv)"));
        if (!filename.isEmpty()) {
            write_csv(user_records, filename);
        }
    }
}

void AttendancePage::batch_export_by_user() {
    if (all_records_.empty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可导出的记录"));
        return;
    }

    QString dir = QFileDialog::getExistingDirectory(this, tr("选择导出目录"),
                                                    "",
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) {
        return;
    }

    // Group records by user
    std::unordered_map<int, std::vector<db::AttendanceRecord>> user_records;
    for (const auto& record : all_records_) {
        user_records[record.user_id].push_back(record);
    }

    int success_count = 0;
    
    for (const auto& kv : user_records) {
        const auto& records = kv.second;
        if (records.empty()) continue;

        QString user_name = QString::fromStdString(records[0].user_name);
        QString filename = QString("%1/%2_%3.csv").arg(dir).arg(user_name).arg(kv.first);
        
        QFile file(filename);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out.setCodec("UTF-8");
            out << "\xEF\xBB\xBF"; // BOM
            out << "记录ID,用户ID,姓名,打卡时间,类型,状态,相似度\n";
            
            for (const auto& record : records) {
                out << record.record_id << ","
                    << record.user_id << ","
                    << QString::fromStdString(record.user_name) << ","
                    << format_timestamp(record.check_time) << ","
                    << get_check_type_text(record.check_type) << ","
                    << get_status_text(record.status) << ","
                    << QString::number(record.similarity * 100, 'f', 2) << "%\n";
            }
            file.close();
            success_count++;
        }
    }

    QMessageBox::information(this, tr("导出完成"), tr("已成功导出 %1 位用户的考勤记录到目录：\n%2").arg(success_count).arg(dir));
}

void AttendancePage::on_query_clicked() {
    load_attendance_records();
}

void AttendancePage::on_export_clicked() {
    QString default_filename = QString("attendance_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filename = QFileDialog::getSaveFileName(this, tr("导出"), default_filename, tr("CSV 文件 (*.csv)"));
    if (!filename.isEmpty()) export_to_csv(filename);
}

void AttendancePage::on_refresh_clicked() {
    load_attendance_records();
}

void AttendancePage::on_filter_mode_changed(int id) {
    current_mode_ = static_cast<FilterMode>(id);
    update_ui_state();
}

void AttendancePage::update_ui_state() {
    if (current_mode_ == Mode_SingleDay) {
        // Single Day Mode
        end_date_edit_->setVisible(false);
        range_separator_label_->setVisible(false);
    } else {
        // Range Mode
        end_date_edit_->setVisible(true);
        range_separator_label_->setVisible(true);
    }
}

void AttendancePage::on_date_changed() {
    // Boundary check
    if (current_mode_ == Mode_DateRange) {
        if (start_date_edit_->date() > end_date_edit_->date()) {
            // Fix invalid range by pushing the other date
            auto sender_widget = sender();
            if (sender_widget == start_date_edit_) {
                end_date_edit_->blockSignals(true);
                end_date_edit_->setDate(start_date_edit_->date());
                end_date_edit_->blockSignals(false);
            } else if (sender_widget == end_date_edit_) {
                start_date_edit_->blockSignals(true);
                start_date_edit_->setDate(end_date_edit_->date());
                start_date_edit_->blockSignals(false);
            }
        }
    }
}

void AttendancePage::on_user_combo_changed(int index) {
    filter_records();
}
/**
 * @file user_management_widget.cc
 * @brief 用户管理组件类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/user_management_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <spdlog/spdlog.h>

UserManagementWidget::UserManagementWidget(service::UserService* user_service, QWidget* parent)
    : QWidget(parent)
    , user_service_(user_service)
{
    setup_ui();
    load_users();
}

UserManagementWidget::~UserManagementWidget() {
}

void UserManagementWidget::setup_ui() {
    QVBoxLayout* main_layout = new QVBoxLayout(this);
    
    // 工具栏
    QHBoxLayout* toolbar_layout = new QHBoxLayout();
    
    search_edit_ = new QLineEdit();
    search_edit_->setPlaceholderText("搜索用户名或工号...");
    connect(search_edit_, &QLineEdit::textChanged, this, &UserManagementWidget::on_search_text_changed);
    
    status_filter_ = new QComboBox();
    status_filter_->addItem("全部", -1);
    status_filter_->addItem("启用", 1);
    status_filter_->addItem("禁用", 0);
    connect(status_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UserManagementWidget::on_status_filter_changed);
    
    refresh_btn_ = new QPushButton("刷新");
    connect(refresh_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_refresh_clicked);
    
    add_btn_ = new QPushButton("添加用户");
    connect(add_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_add_clicked);
    
    edit_btn_ = new QPushButton("编辑");
    connect(edit_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_edit_clicked);
    
    delete_btn_ = new QPushButton("删除");
    connect(delete_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_delete_clicked);
    
    enable_btn_ = new QPushButton("启用");
    connect(enable_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_enable_clicked);
    
    disable_btn_ = new QPushButton("禁用");
    connect(disable_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_disable_clicked);

    QPushButton* close_btn = new QPushButton("关闭");
    connect(close_btn, &QPushButton::clicked, this, &QWidget::close);

    toolbar_layout->addWidget(search_edit_);
    toolbar_layout->addWidget(status_filter_);
    toolbar_layout->addWidget(refresh_btn_);
    toolbar_layout->addStretch();
    toolbar_layout->addWidget(add_btn_);
    toolbar_layout->addWidget(edit_btn_);
    toolbar_layout->addWidget(delete_btn_);
    toolbar_layout->addWidget(enable_btn_);
    toolbar_layout->addWidget(disable_btn_);
    toolbar_layout->addWidget(close_btn);
    
    // 用户表格
    user_table_ = new QTableWidget();
    user_table_->setColumnCount(7);
    user_table_->setHorizontalHeaderLabels({"ID", "姓名", "工号", "部门", "职位", "状态", "特征数"});
    user_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    user_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    user_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    user_table_->horizontalHeader()->setStretchLastSection(true);
    user_table_->verticalHeader()->setVisible(false);
    
    connect(user_table_, &QTableWidget::itemSelectionChanged,
            this, &UserManagementWidget::on_table_selection_changed);
    
    main_layout->addLayout(toolbar_layout);
    main_layout->addWidget(user_table_);
    
    update_button_states();
}

void UserManagementWidget::load_users() {
    if (!user_service_) {
        return;
    }
    
    int status = status_filter_->currentData().toInt();
    all_users_ = user_service_->get_all_users(status);
    
    update_table(all_users_);
    
    spdlog::info("Loaded {} users", all_users_.size());
}

void UserManagementWidget::update_table(const std::vector<db::UserInfo>& users) {
    user_table_->setRowCount(0);
    
    for (const auto& user : users) {
        int row = user_table_->rowCount();
        user_table_->insertRow(row);
        
        user_table_->setItem(row, 0, new QTableWidgetItem(QString::number(user.user_id)));
        user_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(user.user_name)));
        user_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(user.employee_id)));
        user_table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(user.department)));
        user_table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(user.position)));
        user_table_->setItem(row, 5, new QTableWidgetItem(user.status == 1 ? "启用" : "禁用"));
        
        // 获取特征数
        int feature_count = user_service_->get_feature_count(user.user_id);
        user_table_->setItem(row, 6, new QTableWidgetItem(QString::number(feature_count)));
    }
}

void UserManagementWidget::update_button_states() {
    bool has_selection = user_table_->currentRow() >= 0;
    edit_btn_->setEnabled(has_selection);
    delete_btn_->setEnabled(has_selection);
    enable_btn_->setEnabled(has_selection);
    disable_btn_->setEnabled(has_selection);
}

void UserManagementWidget::on_refresh_clicked() {
    load_users();
}

void UserManagementWidget::on_add_clicked() {
    QMessageBox::information(this, "提示", "请使用主界面的人脸注册功能添加用户");
}

void UserManagementWidget::on_edit_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }
    
    QMessageBox::information(this, "提示", "编辑功能开发中...");
}

void UserManagementWidget::on_delete_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }
    
    int user_id = user_table_->item(row, 0)->text().toInt();
    QString user_name = user_table_->item(row, 1)->text();
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除",
        QString("确定要删除用户 %1 吗？\n这将同时删除该用户的所有人脸特征。").arg(user_name),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        if (user_service_->delete_user(user_id)) {
            QMessageBox::information(this, "成功", "用户已删除");
            load_users();
            emit user_updated();
            emit data_changed();  // 通知主窗口刷新
        } else {
            QMessageBox::critical(this, "错误", "删除用户失败");
        }
    }
}

void UserManagementWidget::on_enable_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }

    int user_id = user_table_->item(row, 0)->text().toInt();

    if (user_service_->set_user_status(user_id, true)) {
        QMessageBox::information(this, "成功", "用户已启用");
        load_users();
        emit user_updated();
        emit data_changed();  // 通知主窗口刷新
    } else {
        QMessageBox::critical(this, "错误", "启用用户失败");
    }
}

void UserManagementWidget::on_disable_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }

    int user_id = user_table_->item(row, 0)->text().toInt();

    if (user_service_->set_user_status(user_id, false)) {
        QMessageBox::information(this, "成功", "用户已禁用");
        load_users();
        emit user_updated();
        emit data_changed();  // 通知主窗口刷新
    } else {
        QMessageBox::critical(this, "错误", "禁用用户失败");
    }
}

void UserManagementWidget::on_search_text_changed(const QString& text) {
    if (text.isEmpty()) {
        update_table(all_users_);
        return;
    }

    std::vector<db::UserInfo> filtered;
    for (const auto& user : all_users_) {
        if (QString::fromStdString(user.user_name).contains(text, Qt::CaseInsensitive) ||
            QString::fromStdString(user.employee_id).contains(text, Qt::CaseInsensitive)) {
            filtered.push_back(user);
        }
    }

    update_table(filtered);
}

void UserManagementWidget::on_status_filter_changed(int) {
    load_users();
}

void UserManagementWidget::on_table_selection_changed() {
    update_button_states();
}


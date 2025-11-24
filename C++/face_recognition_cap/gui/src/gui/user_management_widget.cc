/**
 * @file user_management_widget.cc
 * @brief 用户管理组件类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/user_management_widget.h"

#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "widgets/search_input.h"
#include "widgets/status_tag.h"
#include "widgets/toast_notification.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>
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
    setWindowTitle(tr("用户管理"));

    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(0);

    auto card = new CardWidget(this);
    card->setTitle(tr("用户管理"));
    card->setSubtitle(tr("查看、筛选并维护考勤用户"));
    main_layout->addWidget(card);

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(16);

    auto toolbar_layout = new QHBoxLayout();
    toolbar_layout->setSpacing(12);
    
    search_input_ = new SearchInput(card);
    search_input_->setPlaceholderText(tr("搜索姓名或工号"));
    connect(search_input_, &SearchInput::searchTriggered,
            this, &UserManagementWidget::on_search_text_changed);
    
    status_filter_ = new QComboBox(card);
    status_filter_->addItem(tr("全部状态"), -1);
    status_filter_->addItem(tr("启用"), 1);
    status_filter_->addItem(tr("禁用"), 0);
    connect(status_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UserManagementWidget::on_status_filter_changed);
    
    refresh_btn_ = new QPushButton(tr("刷新"), card);
    connect(refresh_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_refresh_clicked);
    
    add_btn_ = new QPushButton(tr("添加用户"), card);
    add_btn_->setProperty("primary", QVariant(true));
    connect(add_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_add_clicked);
    
    edit_btn_ = new QPushButton(tr("编辑"), card);
    connect(edit_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_edit_clicked);
    
    delete_btn_ = new QPushButton(tr("删除"), card);
    delete_btn_->setProperty("danger", QVariant(true));
    connect(delete_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_delete_clicked);
    
    enable_btn_ = new QPushButton(tr("启用"), card);
    connect(enable_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_enable_clicked);
    
    disable_btn_ = new QPushButton(tr("禁用"), card);
    connect(disable_btn_, &QPushButton::clicked, this, &UserManagementWidget::on_disable_clicked);

    close_btn_ = new QPushButton(tr("关闭"), card);
    connect(close_btn_, &QPushButton::clicked, this, &QWidget::close);

    toolbar_layout->addWidget(search_input_, 1);
    toolbar_layout->addWidget(status_filter_);
    toolbar_layout->addWidget(refresh_btn_);
    toolbar_layout->addStretch();
    toolbar_layout->addWidget(add_btn_);
    toolbar_layout->addWidget(edit_btn_);
    toolbar_layout->addWidget(delete_btn_);
    toolbar_layout->addWidget(enable_btn_);
    toolbar_layout->addWidget(disable_btn_);
    toolbar_layout->addWidget(close_btn_);
    
    user_table_ = new ModernTableView(card);
    user_table_->setColumnCount(7);
    user_table_->setHorizontalHeaderLabels({tr("ID"), tr("姓名"), tr("工号"),
                                            tr("部门"), tr("职位"), tr("状态"), tr("特征数")});
    user_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    user_table_->setSelectionMode(QAbstractItemView::SingleSelection);
    user_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    user_table_->horizontalHeader()->setStretchLastSection(true);
    user_table_->verticalHeader()->setVisible(false);
    user_table_->setEmptyText(tr("暂无用户数据"));
    
    connect(user_table_, &QTableWidget::itemSelectionChanged,
            this, &UserManagementWidget::on_table_selection_changed);
    
    body_layout->addLayout(toolbar_layout);
    body_layout->addWidget(user_table_, 1);
    
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
        
        auto status_tag = new StatusTag(user_table_);
        status_tag->setText(user.status == 1 ? tr("启用") : tr("禁用"));
        status_tag->setType(user.status == 1 ? StatusTag::Type::Success : StatusTag::Type::Error);
        user_table_->setCellWidget(row, 5, status_tag);
        
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
    ToastNotification::showMessage(this, tr("提示"), tr("用户列表已刷新"),
                                   ToastNotification::Level::Info);
}

void UserManagementWidget::on_add_clicked() {
    QMessageBox::information(this, "提示", "请使用主界面的人脸注册功能添加用户");
}

void UserManagementWidget::on_edit_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }
    
    ToastNotification::showMessage(this,
                                   tr("敬请期待"),
                                   tr("编辑功能开发中..."),
                                   ToastNotification::Level::Warning);
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
            ToastNotification::showMessage(this, tr("操作成功"),
                                           QString(tr("用户 %1 已删除")).arg(user_name),
                                           ToastNotification::Level::Success);
            load_users();
            emit user_updated();
            emit data_changed();  // 通知主窗口刷新
        } else {
            ToastNotification::showMessage(this, tr("操作失败"),
                                           tr("删除用户失败"),
                                           ToastNotification::Level::Error);
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
        ToastNotification::showMessage(this, tr("操作成功"),
                                       tr("用户已启用"),
                                       ToastNotification::Level::Success);
        load_users();
        emit user_updated();
        emit data_changed();  // 通知主窗口刷新
    } else {
        ToastNotification::showMessage(this, tr("操作失败"),
                                       tr("启用用户失败"),
                                       ToastNotification::Level::Error);
    }
}

void UserManagementWidget::on_disable_clicked() {
    int row = user_table_->currentRow();
    if (row < 0) {
        return;
    }

    int user_id = user_table_->item(row, 0)->text().toInt();

    if (user_service_->set_user_status(user_id, false)) {
        ToastNotification::showMessage(this, tr("操作成功"),
                                       tr("用户已禁用"),
                                       ToastNotification::Level::Success);
        load_users();
        emit user_updated();
        emit data_changed();  // 通知主窗口刷新
    } else {
        ToastNotification::showMessage(this, tr("操作失败"),
                                       tr("禁用用户失败"),
                                       ToastNotification::Level::Error);
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


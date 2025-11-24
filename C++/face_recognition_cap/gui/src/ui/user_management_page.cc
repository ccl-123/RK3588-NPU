#include "ui/user_management_page.h"

#include "gui/user_edit_dialog.h"
#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "widgets/search_input.h"
#include "widgets/status_tag.h"
#include "widgets/toast_notification.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>
#include <spdlog/spdlog.h>

UserManagementPage::UserManagementPage(QWidget* parent)
    : QWidget(parent)
    , user_service_(nullptr)
    , search_input_(nullptr)
    , status_filter_(nullptr)
    , refresh_btn_(nullptr)
    , add_btn_(nullptr)
    , edit_btn_(nullptr)
    , delete_btn_(nullptr)
    , enable_btn_(nullptr)
    , disable_btn_(nullptr)
    , table_(nullptr) {
    setup_ui();
}

void UserManagementPage::setUserService(service::UserService* service) {
    user_service_ = service;
    if (user_service_) {
        load_users();
    }
}

void UserManagementPage::setup_ui() {
    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(32, 24, 32, 24);
    main_layout->setSpacing(24);

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
            this, &UserManagementPage::on_search_text_changed);
    
    status_filter_ = new QComboBox(card);
    status_filter_->addItem(tr("全部状态"), -1);
    status_filter_->addItem(tr("启用"), 1);
    status_filter_->addItem(tr("禁用"), 0);
    connect(status_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UserManagementPage::on_status_filter_changed);
    
    refresh_btn_ = new QPushButton(tr("刷新"), card);
    connect(refresh_btn_, &QPushButton::clicked, this, &UserManagementPage::on_refresh_clicked);
    
    add_btn_ = new QPushButton(tr("添加用户"), card);
    add_btn_->setProperty("primary", QVariant(true));
    connect(add_btn_, &QPushButton::clicked, this, &UserManagementPage::on_add_clicked);
    
    edit_btn_ = new QPushButton(tr("编辑"), card);
    connect(edit_btn_, &QPushButton::clicked, this, &UserManagementPage::on_edit_clicked);
    
    delete_btn_ = new QPushButton(tr("删除"), card);
    delete_btn_->setProperty("danger", QVariant(true));
    connect(delete_btn_, &QPushButton::clicked, this, &UserManagementPage::on_delete_clicked);
    
    enable_btn_ = new QPushButton(tr("启用"), card);
    connect(enable_btn_, &QPushButton::clicked, this, &UserManagementPage::on_enable_clicked);
    
    disable_btn_ = new QPushButton(tr("禁用"), card);
    connect(disable_btn_, &QPushButton::clicked, this, &UserManagementPage::on_disable_clicked);

    toolbar_layout->addWidget(search_input_, 1);
    toolbar_layout->addWidget(status_filter_);
    toolbar_layout->addWidget(refresh_btn_);
    toolbar_layout->addStretch();
    toolbar_layout->addWidget(add_btn_);
    toolbar_layout->addWidget(edit_btn_);
    toolbar_layout->addWidget(delete_btn_);
    toolbar_layout->addWidget(enable_btn_);
    toolbar_layout->addWidget(disable_btn_);

    table_ = new ModernTableView(card);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({tr("ID"), tr("姓名"), tr("工号"),
                                       tr("部门"), tr("职位"), tr("状态"), tr("特征数")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEmptyText(tr("暂无用户数据"));
    
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &UserManagementPage::on_table_selection_changed);
    
    body_layout->addLayout(toolbar_layout);
    body_layout->addWidget(table_, 1);

    update_button_states();
}

void UserManagementPage::load_users() {
    if (!user_service_) {
        return;
    }
    
    int status = status_filter_->currentData().toInt();
    all_users_ = user_service_->get_all_users(status);
    
    update_table(all_users_);
    
    spdlog::info("Loaded {} users", all_users_.size());
}

void UserManagementPage::update_table(const std::vector<db::UserInfo>& users) {
    if (!table_) {
        return;
    }
    
    table_->setRowCount(0);
    
    for (const auto& user : users) {
        int row = table_->rowCount();
        table_->insertRow(row);
        
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(user.user_id)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(user.user_name)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(user.employee_id)));
        table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(user.department)));
        table_->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(user.position)));
        
        auto status_tag = new StatusTag(table_);
        status_tag->setText(user.status == 1 ? tr("启用") : tr("禁用"));
        status_tag->setType(user.status == 1 ? StatusTag::Type::Success : StatusTag::Type::Error);
        table_->setCellWidget(row, 5, status_tag);
        
        int feature_count = user_service_ ? user_service_->get_feature_count(user.user_id) : 0;
        table_->setItem(row, 6, new QTableWidgetItem(QString::number(feature_count)));
    }
}

void UserManagementPage::update_button_states() {
    bool has_selection = table_ && table_->currentRow() >= 0;
    if (edit_btn_) edit_btn_->setEnabled(has_selection);
    if (delete_btn_) delete_btn_->setEnabled(has_selection);
    if (enable_btn_) enable_btn_->setEnabled(has_selection);
    if (disable_btn_) disable_btn_->setEnabled(has_selection);
}

void UserManagementPage::on_refresh_clicked() {
    load_users();
    ToastNotification::showMessage(this, tr("提示"), tr("用户列表已刷新"),
                                   ToastNotification::Level::Info);
}

void UserManagementPage::on_add_clicked() {
    QMessageBox::information(this, "提示", "请使用主界面的人脸注册功能添加用户");
}

void UserManagementPage::on_edit_clicked() {
    if (!table_ || table_->currentRow() < 0 || !user_service_) {
        return;
    }
    
    int row = table_->currentRow();
    int user_id = table_->item(row, 0)->text().toInt();
    QString user_name = table_->item(row, 1)->text();
    
    UserEditDialog dialog(user_service_, user_id, this);
    if (dialog.exec() == QDialog::Accepted) {
        ToastNotification::showMessage(this, tr("操作成功"),
                                       QString(tr("用户 %1 信息已更新")).arg(user_name),
                                       ToastNotification::Level::Success);
        load_users();
        emit userUpdated();
        emit dataChanged();
    }
}

void UserManagementPage::on_delete_clicked() {
    if (!table_ || table_->currentRow() < 0 || !user_service_) {
        return;
    }
    
    int row = table_->currentRow();
    int user_id = table_->item(row, 0)->text().toInt();
    QString user_name = table_->item(row, 1)->text();
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "确认删除",
        QString("确定要删除用户 %1 吗？\n这将同时删除该用户的所有人脸特征。").arg(user_name),
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        if (user_service_->delete_user(user_id)) {
            ToastNotification::showMessage(this, tr("操作成功"),
                                           QString(tr("用户 %1 已删除")).arg(user_name),
                                           ToastNotification::Level::Success);
            load_users();
            emit userUpdated();
            emit dataChanged();
        } else {
            ToastNotification::showMessage(this, tr("操作失败"),
                                           tr("删除用户失败"),
                                           ToastNotification::Level::Error);
        }
    }
}

void UserManagementPage::on_enable_clicked() {
    if (!table_ || table_->currentRow() < 0 || !user_service_) {
        return;
    }

    int row = table_->currentRow();
    int user_id = table_->item(row, 0)->text().toInt();

    if (user_service_->set_user_status(user_id, true)) {
        ToastNotification::showMessage(this, tr("操作成功"),
                                       tr("用户已启用"),
                                       ToastNotification::Level::Success);
        load_users();
        emit userUpdated();
        emit dataChanged();
    } else {
        ToastNotification::showMessage(this, tr("操作失败"),
                                       tr("启用用户失败"),
                                       ToastNotification::Level::Error);
    }
}

void UserManagementPage::on_disable_clicked() {
    if (!table_ || table_->currentRow() < 0 || !user_service_) {
        return;
    }

    int row = table_->currentRow();
    int user_id = table_->item(row, 0)->text().toInt();

    if (user_service_->set_user_status(user_id, false)) {
        ToastNotification::showMessage(this, tr("操作成功"),
                                       tr("用户已禁用"),
                                       ToastNotification::Level::Success);
        load_users();
        emit userUpdated();
        emit dataChanged();
    } else {
        ToastNotification::showMessage(this, tr("操作失败"),
                                       tr("禁用用户失败"),
                                       ToastNotification::Level::Error);
    }
}

void UserManagementPage::on_search_text_changed(const QString& text) {
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

void UserManagementPage::on_status_filter_changed(int) {
    load_users();
}

void UserManagementPage::on_table_selection_changed() {
    update_button_states();
}

ModernTableView* UserManagementPage::table() const {
    return table_;
}

SearchInput* UserManagementPage::searchInput() const {
    return search_input_;
}


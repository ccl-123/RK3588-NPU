/**
 * @file user_management_widget.cc
 * @brief 用户管理组件类实现
 * @author CL
 * @date 2025-11-20
 */

#include "gui/user_management_widget.h"

#include "gui/user_edit_dialog.h"
#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "widgets/search_input.h"
#include "widgets/status_item_delegate.h"
#include "widgets/toast_notification.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>
#include <QShowEvent>
#include <QTimer>
#include <spdlog/spdlog.h>

UserManagementWidget::UserManagementWidget(service::UserService* user_service, QWidget* parent)
    : QWidget(parent)
    , user_service_(user_service)
    , need_reload_(true)  // 标记需要在 showEvent 中加载数据
{
    setup_ui();
    // 不在构造函数中加载数据，延迟到 showEvent 中
}

UserManagementWidget::~UserManagementWidget() {
}

void UserManagementWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    
    // 页面显示时，如果需要重新加载数据
    if (need_reload_ && user_service_) {
        // 使用 QTimer::singleShot 延迟一帧，确保布局完全完成
        QTimer::singleShot(0, this, [this]() {
            if (need_reload_) {
                load_users();
                need_reload_ = false;
            }
        });
    }
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
    user_table_->verticalHeader()->setVisible(false);
    user_table_->setEmptyText(tr("暂无用户数据"));
    
    // 设置列宽策略：所有列按比例分配空间
    user_table_->horizontalHeader()->setStretchLastSection(false);
    user_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // 为状态列设置自定义绘制代理，避免使用 setCellWidget 导致的列错位问题
    user_table_->setItemDelegateForColumn(5, new StatusItemDelegate(user_table_));
    
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
    if (!user_table_) {
        return;
    }
    
    // 在批量更新期间禁用界面更新和信号，提高性能并避免渲染问题
    user_table_->setUpdatesEnabled(false);
    user_table_->blockSignals(true);
    
    // 清空表格内容并预设行数
    user_table_->clearContents();
    user_table_->setRowCount(static_cast<int>(users.size()));
        
    for (int row = 0; row < static_cast<int>(users.size()); ++row) {
        const auto& user = users[row];
        
        // ID
        auto id_item = new QTableWidgetItem(QString::number(user.user_id));
        id_item->setTextAlignment(Qt::AlignCenter);
        user_table_->setItem(row, 0, id_item);
        
        // 姓名
        auto name_item = new QTableWidgetItem(QString::fromStdString(user.user_name));
        user_table_->setItem(row, 1, name_item);
        
        // 工号
        auto emp_item = new QTableWidgetItem(QString::fromStdString(user.employee_id));
        user_table_->setItem(row, 2, emp_item);
        
        // 部门
        auto dept_item = new QTableWidgetItem(QString::fromStdString(user.department));
        user_table_->setItem(row, 3, dept_item);
        
        // 职位
        auto pos_item = new QTableWidgetItem(QString::fromStdString(user.position));
        user_table_->setItem(row, 4, pos_item);
        
        // 状态 (使用 delegate 绘制，不需要 setCellWidget)
        auto status_item = new QTableWidgetItem(user.status == 1 ? tr("启用") : tr("禁用"));
        status_item->setData(Qt::UserRole, user.status);
        status_item->setTextAlignment(Qt::AlignCenter);
        user_table_->setItem(row, 5, status_item);
        
        // 特征数
        int feature_count = user_service_ ? user_service_->get_feature_count(user.user_id) : 0;
        auto feature_item = new QTableWidgetItem(QString::number(feature_count));
        feature_item->setTextAlignment(Qt::AlignCenter);
        user_table_->setItem(row, 6, feature_item);
    }
    
    // 恢复信号和界面更新
    user_table_->blockSignals(false);
    user_table_->setUpdatesEnabled(true);
    
    // 强制刷新视图
    user_table_->viewport()->update();
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
    // 发出信号请求主窗口打开人脸注册对话框
    emit register_face_requested();
    
    // 关闭当前窗口，让用户专注于人脸注册
    close();
}

void UserManagementWidget::on_edit_clicked() {
    int row = user_table_->currentRow();
    if (row < 0 || !user_service_) {
        return;
    }
    
    int user_id = user_table_->item(row, 0)->text().toInt();
    QString user_name = user_table_->item(row, 1)->text();
    
    UserEditDialog dialog(user_service_, user_id, this);
    if (dialog.exec() == QDialog::Accepted) {
        ToastNotification::showMessage(this, tr("操作成功"),
                                       QString(tr("用户 %1 信息已更新")).arg(user_name),
                                       ToastNotification::Level::Success);
        // 延迟一帧再刷新表格，避免对话框关闭后立即更新导致的渲染问题
        QTimer::singleShot(0, this, [this]() {
        load_users();
        });
        emit user_updated();
        emit data_changed();
    }
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
            QTimer::singleShot(0, this, [this]() {
            load_users();
            });
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
        QTimer::singleShot(0, this, [this]() {
        load_users();
        });
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
        QTimer::singleShot(0, this, [this]() {
        load_users();
        });
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


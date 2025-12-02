#include "ui/user_management_page.h"

#include "gui/user_edit_dialog.h"
#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "widgets/search_input.h"
#include "widgets/status_item_delegate.h"
#include "widgets/toast_notification.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>
#include <QShowEvent>
#include <QTimer>
#include <spdlog/spdlog.h>

UserManagementPage::UserManagementPage(QWidget* parent)
    : QWidget(parent)
    , user_service_(nullptr)
    , need_reload_(false)
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
    // 标记需要重新加载，在 showEvent 中延迟加载
    // 避免在页面还没显示时加载数据导致表格渲染问题
    need_reload_ = true;
    
    // 如果页面已经可见，立即加载
    if (isVisible() && user_service_) {
        QTimer::singleShot(0, this, [this]() {
            if (need_reload_) {
                load_users();
                need_reload_ = false;
            }
        });
    }
}

void UserManagementPage::showEvent(QShowEvent* event) {
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
    table_->verticalHeader()->setVisible(false);
    table_->setEmptyText(tr("暂无用户数据"));
    
    // 设置列宽策略：所有列按比例分配空间
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    
    // 为状态列设置自定义绘制代理，避免使用 setCellWidget 导致的列错位问题
    table_->setItemDelegateForColumn(5, new StatusItemDelegate(table_));
    
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
    
    // 在批量更新期间禁用界面更新和信号，提高性能并避免渲染问题
    table_->setUpdatesEnabled(false);
    table_->blockSignals(true);
    
    // 清空表格内容并预设行数
    table_->clearContents();
    table_->setRowCount(static_cast<int>(users.size()));
    
    for (int row = 0; row < static_cast<int>(users.size()); ++row) {
        const auto& user = users[row];
        
        // ID
        auto id_item = new QTableWidgetItem(QString::number(user.user_id));
        id_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 0, id_item);
        
        // 姓名
        auto name_item = new QTableWidgetItem(QString::fromStdString(user.user_name));
        table_->setItem(row, 1, name_item);
        
        // 工号
        auto emp_item = new QTableWidgetItem(QString::fromStdString(user.employee_id));
        table_->setItem(row, 2, emp_item);
        
        // 部门
        auto dept_item = new QTableWidgetItem(QString::fromStdString(user.department));
        table_->setItem(row, 3, dept_item);
        
        // 职位
        auto pos_item = new QTableWidgetItem(QString::fromStdString(user.position));
        table_->setItem(row, 4, pos_item);
        
        // 状态 (使用 delegate 绘制，不需要 setCellWidget)
        auto status_item = new QTableWidgetItem(user.status == 1 ? tr("启用") : tr("禁用"));
        status_item->setData(Qt::UserRole, user.status);
        status_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 5, status_item);
        
        // 特征数
        int feature_count = user_service_ ? user_service_->get_feature_count(user.user_id) : 0;
        auto feature_item = new QTableWidgetItem(QString::number(feature_count));
        feature_item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 6, feature_item);
    }
    
    // 恢复信号和界面更新
    table_->blockSignals(false);
    table_->setUpdatesEnabled(true);
    
    // 强制刷新视图
    table_->viewport()->update();
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
    emit registerFaceRequested();
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
        // 延迟一帧再刷新表格，避免对话框关闭后立即更新导致的渲染问题
        QTimer::singleShot(0, this, [this]() {
            load_users();
        });
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
            QTimer::singleShot(0, this, [this]() {
                load_users();
            });
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
        QTimer::singleShot(0, this, [this]() {
            load_users();
        });
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
        QTimer::singleShot(0, this, [this]() {
            load_users();
        });
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


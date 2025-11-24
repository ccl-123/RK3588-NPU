/**
 * @file user_edit_dialog.cc
 * @brief 用户编辑对话框实现
 * @author CL
 * @date 2025-11-24
 */

#include "gui/user_edit_dialog.h"

#include "widgets/card_widget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVariant>
#include <spdlog/spdlog.h>

UserEditDialog::UserEditDialog(service::UserService* user_service,
                               int user_id,
                               QWidget* parent)
    : QDialog(parent)
    , user_service_(user_service)
    , user_id_(user_id)
    , name_edit_(nullptr)
    , employee_id_edit_(nullptr)
    , department_edit_(nullptr)
    , position_edit_(nullptr)
    , phone_edit_(nullptr)
    , email_edit_(nullptr)
    , save_btn_(nullptr)
    , cancel_btn_(nullptr) {
    setup_ui();
    load_user_data();
}

UserEditDialog::~UserEditDialog() {
}

void UserEditDialog::setup_ui() {
    setWindowTitle(tr("编辑用户信息"));
    resize(500, 450);

    auto main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(24, 24, 24, 24);
    main_layout->setSpacing(24);

    auto card = new CardWidget(this);
    card->setTitle(tr("用户信息"));
    card->setSubtitle(tr("编辑用户的基本信息"));

    auto form_layout = new QFormLayout(card->bodyContainer());
    form_layout->setLabelAlignment(Qt::AlignRight);
    form_layout->setHorizontalSpacing(24);
    form_layout->setVerticalSpacing(16);

    // 姓名（必填）
    name_edit_ = new QLineEdit(card);
    name_edit_->setPlaceholderText(tr("请输入姓名"));
    form_layout->addRow(tr("姓名 *:"), name_edit_);

    // 工号（必填）
    employee_id_edit_ = new QLineEdit(card);
    employee_id_edit_->setPlaceholderText(tr("请输入工号"));
    form_layout->addRow(tr("工号 *:"), employee_id_edit_);

    // 部门
    department_edit_ = new QLineEdit(card);
    department_edit_->setPlaceholderText(tr("请输入部门"));
    form_layout->addRow(tr("部门:"), department_edit_);

    // 职位
    position_edit_ = new QLineEdit(card);
    position_edit_->setPlaceholderText(tr("请输入职位"));
    form_layout->addRow(tr("职位:"), position_edit_);

    // 电话
    phone_edit_ = new QLineEdit(card);
    phone_edit_->setPlaceholderText(tr("请输入电话"));
    form_layout->addRow(tr("电话:"), phone_edit_);

    // 邮箱
    email_edit_ = new QLineEdit(card);
    email_edit_->setPlaceholderText(tr("请输入邮箱"));
    form_layout->addRow(tr("邮箱:"), email_edit_);

    main_layout->addWidget(card);

    // 按钮栏
    auto button_layout = new QHBoxLayout();
    button_layout->setSpacing(12);
    button_layout->addStretch();

    cancel_btn_ = new QPushButton(tr("取消"), this);
    connect(cancel_btn_, &QPushButton::clicked, this, &UserEditDialog::on_cancel_clicked);

    save_btn_ = new QPushButton(tr("保存"), this);
    save_btn_->setProperty("primary", QVariant(true));
    connect(save_btn_, &QPushButton::clicked, this, &UserEditDialog::on_save_clicked);

    button_layout->addWidget(cancel_btn_);
    button_layout->addWidget(save_btn_);

    main_layout->addLayout(button_layout);
}

void UserEditDialog::load_user_data() {
    if (!user_service_) {
        return;
    }

    if (!user_service_->get_user(user_id_, user_info_)) {
        QMessageBox::critical(this, tr("错误"), tr("无法加载用户信息"));
        reject();
        return;
    }

    // 填充表单
    if (name_edit_) name_edit_->setText(QString::fromStdString(user_info_.user_name));
    if (employee_id_edit_) employee_id_edit_->setText(QString::fromStdString(user_info_.employee_id));
    if (department_edit_) department_edit_->setText(QString::fromStdString(user_info_.department));
    if (position_edit_) position_edit_->setText(QString::fromStdString(user_info_.position));
    if (phone_edit_) phone_edit_->setText(QString::fromStdString(user_info_.phone));
    if (email_edit_) email_edit_->setText(QString::fromStdString(user_info_.email));

    spdlog::info("Loaded user data for editing: ID={}, Name={}", user_id_, user_info_.user_name);
}

bool UserEditDialog::validate_input() {
    if (!name_edit_ || !employee_id_edit_) {
        return false;
    }

    QString name = name_edit_->text().trimmed();
    QString employee_id = employee_id_edit_->text().trimmed();

    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("请输入用户姓名"));
        name_edit_->setFocus();
        return false;
    }

    if (employee_id.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("请输入工号"));
        employee_id_edit_->setFocus();
        return false;
    }

    return true;
}

void UserEditDialog::on_save_clicked() {
    if (!validate_input() || !user_service_) {
        return;
    }

    // 更新用户信息
    user_info_.user_name = name_edit_->text().trimmed().toStdString();
    user_info_.employee_id = employee_id_edit_->text().trimmed().toStdString();
    user_info_.department = department_edit_ ? department_edit_->text().trimmed().toStdString() : "";
    user_info_.position = position_edit_ ? position_edit_->text().trimmed().toStdString() : "";
    user_info_.phone = phone_edit_ ? phone_edit_->text().trimmed().toStdString() : "";
    user_info_.email = email_edit_ ? email_edit_->text().trimmed().toStdString() : "";

    // 保存到数据库
    if (user_service_->update_user(user_info_)) {
        spdlog::info("User updated successfully: ID={}, Name={}", user_id_, user_info_.user_name);
        accept();
    } else {
        QMessageBox::critical(this, tr("错误"), tr("保存用户信息失败"));
        spdlog::error("Failed to update user: ID={}", user_id_);
    }
}

void UserEditDialog::on_cancel_clicked() {
    reject();
}

db::UserInfo UserEditDialog::getUserInfo() const {
    return user_info_;
}


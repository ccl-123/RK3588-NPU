/**
 * @file user_edit_dialog.h
 * @brief 用户编辑对话框
 * @author CL
 * @date 2025-11-24
 */

#ifndef USER_EDIT_DIALOG_H
#define USER_EDIT_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

#include "database/database_types.h"
#include "service/user_service.h"

/**
 * @brief 用户编辑对话框
 * 
 * 用于编辑现有用户的基本信息
 */
class UserEditDialog : public QDialog {
    Q_OBJECT

public:
    explicit UserEditDialog(service::UserService* user_service,
                           int user_id,
                           QWidget* parent = nullptr);
    ~UserEditDialog();

    // 获取编辑后的用户信息
    db::UserInfo getUserInfo() const;

private slots:
    void on_save_clicked();
    void on_cancel_clicked();

private:
    void setup_ui();
    void load_user_data();
    bool validate_input();

    service::UserService* user_service_;
    int user_id_;
    db::UserInfo user_info_;

    // UI 组件
    QLineEdit* name_edit_;
    QLineEdit* employee_id_edit_;
    QLineEdit* department_edit_;
    QLineEdit* position_edit_;
    QLineEdit* phone_edit_;
    QLineEdit* email_edit_;
    QPushButton* save_btn_;
    QPushButton* cancel_btn_;
};

#endif // USER_EDIT_DIALOG_H


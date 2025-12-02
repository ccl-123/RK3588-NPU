/**
 * @file user_management_widget.h
 * @brief 用户管理界面
 * @author CL
 * @date 2025-11-20
 */

/**
 * @file user_management_widget.h
 * @brief 用户管理组件类定义
 * @author CL
 * @date 2025-11-20
 *
 * 提供用户信息管理功能，包括查看、搜索、启用/禁用、删除等操作。
 */

#ifndef USER_MANAGEMENT_WIDGET_H
#define USER_MANAGEMENT_WIDGET_H

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <vector>

#include "service/user_service.h"
#include "database/database_types.h"

class SearchInput;
class ModernTableView;

/**
 * @brief 用户管理组件
 */
class UserManagementWidget : public QWidget {
    Q_OBJECT

public:
    explicit UserManagementWidget(service::UserService* user_service, QWidget* parent = nullptr);
    ~UserManagementWidget();

signals:
    void user_updated();
    void data_changed();  // 数据变更信号（用于通知主窗口刷新）
    void register_face_requested();  // 请求打开人脸注册对话框

protected:
    // 重写 showEvent 实现延迟加载
    void showEvent(QShowEvent* event) override;

private slots:
    void on_refresh_clicked();
    void on_add_clicked();
    void on_edit_clicked();
    void on_delete_clicked();
    void on_enable_clicked();
    void on_disable_clicked();
    void on_search_text_changed(const QString& text);
    void on_status_filter_changed(int index);
    void on_table_selection_changed();

private:
    void setup_ui();
    void load_users();
    void update_table(const std::vector<db::UserInfo>& users);
    void update_button_states();

private:
    service::UserService* user_service_;
    bool need_reload_;  // 标记是否需要重新加载数据
    
    // UI 组件
    SearchInput* search_input_;
    QComboBox* status_filter_;
    QPushButton* refresh_btn_;
    QPushButton* add_btn_;
    QPushButton* edit_btn_;
    QPushButton* delete_btn_;
    QPushButton* enable_btn_;
    QPushButton* disable_btn_;
    QPushButton* close_btn_;
    ModernTableView* user_table_;
    
    // 数据
    std::vector<db::UserInfo> all_users_;
};

#endif // USER_MANAGEMENT_WIDGET_H


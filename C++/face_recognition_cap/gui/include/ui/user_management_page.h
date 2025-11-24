#pragma once

#include <QWidget>
#include <QPushButton>
#include <QComboBox>
#include <vector>

#include "service/user_service.h"
#include "database/database_types.h"

class ModernTableView;
class SearchInput;

class UserManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit UserManagementPage(QWidget* parent = nullptr);

    ModernTableView* table() const;
    SearchInput* searchInput() const;
    
    void setUserService(service::UserService* service);

signals:
    void userUpdated();
    void dataChanged();
    void registerFaceRequested();

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

    service::UserService* user_service_;
    
    SearchInput* search_input_;
    QComboBox* status_filter_;
    QPushButton* refresh_btn_;
    QPushButton* add_btn_;
    QPushButton* edit_btn_;
    QPushButton* delete_btn_;
    QPushButton* enable_btn_;
    QPushButton* disable_btn_;
    ModernTableView* table_;
    
    std::vector<db::UserInfo> all_users_;
};


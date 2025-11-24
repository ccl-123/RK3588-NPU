#pragma once

#include <QWidget>

class ModernTableView;
class SearchInput;

class UserManagementPage : public QWidget {
    Q_OBJECT
public:
    explicit UserManagementPage(QWidget* parent = nullptr);

    ModernTableView* table() const;
    SearchInput* searchInput() const;

signals:
    void openUserManagementRequested();

private:
    ModernTableView* table_;
    SearchInput* search_input_;
};


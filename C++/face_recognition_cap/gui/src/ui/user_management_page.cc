#include "ui/user_management_page.h"

#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"
#include "widgets/search_input.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

UserManagementPage::UserManagementPage(QWidget* parent)
    : QWidget(parent)
    , table_(nullptr)
    , search_input_(nullptr) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(24);

    auto card = new CardWidget(this);
    card->setTitle(tr("用户管理"));

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(12);

    auto toolbar = new QHBoxLayout();
    toolbar->setSpacing(12);

    search_input_ = new SearchInput(card);
    search_input_->setPlaceholderText(tr("搜索用户名或工号"));

    auto open_button = new QPushButton(tr("打开完整管理窗口"), card);
    open_button->setProperty("primary", QVariant(true));
    connect(open_button, &QPushButton::clicked, this, &UserManagementPage::openUserManagementRequested);

    toolbar->addWidget(search_input_, 1);
    toolbar->addWidget(open_button);

    table_ = new ModernTableView(card);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels(
        {tr("ID"), tr("姓名"), tr("工号"), tr("部门"), tr("职位"), tr("状态"), tr("特征数")});

    body_layout->addLayout(toolbar);
    body_layout->addWidget(table_, 1);

    layout->addWidget(card, 1);
}

ModernTableView* UserManagementPage::table() const {
    return table_;
}

SearchInput* UserManagementPage::searchInput() const {
    return search_input_;
}


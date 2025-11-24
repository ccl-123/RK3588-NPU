#include "widgets/modern_table_view.h"

#include <QHeaderView>
#include <QLabel>
#include <QResizeEvent>

ModernTableView::ModernTableView(QWidget* parent)
    : QTableWidget(parent)
    , placeholder_label_(new QLabel(this)) {
    setObjectName("ModernTableView");
    setAlternatingRowColors(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setShowGrid(false);
    horizontalHeader()->setStretchLastSection(true);
    verticalHeader()->setDefaultSectionSize(48);
    verticalHeader()->setVisible(false);

    placeholder_label_->setAlignment(Qt::AlignCenter);
    placeholder_label_->setStyleSheet("color: #8c8c8c;");
    placeholder_label_->hide();
}

void ModernTableView::setEmptyText(const QString& text) {
    placeholder_label_->setText(text);
    updatePlaceholder();
}

void ModernTableView::setLoading(bool loading) {
    loading_ = loading;
    updatePlaceholder();
}

void ModernTableView::resizeEvent(QResizeEvent* event) {
    QTableView::resizeEvent(event);
    updatePlaceholder();
}

void ModernTableView::updatePlaceholder() {
    if (!placeholder_label_) {
        return;
    }

    const bool show_placeholder = loading_ || rowCount() == 0;
    placeholder_label_->setVisible(show_placeholder);
    placeholder_label_->resize(viewport()->size());
    placeholder_label_->move(viewport()->pos());

    if (loading_) {
        placeholder_label_->setText(tr("加载中..."));
    }
}


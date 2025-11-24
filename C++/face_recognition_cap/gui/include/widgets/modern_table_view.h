#pragma once

#include <QTableWidget>

class QLabel;

/**
 * @brief ModernTableView 封装统一表格样式与空状态展示。
 */
class ModernTableView : public QTableWidget {
    Q_OBJECT
public:
    explicit ModernTableView(QWidget* parent = nullptr);

    void setEmptyText(const QString& text);
    void setLoading(bool loading);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePlaceholder();

    QLabel* placeholder_label_;
    bool loading_ = false;
};


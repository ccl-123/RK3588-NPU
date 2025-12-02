#pragma once

#include <QStyledItemDelegate>

/**
 * @brief 状态列的自定义绘制代理
 * 
 * 用于在表格中绘制状态标签样式，避免使用 setCellWidget 导致的列错位问题
 */
class StatusItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit StatusItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};


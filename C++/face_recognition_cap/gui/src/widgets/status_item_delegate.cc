#include "widgets/status_item_delegate.h"

#include <QPainter>
#include <QApplication>

StatusItemDelegate::StatusItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void StatusItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    // 获取状态值 (存储在 UserRole 中，1=启用，0=禁用)
    int status = index.data(Qt::UserRole).toInt();
    QString text = index.data(Qt::DisplayRole).toString();
    
    if (text.isEmpty()) {
        text = (status == 1) ? tr("启用") : tr("禁用");
    }
    
    painter->save();
    
    // 绘制选中背景
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }
    
    // 根据状态设置颜色
    QColor bgColor, textColor;
    if (status == 1) {
        // 启用状态 - 绿色
        bgColor = QColor(237, 247, 237);   // 浅绿背景
        textColor = QColor(56, 142, 60);    // 深绿文字
    } else {
        // 禁用状态 - 红色
        bgColor = QColor(253, 237, 237);   // 浅红背景
        textColor = QColor(211, 47, 47);    // 深红文字
    }
    
    // 计算标签位置（居中）
    QFontMetrics fm(option.font);
    int textWidth = fm.horizontalAdvance(text) + 16;  // 加上左右 padding
    int textHeight = fm.height() + 8;                  // 加上上下 padding
    
    QRect tagRect(
        option.rect.center().x() - textWidth / 2,
        option.rect.center().y() - textHeight / 2,
        textWidth,
        textHeight
    );
    
    // 绘制圆角背景
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(bgColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(tagRect, 4, 4);
    
    // 绘制文字
    painter->setPen(textColor);
    painter->drawText(tagRect, Qt::AlignCenter, text);
    
    painter->restore();
}

QSize StatusItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    Q_UNUSED(index)
    QFontMetrics fm(option.font);
    return QSize(fm.horizontalAdvance(tr("启用")) + 32, fm.height() + 16);
}


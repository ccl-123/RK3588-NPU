/**
 * @file attendance_list_widget.cc
 * @brief 实时考勤动态列表组件实现
 * @author CL
 * @date 2025-12-18
 */

#include "widgets/attendance_list_widget.h"
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QVariant>
#include <QLabel>
#include <QScroller>

// ============================================================================
// AttendanceItemDelegate Implementation
// ============================================================================

AttendanceItemDelegate::AttendanceItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

QSize AttendanceItemDelegate::sizeHint(const QStyleOptionViewItem& option, 
                                       const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(-1, 72); // 固定高度 72px，更宽敞
}

void AttendanceItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, 
                                   const QModelIndex& index) const {
    if (!index.isValid()) return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // 获取数据
    QString name = index.data(Qt::UserRole + 1).toString();
    QString dept = index.data(Qt::UserRole + 2).toString();
    QString timeStr = index.data(Qt::UserRole + 3).toString();
    int checkType = index.data(Qt::UserRole + 4).toInt();
    bool isStranger = index.data(Qt::UserRole + 5).toBool();
    QVariant avatarVar = index.data(Qt::UserRole + 6);

    QRect rect = option.rect;

    // 1. 绘制背景 (Hover 效果)
    if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(rect, QColor("#FAFAFA"));
    } else {
        painter->fillRect(rect, Qt::white);
    }

    // 2. 绘制头像 (左侧 40x40 圆形)
    int avatarSize = 40;
    int padding = 16;
    QRect avatarRect(rect.left() + padding, rect.top() + (rect.height() - avatarSize) / 2, 
                     avatarSize, avatarSize);
    
    QPainterPath path;
    path.addEllipse(avatarRect);
    painter->setClipPath(path);
    
    // 如果有头像且不为空，绘制图片，否则绘制默认颜色的圆 + 文字
    // 简化处理：这里暂时画一个带颜色的圆
    QColor avatarBg = isStranger ? QColor("#FF4D4F") : QColor("#1677FF");
    painter->fillRect(avatarRect, avatarBg);
    
    // 绘制头像文字（取名字第一个字）
    painter->setPen(Qt::white);
    QFont avatarFont = painter->font();
    avatarFont.setPixelSize(16);
    avatarFont.setBold(true);
    painter->setFont(avatarFont);
    painter->drawText(avatarRect, Qt::AlignCenter, name.left(1));
    
    painter->setClipping(false); // 取消裁剪

    // 3. 绘制姓名和部门 (中间)
    int textLeft = avatarRect.right() + 12;
    int textWidth = rect.width() - textLeft - 80; // 右侧预留给时间
    
    // 姓名
    QRect nameRect(textLeft, rect.top() + 14, textWidth, 22);
    painter->setPen(QColor("#262626"));
    QFont nameFont = painter->font();
    nameFont.setPixelSize(15);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, isStranger ? "陌生人" : name);
    
    // 部门 / 详情
    QRect deptRect(textLeft, nameRect.bottom() + 2, textWidth, 18);
    painter->setPen(QColor("#8C8C8C"));
    QFont deptFont = painter->font();
    deptFont.setPixelSize(12);
    deptFont.setBold(false);
    painter->setFont(deptFont);
    QString subText = isStranger ? "未注册人员" : (dept.isEmpty() ? "员工" : dept);
    painter->drawText(deptRect, Qt::AlignLeft | Qt::AlignVCenter, subText);

    // 4. 绘制时间和状态 (右侧)
    int rightPadding = 16;
    int timeWidth = 70;
    QRect rightRect(rect.right() - rightPadding - timeWidth, rect.top(), timeWidth, rect.height());
    
    // 状态标签背景 (右上角)
    // 简化：直接显示时间，状态通过颜色区分
    
    // 时间
    painter->setPen(QColor("#BFBFBF"));
    QFont timeFont = painter->font();
    timeFont.setPixelSize(12);
    painter->setFont(timeFont);
    painter->drawText(rightRect, Qt::AlignRight | Qt::AlignVCenter, timeStr);
    
    // 状态点 (时间左边)
    int statusSize = 8;
    QRect statusRect(rightRect.left() - 12, rect.center().y() - statusSize/2, statusSize, statusSize);
    QColor statusColor;
    if (isStranger) statusColor = QColor("#FF4D4F"); // Red
    else if (checkType == 2) statusColor = QColor("#FAAD14"); // Orange (Checkout)
    else statusColor = QColor("#52C41A"); // Green (Checkin)
    
    painter->setBrush(statusColor);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(statusRect);

    // 5. 分割线 (底部)
    painter->setPen(QColor("#F0F0F0"));
    painter->drawLine(rect.left() + padding, rect.bottom(), rect.right() - padding, rect.bottom());

    painter->restore();
}

// ============================================================================
// AttendanceListWidget Implementation
// ============================================================================

AttendanceListWidget::AttendanceListWidget(QWidget* parent)
    : QWidget(parent)
{
    setup_ui();
}

AttendanceListWidget::~AttendanceListWidget() {
}

void AttendanceListWidget::setup_ui() {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    list_view_ = new QListWidget(this);
    list_view_->setFrameShape(QFrame::NoFrame);
    list_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_view_->setSelectionMode(QAbstractItemView::NoSelection);
    list_view_->setFocusPolicy(Qt::NoFocus);
    list_view_->setStyleSheet("background: transparent;");
    
    // 设置代理
    list_view_->setItemDelegate(new AttendanceItemDelegate(this));
    
    // 支持触摸滑动
    QScroller::grabGesture(list_view_->viewport(), QScroller::TouchGesture);
    
    layout->addWidget(list_view_);
}

void AttendanceListWidget::addRecord(const AttendanceItem& item) {
    auto listItem = new QListWidgetItem();
    
    // 存储数据供 Delegate 使用
    listItem->setData(Qt::UserRole + 1, item.name);
    listItem->setData(Qt::UserRole + 2, item.department);
    listItem->setData(Qt::UserRole + 3, item.time.toString("HH:mm:ss"));
    listItem->setData(Qt::UserRole + 4, item.check_type);
    listItem->setData(Qt::UserRole + 5, item.is_stranger);
    listItem->setData(Qt::UserRole + 6, item.avatar_path);
    
    // 插入到第一行
    list_view_->insertItem(0, listItem);
    
    // 限制列表长度，防止内存无限增长 (保留最近50条)
    if (list_view_->count() > 50) {
        delete list_view_->takeItem(list_view_->count() - 1);
    }
}

void AttendanceListWidget::clear() {
    list_view_->clear();
}

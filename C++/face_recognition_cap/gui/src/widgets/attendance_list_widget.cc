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
    return QSize(-1, 86); // 固定高度 86px，更宽敞
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
    // bool isStranger = index.data(Qt::UserRole + 5).toBool(); // 陌生人不再显示在列表中
    QVariant avatarVar = index.data(Qt::UserRole + 6);
    int status = index.data(Qt::UserRole + 7).toInt(); // 新增状态字段
    qint64 recordMs = index.data(Qt::UserRole + 8).toLongLong();

    QRect rect = option.rect;

    // 获取主题颜色
    QColor bgColor = option.palette.base().color();
    QColor textColor = option.palette.text().color();
    
    // 判断是否为暗色模式 (简单的亮度判断)
    bool isDarkMode = (bgColor.value() < 128);
    
    // 安全检查：确保文字颜色与背景有对比度
    if (isDarkMode && textColor.value() < 128) {
        textColor = QColor("#E8E8E8"); // 暗色背景强制使用亮色文字
    } else if (!isDarkMode && textColor.value() > 128) {
        textColor = QColor("#262626"); // 亮色背景强制使用暗色文字
    }
    
    QColor subTextColor = textColor;
    subTextColor.setAlphaF(0.6); // 次要文字透明度
    
    QColor hoverColor = isDarkMode ? QColor("#363636") : QColor("#FAFAFA");
    QColor dividerColor = isDarkMode ? QColor("#3a3a3a") : QColor("#F0F0F0");

    // 1. 绘制背景 (Hover 效果)
    if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(rect, hoverColor);
    } else {
        painter->fillRect(rect, bgColor);
    }

    // 新记录轻微高亮，便于捕捉最新签到
    if (recordMs > 0) {
        qint64 ageMs = QDateTime::currentMSecsSinceEpoch() - recordMs;
        if (ageMs >= 0 && ageMs < 2500) {
            double t = 1.0 - static_cast<double>(ageMs) / 2500.0;
            QColor highlight = (checkType == 2) ? QColor("#1890ff") : QColor("#52c41a");
            int alpha = isDarkMode ? 45 : 30;
            highlight.setAlphaF((alpha + (40 * t)) / 255.0);
            painter->fillRect(rect, highlight);
        }
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
    // 简化处理：这里暂时画一个默认颜色的圆
    QColor avatarBg = QColor("#1677FF"); // 统一使用默认蓝色
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
    int textWidth = rect.width() - textLeft - 20; // 右侧信息放在主行末尾，减少空白
    
    // 姓名
    QRect nameRect(textLeft, rect.top() + 14, textWidth, 22);
    painter->setPen(textColor);
    QFont nameFont = painter->font();
    nameFont.setPixelSize(16);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    QFontMetrics nameFm(nameFont);
    QString elidedName = nameFm.elidedText(name, Qt::ElideRight, nameRect.width());
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
    
    // 部门 / 详情
    QRect deptRect(textLeft, nameRect.bottom() + 4, textWidth, 18);
    painter->setPen(subTextColor);
    QFont deptFont = painter->font();
    deptFont.setPixelSize(13);
    deptFont.setBold(false);
    painter->setFont(deptFont);
    QString subText = dept.isEmpty() ? "员工" : dept;
    painter->drawText(deptRect, Qt::AlignLeft | Qt::AlignVCenter, subText);

    // 4. 绘制时间/类型/相似度 (同一行，靠近姓名区域)
    QFont infoFont = painter->font();
    infoFont.setPixelSize(15);
    infoFont.setBold(true);
    painter->setFont(infoFont);
    QFontMetrics infoFm(infoFont);

    QString typeText = (checkType == 2) ? "签退" : "签到";
    QString simText = QString("相似度 %1%").arg(
        index.data(Qt::UserRole + 9).toFloat(), 0, 'f', 2);

    int chipHeight = 24;
    int chipPadding = 12;
    int typeWidth = infoFm.horizontalAdvance(typeText) + chipPadding * 2;
    int timeWidth = infoFm.horizontalAdvance(timeStr) + 6;
    int simWidth = infoFm.horizontalAdvance(simText) + 6;
    int gap = 10;
    int abnormalWidth = 0;
    if (status == 2 || status == 3) {
        QString abnormalTextTmp = (status == 2) ? "迟到" : "早退";
        abnormalWidth = infoFm.horizontalAdvance(abnormalTextTmp) + chipPadding * 2 + gap;
    }
    int infoTotal = typeWidth + abnormalWidth + gap + timeWidth + gap + simWidth;
    int infoRight = rect.right() - 12;
    int minInfoLeft = textLeft + 8;
    int infoLeft = infoRight - infoTotal;
    if (infoLeft < minInfoLeft) {
        infoLeft = minInfoLeft;
    }
    int infoY = rect.center().y() - chipHeight / 2;

    int timeStart = infoLeft + typeWidth + abnormalWidth + gap;
    QRect timeRect(timeStart, infoY, timeWidth, chipHeight);
    QRect simRect(timeRect.right() + gap, infoY, simWidth, chipHeight);

    // 状态点 (信息区左侧)
    int statusSize = 14;
    QRect statusRect(infoLeft - 18, rect.center().y() - statusSize / 2, statusSize, statusSize);
    
    // 颜色逻辑修改：
    // status: 1=正常, 2=迟到, 3=早退
    // 异常状态 (迟到/早退) 显示红色，正常显示绿色
    QColor statusColor;
    if (status == 2 || status == 3) {
        statusColor = QColor("#FF4D4F"); // Red (异常/迟到/早退)
    } else {
        statusColor = QColor("#52C41A"); // Green (正常)
    }
    
    painter->setBrush(statusColor);
    painter->setPen(QPen(statusColor.darker(120), 1));
    painter->drawEllipse(statusRect.adjusted(0, 0, -1, -1));
    painter->setBrush(QColor(255, 255, 255, isDarkMode ? 80 : 120));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(statusRect.adjusted(3, 3, -3, -3));

    // 签到/签退标签 + 异常标签
    QColor typeTextColor = Qt::white;

    QString abnormalText;
    if (status == 2) {
        abnormalText = "迟到";
    } else if (status == 3) {
        abnormalText = "早退";
    }

    int chipY = infoY;
    QFont chipFont = painter->font();
    chipFont.setPixelSize(15);
    chipFont.setBold(true);
    painter->setFont(chipFont);
    QFontMetrics chipFm(chipFont);
    QRect typeRect(infoLeft, chipY, typeWidth, chipHeight);

    QLinearGradient typeGrad(typeRect.topLeft(), typeRect.bottomRight());
    if (checkType == 2) {
        typeGrad.setColorAt(0, QColor("#3ba0ff"));
        typeGrad.setColorAt(1, QColor("#1677ff"));
    } else {
        typeGrad.setColorAt(0, QColor("#6ad86a"));
        typeGrad.setColorAt(1, QColor("#34a853"));
    }
    painter->setBrush(typeGrad);
    painter->setPen(QPen(QColor(255, 255, 255, isDarkMode ? 50 : 80), 1));
    painter->drawRoundedRect(typeRect, 10, 10);
    painter->setPen(typeTextColor);
    painter->drawText(typeRect, Qt::AlignCenter, typeText);

    if (!abnormalText.isEmpty()) {
        int abnormalChipWidth = chipFm.horizontalAdvance(abnormalText) + chipPadding * 2;
        QRect abnormalRect(typeRect.right() + 8, chipY, abnormalChipWidth, chipHeight);
        QLinearGradient abnormalGrad(abnormalRect.topLeft(), abnormalRect.bottomRight());
        abnormalGrad.setColorAt(0, QColor("#ff7a7a"));
        abnormalGrad.setColorAt(1, QColor("#ff4d4f"));
        painter->setBrush(abnormalGrad);
        painter->setPen(QPen(QColor(255, 255, 255, isDarkMode ? 50 : 80), 1));
        painter->drawRoundedRect(abnormalRect, 10, 10);
        painter->setPen(Qt::white);
        painter->drawText(abnormalRect, Qt::AlignCenter, abnormalText);
    }

    // 时间 + 相似度（同一行）
    QRect infoStripRect(timeRect.left() - 8, chipY - 2,
                        simRect.right() - timeRect.left() + 16, chipHeight + 4);
    QLinearGradient stripGrad(infoStripRect.topLeft(), infoStripRect.bottomRight());
    if (isDarkMode) {
        stripGrad.setColorAt(0, QColor(255, 255, 255, 28));
        stripGrad.setColorAt(1, QColor(255, 255, 255, 14));
    } else {
        stripGrad.setColorAt(0, QColor(255, 255, 255, 200));
        stripGrad.setColorAt(1, QColor(230, 236, 245, 160));
    }
    painter->setBrush(stripGrad);
    painter->setPen(QPen(QColor(0, 0, 0, isDarkMode ? 0 : 20), 1));
    painter->drawRoundedRect(infoStripRect, 12, 12);

    painter->setPen(subTextColor);
    QFont infoSubFont = painter->font();
    infoSubFont.setPixelSize(14);
    infoSubFont.setBold(false);
    painter->setFont(infoSubFont);
    painter->drawText(timeRect, Qt::AlignCenter, timeStr);
    painter->drawText(simRect, Qt::AlignCenter, simText);

    // 5. 分割线 (底部)
    painter->setPen(dividerColor);
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
    // list_view_->setStyleSheet("background: transparent;"); // 移除内联样式，让全局 QSS 控制背景色
    
    // 设置代理
    list_view_->setItemDelegate(new AttendanceItemDelegate(this));
    
    // 支持触摸滑动
    QScroller::grabGesture(list_view_->viewport(), QScroller::TouchGesture);
    
    layout->addWidget(list_view_);
}

void AttendanceListWidget::addRecord(const AttendanceItem& item) {
    items_.insert(items_.begin(), item);
    if (items_.size() > 50) {
        items_.pop_back();
    }

    rebuildList();
}

void AttendanceListWidget::clear() {
    items_.clear();
    list_view_->clear();
}

void AttendanceListWidget::setFilter(FilterType filter) {
    if (current_filter_ == filter) {
        return;
    }
    current_filter_ = filter;
    rebuildList();
}

bool AttendanceListWidget::matchesFilter(const AttendanceItem& item) const {
    switch (current_filter_) {
        case FilterType::CheckIn:
            return item.check_type == 1;
        case FilterType::CheckOut:
            return item.check_type == 2;
        case FilterType::Abnormal:
            return item.status == 2 || item.status == 3;
        case FilterType::All:
        default:
            return true;
    }
}

void AttendanceListWidget::rebuildList() {
    list_view_->clear();

    for (const auto& item : items_) {
        if (!matchesFilter(item)) {
            continue;
        }
        auto listItem = new QListWidgetItem();
        listItem->setData(Qt::UserRole + 1, item.name);
        listItem->setData(Qt::UserRole + 2, item.department);
        listItem->setData(Qt::UserRole + 3, item.time.toString("HH:mm:ss"));
        listItem->setData(Qt::UserRole + 4, item.check_type);
        listItem->setData(Qt::UserRole + 6, item.avatar_path);
        listItem->setData(Qt::UserRole + 7, item.status);
        listItem->setData(Qt::UserRole + 8, item.time.toMSecsSinceEpoch());
        listItem->setData(Qt::UserRole + 9, item.similarity);

        list_view_->addItem(listItem);
    }
}

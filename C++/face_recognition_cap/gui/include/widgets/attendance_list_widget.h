/**
 * @file attendance_list_widget.h
 * @brief 实时考勤动态列表组件 (Dashboard Feed Style)
 * @author Gemini-3Pro Agent
 * @date 2025-12-18
 */

#pragma once

#include <QWidget>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QDateTime>
#include <vector>

// 数据结构：单条考勤记录
struct AttendanceItem {
    int user_id;
    QString name;
    QString department;
    QString avatar_path; // 抓拍图路径
    QDateTime time;
    int check_type;      // 1=签到, 2=签退
    int status;          // 1=正常, 2=迟到, 3=早退 (新增)
    float similarity;
};

// 自定义绘制代理
class AttendanceItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit AttendanceItemDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option, 
               const QModelIndex& index) const override;
               
    QSize sizeHint(const QStyleOptionViewItem& option, 
                   const QModelIndex& index) const override;
};

// 主组件
class AttendanceListWidget : public QWidget {
    Q_OBJECT

public:
    enum class FilterType {
        All,
        CheckIn,
        CheckOut,
        Abnormal
    };

    explicit AttendanceListWidget(QWidget* parent = nullptr);
    ~AttendanceListWidget();

    // 添加一条新记录 (会自动插入到顶部)
    void addRecord(const AttendanceItem& item);
    
    // 清空列表
    void clear();

    // 设置当前过滤器
    void setFilter(FilterType filter);

private:
    void setup_ui();
    void rebuildList();
    bool matchesFilter(const AttendanceItem& item) const;
    
    QListWidget* list_view_;
    std::vector<AttendanceItem> items_; // 本地缓存，用于 Delegate 访问
    FilterType current_filter_ = FilterType::All;
};


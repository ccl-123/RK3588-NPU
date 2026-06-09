/**
 * @file side_menu.h
 * @brief 现代化侧边导航菜单
 * @author CL
 * @date 2025-11-25
 */

#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QListWidget;
class QLabel;

/**
 * @brief SideMenu 现代化左侧导航菜单
 * 
 * 特性：
 * - 浅色背景 + 蓝色高亮
 * - Logo 区域
 * - 图标 + 文字菜单项
 * - 版本信息底部显示
 */
class SideMenu : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString key;   // 路由键
        QString text;  // 显示文本
        QString icon;  // 图标路径
    };

    explicit SideMenu(QWidget* parent = nullptr);

    void setCompactMode(bool compact);

    /**
     * @brief 设置菜单项
     */
    void setItems(const QList<Item>& items);

    /**
     * @brief 设置当前激活项
     */
    void setActiveKey(const QString& key);

signals:
    /**
     * @brief 路由变化信号
     */
    void routeChanged(const QString& key);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void handleSelectionChanged();

private:
    void applyCompactMode();

    QLabel* logo_icon_;
    QLabel* logo_label_;
    QLabel* version_label_;
    QListWidget* list_widget_;
    bool compact_mode_;
};

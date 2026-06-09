#pragma once

#include <QStringList>
#include <QWidget>

class IconButton;
class QLabel;
class QMenu;
class QHBoxLayout;
class QMouseEvent;
class NewsTicker;

/**
 * @brief TitleBar 自定义顶部栏，包含 logo、面包屑与用户菜单。
 */
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setBreadcrumb(const QStringList& crumbs);
    void setUserMenu(QMenu* menu);
    void setHeadlines(const QStringList& headlines);
    void setSideBarWidth(int width);
    void updateMaximizeIcon();  // 根据窗口状态更新图标

signals:
    void requestMinimize();
    void requestMaximize();
    void requestClose();
    void requestToggleTheme();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    bool isDragArea(const QPoint& pos) const;

    QPoint drag_pos_;
    QWidget* left_spacer_;
    QLabel* title_label_;
    QLabel* breadcrumb_label_;
    NewsTicker* news_ticker_;
    IconButton* theme_button_;
    IconButton* user_button_;
    IconButton* minimize_button_;
    IconButton* maximize_button_;  // 最大化按钮
    IconButton* close_button_;
    bool dragging_;
};

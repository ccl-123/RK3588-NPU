#pragma once

#include <QStringList>
#include <QWidget>

class IconButton;
class QLabel;
class QMenu;
class QHBoxLayout;

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

signals:
    void requestMinimize();
    void requestMaximize();
    void requestClose();
    void requestToggleTheme();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QPoint drag_pos_;
    QLabel* title_label_;
    QLabel* breadcrumb_label_;
    IconButton* theme_button_;
    IconButton* user_button_;
    IconButton* minimize_button_;
    IconButton* close_button_;
};


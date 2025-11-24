#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QListWidget;

/**
 * @brief SideMenu 左侧导航，支持路由 key。
 */
class SideMenu : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString key;
        QString text;
        QString icon;
    };

    explicit SideMenu(QWidget* parent = nullptr);

    void setItems(const QList<Item>& items);
    void setActiveKey(const QString& key);

signals:
    void routeChanged(const QString& key);

private slots:
    void handleSelectionChanged();

private:
    QListWidget* list_widget_;
};


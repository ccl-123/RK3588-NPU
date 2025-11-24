#pragma once

#include <QObject>
#include <QPointer>
#include <QStackedWidget>
#include <QString>
#include <QHash>

/**
 * @brief UiRouter 基于 key 管理 QStackedWidget 的页面切换。
 */
class UiRouter : public QObject {
    Q_OBJECT
public:
    explicit UiRouter(QStackedWidget* stack, QObject* parent = nullptr);

    void registerPage(const QString& key, QWidget* widget);
    void navigateTo(const QString& key);
    QString currentKey() const;

signals:
    void routeChanged(const QString& key, QWidget* widget);

private:
    QPointer<QStackedWidget> stack_;
    QHash<QString, int> indices_;
    QString current_key_;
};


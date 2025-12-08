/**
 * @file holiday_service.h
 * @brief 节假日查询服务（jiejiariapi.com）
 */

#pragma once

#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDate>
#include <QList>
#include <QPair>

class HolidayService : public QObject {
    Q_OBJECT
public:
    static HolidayService* instance();

    // 查询今日节假日状态，并计算距离下次假期
    void requestTodayAndNext();

signals:
    void holidayStatusUpdated(const QString& status, const QString& countdown);
    void errorOccurred(const QString& message);

private:
    explicit HolidayService(QObject* parent = nullptr);
    ~HolidayService() = default;

    void requestToday();
    void requestHolidaysOfYear(int year);
    void onTodayFinished(QNetworkReply* reply);
    void onYearFinished(QNetworkReply* reply, int year);
    void tryEmit();

    QString parseTodayStatus(QNetworkReply* reply, QString& holiday_name, bool& is_holiday);
    QString computeCountdownText(const QList<QPair<QDate, QString>>& holidays, const QDate& today);
    bool hasUpcoming(const QList<QPair<QDate, QString>>& holidays, const QDate& today, QDate& next, QString& name) const;
    void getWithRetry(const QUrl& url,
                      QPointer<QNetworkReply>& slot,
                      int retries_left,
                      const QString& tag,
                      const std::function<void(QNetworkReply*)>& on_ok,
                      const std::function<void()>& on_fail);

    static HolidayService* instance_;

    QNetworkAccessManager* network_manager_;
    QPointer<QNetworkReply> today_reply_;
    QPointer<QNetworkReply> year_reply_;

    bool today_ready_;
    bool year_ready_;
    bool today_is_holiday_;
    QString today_name_;
    QList<QPair<QDate, QString>> holidays_;
    QDate today_date_;
    bool next_year_requested_;

    // 缓存上一次成功的结果
    bool has_cache_ = false;
    QString cached_status_;
    QString cached_countdown_;
};



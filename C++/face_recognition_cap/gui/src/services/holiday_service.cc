/**
 * @file holiday_service.cc
 * @brief 节假日查询服务（jiejiariapi.com）
 */

#include "services/holiday_service.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QTimer>
#include <spdlog/spdlog.h>

#include "config/config.h"

namespace {
constexpr int kTimeoutMs = 2000;
constexpr int kMaxRetries = 2;
}

HolidayService* HolidayService::instance_ = nullptr;

HolidayService* HolidayService::instance() {
    if (!instance_) {
        instance_ = new HolidayService();
    }
    return instance_;
}

HolidayService::HolidayService(QObject* parent)
    : QObject(parent)
    , network_manager_(new QNetworkAccessManager(this))
    , today_ready_(false)
    , year_ready_(false)
    , today_is_holiday_(false)
    , next_year_requested_(false)
{
}

void HolidayService::requestTodayAndNext() {
    today_ready_ = false;
    year_ready_ = false;
    today_is_holiday_ = false;
    today_name_.clear();
    holidays_.clear();
    today_date_ = QDate::currentDate();
    next_year_requested_ = false;

    requestToday();
    requestHolidaysOfYear(today_date_.year());
}

void HolidayService::requestToday() {
    QUrl url(QString("%1/is_holiday").arg(Config::API::HOLIDAY_BASE));
    QUrlQuery q;
    q.addQueryItem("date", today_date_.toString("yyyy-MM-dd"));
    url.setQuery(q);

    auto on_fail = [this]() {
        today_is_holiday_ = false;
        today_name_.clear();
        today_ready_ = true;
        tryEmit();
        if (has_cache_) {
            emit holidayStatusUpdated(cached_status_, cached_countdown_);
        }
    };

    getWithRetry(url, today_reply_, kMaxRetries, "today",
                 [this](QNetworkReply* r) { onTodayFinished(r); },
                 on_fail);
}

void HolidayService::requestHolidaysOfYear(int year) {
    QUrl url(QString("%1/holidays/%2").arg(Config::API::HOLIDAY_BASE).arg(year));

    auto on_fail = [this, year]() {
        year_ready_ = true;
        if (!next_year_requested_ && year < today_date_.year() + 1) {
            next_year_requested_ = true;
            emit holidayStatusUpdated(QStringLiteral("今日上班"), QStringLiteral("查询下一年度假期..."));
            requestHolidaysOfYear(today_date_.year() + 1);
            return;
        }
        tryEmit();
        if (has_cache_) {
            emit holidayStatusUpdated(cached_status_, cached_countdown_);
        }
    };

    getWithRetry(url, year_reply_, kMaxRetries, QString("year-%1").arg(year),
                 [this, year](QNetworkReply* r) { onYearFinished(r, year); },
                 on_fail);
}

QString HolidayService::parseTodayStatus(QNetworkReply* reply, QString& holiday_name, bool& is_holiday) {
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return QStringLiteral("节假日查询失败");
    }
    QJsonObject root = doc.object();
    QJsonObject dataObj = root.value("data").toObject();
    if (dataObj.isEmpty()) {
        dataObj = root;
    }
    // 官方文档字段 is_holiday / holiday 或 isOffDay / name
    if (dataObj.contains("is_holiday"))
        is_holiday = dataObj.value("is_holiday").toBool(false);
    else if (dataObj.contains("isOffDay"))
        is_holiday = dataObj.value("isOffDay").toBool(false);
    else
        is_holiday = false;

    if (dataObj.contains("holiday"))
        holiday_name = dataObj.value("holiday").toString();
    else if (dataObj.contains("name"))
        holiday_name = dataObj.value("name").toString();
    if (holiday_name.isEmpty() && is_holiday) {
        holiday_name = QStringLiteral("节假日");
    }

    if (is_holiday) {
        return QStringLiteral("今天放假：%1").arg(holiday_name.isEmpty() ? QStringLiteral("节假日") : holiday_name);
    } else {
        return QStringLiteral("今天上班");
    }
}

void HolidayService::onTodayFinished(QNetworkReply* reply) {
    QString name;
    bool is_holiday = false;
    QString status = parseTodayStatus(reply, name, is_holiday);
    today_is_holiday_ = is_holiday;
    today_name_ = name;
    today_ready_ = true;
    // 先暂存，倒计时待 yearReady
    tryEmit();
}

bool HolidayService::hasUpcoming(const QList<QPair<QDate, QString>>& holidays, const QDate& today, QDate& next, QString& name) const {
    for (const auto& item : holidays) {
        if (item.first >= today) {
            next = item.first;
            name = item.second;
            return true;
        }
    }
    return false;
}

QString HolidayService::computeCountdownText(const QList<QPair<QDate, QString>>& holidays, const QDate& today) {
    QDate next;
    QString name;
    if (!hasUpcoming(holidays, today, next, name)) {
        return QStringLiteral("查询下一年度假期...");
    }
    int days = today.daysTo(next);
    if (days == 0) {
        return QStringLiteral("今天是假期：%1").arg(name.isEmpty() ? QStringLiteral("节假日") : name);
    }
    return QStringLiteral("距离%1还有 %2 天").arg(name.isEmpty() ? QStringLiteral("下次放假") : name).arg(days);
}

void HolidayService::onYearFinished(QNetworkReply* reply, int year) {
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
        QJsonObject root = doc.object();
        QJsonObject dataObj = root.value("data").toObject();
        if (dataObj.isEmpty()) {
            // 部分接口可能直接返回字典在根
            dataObj = root;
        }
        for (auto it = dataObj.begin(); it != dataObj.end(); ++it) {
            if (!it.value().isObject()) continue;
            QJsonObject obj = it.value().toObject();
            QString dateStr = obj.value("date").toString();
            bool isOff = obj.value("isOffDay").toBool(true);
            QString name = obj.value("name").toString();
            QDate d = QDate::fromString(dateStr, "yyyy-MM-dd");
            if (d.isValid() && isOff) {
                holidays_.push_back(qMakePair(d, name));
            }
        }
        std::sort(holidays_.begin(), holidays_.end(), [](const QPair<QDate, QString>& a, const QPair<QDate, QString>& b){
            return a.first < b.first;
        });
    } else {
        spdlog::warn("HolidayService year parse failed");
    }
    year_ready_ = true;
    tryEmit();
}

void HolidayService::tryEmit() {
    if (!today_ready_ || !year_ready_) return;
    bool is_weekend = today_date_.dayOfWeek() >= 6;
    QString today_label = today_is_holiday_
        ? QStringLiteral("今日假期：%1").arg(today_name_.isEmpty() ? QStringLiteral("节假日") : today_name_)
        : (is_weekend ? QStringLiteral("今日休息") : QStringLiteral("今日上班"));

    QDate next;
    QString name;
    if (!hasUpcoming(holidays_, today_date_, next, name)) {
        if (!next_year_requested_) {
            next_year_requested_ = true;
            emit holidayStatusUpdated(today_label, QStringLiteral("查询下一年度假期..."));
            requestHolidaysOfYear(today_date_.year() + 1);
            return;  // 等待下一年度数据
        } else {
            // 已尝试下一年仍无数据，给出占位提示
            emit holidayStatusUpdated(today_label, QStringLiteral("暂未获取节假日数据"));
            return;
        }
    }

    QString countdown = computeCountdownText(holidays_, today_date_);
    emit holidayStatusUpdated(today_label, countdown);
    cached_status_ = today_label;
    cached_countdown_ = countdown;
    has_cache_ = true;
}

void HolidayService::getWithRetry(const QUrl& url,
                      QPointer<QNetworkReply>& slot,
                      int retries_left,
                      const QString& tag,
                      const std::function<void(QNetworkReply*)>& on_ok,
                      const std::function<void()>& on_fail) {
    if (slot) {
        slot->abort();
        slot->deleteLater();
        slot.clear();
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");

    slot = network_manager_->get(req);
    QPointer<QNetworkReply> reply = slot;

    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [reply, tag]() {
        if (reply && reply->isRunning()) {
            spdlog::warn("HolidayService {} timeout", tag.toStdString());
            reply->abort();
        }
    });
    timer->start(kTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, tag, retries_left, url, &slot, on_ok, on_fail]() {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }

        if (!reply) return;

        if (reply->error() == QNetworkReply::NoError) {
            on_ok(reply);
            reply->deleteLater();
            slot.clear();
            return;
        }

        QString err = reply->errorString();
        reply->deleteLater();
        slot.clear();

        if (retries_left > 0) {
            int backoff = kTimeoutMs * (1 << (kMaxRetries - retries_left));
            spdlog::warn("HolidayService {} failed: {}, retry in {} ms", tag.toStdString(), err.toStdString(), backoff);
            QTimer::singleShot(backoff, this, [this, url, &slot, retries_left, tag, on_ok, on_fail]() {
                getWithRetry(url, slot, retries_left - 1, tag, on_ok, on_fail);
            });
            return;
        }

        spdlog::warn("HolidayService {} failed after retries: {}", tag.toStdString(), err.toStdString());
        on_fail();
        if (has_cache_) {
            emit holidayStatusUpdated(cached_status_, cached_countdown_);
        }
    });
}

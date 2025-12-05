/**
 * @file weather_service.cc
 * @brief 天气服务类实现
 * @author CL
 * @date 2025-12-05
 */

#include "services/weather_service.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <spdlog/spdlog.h>
#include <functional>

WeatherService* WeatherService::instance_ = nullptr;

WeatherService* WeatherService::instance() {
    if (!instance_) {
        instance_ = new WeatherService();
    }
    return instance_;
}

WeatherService::WeatherService(QObject* parent)
    : QObject(parent)
    , network_manager_(new QNetworkAccessManager(this))
    , current_city_(tr("定位中..."))
{
}

void WeatherService::setCity(const QString& city) {
    current_city_ = city;
}

void WeatherService::abortIfRunning(QPointer<QNetworkReply>& slot) {
    if (slot) {
        slot->abort();
        slot->deleteLater();
        slot.clear();
    }
}

void WeatherService::sendGet(const QUrl& url,
                             const std::function<void(QNetworkReply*)>& on_ok,
                             QPointer<QNetworkReply>& slot,
                             int retries_left,
                             const QString& tag) {
    // 单类型请求只保留一个：先取消在途，再发起
    abortIfRunning(slot);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    request.setRawHeader("Referer", "https://open-meteo.com/");

    slot = network_manager_->get(request);

    // 超时控制（QTimer + abort）
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, tag, reply = slot]() {
        if (reply) {
            spdlog::warn("{} request timeout", tag.toStdString());
            reply->abort();
        }
    });
    timer->start(kRequestTimeoutMs);

    connect(slot, &QNetworkReply::finished, this,
            [this, on_ok, retries_left, tag, url, timer, reply = slot, &slot]() mutable {
        timer->stop();
        timer->deleteLater();

        if (!reply) {
            slot.clear();
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            on_ok(reply);
            reply->deleteLater();
            slot.clear();
            return;
        }

        // 失败处理与重试
        QString err = reply->errorString();
        reply->deleteLater();
        slot.clear();

        if (retries_left > 0) {
            int backoff = kRetryDelayMs * (1 << (kMaxRetries - retries_left));
            spdlog::warn("{} failed: {}, retry in {} ms", tag.toStdString(), err.toStdString(), backoff);
            QTimer::singleShot(backoff, this, [this, url, on_ok, retries_left, tag, &slot]() mutable {
                sendGet(url, on_ok, slot, retries_left - 1, tag);
            });
            return;
        }

        spdlog::warn("{} failed after retries: {}", tag.toStdString(), err.toStdString());
        emit errorOccurred(QString("%1 failed: %2").arg(tag, err));

        // 回退到最近一次成功的缓存
        if (tag == "weather" && has_weather_cache_) {
            emit weatherUpdated(current_city_, cached_temp_, cached_desc_);
        } else if (tag == "aqi" && has_aqi_cache_) {
            emit aqiUpdated(cached_aqi_, cached_aqi_level_);
        } else if (tag == "uv" && has_uv_cache_) {
            emit uvUpdated(cached_uv_, cached_uv_level_);
        } else if (tag == "daily" && has_sentence_cache_) {
            emit dailySentenceUpdated(cached_sentence_en_, cached_sentence_from_);
        }
    });
}

void WeatherService::requestLocation() {
    // 使用 ip-api.com 获取设备位置
    QUrl url("http://ip-api.com/json/?lang=zh-CN");
    sendGet(url,
            [this](QNetworkReply* reply) { onLocationReplyFinished(reply); },
            location_reply_,
            kMaxRetries,
            "location");
    spdlog::debug("Location request sent to ip-api.com");
}

void WeatherService::onLocationReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QString status = root["status"].toString();
            
            if (status == "success") {
                current_city_ = root["city"].toString();
                double lat = root["lat"].toDouble();
                double lon = root["lon"].toDouble();
                
                emit locationUpdated(current_city_, lat, lon);
                spdlog::info("Location detected: {} (lat: {}, lon: {})", 
                    current_city_.toStdString(), lat, lon);
            } else {
                emit errorOccurred("Location API returned error status");
            }
        } else {
            emit errorOccurred("Location JSON parse failed");
        }
    } else {
        emit errorOccurred(QString("Location request failed: %1").arg(reply->errorString()));
    }
    reply->deleteLater();
}

void WeatherService::requestWeather(double lat, double lon) {
    // 1. 天气请求
    QString weatherUrlStr = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current_weather=true")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    sendGet(QUrl(weatherUrlStr),
            [this](QNetworkReply* reply) { onWeatherReplyFinished(reply); },
            weather_reply_,
            kMaxRetries,
            "weather");

    // 2. AQI 请求
    QString aqiUrlStr = QString("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%1&longitude=%2&current=us_aqi,pm2_5")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    sendGet(QUrl(aqiUrlStr),
            [this](QNetworkReply* reply) { onAqiReplyFinished(reply); },
            aqi_reply_,
            kMaxRetries,
            "aqi");

    // 3. UV 请求
    QString uvUrlStr = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=uv_index&timezone=Asia/Shanghai")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    sendGet(QUrl(uvUrlStr),
            [this](QNetworkReply* reply) { onUvReplyFinished(reply); },
            uv_reply_,
            kMaxRetries,
            "uv");
}

void WeatherService::onWeatherReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current_weather"].toObject();
            
            double temp = current["temperature"].toDouble();
            int weatherCode = current["weathercode"].toInt();
            
            QString weatherDesc = weatherCodeToString(weatherCode);
            QString tempStr = QString("%1°").arg(temp, 0, 'f', 0);
            
            // 缓存成功结果
            cached_temp_ = tempStr;
            cached_desc_ = weatherDesc;
            has_weather_cache_ = true;

            emit weatherUpdated(current_city_, tempStr, weatherDesc);
        }
    }
    reply->deleteLater();
}

void WeatherService::onAqiReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current"].toObject();
            
            int aqi = current["us_aqi"].toInt();
            QString level = aqiToLevel(aqi);
            
            cached_aqi_ = aqi;
            cached_aqi_level_ = level;
            has_aqi_cache_ = true;

            emit aqiUpdated(aqi, level);
        }
    }
    reply->deleteLater();
}

void WeatherService::onUvReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current"].toObject();
            double uv = current["uv_index"].toDouble();
            QString level = uvToLevel(uv);
            
            cached_uv_ = uv;
            cached_uv_level_ = level;
            has_uv_cache_ = true;

            emit uvUpdated(uv, level);
        }
    }
    reply->deleteLater();
}

void WeatherService::requestDailySentence() {
    QUrl url("https://v1.hitokoto.cn/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    
    sendGet(url,
            [this](QNetworkReply* reply) { onDailySentenceReplyFinished(reply); },
            sentence_reply_,
            kMaxRetries,
            "daily");
}

void WeatherService::onDailySentenceReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QString hitokoto = root["hitokoto"].toString();
            QString from = root["from"].toString();
            
            cached_sentence_en_ = hitokoto;
            cached_sentence_from_ = from.isEmpty() ? "" : QString("—— %1").arg(from);
            has_sentence_cache_ = true;

            emit dailySentenceUpdated(hitokoto, from.isEmpty() ? "" : QString("—— %1").arg(from));
        }
    }
    reply->deleteLater();
}

QString WeatherService::weatherCodeToString(int code) {
    switch (code) {
        case 0: return tr("晴");
        case 1: return tr("晴");
        case 2: return tr("多云");
        case 3: return tr("阴");
        case 45: case 48: return tr("雾");
        case 51: case 53: case 55: return tr("小雨");
        case 56: case 57: return tr("冻雨");
        case 61: case 63: return tr("中雨");
        case 65: return tr("大雨");
        case 66: case 67: return tr("冻雨");
        case 71: case 73: return tr("小雪");
        case 75: return tr("大雪");
        case 77: return tr("雪粒");
        case 80: case 81: case 82: return tr("阵雨");
        case 85: case 86: return tr("阵雪");
        case 95: return tr("雷雨");
        case 96: case 99: return tr("冰雹");
        default: return tr("未知");
    }
}

QString WeatherService::aqiToLevel(int aqi) {
    if (aqi <= 50) return tr("优");
    if (aqi <= 100) return tr("良");
    if (aqi <= 150) return tr("轻度");
    if (aqi <= 200) return tr("中度");
    if (aqi <= 300) return tr("重度");
    return tr("严重");
}

QString WeatherService::uvToLevel(double uv) {
    if (uv <= 2) return tr("低");
    if (uv <= 5) return tr("中等");
    if (uv <= 7) return tr("高");
    if (uv <= 10) return tr("很高");
    return tr("极高");
}

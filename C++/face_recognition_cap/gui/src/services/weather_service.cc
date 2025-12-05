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
#include <spdlog/spdlog.h>

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

void WeatherService::requestLocation() {
    // 使用 ip-api.com 获取设备位置
    QUrl url("http://ip-api.com/json/?lang=zh-CN");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    
    QNetworkReply* reply = network_manager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onLocationReplyFinished(reply);
    });
    
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
    QNetworkRequest weatherReq((QUrl(weatherUrlStr)));
    weatherReq.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    
    QNetworkReply* weatherReply = network_manager_->get(weatherReq);
    connect(weatherReply, &QNetworkReply::finished, this, [this, weatherReply]() {
        onWeatherReplyFinished(weatherReply);
    });

    // 2. AQI 请求
    QString aqiUrlStr = QString("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%1&longitude=%2&current=us_aqi,pm2_5")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    QNetworkRequest aqiReq((QUrl(aqiUrlStr)));
    aqiReq.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");

    QNetworkReply* aqiReply = network_manager_->get(aqiReq);
    connect(aqiReply, &QNetworkReply::finished, this, [this, aqiReply]() {
        onAqiReplyFinished(aqiReply);
    });

    // 3. UV 请求
    QString uvUrlStr = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current=uv_index&timezone=Asia/Shanghai")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    QNetworkRequest uvReq((QUrl(uvUrlStr)));
    uvReq.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    
    QNetworkReply* uvReply = network_manager_->get(uvReq);
    connect(uvReply, &QNetworkReply::finished, this, [this, uvReply]() {
        onUvReplyFinished(uvReply);
    });
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
            
            emit uvUpdated(uv, level);
        }
    }
    reply->deleteLater();
}

void WeatherService::requestDailySentence() {
    QUrl url("https://v1.hitokoto.cn/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    
    QNetworkReply* reply = network_manager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onDailySentenceReplyFinished(reply);
    });
}

void WeatherService::onDailySentenceReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QString hitokoto = root["hitokoto"].toString();
            QString from = root["from"].toString();
            
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

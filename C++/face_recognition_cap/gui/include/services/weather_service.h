/**
 * @file weather_service.h
 * @brief 天气服务类 - 封装网络请求逻辑
 * @author CL
 * @date 2025-12-05
 */

#ifndef GUI_SERVICES_WEATHER_SERVICE_H_
#define GUI_SERVICES_WEATHER_SERVICE_H_

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class WeatherService : public QObject {
    Q_OBJECT

public:
    static WeatherService* instance();

    // 请求逻辑
    void requestLocation();
    void setCity(const QString& city); // 手动设置城市
    void requestWeather(double lat, double lon);
    void requestDailySentence();
    
    // 辅助函数
    static QString weatherCodeToString(int code);
    static QString aqiToLevel(int aqi);
    static QString uvToLevel(double uv);

signals:
    void locationUpdated(const QString& city, double lat, double lon);
    void weatherUpdated(const QString& city, const QString& temp, const QString& desc);
    void aqiUpdated(int aqi, const QString& level);
    void uvUpdated(double uv, const QString& level);
    void dailySentenceUpdated(const QString& en, const QString& cn);
    
    void errorOccurred(const QString& message);

private:
    explicit WeatherService(QObject* parent = nullptr);
    ~WeatherService() = default;

    void onLocationReplyFinished(QNetworkReply* reply);
    void onWeatherReplyFinished(QNetworkReply* reply);
    void onAqiReplyFinished(QNetworkReply* reply);
    void onUvReplyFinished(QNetworkReply* reply);
    void onDailySentenceReplyFinished(QNetworkReply* reply);

    static WeatherService* instance_;
    
    QNetworkAccessManager* network_manager_;
    QString current_city_;
};

#endif // GUI_SERVICES_WEATHER_SERVICE_H_

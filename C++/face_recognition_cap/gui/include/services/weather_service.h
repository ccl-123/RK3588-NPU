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
#include <QPointer>
#include <QUrl>

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
    // 网络可靠性配置
    static constexpr int kRequestTimeoutMs = 5000;   // 单次请求超时
    static constexpr int kMaxRetries      = 2;       // 失败重试次数
    static constexpr int kRetryDelayMs    = 800;     // 首次重试延迟，之后指数退避

    explicit WeatherService(QObject* parent = nullptr);
    ~WeatherService() = default;

    void sendGet(const QUrl& url,
                 const std::function<void(QNetworkReply*)>& on_ok,
                 QPointer<QNetworkReply>& slot,
                 int retries_left,
                 const QString& tag);
    void abortIfRunning(QPointer<QNetworkReply>& slot);

    void onLocationReplyFinished(QNetworkReply* reply);
    void onWeatherReplyFinished(QNetworkReply* reply);
    void onAqiReplyFinished(QNetworkReply* reply);
    void onUvReplyFinished(QNetworkReply* reply);
    void onDailySentenceReplyFinished(QNetworkReply* reply);

    static WeatherService* instance_;
    
    QNetworkAccessManager* network_manager_;
    QString current_city_;

    // 请求并发控制（同类型仅一个在途）
    QPointer<QNetworkReply> location_reply_;
    QPointer<QNetworkReply> weather_reply_;
    QPointer<QNetworkReply> aqi_reply_;
    QPointer<QNetworkReply> uv_reply_;
    QPointer<QNetworkReply> sentence_reply_;

    // 缓存上一次成功数据，用于失败时回退显示
    QString cached_temp_;
    QString cached_desc_;
    QString cached_sentence_en_;
    QString cached_sentence_from_;
    int cached_aqi_ = -1;
    QString cached_aqi_level_;
    double cached_uv_ = -1.0;
    QString cached_uv_level_;
    bool has_weather_cache_ = false;
    bool has_aqi_cache_ = false;
    bool has_uv_cache_ = false;
    bool has_sentence_cache_ = false;
};

#endif // GUI_SERVICES_WEATHER_SERVICE_H_

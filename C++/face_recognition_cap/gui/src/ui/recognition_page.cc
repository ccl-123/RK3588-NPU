/**
 * @file recognition_page.cc
 * @brief 实时识别页面 - 现代化设计
 * @author CL
 * @date 2025-11-25
 */

#include "ui/recognition_page.h"

#include "gui/video_display_widget.h"
#include "widgets/card_widget.h"
#include "widgets/modern_table_view.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>
#include <QSizePolicy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <spdlog/spdlog.h>
#include "utils/config_manager.h"

RecognitionPage::RecognitionPage(QWidget* parent)
    : QWidget(parent)
    , video_widget_(nullptr)
    , attendance_table_(nullptr)
    , status_label_(nullptr)
    , fps_label_(nullptr)
    , recognition_label_(nullptr)
    , attendance_status_label_(nullptr)
    , user_name_label_(nullptr)
    , user_id_label_(nullptr)
    , user_dept_label_(nullptr)
    , user_similarity_label_(nullptr)
    , check_type_label_(nullptr)
    , clock_label_(nullptr)
    , date_label_(nullptr)
    , face_count_label_(nullptr)
    , detection_status_label_(nullptr)
    , detection_progress_bar_(nullptr)
    , weather_label_(nullptr)
    , temp_label_(nullptr)
    , weather_desc_(nullptr)
    , checkin_count_label_(nullptr)
    , checkout_count_label_(nullptr)
    , late_count_label_(nullptr)
    , check_mode_label_(nullptr)
    , network_manager_(nullptr)
    , location_manager_(nullptr)
    , aqi_manager_(nullptr)
    , uv_manager_(nullptr)
    , aqi_label_(nullptr)
    , uv_label_(nullptr)
    , sentence_manager_(nullptr)
    , sentence_en_label_(nullptr)
    , sentence_cn_label_(nullptr)
    , current_city_(tr("定位中..."))
    , current_lat_(23.0215)   // 默认佛山坐标
    , current_lon_(113.1214)
    , location_fetched_(false) {
    
    setObjectName("RecognitionPage");
    setAttribute(Qt::WA_StyledBackground, true);
    
    // 不设置内联样式，让全局 QSS 控制背景色

    auto layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(20);

    // 左侧：视频区域
    auto video_card = createVideoCard();
    
    // 右侧：控制面板 + 签到列表
    auto right_column = new QVBoxLayout();
    right_column->setSpacing(20);

    auto status_card = createStatusCard();
    auto attendance_card = createAttendanceCard();

    layout->addWidget(video_card, 3);  // 视频占更大比例
    layout->addLayout(right_column, 2);
    
    right_column->addWidget(status_card);
    right_column->addWidget(attendance_card, 1);
}

VideoDisplayWidget* RecognitionPage::videoWidget() const {
    return video_widget_;
}

ModernTableView* RecognitionPage::attendanceTable() const {
    return attendance_table_;
}

QLabel* RecognitionPage::statusLabel() const {
    return status_label_;
}

QLabel* RecognitionPage::fpsLabel() const {
    return fps_label_;
}

QLabel* RecognitionPage::recognitionLabel() const {
    return recognition_label_;
}

QLabel* RecognitionPage::attendanceStatusLabel() const {
    return attendance_status_label_;
}

QLabel* RecognitionPage::userNameLabel() const {
    return user_name_label_;
}

QLabel* RecognitionPage::userIdLabel() const {
    return user_id_label_;
}

QLabel* RecognitionPage::userDeptLabel() const {
    return user_dept_label_;
}

QLabel* RecognitionPage::userSimilarityLabel() const {
    return user_similarity_label_;
}

QLabel* RecognitionPage::checkTypeLabel() const {
    return check_type_label_;
}

QLabel* RecognitionPage::clockLabel() const {
    return clock_label_;
}

QLabel* RecognitionPage::dateLabel() const {
    return date_label_;
}

QLabel* RecognitionPage::faceCountLabel() const {
    return face_count_label_;
}

QLabel* RecognitionPage::detectionStatusLabel() const {
    return detection_status_label_;
}

QProgressBar* RecognitionPage::detectionProgressBar() const {
    return detection_progress_bar_;
}

void RecognitionPage::updateClock(const QString& time) {
    if (clock_label_) {
        clock_label_->setText(time);
    }
}

void RecognitionPage::updateDate(const QString& date) {
    if (date_label_) {
        date_label_->setText(date);
    }
}

void RecognitionPage::updateFaceCount(int count) {
    if (face_count_label_) {
        // 只显示数字，标题已在卡片上方
        face_count_label_->setText(QString::number(count));
        face_count_label_->setProperty("status", count > 0 ? "active" : "inactive");
        face_count_label_->style()->unpolish(face_count_label_);
        face_count_label_->style()->polish(face_count_label_);
    }
}

void RecognitionPage::updateDetectionStatus(const QString& status, int progress) {
    if (detection_status_label_) {
        detection_status_label_->setText(status);
    }
    if (detection_progress_bar_) {
        // 始终显示进度条，防止布局抖动
        if (progress < 0) {
            detection_progress_bar_->setValue(0);  // 重置为0，但不隐藏
        } else {
            detection_progress_bar_->setValue(progress);
        }
    }
}

void RecognitionPage::updateWeather(const QString& city, const QString& temp) {
    if (weather_label_) {
        weather_label_->setText(city);
    }
    if (temp_label_) {
        temp_label_->setText(temp);
    }
}

void RecognitionPage::updateWeatherDesc(const QString& desc) {
    if (weather_desc_) {
        weather_desc_->setText(desc);
    }
}

void RecognitionPage::updateAttendanceStats(int checkin_count, int checkout_count, int late_count, int early_leave_count) {
    if (checkin_count_label_) {
        checkin_count_label_->setText(QString::number(checkin_count));
    }
    if (checkout_count_label_) {
        checkout_count_label_->setText(QString::number(checkout_count));
    }
    if (late_count_label_) {
        // 迟到 + 早退 = 异常总数
        int abnormal = late_count + early_leave_count;
        late_count_label_->setText(QString::number(abnormal));
        // 有异常时变红，使用 hasAlert 属性控制样式
        late_count_label_->setProperty("hasAlert", abnormal > 0 ? "true" : "false");
        late_count_label_->style()->unpolish(late_count_label_);
        late_count_label_->style()->polish(late_count_label_);
    }
}

void RecognitionPage::updateCheckMode(bool is_checkout_mode) {
    if (check_mode_label_) {
        if (is_checkout_mode) {
            check_mode_label_->setText(tr("签退"));
            check_mode_label_->setProperty("mode", "checkout");
        } else {
            check_mode_label_->setText(tr("签到"));
            check_mode_label_->setProperty("mode", "checkin");
        }
        check_mode_label_->style()->unpolish(check_mode_label_);
        check_mode_label_->style()->polish(check_mode_label_);
    }
}

void RecognitionPage::resetLocationCache() {
    location_fetched_ = false;
    current_city_ = tr("定位中...");
    spdlog::info("Location cache reset");
}

void RecognitionPage::refreshWeather() {
    auto config = ConfigManager::instance();
    
    // 检查是否使用自动定位
    if (config->isAutoLocationEnabled()) {
        // 使用 IP 自动定位
        if (!location_fetched_) {
            requestLocation();
        } else {
            requestWeather(current_lat_, current_lon_);
        }
    } else {
        // 使用手动配置的城市
        current_city_ = config->getManualCity();
        current_lat_ = config->getManualLatitude();
        current_lon_ = config->getManualLongitude();
        location_fetched_ = true;
        
        spdlog::debug("Using manual location: {} (lat: {}, lon: {})", 
            current_city_.toStdString(), current_lat_, current_lon_);
        
        requestWeather(current_lat_, current_lon_);
    }
}

void RecognitionPage::requestLocation() {
    if (!location_manager_) {
        location_manager_ = new QNetworkAccessManager(this);
        connect(location_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onLocationReplyFinished);
    }
    
    // 使用 ip-api.com 获取设备位置（免费，无需 Key）
    QUrl url("http://ip-api.com/json/?lang=zh-CN");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    location_manager_->get(request);
    
    spdlog::debug("Location request sent to ip-api.com");
}

void RecognitionPage::onLocationReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            
            // 获取位置信息
            QString status = root["status"].toString();
            if (status == "success") {
                current_city_ = root["city"].toString();
                current_lat_ = root["lat"].toDouble();
                current_lon_ = root["lon"].toDouble();
                location_fetched_ = true;
                
                spdlog::info("Location detected: {} (lat: {}, lon: {})", 
                    current_city_.toStdString(), current_lat_, current_lon_);
                
                // 获取位置成功后，请求天气
                requestWeather(current_lat_, current_lon_);
            } else {
                spdlog::warn("Location API returned status: {}", status.toStdString());
                // 使用默认位置（佛山）
                current_city_ = tr("佛山");
                location_fetched_ = true;
                requestWeather(current_lat_, current_lon_);
            }
        } else {
            spdlog::warn("Location JSON parse failed");
            current_city_ = tr("佛山");
            location_fetched_ = true;
            requestWeather(current_lat_, current_lon_);
        }
    } else {
        spdlog::warn("Location request failed: {}", reply->errorString().toStdString());
        // 网络失败，使用默认位置
        current_city_ = tr("佛山");
        location_fetched_ = true;
        requestWeather(current_lat_, current_lon_);
    }
    reply->deleteLater();
}

void RecognitionPage::requestWeather(double lat, double lon) {
    if (!network_manager_) {
        network_manager_ = new QNetworkAccessManager(this);
        connect(network_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onWeatherReplyFinished);
    }
    
    // 使用 Open-Meteo API（完全免费，无需 API Key）
    QString urlStr = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&current_weather=true")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    network_manager_->get(request);
    
    spdlog::debug("Weather request sent to Open-Meteo for {} (lat: {}, lon: {})", 
        current_city_.toStdString(), lat, lon);
    
    // 同时请求 AQI 和 UV
    requestAqi(lat, lon);
    requestUv(lat, lon);
}

void RecognitionPage::requestAqi(double lat, double lon) {
    if (!aqi_manager_) {
        aqi_manager_ = new QNetworkAccessManager(this);
        connect(aqi_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onAqiReplyFinished);
    }
    
    // Open-Meteo Air Quality API
    QString urlStr = QString("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%1&longitude=%2&current=us_aqi,pm2_5")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    aqi_manager_->get(request);
}

void RecognitionPage::requestUv(double lat, double lon) {
    if (!uv_manager_) {
        uv_manager_ = new QNetworkAccessManager(this);
        connect(uv_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onUvReplyFinished);
    }
    
    // Open-Meteo UV Index API
    QString urlStr = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&hourly=uv_index&forecast_days=1")
                        .arg(lat, 0, 'f', 4)
                        .arg(lon, 0, 'f', 4);
    QUrl url(urlStr);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    uv_manager_->get(request);
}

void RecognitionPage::onWeatherReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current_weather"].toObject();
            
            double temp = current["temperature"].toDouble();
            int weatherCode = current["weathercode"].toInt();
            
            // 将天气代码转换为中文描述
            QString weatherDesc = weatherCodeToString(weatherCode);
            QString tempStr = QString("%1°").arg(temp, 0, 'f', 0);
            
            // 分别更新城市名、温度、天气描述
            updateWeather(current_city_, tempStr);
            updateWeatherDesc(weatherDesc);
            
            spdlog::info("Weather updated: {} {} {}", 
                current_city_.toStdString(), weatherDesc.toStdString(), tempStr.toStdString());
        } else {
            spdlog::warn("Weather JSON parse failed");
            updateWeather(current_city_, tr("--°"));
            updateWeatherDesc(tr("获取失败"));
        }
    } else {
        spdlog::warn("Weather request failed: {}", reply->errorString().toStdString());
        updateWeather(current_city_, tr("--°"));
        updateWeatherDesc(tr("网络错误"));
    }
    reply->deleteLater();
}

QString RecognitionPage::weatherCodeToString(int code) {
    // Open-Meteo WMO Weather interpretation codes
    // https://open-meteo.com/en/docs
    switch (code) {
        case 0: return tr("晴");
        case 1: return tr("晴");
        case 2: return tr("多云");
        case 3: return tr("阴");
        case 45:
        case 48: return tr("雾");
        case 51:
        case 53:
        case 55: return tr("小雨");
        case 56:
        case 57: return tr("冻雨");
        case 61:
        case 63: return tr("中雨");
        case 65: return tr("大雨");
        case 66:
        case 67: return tr("冻雨");
        case 71:
        case 73: return tr("小雪");
        case 75: return tr("大雪");
        case 77: return tr("雪粒");
        case 80:
        case 81:
        case 82: return tr("阵雨");
        case 85:
        case 86: return tr("阵雪");
        case 95: return tr("雷雨");
        case 96:
        case 99: return tr("冰雹");
        default: return tr("未知");
    }
}

void RecognitionPage::onAqiReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject current = root["current"].toObject();
            
            int aqi = current["us_aqi"].toInt();
            QString level = aqiToLevel(aqi);
            
            if (aqi_label_) {
                aqi_label_->setText(QString("空气 %1 %2").arg(aqi).arg(level));
                // 根据 AQI 值设置颜色
                if (aqi <= 50) {
                    aqi_label_->setStyleSheet("color: #52c41a;");  // 优
                } else if (aqi <= 100) {
                    aqi_label_->setStyleSheet("color: #faad14;");  // 良
                } else if (aqi <= 150) {
                    aqi_label_->setStyleSheet("color: #fa8c16;");  // 轻度
                } else if (aqi <= 200) {
                    aqi_label_->setStyleSheet("color: #f5222d;");  // 中度
                } else {
                    aqi_label_->setStyleSheet("color: #722ed1;");  // 重度
                }
            }
            spdlog::debug("AQI updated: {} ({})", aqi, level.toStdString());
        }
    } else {
        if (aqi_label_) {
            aqi_label_->setText(tr("空气 --"));
        }
    }
    reply->deleteLater();
}

void RecognitionPage::onUvReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonObject hourly = root["hourly"].toObject();
            QJsonArray uvArray = hourly["uv_index"].toArray();
            
            // 获取当前小时的 UV 值（取第一个非空值）
            double uv = 0;
            for (int i = 0; i < uvArray.size() && i < 24; i++) {
                if (!uvArray[i].isNull()) {
                    uv = uvArray[i].toDouble();
                    break;
                }
            }
            
            QString level = uvToLevel(uv);
            
            if (uv_label_) {
                uv_label_->setText(QString("紫外线 %1 %2").arg(uv, 0, 'f', 0).arg(level));
                // 根据 UV 值设置颜色
                if (uv <= 2) {
                    uv_label_->setStyleSheet("color: #52c41a;");  // 低
                } else if (uv <= 5) {
                    uv_label_->setStyleSheet("color: #faad14;");  // 中等
                } else if (uv <= 7) {
                    uv_label_->setStyleSheet("color: #fa8c16;");  // 高
                } else if (uv <= 10) {
                    uv_label_->setStyleSheet("color: #f5222d;");  // 很高
                } else {
                    uv_label_->setStyleSheet("color: #722ed1;");  // 极高
                }
            }
            spdlog::debug("UV updated: {} ({})", uv, level.toStdString());
        }
    } else {
        if (uv_label_) {
            uv_label_->setText(tr("紫外线 --"));
        }
    }
    reply->deleteLater();
}

QString RecognitionPage::aqiToLevel(int aqi) {
    if (aqi <= 50) return tr("优");
    if (aqi <= 100) return tr("良");
    if (aqi <= 150) return tr("轻度");
    if (aqi <= 200) return tr("中度");
    if (aqi <= 300) return tr("重度");
    return tr("严重");
}

QString RecognitionPage::uvToLevel(double uv) {
    if (uv <= 2) return tr("低");
    if (uv <= 5) return tr("中等");
    if (uv <= 7) return tr("高");
    if (uv <= 10) return tr("很高");
    return tr("极高");
}

void RecognitionPage::refreshDailySentence() {
    requestDailySentence();   // 一言（每次随机）
}

void RecognitionPage::requestDailySentence() {
    if (!sentence_manager_) {
        sentence_manager_ = new QNetworkAccessManager(this);
        connect(sentence_manager_, &QNetworkAccessManager::finished, 
                this, &RecognitionPage::onDailySentenceReplyFinished);
    }
    
    // 一言 API (hitokoto.cn) - 每次返回不同的随机句子
    QUrl url("https://v1.hitokoto.cn/");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");
    sentence_manager_->get(request);
    
    spdlog::debug("Hitokoto sentence request sent");
}

void RecognitionPage::onDailySentenceReplyFinished(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            
            // 一言 API 返回格式: { "hitokoto": "句子", "from": "来源" }
            QString hitokoto = root["hitokoto"].toString();  // 句子内容
            QString from = root["from"].toString();          // 来源
            
            if (sentence_en_label_ && !hitokoto.isEmpty()) {
                sentence_en_label_->setText(hitokoto);
            }
            if (sentence_cn_label_) {
                // 显示来源，格式: "—— 来源"
                if (!from.isEmpty()) {
                    sentence_cn_label_->setText(QString("—— %1").arg(from));
                } else {
                    sentence_cn_label_->setText("");
                }
            }
            
            spdlog::info("Hitokoto updated: {}", hitokoto.left(50).toStdString());
        }
    } else {
        spdlog::warn("Hitokoto request failed: {}", reply->errorString().toStdString());
    }
    reply->deleteLater();
}

CardWidget* RecognitionPage::createVideoCard() {
    auto card = new CardWidget();
    card->setVariant("dark");
    card->setTitle("");
    
    auto container = new QWidget();
    container->setObjectName("VideoContainer");
    container->setAttribute(Qt::WA_StyledBackground, true);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    auto main_layout = new QVBoxLayout(container);
    main_layout->setContentsMargins(0, 0, 0, 0);
    main_layout->setSpacing(0);
    
    // ========== 视频显示区域 ==========
    video_widget_ = new VideoDisplayWidget(container);
    video_widget_->set_show_fps(true);
    video_widget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    main_layout->addWidget(video_widget_, 1);
    
    // ========== 信息栏（天气 + 考勤统计） ==========
    auto info_bar = createInfoBar();
    main_layout->addWidget(info_bar, 0);
    
    // ========== 综合状态栏（时钟 + 状态指示 + 进度条） ==========
    auto status_bar = createStatusBar();
    main_layout->addWidget(status_bar, 0);
    
    // ========== 用户信息面板（现代化设计） ==========
    auto info_panel = new QWidget(container);
    info_panel->setObjectName("UserInfoPanel");
    info_panel->setAttribute(Qt::WA_StyledBackground, true);
    info_panel->setFixedHeight(100);

    auto info_layout = new QHBoxLayout(info_panel);
    info_layout->setContentsMargins(20, 16, 20, 16);
    info_layout->setSpacing(20);
    
    // ===== 左侧：用户头像占位符 =====
    auto avatar_container = new QWidget(info_panel);
    avatar_container->setObjectName("AvatarContainer");
    avatar_container->setAttribute(Qt::WA_StyledBackground, true);
    avatar_container->setFixedSize(68, 68);
    
    auto avatar_layout = new QVBoxLayout(avatar_container);
    avatar_layout->setContentsMargins(0, 0, 0, 0);
    avatar_layout->setAlignment(Qt::AlignCenter);
    
    auto avatar_icon = new QLabel("", avatar_container);
    avatar_icon->setObjectName("AvatarIcon");
    avatar_icon->setAlignment(Qt::AlignCenter);
    avatar_icon->setStyleSheet("font-size: 32px; color: #8c8c8c;");
    avatar_icon->setText("◉");  // 使用简单的圆形符号作为头像占位符
    avatar_layout->addWidget(avatar_icon);
    
    info_layout->addWidget(avatar_container);
    
    // ===== 中间：用户基本信息 =====
    auto user_info_column = new QVBoxLayout();
    user_info_column->setSpacing(6);
    
    // 用户名 + 状态标签
    auto name_row = new QHBoxLayout();
    name_row->setSpacing(10);
    
    user_name_label_ = new QLabel(tr("等待识别..."), info_panel);
    user_name_label_->setObjectName("UserNameLabel");
    
    attendance_status_label_ = new QLabel(info_panel);
    attendance_status_label_->setObjectName("AttendanceStatusLabel");
    attendance_status_label_->setVisible(false);
    
    name_row->addWidget(user_name_label_);
    name_row->addWidget(attendance_status_label_);
    name_row->addStretch();
    
    // 工号 | 部门
    auto detail_row = new QHBoxLayout();
    detail_row->setSpacing(0);
    
    user_id_label_ = new QLabel(tr("工号: --"), info_panel);
    user_id_label_->setObjectName("UserMetaLabel");
    
    auto separator1 = new QLabel("  •  ", info_panel);
    separator1->setObjectName("MetaSeparator");
    
    user_dept_label_ = new QLabel(tr("部门: --"), info_panel);
    user_dept_label_->setObjectName("UserMetaLabel");
    
    detail_row->addWidget(user_id_label_);
    detail_row->addWidget(separator1);
    detail_row->addWidget(user_dept_label_);
    detail_row->addStretch();
    
    // 识别状态（隐藏，内部使用）
    recognition_label_ = new QLabel(tr("状态: 未识别"), info_panel);
    recognition_label_->setObjectName("UserDetailLabel");
    recognition_label_->setVisible(false);
    
    user_info_column->addLayout(name_row);
    user_info_column->addLayout(detail_row);
    user_info_column->addStretch();
    
    info_layout->addLayout(user_info_column);
    
    // ===== 中间：一言区域 =====
    auto sentence_container = new QWidget(info_panel);
    sentence_container->setObjectName("SentenceContainer");
    sentence_container->setAttribute(Qt::WA_StyledBackground, true);
    auto sentence_layout = new QVBoxLayout(sentence_container);
    sentence_layout->setContentsMargins(16, 8, 16, 8);
    sentence_layout->setSpacing(4);
    sentence_layout->setAlignment(Qt::AlignCenter);
    
    sentence_en_label_ = new QLabel(tr("Loading..."), sentence_container);
    sentence_en_label_->setObjectName("SentenceEnLabel");
    sentence_en_label_->setAlignment(Qt::AlignCenter);
    sentence_en_label_->setWordWrap(false);
    
    sentence_cn_label_ = new QLabel(tr("加载中..."), sentence_container);
    sentence_cn_label_->setObjectName("SentenceCnLabel");
    sentence_cn_label_->setAlignment(Qt::AlignCenter);
    
    sentence_layout->addWidget(sentence_en_label_);
    sentence_layout->addWidget(sentence_cn_label_);
    
    info_layout->addWidget(sentence_container, 1);  // stretch factor = 1, 占据中间空白
    
    // ===== 右侧：识别指标卡片 =====
    auto metrics_container = new QWidget(info_panel);
    metrics_container->setObjectName("MetricsContainer");
    metrics_container->setAttribute(Qt::WA_StyledBackground, true);
    
    auto metrics_layout = new QHBoxLayout(metrics_container);
    metrics_layout->setContentsMargins(0, 0, 0, 0);
    metrics_layout->setSpacing(16);
    
    // 相似度指标
    auto similarity_card = new QWidget(metrics_container);
    similarity_card->setObjectName("MetricCard");
    similarity_card->setAttribute(Qt::WA_StyledBackground, true);
    similarity_card->setFixedWidth(90);
    
    auto sim_layout = new QVBoxLayout(similarity_card);
    sim_layout->setContentsMargins(12, 8, 12, 8);
    sim_layout->setSpacing(2);
    sim_layout->setAlignment(Qt::AlignCenter);
    
    user_similarity_label_ = new QLabel(tr("--"), similarity_card);
    user_similarity_label_->setObjectName("MetricValue");
    user_similarity_label_->setAlignment(Qt::AlignCenter);
    
    auto sim_title = new QLabel(tr("相似度"), similarity_card);
    sim_title->setObjectName("MetricTitle");
    sim_title->setAlignment(Qt::AlignCenter);
    
    sim_layout->addWidget(user_similarity_label_);
    sim_layout->addWidget(sim_title);
    
    metrics_layout->addWidget(similarity_card);
    
    info_layout->addWidget(metrics_container);
    
    // check_type_label_ 不再显示（已在信息栏的"当前模式"中显示）
    check_type_label_ = nullptr;
    
    main_layout->addWidget(info_panel, 0);
    
    auto card_layout = new QVBoxLayout(card->bodyContainer());
    card_layout->setContentsMargins(0, 0, 0, 0);
    card_layout->setSpacing(0);
    card_layout->addWidget(container);
    
    return card;
}

QWidget* RecognitionPage::createInfoBar() {
    auto info_bar = new QWidget();
    info_bar->setObjectName("InfoBar");
    info_bar->setAttribute(Qt::WA_StyledBackground, true);
    info_bar->setFixedHeight(100);  // 与用户信息栏同高
    
    auto layout = new QHBoxLayout(info_bar);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(32);
    
    // ===== 左侧：天气信息卡片 =====
    auto weather_card = new QWidget(info_bar);
    weather_card->setObjectName("WeatherCard");
    weather_card->setAttribute(Qt::WA_StyledBackground, true);
    auto weather_main_layout = new QHBoxLayout(weather_card);
    weather_main_layout->setContentsMargins(20, 12, 20, 12);
    weather_main_layout->setSpacing(16);
    
    // 左侧：温度大数字
    temp_label_ = new QLabel(tr("--°"), weather_card);
    temp_label_->setObjectName("WeatherTempBig");
    temp_label_->setAlignment(Qt::AlignCenter);
    
    weather_main_layout->addWidget(temp_label_);
    
    // 中间：城市 + 天气描述
    auto weather_info = new QWidget(weather_card);
    auto weather_info_layout = new QVBoxLayout(weather_info);
    weather_info_layout->setContentsMargins(0, 0, 0, 0);
    weather_info_layout->setSpacing(2);
    
    weather_label_ = new QLabel(tr("定位中..."), weather_info);
    weather_label_->setObjectName("WeatherCityLabel");
    
    auto weather_desc = new QLabel(tr("获取天气"), weather_info);
    weather_desc->setObjectName("WeatherDescLabel");
    weather_desc_ = weather_desc;
    
    weather_info_layout->addWidget(weather_label_);
    weather_info_layout->addWidget(weather_desc);
    
    weather_main_layout->addWidget(weather_info);
    
    // 右侧：AQI + UV 指标
    auto env_info = new QWidget(weather_card);
    auto env_layout = new QVBoxLayout(env_info);
    env_layout->setContentsMargins(0, 0, 0, 0);
    env_layout->setSpacing(4);
    
    aqi_label_ = new QLabel(tr("空气 --"), env_info);
    aqi_label_->setObjectName("EnvLabel");
    aqi_label_->setStyleSheet("font-size: 12px; font-weight: 600;");
    
    uv_label_ = new QLabel(tr("紫外线 --"), env_info);
    uv_label_->setObjectName("EnvLabel");
    uv_label_->setStyleSheet("font-size: 12px; font-weight: 600;");
    
    env_layout->addWidget(aqi_label_);
    env_layout->addWidget(uv_label_);
    
    weather_main_layout->addWidget(env_info);
    weather_main_layout->addStretch();
    
    layout->addWidget(weather_card);
    
    // ===== 分隔符 =====
    auto sep1 = new QWidget(info_bar);
    sep1->setObjectName("InfoBarSeparator");
    sep1->setFixedWidth(1);
    sep1->setMinimumHeight(50);
    layout->addWidget(sep1);
    
    // ===== 中间：今日考勤统计卡片 =====
    auto stats_card = new QWidget(info_bar);
    stats_card->setObjectName("InfoBarCard");
    stats_card->setAttribute(Qt::WA_StyledBackground, true);
    auto stats_layout = new QVBoxLayout(stats_card);
    stats_layout->setContentsMargins(16, 12, 16, 12);
    stats_layout->setSpacing(8);
    
    // 统计标题
    auto stats_title = new QLabel(tr("今日考勤统计"), stats_card);
    stats_title->setObjectName("InfoBarCardTitle");
    
    // 统计数据行
    auto stats_data_row = new QHBoxLayout();
    stats_data_row->setSpacing(24);
    
    // 签到
    auto checkin_box = new QWidget(stats_card);
    auto checkin_layout = new QHBoxLayout(checkin_box);
    checkin_layout->setContentsMargins(0, 0, 0, 0);
    checkin_layout->setSpacing(6);
    auto checkin_label = new QLabel(tr("签到"), checkin_box);
    checkin_label->setObjectName("StatsItemLabel");
    checkin_count_label_ = new QLabel("0", checkin_box);
    checkin_count_label_->setObjectName("StatsCheckinValue");
    checkin_layout->addWidget(checkin_label);
    checkin_layout->addWidget(checkin_count_label_);
    
    // 签退
    auto checkout_box = new QWidget(stats_card);
    auto checkout_layout = new QHBoxLayout(checkout_box);
    checkout_layout->setContentsMargins(0, 0, 0, 0);
    checkout_layout->setSpacing(6);
    auto checkout_label = new QLabel(tr("签退"), checkout_box);
    checkout_label->setObjectName("StatsItemLabel");
    checkout_count_label_ = new QLabel("0", checkout_box);
    checkout_count_label_->setObjectName("StatsCheckoutValue");
    checkout_layout->addWidget(checkout_label);
    checkout_layout->addWidget(checkout_count_label_);
    
    // 异常
    auto late_box = new QWidget(stats_card);
    auto late_layout = new QHBoxLayout(late_box);
    late_layout->setContentsMargins(0, 0, 0, 0);
    late_layout->setSpacing(6);
    auto late_label = new QLabel(tr("异常"), late_box);
    late_label->setObjectName("StatsItemLabel");
    late_count_label_ = new QLabel("0", late_box);
    late_count_label_->setObjectName("StatsAbnormalValue");
    late_layout->addWidget(late_label);
    late_layout->addWidget(late_count_label_);
    
    stats_data_row->addWidget(checkin_box);
    stats_data_row->addWidget(checkout_box);
    stats_data_row->addWidget(late_box);
    
    stats_layout->addWidget(stats_title);
    stats_layout->addLayout(stats_data_row);
    
    layout->addWidget(stats_card);
    
    layout->addStretch();
    
    // ===== 右侧：当前模式标签 =====
    auto mode_card = new QWidget(info_bar);
    mode_card->setObjectName("ModeCard");
    mode_card->setAttribute(Qt::WA_StyledBackground, true);
    auto mode_layout = new QVBoxLayout(mode_card);
    mode_layout->setContentsMargins(20, 12, 20, 12);
    mode_layout->setSpacing(4);
    mode_layout->setAlignment(Qt::AlignCenter);
    
    auto mode_title = new QLabel(tr("当前模式"), mode_card);
    mode_title->setObjectName("ModeTitle");
    mode_title->setAlignment(Qt::AlignCenter);
    
    check_mode_label_ = new QLabel(tr("签到"), mode_card);
    check_mode_label_->setObjectName("CheckModeLabel");
    check_mode_label_->setAlignment(Qt::AlignCenter);
    
    mode_layout->addWidget(mode_title);
    mode_layout->addWidget(check_mode_label_);
    
    layout->addWidget(mode_card);
    
    return info_bar;
}

QWidget* RecognitionPage::createStatusBar() {
    auto status_bar = new QWidget();
    status_bar->setObjectName("DetectionStatusBar");
    status_bar->setAttribute(Qt::WA_StyledBackground, true);
    status_bar->setFixedHeight(100);  // 与用户信息栏同高
    
    auto layout = new QHBoxLayout(status_bar);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(32);
    
    // ===== 左侧：时钟卡片 =====
    auto clock_card = new QWidget(status_bar);
    clock_card->setObjectName("ClockCard");
    clock_card->setAttribute(Qt::WA_StyledBackground, true);
    auto clock_card_layout = new QVBoxLayout(clock_card);
    clock_card_layout->setContentsMargins(16, 12, 16, 12);
    clock_card_layout->setSpacing(2);
    clock_card_layout->setAlignment(Qt::AlignCenter);
    
    clock_label_ = new QLabel("--:--:--", clock_card);
    clock_label_->setObjectName("ClockLabel");
    clock_label_->setAlignment(Qt::AlignCenter);
    
    date_label_ = new QLabel("----年--月--日 星期-", clock_card);
    date_label_->setObjectName("DateLabel");
    date_label_->setAlignment(Qt::AlignCenter);
    
    clock_card_layout->addWidget(clock_label_);
    clock_card_layout->addWidget(date_label_);
    
    layout->addWidget(clock_card);
    
    // ===== 分隔符 =====
    auto separator1 = new QWidget(status_bar);
    separator1->setObjectName("StatusBarSeparator");
    separator1->setFixedWidth(1);
    separator1->setMinimumHeight(50);
    layout->addWidget(separator1);
    
    // ===== 人脸检测状态卡片 =====
    auto face_card = new QWidget(status_bar);
    face_card->setObjectName("FaceDetectionCard");
    face_card->setAttribute(Qt::WA_StyledBackground, true);
    face_card->setFixedWidth(160);  // 固定宽度防止抖动
    auto face_card_layout = new QVBoxLayout(face_card);
    face_card_layout->setContentsMargins(16, 12, 16, 12);
    face_card_layout->setSpacing(4);
    face_card_layout->setAlignment(Qt::AlignCenter);
    
    auto face_title = new QLabel(tr("人脸检测"), face_card);
    face_title->setObjectName("FaceCardTitle");
    face_title->setAlignment(Qt::AlignCenter);
    
    face_count_label_ = new QLabel(tr("0"), face_card);
    face_count_label_->setObjectName("FaceCountValue");
    face_count_label_->setProperty("status", "inactive");
    face_count_label_->setAlignment(Qt::AlignCenter);
    
    face_card_layout->addWidget(face_title);
    face_card_layout->addWidget(face_count_label_);
    
    layout->addWidget(face_card);
    
    layout->addStretch();
    
    // ===== 右侧：识别进度卡片 =====
    auto progress_card = new QWidget(status_bar);
    progress_card->setObjectName("ProgressCard");
    progress_card->setAttribute(Qt::WA_StyledBackground, true);
    auto progress_card_layout = new QVBoxLayout(progress_card);
    progress_card_layout->setContentsMargins(20, 12, 20, 12);
    progress_card_layout->setSpacing(8);
    progress_card_layout->setAlignment(Qt::AlignCenter);
    
    detection_status_label_ = new QLabel(tr("等待识别"), progress_card);
    detection_status_label_->setObjectName("DetectionStatusLabel");
    detection_status_label_->setAlignment(Qt::AlignCenter);
    
    detection_progress_bar_ = new QProgressBar(progress_card);
    detection_progress_bar_->setObjectName("DetectionProgressBar");
    detection_progress_bar_->setRange(0, 100);
    detection_progress_bar_->setValue(0);
    detection_progress_bar_->setTextVisible(false);
    detection_progress_bar_->setFixedSize(160, 8);
    detection_progress_bar_->setVisible(true);
    
    progress_card_layout->addWidget(detection_status_label_);
    progress_card_layout->addWidget(detection_progress_bar_);
    
    layout->addWidget(progress_card);
    
    return status_bar;
}

CardWidget* RecognitionPage::createStatusCard() {
    auto card = new CardWidget();
    card->setTitle(tr("系统控制"));

    // 状态指示器
    auto status_container = new QWidget();
    status_container->setObjectName("StatusContainer");
    status_container->setAttribute(Qt::WA_StyledBackground, true);
    
    auto status_layout = new QHBoxLayout(status_container);
    status_layout->setContentsMargins(0, 0, 0, 0);
    status_layout->setSpacing(24);
    
    // 运行状态
    auto status_item = new QWidget();
    status_item->setAttribute(Qt::WA_StyledBackground, true);
    auto status_item_layout = new QVBoxLayout(status_item);
    status_item_layout->setContentsMargins(0, 0, 0, 0);
    status_item_layout->setSpacing(4);
    
    auto status_title = new QLabel(tr("运行状态"));
    status_title->setObjectName("StatusTitle");

    status_label_ = new QLabel(tr("就绪"));
    status_label_->setObjectName("StatusValue");
    status_label_->setProperty("status", "success");
    
    status_item_layout->addWidget(status_title);
    status_item_layout->addWidget(status_label_);
    
    // 帧率
    auto fps_item = new QWidget();
    fps_item->setAttribute(Qt::WA_StyledBackground, true);
    auto fps_item_layout = new QVBoxLayout(fps_item);
    fps_item_layout->setContentsMargins(0, 0, 0, 0);
    fps_item_layout->setSpacing(4);
    
    auto fps_title = new QLabel(tr("实时帧率"));
    fps_title->setObjectName("StatusTitle");

    fps_label_ = new QLabel(tr("0 FPS"));
    fps_label_->setObjectName("FpsValue");
    
    fps_item_layout->addWidget(fps_title);
    fps_item_layout->addWidget(fps_label_);
    
    status_layout->addWidget(status_item);
    status_layout->addWidget(fps_item);
    status_layout->addStretch();

    // 按钮组
    auto button_row = new QHBoxLayout();
    button_row->setSpacing(12);

    auto start_btn = new QPushButton(tr("▶ 开始识别"), card);
    start_btn->setProperty("buttonType", "primary");
    start_btn->setMinimumHeight(40);
    start_btn->setCursor(Qt::PointingHandCursor);
    connect(start_btn, &QPushButton::clicked, this, &RecognitionPage::startRecognitionRequested);

    auto stop_btn = new QPushButton(tr("■ 停止识别"), card);
    stop_btn->setMinimumHeight(40);
    stop_btn->setCursor(Qt::PointingHandCursor);
    connect(stop_btn, &QPushButton::clicked, this, &RecognitionPage::stopRecognitionRequested);

    auto register_btn = new QPushButton(tr("+ 注册人脸"), card);
    register_btn->setObjectName("GhostButton");
    register_btn->setMinimumHeight(40);
    register_btn->setCursor(Qt::PointingHandCursor);
    connect(register_btn, &QPushButton::clicked, this, &RecognitionPage::registerFaceRequested);

    button_row->addWidget(start_btn);
    button_row->addWidget(stop_btn);
    button_row->addWidget(register_btn);
    button_row->addStretch();

    auto body_layout = new QVBoxLayout(card->bodyContainer());
    body_layout->setContentsMargins(0, 0, 0, 0);
    body_layout->setSpacing(20);
    body_layout->addWidget(status_container);
    body_layout->addLayout(button_row);

    return card;
}

CardWidget* RecognitionPage::createAttendanceCard() {
    auto card = new CardWidget();
    card->setTitle(tr("今日签到记录"));

    attendance_table_ = new ModernTableView(card);
    attendance_table_->setColumnCount(4);
    attendance_table_->setHorizontalHeaderLabels({tr("姓名"), tr("时间"), tr("类型"), tr("相似度")});
    
    // 不设置内联样式，让全局 QSS 控制

    auto layout = new QVBoxLayout(card->bodyContainer());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(attendance_table_);

    return card;
}

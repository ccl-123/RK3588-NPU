/**
 * @file config_manager.cc
 * @brief 配置管理器实现
 * @author CL
 * @date 2025-11-25
 */

#include "gui_utils/config_manager.h"
#include "config/config.h"
#include <QCoreApplication>
#include <spdlog/spdlog.h>

#define SETTINGS_READ(expr) \
    do { \
        std::lock_guard<std::mutex> lock(settings_mutex_); \
        return (expr); \
    } while (false)

#define SETTINGS_WRITE(stmt) \
    do { \
        std::lock_guard<std::mutex> lock(settings_mutex_); \
        stmt; \
        settings_->sync(); \
    } while (false)

ConfigManager::ConfigManager() {
    // 配置文件存储位置：~/.config/FaceRecognition/settings.ini
    settings_ = new QSettings(
        QSettings::IniFormat,
        QSettings::UserScope,
        "FaceRecognition",
        "settings"
    );
    
    spdlog::info("ConfigManager: Config file: {}", settings_->fileName().toStdString());
}

ConfigManager::~ConfigManager() {
    std::lock_guard<std::mutex> lock(settings_mutex_);
    if (settings_) {
        settings_->sync();
        delete settings_;
        settings_ = nullptr;
    }
}

ConfigManager* ConfigManager::instance() {
    static ConfigManager instance;
    return &instance;
}

// 识别设置
float ConfigManager::getRecognitionThreshold() const {
    SETTINGS_READ(settings_->value("recognition/threshold",
                                   Config::Default::RECOGNITION_THRESHOLD).toDouble());
}

void ConfigManager::setRecognitionThreshold(float threshold) {
    SETTINGS_WRITE(settings_->setValue("recognition/threshold",
                                       static_cast<double>(threshold)));
}

int ConfigManager::getDuplicateCheckInterval() const {
    SETTINGS_READ(settings_->value("recognition/duplicate_interval",
                                   Config::Default::DUPLICATE_CHECK_INTERVAL).toInt());
}

void ConfigManager::setDuplicateCheckInterval(int seconds) {
    SETTINGS_WRITE(settings_->setValue("recognition/duplicate_interval", seconds));
}

int ConfigManager::getRecognitionConfirmCount() const {
    SETTINGS_READ(settings_->value("recognition/confirm_count",
                                   Config::Default::RECOGNITION_CONFIRM_COUNT).toInt());
}

void ConfigManager::setRecognitionConfirmCount(int count) {
    SETTINGS_WRITE(settings_->setValue("recognition/confirm_count", count));
}

int ConfigManager::getUserConfirmDuration() const {
    SETTINGS_READ(settings_->value("recognition/user_confirm_duration_ms",
                                   Config::Default::USER_CONFIRM_DURATION_MS).toInt());
}

void ConfigManager::setUserConfirmDuration(int milliseconds) {
    SETTINGS_WRITE(settings_->setValue("recognition/user_confirm_duration_ms", milliseconds));
}

// 音频设置
bool ConfigManager::isAudioEnabled() const {
    SETTINGS_READ(settings_->value("audio/enabled", Config::Default::AUDIO_ENABLED).toBool());
}

void ConfigManager::setAudioEnabled(bool enabled) {
    SETTINGS_WRITE(settings_->setValue("audio/enabled", enabled));
}

int ConfigManager::getAudioVolume() const {
    SETTINGS_READ(settings_->value("audio/volume", Config::Default::AUDIO_VOLUME).toInt());
}

void ConfigManager::setAudioVolume(int volume) {
    SETTINGS_WRITE(settings_->setValue("audio/volume", volume));
}

QString ConfigManager::getAudioDevice() const {
    SETTINGS_READ(settings_->value("audio/device", "").toString());
}

void ConfigManager::setAudioDevice(const QString& device) {
    SETTINGS_WRITE(settings_->setValue("audio/device", device));
}

// 显示设置
bool ConfigManager::isShowFPS() const {
    SETTINGS_READ(settings_->value("display/show_fps", true).toBool());
}

void ConfigManager::setShowFPS(bool show) {
    SETTINGS_WRITE(settings_->setValue("display/show_fps", show));
}

bool ConfigManager::isShowConfidence() const {
    SETTINGS_READ(settings_->value("display/show_confidence", true).toBool());
}

void ConfigManager::setShowConfidence(bool show) {
    SETTINGS_WRITE(settings_->setValue("display/show_confidence", show));
}

bool ConfigManager::isAutoStart() const {
    SETTINGS_READ(settings_->value("display/auto_start", false).toBool());
}

void ConfigManager::setAutoStart(bool autoStart) {
    SETTINGS_WRITE(settings_->setValue("display/auto_start", autoStart));
}

// 考勤设置
QString ConfigManager::getWorkStartTime() const {
    SETTINGS_READ(settings_->value("attendance/work_start_time", "09:00").toString());
}

void ConfigManager::setWorkStartTime(const QString& time) {
    SETTINGS_WRITE(settings_->setValue("attendance/work_start_time", time));
}

QString ConfigManager::getWorkEndTime() const {
    SETTINGS_READ(settings_->value("attendance/work_end_time", "18:00").toString());
}

void ConfigManager::setWorkEndTime(const QString& time) {
    SETTINGS_WRITE(settings_->setValue("attendance/work_end_time", time));
}

int ConfigManager::getLateThreshold() const {
    SETTINGS_READ(settings_->value("attendance/late_threshold",
                                   Config::Default::LATE_THRESHOLD).toInt());
}

void ConfigManager::setLateThreshold(int minutes) {
    SETTINGS_WRITE(settings_->setValue("attendance/late_threshold", minutes));
}

int ConfigManager::getEarlyLeaveThreshold() const {
    SETTINGS_READ(settings_->value("attendance/early_leave_threshold",
                                   Config::Default::EARLY_LEAVE_THRESHOLD).toInt());
}

void ConfigManager::setEarlyLeaveThreshold(int minutes) {
    SETTINGS_WRITE(settings_->setValue("attendance/early_leave_threshold", minutes));
}

bool ConfigManager::isAllowMultipleCheckin() const {
    SETTINGS_READ(settings_->value("attendance/allow_multiple_checkin", false).toBool());
}

void ConfigManager::setAllowMultipleCheckin(bool allow) {
    SETTINGS_WRITE(settings_->setValue("attendance/allow_multiple_checkin", allow));
}

bool ConfigManager::isCheckinSound() const {
    SETTINGS_READ(settings_->value("attendance/checkin_sound", true).toBool());
}

void ConfigManager::setCheckinSound(bool enabled) {
    SETTINGS_WRITE(settings_->setValue("attendance/checkin_sound", enabled));
}

bool ConfigManager::isShowCheckinReminder() const {
    SETTINGS_READ(settings_->value("attendance/show_checkin_reminder", true).toBool());
}

void ConfigManager::setShowCheckinReminder(bool show) {
    SETTINGS_WRITE(settings_->setValue("attendance/show_checkin_reminder", show));
}

// 摄像头设置（简化：固定 USB + 异步）
int ConfigManager::getCameraId() const {
    SETTINGS_READ(settings_->value("camera/id", Config::Default::CAMERA_ID).toInt());
}

void ConfigManager::setCameraId(int id) {
    SETTINGS_WRITE(settings_->setValue("camera/id", id));
}

// 天气/城市设置
bool ConfigManager::isAutoLocationEnabled() const {
    SETTINGS_READ(settings_->value("weather/auto_location", false).toBool());  // 默认关闭自动定位
}

void ConfigManager::setAutoLocationEnabled(bool enabled) {
    SETTINGS_WRITE(settings_->setValue("weather/auto_location", enabled));
}

QString ConfigManager::getManualCity() const {
    SETTINGS_READ(settings_->value("weather/manual_city", QString::fromUtf8("佛山")).toString());
}

void ConfigManager::setManualCity(const QString& city) {
    SETTINGS_WRITE(settings_->setValue("weather/manual_city", city));
}

double ConfigManager::getManualLatitude() const {
    SETTINGS_READ(settings_->value("weather/manual_lat", 23.0215).toDouble());  // 佛山默认纬度
}

void ConfigManager::setManualLatitude(double lat) {
    SETTINGS_WRITE(settings_->setValue("weather/manual_lat", lat));
}

double ConfigManager::getManualLongitude() const {
    SETTINGS_READ(settings_->value("weather/manual_lon", 113.1214).toDouble());  // 佛山默认经度
}

void ConfigManager::setManualLongitude(double lon) {
    SETTINGS_WRITE(settings_->setValue("weather/manual_lon", lon));
}

#undef SETTINGS_READ
#undef SETTINGS_WRITE

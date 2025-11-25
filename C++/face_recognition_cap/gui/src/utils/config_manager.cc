/**
 * @file config_manager.cc
 * @brief 配置管理器实现
 * @author CL
 * @date 2025-11-25
 */

#include "utils/config_manager.h"
#include <QCoreApplication>
#include <spdlog/spdlog.h>

ConfigManager* ConfigManager::instance_ = nullptr;

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
    if (settings_) {
        settings_->sync();
        delete settings_;
    }
}

ConfigManager* ConfigManager::instance() {
    if (!instance_) {
        instance_ = new ConfigManager();
    }
    return instance_;
}

// 识别设置
float ConfigManager::getRecognitionThreshold() const {
    return settings_->value("recognition/threshold", 0.60).toDouble();
}

void ConfigManager::setRecognitionThreshold(float threshold) {
    settings_->setValue("recognition/threshold", static_cast<double>(threshold));
    settings_->sync();
}

int ConfigManager::getDuplicateCheckInterval() const {
    return settings_->value("recognition/duplicate_interval", 300).toInt();
}

void ConfigManager::setDuplicateCheckInterval(int seconds) {
    settings_->setValue("recognition/duplicate_interval", seconds);
    settings_->sync();
}

int ConfigManager::getRecognitionConfirmCount() const {
    return settings_->value("recognition/confirm_count", 5).toInt();
}

void ConfigManager::setRecognitionConfirmCount(int count) {
    settings_->setValue("recognition/confirm_count", count);
    settings_->sync();
}

int ConfigManager::getUserConfirmDuration() const {
    return settings_->value("recognition/user_confirm_duration_ms", 1000).toInt();
}

void ConfigManager::setUserConfirmDuration(int milliseconds) {
    settings_->setValue("recognition/user_confirm_duration_ms", milliseconds);
    settings_->sync();
}

// 音频设置
bool ConfigManager::isAudioEnabled() const {
    return settings_->value("audio/enabled", true).toBool();
}

void ConfigManager::setAudioEnabled(bool enabled) {
    settings_->setValue("audio/enabled", enabled);
    settings_->sync();
}

int ConfigManager::getAudioVolume() const {
    return settings_->value("audio/volume", 70).toInt();
}

void ConfigManager::setAudioVolume(int volume) {
    settings_->setValue("audio/volume", volume);
    settings_->sync();
}

QString ConfigManager::getAudioDevice() const {
    return settings_->value("audio/device", "").toString();
}

void ConfigManager::setAudioDevice(const QString& device) {
    settings_->setValue("audio/device", device);
    settings_->sync();
}

// 显示设置
bool ConfigManager::isShowFPS() const {
    return settings_->value("display/show_fps", true).toBool();
}

void ConfigManager::setShowFPS(bool show) {
    settings_->setValue("display/show_fps", show);
    settings_->sync();
}

bool ConfigManager::isShowConfidence() const {
    return settings_->value("display/show_confidence", true).toBool();
}

void ConfigManager::setShowConfidence(bool show) {
    settings_->setValue("display/show_confidence", show);
    settings_->sync();
}

bool ConfigManager::isAutoStart() const {
    return settings_->value("display/auto_start", false).toBool();
}

void ConfigManager::setAutoStart(bool autoStart) {
    settings_->setValue("display/auto_start", autoStart);
    settings_->sync();
}

// 考勤设置
QString ConfigManager::getWorkStartTime() const {
    return settings_->value("attendance/work_start_time", "09:00").toString();
}

void ConfigManager::setWorkStartTime(const QString& time) {
    settings_->setValue("attendance/work_start_time", time);
    settings_->sync();
}

QString ConfigManager::getWorkEndTime() const {
    return settings_->value("attendance/work_end_time", "18:00").toString();
}

void ConfigManager::setWorkEndTime(const QString& time) {
    settings_->setValue("attendance/work_end_time", time);
    settings_->sync();
}

int ConfigManager::getLateThreshold() const {
    return settings_->value("attendance/late_threshold", 30).toInt();
}

void ConfigManager::setLateThreshold(int minutes) {
    settings_->setValue("attendance/late_threshold", minutes);
    settings_->sync();
}

int ConfigManager::getEarlyLeaveThreshold() const {
    return settings_->value("attendance/early_leave_threshold", 30).toInt();
}

void ConfigManager::setEarlyLeaveThreshold(int minutes) {
    settings_->setValue("attendance/early_leave_threshold", minutes);
    settings_->sync();
}

bool ConfigManager::isAllowMultipleCheckin() const {
    return settings_->value("attendance/allow_multiple_checkin", false).toBool();
}

void ConfigManager::setAllowMultipleCheckin(bool allow) {
    settings_->setValue("attendance/allow_multiple_checkin", allow);
    settings_->sync();
}

bool ConfigManager::isCheckinSound() const {
    return settings_->value("attendance/checkin_sound", true).toBool();
}

void ConfigManager::setCheckinSound(bool enabled) {
    settings_->setValue("attendance/checkin_sound", enabled);
    settings_->sync();
}

bool ConfigManager::isShowCheckinReminder() const {
    return settings_->value("attendance/show_checkin_reminder", true).toBool();
}

void ConfigManager::setShowCheckinReminder(bool show) {
    settings_->setValue("attendance/show_checkin_reminder", show);
    settings_->sync();
}

// 摄像头设置（简化：固定 USB + 异步）
int ConfigManager::getCameraId() const {
    return settings_->value("camera/id", 21).toInt();
}

void ConfigManager::setCameraId(int id) {
    settings_->setValue("camera/id", id);
    settings_->sync();
}


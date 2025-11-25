/**
 * @file config_manager.h
 * @brief 配置管理器 - 负责配置的保存和加载
 * @author CL
 * @date 2025-11-25
 */

#pragma once

#include <QSettings>
#include <QString>

/**
 * @brief 配置管理器类（单例）
 */
class ConfigManager {
public:
    static ConfigManager* instance();

    // 识别设置
    float getRecognitionThreshold() const;
    void setRecognitionThreshold(float threshold);

    int getDuplicateCheckInterval() const;
    void setDuplicateCheckInterval(int seconds);

    int getRecognitionConfirmCount() const;
    void setRecognitionConfirmCount(int count);

    // 用户识别确认时间（毫秒）
    int getUserConfirmDuration() const;
    void setUserConfirmDuration(int milliseconds);

    // 音频设置
    bool isAudioEnabled() const;
    void setAudioEnabled(bool enabled);

    int getAudioVolume() const;
    void setAudioVolume(int volume);

    QString getAudioDevice() const;
    void setAudioDevice(const QString& device);

    // 显示设置
    bool isShowFPS() const;
    void setShowFPS(bool show);

    bool isShowConfidence() const;
    void setShowConfidence(bool show);

    bool isAutoStart() const;
    void setAutoStart(bool autoStart);

    // 考勤设置
    QString getWorkStartTime() const;
    void setWorkStartTime(const QString& time);

    QString getWorkEndTime() const;
    void setWorkEndTime(const QString& time);

    int getLateThreshold() const;
    void setLateThreshold(int minutes);

    int getEarlyLeaveThreshold() const;
    void setEarlyLeaveThreshold(int minutes);

    bool isAllowMultipleCheckin() const;
    void setAllowMultipleCheckin(bool allow);

    bool isCheckinSound() const;
    void setCheckinSound(bool enabled);

    bool isShowCheckinReminder() const;
    void setShowCheckinReminder(bool show);

    // 摄像头设置（固定 USB + 异步）
    int getCameraId() const;
    void setCameraId(int id);

private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    static ConfigManager* instance_;
    QSettings* settings_;
};


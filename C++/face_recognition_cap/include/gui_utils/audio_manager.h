/**
 * @file audio_manager.h
 * @brief 音频管理器 - 负责音频播放、队列管理和设备控制
 * @author CL
 * @date 2025-11-25
 *
 * 功能：
 * - 异步音频播放（不阻塞 UI 和识别线程）
 * - FIFO 队列机制（防止音频重叠）
 * - 音频设备枚举和选择
 * - 音量控制
 * - 线程安全
 */

#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QThread>
#include <chrono>
#include <map>
#include "gui_services/tts_service_local.h"

/**
 * @brief 音频类型枚举
 */
enum class AudioType {
    CheckInSuccess,        // 签到成功
    AlreadyCheckedIn,      // 已签到（重复签到）
    CheckOutSuccess,       // 签退成功
    AlreadyCheckedOut,     // 已签退（重复签退）
    RegistrationSuccess,   // 注册成功
    RegisteringFace,       // 正在注册人脸
    LowLight,              // 光线太暗
    MoveCloser,            // 请靠近
    DoNotBlock,            // 不要遮挡
    StrangerDetected       // 陌生人/人脸未注册
};

/**
 * @brief 音频管理器类（单例）
 * 
 * 特点：
 * - 单例模式，全局唯一实例
 * - FIFO 队列，音频按顺序播放
 * - 线程安全，可在任意线程调用
 * - 支持音频设备选择和音量控制
 */
class AudioManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief 获取单例实例
     * @return AudioManager* 单例指针
     */
    static AudioManager* instance();

    /**
     * @brief 播放音频（加入队列，支持跨线程 invokeMethod 调用）
     * @param audioFile 音频文件路径（相对或绝对路径）
     */
    Q_INVOKABLE void playSound(const QString& audioFile);

    /**
     * @brief 播放指定类型的音频（支持传入用户姓名进行智能称呼）
     * @param type 音频类型
     * @param userName 用户姓名（可选）
     */
    Q_INVOKABLE void playSound(AudioType type, const QString& userName = "");

    /**
     * @brief 动态实时合成并播放文本 (流式离线 TTS)
     * @param text 文本内容
     */
    Q_INVOKABLE void speakText(const QString& text);

    /**
     * @brief 获取音频类型对应的文件路径
     * @param type 音频类型
     * @return QString 音频文件路径
     */
    static QString audioPath(AudioType type);

    /**
     * @brief 获取可用的音频设备列表
     * @return QStringList 设备名称列表
     */
    QStringList availableDevices() const;

    /**
     * @brief 获取当前使用的音频设备
     * @return QString 设备名称
     */
    QString currentDevice() const;

    /**
     * @brief 设置音频输出设备
     * @param deviceName 设备名称（必须在 availableDevices() 返回的列表中）
     */
    Q_INVOKABLE void setAudioDevice(const QString& deviceName);

    /**
     * @brief 设置音量
     * @param volume 音量值（0-100）
     */
    Q_INVOKABLE void setVolume(int volume);

    /**
     * @brief 获取当前音量
     * @return int 音量值（0-100）
     */
    int volume() const;

    /**
     * @brief 启用或禁用音频播放
     * @param enabled true=启用，false=禁用
     */
    Q_INVOKABLE void setEnabled(bool enabled);

    /**
     * @brief 检查音频是否启用
     * @return bool true=已启用，false=已禁用
     */
    bool isEnabled() const;

    /**
     * @brief 清空播放队列
     */
    Q_INVOKABLE void clearQueue();

    /**
     * @brief 获取队列中待播放的音频数量
     * @return int 队列大小
     */
    int queueSize() const;

    /**
     * @brief 安全停止当前播放
     */
    Q_INVOKABLE void stopPlayback();

    /**
     * @brief 异步刷新设备列表
     * 注意：这是一个 slot，支持跨线程 invokeMethod 调用
     */
    Q_INVOKABLE void refreshDevicesAsync();

    /**
     * @brief 带冷却时间地播放音频
     * @param type 音频类型
     * @param cooldown_ms 冷却时间（毫秒）
     * @return bool true=已触发播放，false=仍在冷却中
     */
    bool playSoundWithCooldown(AudioType type, int cooldown_ms);

    /**
     * @brief 重置指定音频类型的冷却状态
     * @param type 音频类型
     */
    void resetCooldown(AudioType type);

signals:
    /**
     * @brief 音频播放开始信号
     * @param audioFile 正在播放的音频文件路径
     */
    void playbackStarted(const QString& audioFile);

    /**
     * @brief 音频播放结束信号
     * @param audioFile 播放完成的音频文件路径
     */
    void playbackFinished(const QString& audioFile);

    /**
     * @brief 音频播放错误信号
     * @param audioFile 出错的音频文件路径
     * @param error 错误信息
     */
    void playbackError(const QString& audioFile, const QString& error);

    /**
     * @brief 设备列表刷新完成信号
     * @param devices 设备列表
     */
    void devicesRefreshed(const QStringList& devices);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void playSoundInternal(const QString& audioFile);
    void onDevicesDetected(const QStringList& devices);
    void onVolumeTaskFinished(int appliedVolume);

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    explicit AudioManager(QObject* parent = nullptr);

    /**
     * @brief 私有析构函数
     */
    ~AudioManager();

    /**
     * @brief 禁用拷贝构造
     */
    AudioManager(const AudioManager&) = delete;

    /**
     * @brief 禁用赋值操作
     */
    AudioManager& operator=(const AudioManager&) = delete;

    /**
     * @brief 播放队列中的下一个音频
     */
    void playNext();

    void detectAlsaDevice();
    void applyAlsaVolume(int volume);
    QStringList detectDevicesSync() const;

    /**
     * @brief 获取音频文件的基础路径
     * @return QString 音频文件目录路径
     */
    static QString getAudioBasePath();

private:
    static AudioManager* instance_;  ///< 单例实例
    QProcess* aplay_process_;         ///< aplay 外部播放器
    QQueue<QString> audio_queue_;     ///< 音频播放队列（FIFO）
    mutable QMutex mutex_;            ///< 互斥锁（保证线程安全）
    bool enabled_;                    ///< 是否启用音频
    int volume_;                      ///< 音量（0-100）
    QTimer* volume_timer_;            ///< 音量设置节流定时器
    int pending_volume_;              ///< 等待应用的音量
    QString alsa_device_;             ///< ALSA 设备名 (plughw:card,device)
    QString current_device_;          ///< 当前音频设备
    QString current_playing_;         ///< 当前正在播放的文件
    bool is_playing_;                 ///< 是否正在播放
    QStringList cached_devices_;      ///< 缓存的设备列表
    bool devices_loaded_;             ///< 设备列表是否已加载
    bool devices_refresh_in_progress_;///< 设备刷新是否进行中
    bool volume_task_running_;        ///< 音量设置任务进行中
    int last_applied_volume_;         ///< 最近一次应用的音量
    std::map<AudioType, std::chrono::steady_clock::time_point> last_audio_play_times_; // 各音频类型冷却时间
    gui_services::TtsServiceLocal tts_service_; ///< 离线 TTS 实时流式合成服务
};

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
#include <QMediaPlayer>
#include <QAudioDeviceInfo>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QTimer>

/**
 * @brief 音频类型枚举
 */
enum class AudioType {
    CheckInSuccess,        // 签到成功
    AlreadyCheckedIn,      // 已签到（重复签到）
    CheckOutSuccess,       // 签退成功
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
     * @brief 播放音频（加入队列）
     * @param audioFile 音频文件路径（相对或绝对路径）
     */
    void playSound(const QString& audioFile);

    /**
     * @brief 播放指定类型的音频
     * @param type 音频类型
     */
    void playSound(AudioType type);

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
    void setAudioDevice(const QString& deviceName);

    /**
     * @brief 设置音量
     * @param volume 音量值（0-100）
     */
    void setVolume(int volume);

    /**
     * @brief 获取当前音量
     * @return int 音量值（0-100）
     */
    int volume() const;

    /**
     * @brief 启用或禁用音频播放
     * @param enabled true=启用，false=禁用
     */
    void setEnabled(bool enabled);

    /**
     * @brief 检查音频是否启用
     * @return bool true=已启用，false=已禁用
     */
    bool isEnabled() const;

    /**
     * @brief 清空播放队列
     */
    void clearQueue();

    /**
     * @brief 获取队列中待播放的音频数量
     * @return int 队列大小
     */
    int queueSize() const;

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

private slots:
    /**
     * @brief 处理 QMediaPlayer 状态变化
     * @param state 新状态
     */
    void onPlayerStateChanged(QMediaPlayer::State state);

    /**
     * @brief 处理 QMediaPlayer 错误
     * @param error 错误类型
     */
    void onPlayerError(QMediaPlayer::Error error);

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

    /**
     * @brief 获取音频文件的基础路径
     * @return QString 音频文件目录路径
     */
    static QString getAudioBasePath();

private:
    static AudioManager* instance_;  ///< 单例实例
    QMediaPlayer* player_;            ///< 媒体播放器
    QQueue<QString> audio_queue_;     ///< 音频播放队列（FIFO）
    mutable QMutex mutex_;            ///< 互斥锁（保证线程安全）
    bool enabled_;                    ///< 是否启用音频
    int volume_;                      ///< 音量（0-100）
    QString current_device_;          ///< 当前音频设备
    QString current_playing_;         ///< 当前正在播放的文件
    bool is_playing_;                 ///< 是否正在播放
};


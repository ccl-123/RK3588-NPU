/**
 * @file audio_manager.cc
 * @brief 音频管理器实现
 * @author CL
 * @date 2025-11-25
 * 功能：
 * - 异步音频播放（不阻塞 UI 和识别线程）
 * - FIFO 队列机制（防止音频重叠）
 * - 音频设备枚举和选择
 * - 音量控制
 * - 线程安全
 */

#include "utils/audio_manager.h"

#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QUrl>
#include <spdlog/spdlog.h>

// 静态成员初始化
AudioManager* AudioManager::instance_ = nullptr;

AudioManager::AudioManager(QObject* parent)
    : QObject(parent)
    , player_(nullptr)
    , enabled_(true)
    , volume_(70)
    , is_playing_(false) {
    
    // 检查音频设备可用性
    QAudioDeviceInfo default_device = QAudioDeviceInfo::defaultOutputDevice();
    if (!default_device.isNull()) {
        current_device_ = default_device.deviceName();
        spdlog::info("AudioManager: Default audio device: {}", 
                     current_device_.toStdString());
    } else {
        spdlog::error("AudioManager: No default audio output device found - audio disabled");
        enabled_ = false;  // 没有音频设备，禁用音频
        return;
    }
    
    // 延迟创建 QMediaPlayer（异步初始化）
    QTimer::singleShot(0, this, [this]() {
        try {
            player_ = new QMediaPlayer(this);
            
            // 连接信号槽
            connect(player_, &QMediaPlayer::stateChanged,
                    this, &AudioManager::onPlayerStateChanged);
            connect(player_, static_cast<void(QMediaPlayer::*)(QMediaPlayer::Error)>(&QMediaPlayer::error),
                    this, &AudioManager::onPlayerError);
            
            // 设置默认音量
            player_->setVolume(volume_);
            
            spdlog::info("AudioManager: QMediaPlayer initialized (volume: {}, enabled: {})", 
                         volume_, enabled_);
        } catch (const std::exception& e) {
            spdlog::error("AudioManager: Failed to create QMediaPlayer: {}", e.what());
            enabled_ = false;
        }
    });
}

AudioManager::~AudioManager() {
    QMutexLocker locker(&mutex_);
    
    if (player_) {
        player_->stop();
        delete player_;
        player_ = nullptr;
    }
    
    audio_queue_.clear();
    spdlog::info("AudioManager destroyed");
}

AudioManager* AudioManager::instance() {
    if (instance_ == nullptr) {
        instance_ = new AudioManager();
    }
    return instance_;
}

void AudioManager::playSound(const QString& audioFile) {
    QMutexLocker locker(&mutex_);
    
    if (!enabled_) {
        spdlog::debug("AudioManager: playSound called but audio is disabled");
        return;
    }
    
    if (!player_) {
        spdlog::warn("AudioManager: QMediaPlayer not initialized yet, audio request ignored");
        return;
    }
    
    // 检查文件是否存在
    QFileInfo fileInfo(audioFile);
    if (!fileInfo.exists()) {
        spdlog::error("AudioManager: Audio file not found: {}", audioFile.toStdString());
        emit playbackError(audioFile, "文件不存在");
        return;
    }
    
    // 限制队列大小，防止积压
    if (audio_queue_.size() >= 5) {
        spdlog::warn("AudioManager: Queue full ({} items), dropping oldest", audio_queue_.size());
        audio_queue_.dequeue();  // 移除最旧的
    }
    
    // 加入队列
    audio_queue_.enqueue(audioFile);
    spdlog::debug("AudioManager: Added to queue: {} (queue size: {})", 
                  audioFile.toStdString(), audio_queue_.size());
    
    // 如果当前没有播放，异步开始播放
    if (!is_playing_) {
        locker.unlock();  // 释放锁
        QTimer::singleShot(0, this, [this]() {
            playNext();
        });
    }
}

void AudioManager::playSound(AudioType type) {
    QString filePath = audioPath(type);
    playSound(filePath);
}

QString AudioManager::audioPath(AudioType type) {
    QString basePath = getAudioBasePath();
    
    QString fileName;
    switch (type) {
        case AudioType::CheckInSuccess:
            fileName = "check_in_success.wav";
            break;
        case AudioType::AlreadyCheckedIn:
            fileName = "already_checked_in.wav";
            break;
        case AudioType::CheckOutSuccess:
            fileName = "check_out_success.wav";
            break;
        case AudioType::AlreadyCheckedOut:
            fileName = "already_checked_out.wav";
            break;
        case AudioType::RegistrationSuccess:
            fileName = "registration_success.wav";
            break;
        case AudioType::RegisteringFace:
            fileName = "registering_face.wav";
            break;
        case AudioType::LowLight:
            fileName = "low_light.wav";
            break;
        case AudioType::MoveCloser:
            fileName = "move_closer.wav";
            break;
        case AudioType::DoNotBlock:
            fileName = "do_not_block.wav";
            break;
        case AudioType::StrangerDetected:
            fileName = "stranger_detected.wav";
            break;
        default:
            fileName = "check_in_success.wav";
            break;
    }
    
    return basePath + "/" + fileName;
}

QStringList AudioManager::availableDevices() const {
    QStringList devices;
    
    QList<QAudioDeviceInfo> audioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& deviceInfo : audioDevices) {
        devices.append(deviceInfo.deviceName());
    }
    
    return devices;
}

QString AudioManager::currentDevice() const {
    QMutexLocker locker(&mutex_);
    return current_device_;
}

void AudioManager::setAudioDevice(const QString& deviceName) {
    QMutexLocker locker(&mutex_);
    
    // 查找设备
    QList<QAudioDeviceInfo> audioDevices = QAudioDeviceInfo::availableDevices(QAudio::AudioOutput);
    for (const QAudioDeviceInfo& deviceInfo : audioDevices) {
        if (deviceInfo.deviceName() == deviceName) {
            current_device_ = deviceName;
            spdlog::info("AudioManager: Audio device changed to: {}", deviceName.toStdString());
            
            // 注意：Qt5 的 QMediaPlayer 不直接支持切换音频设备
            // 需要重新创建 QMediaPlayer 或使用 QAudioOutput (Qt6)
            // 这里只记录设备名称，实际切换需要更复杂的实现
            
            return;
        }
    }
    
    spdlog::warn("AudioManager: Audio device not found: {}", deviceName.toStdString());
}

void AudioManager::setVolume(int volume) {
    QMutexLocker locker(&mutex_);
    
    volume_ = qBound(0, volume, 100);
    
    if (player_) {
        player_->setVolume(volume_);
    }
    
    spdlog::debug("AudioManager: Volume set to {}", volume_);
}

int AudioManager::volume() const {
    QMutexLocker locker(&mutex_);
    return volume_;
}

void AudioManager::setEnabled(bool enabled) {
    QMutexLocker locker(&mutex_);
    
    enabled_ = enabled;
    
    if (!enabled_ && player_ && player_->state() == QMediaPlayer::PlayingState) {
        player_->stop();
        audio_queue_.clear();
        is_playing_ = false;
    }
    
    spdlog::info("AudioManager: Audio {} ", enabled_ ? "enabled" : "disabled");
}

bool AudioManager::isEnabled() const {
    QMutexLocker locker(&mutex_);
    return enabled_;
}

void AudioManager::clearQueue() {
    QMutexLocker locker(&mutex_);
    
    audio_queue_.clear();
    spdlog::debug("AudioManager: Queue cleared");
}

int AudioManager::queueSize() const {
    QMutexLocker locker(&mutex_);
    return audio_queue_.size();
}

void AudioManager::onPlayerStateChanged(QMediaPlayer::State state) {
    spdlog::debug("AudioManager: Player state changed: {}", static_cast<int>(state));
    
    if (state == QMediaPlayer::StoppedState) {
        QMutexLocker locker(&mutex_);
        
        is_playing_ = false;
        
        // 播放完成
        if (!current_playing_.isEmpty()) {
            spdlog::info("AudioManager: Finished playing: {}", current_playing_.toStdString());
            emit playbackFinished(current_playing_);
            current_playing_.clear();
        }
        
        locker.unlock();
        
        // 播放下一个
        playNext();
    } else if (state == QMediaPlayer::PlayingState) {
        QMutexLocker locker(&mutex_);
        is_playing_ = true;
        
        if (!current_playing_.isEmpty()) {
            spdlog::info("AudioManager: Started playing: {}", current_playing_.toStdString());
            emit playbackStarted(current_playing_);
        }
    }
}

void AudioManager::onPlayerError(QMediaPlayer::Error error) {
    QString errorString = player_->errorString();
    
    spdlog::error("AudioManager: Player error: {} - {}", 
                  static_cast<int>(error), errorString.toStdString());
    
    QMutexLocker locker(&mutex_);
    
    if (!current_playing_.isEmpty()) {
        emit playbackError(current_playing_, errorString);
        current_playing_.clear();
    }
    
    is_playing_ = false;
    
    locker.unlock();
    
    // 尝试播放下一个
    playNext();
}

void AudioManager::playNext() {
    QMutexLocker locker(&mutex_);
    
    if (!enabled_) {
        return;
    }
    
    if (!player_) {
        spdlog::warn("AudioManager: QMediaPlayer not ready, cannot play");
        return;
    }
    
    if (audio_queue_.isEmpty()) {
        return;
    }
    
    if (is_playing_) {
        return;  // 正在播放，等待播放完成
    }
    
    // 从队列中取出下一个音频
    QString nextAudio = audio_queue_.dequeue();
    current_playing_ = nextAudio;
    
    spdlog::debug("AudioManager: Playing next: {} (remaining in queue: {})", 
                  nextAudio.toStdString(), audio_queue_.size());
    
    // 异步设置媒体内容并播放（避免阻塞）
    QUrl url = QUrl::fromLocalFile(nextAudio);
    locker.unlock();  // 释放锁，避免死锁
    
    try {
        player_->setMedia(QMediaContent(url));
        player_->play();
        is_playing_ = true;
    } catch (const std::exception& e) {
        spdlog::error("AudioManager: Failed to play audio: {}", e.what());
        locker.relock();
        is_playing_ = false;
        current_playing_.clear();
        locker.unlock();
        // 尝试播放下一个
        QTimer::singleShot(100, this, [this]() {
            playNext();
        });
    }
}

QString AudioManager::getAudioBasePath() {
    // 尝试多个可能的路径
    QStringList possiblePaths = {
        // 相对于可执行文件的路径
        QCoreApplication::applicationDirPath() + "/data/voice",
        
        // 安装目录路径
        "/home/firefly/open_project/edge2-npu/C++/face_recognition_cap/install/face_recognition_cap/data/voice",
        "/home/firefly/open_project/edge2-npu/C++/face_recognition_cap/data/voice",
        
        // 开发环境路径
        QCoreApplication::applicationDirPath() + "/../data/voice",
        QCoreApplication::applicationDirPath() + "/../../data/voice",
        QCoreApplication::applicationDirPath() + "/../../../data/voice",
    };
    
    // 查找第一个存在的路径
    for (const QString& path : possiblePaths) {
        QDir dir(path);
        if (dir.exists()) {
            spdlog::debug("AudioManager: Using audio base path: {}", path.toStdString());
            return path;
        }
    }
    
    // 如果都不存在，返回默认路径（会在播放时报错）
    QString defaultPath = possiblePaths.first();
    spdlog::warn("AudioManager: Audio directory not found, using default: {}", 
                 defaultPath.toStdString());
    return defaultPath;
}


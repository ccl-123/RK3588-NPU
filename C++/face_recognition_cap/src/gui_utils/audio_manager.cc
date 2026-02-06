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

#include "gui_utils/audio_manager.h"

#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QMetaObject>
#include <QtConcurrent>
#include <QPointer>
#include <mutex>
#include <spdlog/spdlog.h>

// 静态成员初始化
AudioManager* AudioManager::instance_ = nullptr;

AudioManager::AudioManager(QObject* parent)
    : QObject(parent)
    , aplay_process_(new QProcess(this))
    , enabled_(true)
    , volume_(70)
    , volume_timer_(new QTimer(this))
    , pending_volume_(70)
    , alsa_device_("plughw:0,0")
    , is_playing_(false)
    , devices_loaded_(false)
    , devices_refresh_in_progress_(false)
    , volume_task_running_(false)
    , last_applied_volume_(-1) {

    // 延迟检测设备，避免阻塞构造函数
    QTimer::singleShot(0, this, [this]() {
        detectAlsaDevice();
        current_device_ = alsa_device_;
        spdlog::info("AudioManager: Using ALSA device: {}", alsa_device_.toStdString());
    });

    connect(aplay_process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AudioManager::onProcessFinished);
    connect(aplay_process_, &QProcess::errorOccurred,
            this, &AudioManager::onProcessError);

    volume_timer_->setSingleShot(true);
    connect(volume_timer_, &QTimer::timeout, this, [this]() {
        int volume = 0;
        {
            QMutexLocker locker(&mutex_);
            volume = pending_volume_;
        }
        applyAlsaVolume(volume);
    });
}

AudioManager::~AudioManager() {
    // 先断开所有信号连接，防止回调触发死锁
    if (aplay_process_) {
        disconnect(aplay_process_, nullptr, this, nullptr);
    }

    // 停止播放（不会再触发回调了）
    {
        QMutexLocker locker(&mutex_);
        if (aplay_process_ && aplay_process_->state() != QProcess::NotRunning) {
            aplay_process_->kill();
        }
        // 清理状态
        is_playing_ = false;
        current_playing_.clear();
        audio_queue_.clear();
    }

    // 在锁外等待进程结束，避免死锁
    if (aplay_process_) {
        aplay_process_->waitForFinished(1000);
        delete aplay_process_;
        aplay_process_ = nullptr;
    }

    spdlog::info("AudioManager destroyed");
}

AudioManager* AudioManager::instance() {
    static std::mutex instance_mutex;
    std::lock_guard<std::mutex> lock(instance_mutex);

    if (instance_ == nullptr) {
        instance_ = new AudioManager();
        // 确保单例对象在程序结束时被正确销毁
        std::atexit([]() {
            delete instance_;
            instance_ = nullptr;
        });
    }
    return instance_;
}

void AudioManager::playSound(const QString& audioFile) {
    // 线程安全：如果不在 AudioManager 所属线程，使用 invokeMethod 转发
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "playSoundInternal",
            Qt::QueuedConnection, Q_ARG(QString, audioFile));
        return;
    }
    playSoundInternal(audioFile);
}

void AudioManager::playSoundInternal(const QString& audioFile) {
    QMutexLocker locker(&mutex_);
    
    if (!enabled_) {
        spdlog::debug("AudioManager: playSound called but audio is disabled");
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
        // 不在这里释放锁，直接使用QTimer::singleShot异步调用
        // 避免竞态条件：playNext()会重新获取锁
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
    QMutexLocker locker(&mutex_);
    if (devices_loaded_) {
        return cached_devices_;
    }
    // 首次调用时返回默认设备，并自动触发异步刷新
    QStringList defaultList;
    defaultList.append(current_device_.isEmpty() ? alsa_device_ : current_device_);
    return defaultList;
}

QStringList AudioManager::detectDevicesSync() const {
    QStringList devices;
    QString defaultDevice;
    {
        QMutexLocker locker(&mutex_);
        defaultDevice = alsa_device_;
    }

    QProcess proc;
    proc.start("aplay", QStringList() << "-l");
    if (!proc.waitForFinished(2000)) {
        spdlog::warn("AudioManager: aplay -l timeout, returning default device");
        devices.append(defaultDevice);
        return devices;
    }

    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QRegularExpression re("card (\\d+):\\s*(\\S+)\\s*\\[([^\\]]+)\\], device (\\d+):");
    QRegularExpressionMatchIterator it = re.globalMatch(output);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString card = match.captured(1);
        QString cardName = match.captured(3);
        QString device = match.captured(4);

        QString alsaDevice = QString("plughw:%1,%2").arg(card, device);
        QString displayName = QString("%1 (%2)").arg(alsaDevice, cardName);
        devices.append(displayName);
    }

    if (devices.isEmpty()) {
        devices.append(defaultDevice);
    }

    return devices;
}

void AudioManager::refreshDevicesAsync() {
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "refreshDevicesAsync", Qt::QueuedConnection);
        return;
    }

    {
        QMutexLocker locker(&mutex_);
        if (devices_refresh_in_progress_) {
            return;
        }
        devices_refresh_in_progress_ = true;
    }

    // 使用 QPointer 防护，避免析构后回调悬空指针
    // 注意：如果对象在任务执行期间被销毁，devices_refresh_in_progress_
    // 不会被重置，但由于对象已销毁，这个标志也不复存在，所以是安全的
    QPointer<AudioManager> self = this;
    QtConcurrent::run([self]() {
        if (!self) return;  // 对象已销毁
        QStringList devices = self->detectDevicesSync();
        if (!self) return;  // 再次检查
        QMetaObject::invokeMethod(self.data(), "onDevicesDetected",
            Qt::QueuedConnection, Q_ARG(QStringList, devices));
    });
}

void AudioManager::onDevicesDetected(const QStringList& devices) {
    {
        QMutexLocker locker(&mutex_);
        cached_devices_ = devices;
        devices_loaded_ = true;
        devices_refresh_in_progress_ = false;
    }
    spdlog::info("AudioManager: Detected {} audio device(s)", devices.size());
    emit devicesRefreshed(devices);
}

QString AudioManager::currentDevice() const {
    QMutexLocker locker(&mutex_);
    return current_device_;
}

void AudioManager::setAudioDevice(const QString& deviceName) {
    // 线程安全：确保在 AudioManager 所属线程执行
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "setAudioDevice",
            Qt::QueuedConnection, Q_ARG(QString, deviceName));
        return;
    }

    QMutexLocker locker(&mutex_);

    QString trimmed = deviceName.trimmed();
    QString device = trimmed.section(' ', 0, 0).trimmed();
    if (!device.startsWith("plughw:") && !device.startsWith("hw:")) {
        spdlog::warn("AudioManager: Invalid ALSA device: {}", deviceName.toStdString());
        return;
    }

    alsa_device_ = device;
    current_device_ = deviceName;
    spdlog::info("AudioManager: Audio device changed to: {}", deviceName.toStdString());
}

void AudioManager::setVolume(int volume) {
    // 线程安全：确保在 AudioManager 所属线程执行（因为涉及 QTimer）
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "setVolume",
            Qt::QueuedConnection, Q_ARG(int, volume));
        return;
    }

    QMutexLocker locker(&mutex_);

    volume_ = qBound(0, volume, 100);

    pending_volume_ = volume_;
    volume_timer_->start(150);

    spdlog::debug("AudioManager: Volume set to {}", volume_);
}

int AudioManager::volume() const {
    QMutexLocker locker(&mutex_);
    return volume_;
}

void AudioManager::setEnabled(bool enabled) {
    // 线程安全：确保在 AudioManager 所属线程执行（因为涉及 QProcess）
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "setEnabled",
            Qt::QueuedConnection, Q_ARG(bool, enabled));
        return;
    }

    QMutexLocker locker(&mutex_);

    enabled_ = enabled;

    if (!enabled_) {
        if (aplay_process_ && aplay_process_->state() != QProcess::NotRunning) {
            aplay_process_->kill();
        }
        audio_queue_.clear();
        is_playing_ = false;
        current_playing_.clear();
    }

    spdlog::info("AudioManager: Audio {} ", enabled_ ? "enabled" : "disabled");
}

bool AudioManager::isEnabled() const {
    QMutexLocker locker(&mutex_);
    return enabled_;
}

void AudioManager::clearQueue() {
    // 线程安全
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "clearQueue", Qt::QueuedConnection);
        return;
    }

    QMutexLocker locker(&mutex_);

    audio_queue_.clear();
    spdlog::debug("AudioManager: Queue cleared");
}

int AudioManager::queueSize() const {
    QMutexLocker locker(&mutex_);
    return audio_queue_.size();
}

void AudioManager::stopPlayback() {
    // 线程安全：确保在 AudioManager 所属线程执行（因为涉及 QProcess）
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, "stopPlayback", Qt::QueuedConnection);
        return;
    }

    QMutexLocker locker(&mutex_);

    try {
        if (aplay_process_ && aplay_process_->state() != QProcess::NotRunning) {
            aplay_process_->kill();
        }
    } catch (const std::exception& e) {
        spdlog::error("AudioManager: Error stopping playback: {}", e.what());
    }

    // 清理状态
    is_playing_ = false;
    current_playing_.clear();
    spdlog::debug("AudioManager: Playback stopped");
}

void AudioManager::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    QMutexLocker locker(&mutex_);
    is_playing_ = false;
    QString finished = current_playing_;
    current_playing_.clear();
    locker.unlock();

    if (!finished.isEmpty()) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            spdlog::info("AudioManager: Finished playing: {}", finished.toStdString());
            emit playbackFinished(finished);
        } else {
            spdlog::error("AudioManager: playback failed (exitCode={}, status={}) for {}",
                          exitCode, static_cast<int>(status), finished.toStdString());
            emit playbackError(finished, "aplay 播放失败");
        }
    }

    playNext();
}

void AudioManager::onProcessError(QProcess::ProcessError error) {
    QMutexLocker locker(&mutex_);
    is_playing_ = false;
    QString failed = current_playing_;
    current_playing_.clear();
    locker.unlock();

    spdlog::error("AudioManager: aplay process error: {} ({})",
                  static_cast<int>(error), failed.toStdString());
    if (!failed.isEmpty()) {
        emit playbackError(failed, "aplay 进程错误");
    }
    playNext();
}

void AudioManager::playNext() {
    QMutexLocker locker(&mutex_);
    
    if (!enabled_) {
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
    is_playing_ = true;

    // 复制需要的成员变量，解锁后使用
    QString device = alsa_device_;
    int queueSize = audio_queue_.size();

    locker.unlock();  // 释放锁，避免阻塞其他线程

    spdlog::info("AudioManager: Playing (aplay -D {}): {} (remaining: {})",
                 device.toStdString(), nextAudio.toStdString(), queueSize);

    QStringList args;
    args << "-D" << device << "-q" << nextAudio;
    emit playbackStarted(nextAudio);
    aplay_process_->start("aplay", args);
}

QString AudioManager::getAudioBasePath() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString cwd = QDir::currentPath();

    // 尝试多个可能的路径（安装目录、开发目录、当前工作目录）
    QStringList possiblePaths = {
        appDir + "/data/voice",
        appDir + "/../data/voice",
        appDir + "/../../data/voice",
        appDir + "/../../../data/voice",
        appDir + "/../face_recognition_cap/data/voice",
        cwd + "/data/voice",
        cwd + "/install/face_recognition_cap/data/voice",
        cwd + "/C++/face_recognition_cap/data/voice",
        cwd + "/C++/face_recognition_cap/install/face_recognition_cap/data/voice",
    };

    // 查找第一个存在的路径
    for (const QString& path : possiblePaths) {
        QDir dir(QDir::cleanPath(path));
        if (dir.exists()) {
            const QString normalized = dir.absolutePath();
            spdlog::debug("AudioManager: Using audio base path: {}", normalized.toStdString());
            return normalized;
        }
    }

    // 如果都不存在，返回默认路径（会在播放时报错）
    QString defaultPath = QDir::cleanPath(possiblePaths.first());
    spdlog::warn("AudioManager: Audio directory not found, using default: {}", 
                 defaultPath.toStdString());
    return defaultPath;
}

void AudioManager::detectAlsaDevice() {
    QProcess proc;
    proc.start("aplay", QStringList() << "-l");
    if (!proc.waitForFinished(2000)) {
        spdlog::warn("AudioManager: aplay -l timeout, using default: {}", alsa_device_.toStdString());
        return;
    }

    QString output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    QRegularExpression re("card (\\d+):\\s*(\\S+)\\s*\\[([^\\]]+)\\], device (\\d+):");
    QRegularExpressionMatchIterator it = re.globalMatch(output);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString card = match.captured(1);
        QString cardId = match.captured(2);
        QString cardName = match.captured(3);
        QString device = match.captured(4);

        if (cardName.contains("hdmi", Qt::CaseInsensitive) ||
            cardName.contains("dp", Qt::CaseInsensitive) ||
            cardName.contains("bt", Qt::CaseInsensitive) ||
            cardId.contains("hdmi", Qt::CaseInsensitive) ||
            cardId.contains("dp", Qt::CaseInsensitive)) {
            continue;
        }

        alsa_device_ = QString("plughw:%1,%2").arg(card, device);
        current_device_ = QString("%1 (%2)").arg(alsa_device_, cardName);
        spdlog::info("AudioManager: Detected ALSA device: {}", alsa_device_.toStdString());
        return;
    }
}

void AudioManager::applyAlsaVolume(int volume) {
    QString device;
    {
        QMutexLocker locker(&mutex_);
        device = alsa_device_;
        if (volume_task_running_) {
            return;
        }
        volume_task_running_ = true;
    }

    QRegularExpression re("^(?:plug)?hw:(\\d+),");
    QRegularExpressionMatch match = re.match(device);
    if (!match.hasMatch()) {
        spdlog::warn("AudioManager: Cannot parse ALSA card from device: {}", device.toStdString());
        {
            QMutexLocker locker(&mutex_);
            volume_task_running_ = false;
        }
        return;
    }

    QString cardNum = match.captured(1);

    // 在后台线程执行音量设置，避免阻塞 UI
    QPointer<AudioManager> self = this;
    QtConcurrent::run([self, cardNum, volume]() {
        if (!self) return;
        // 尝试多个控件，优先级：Master -> PCM -> Output 1/2
        // es8388 声卡使用 Output 1/2，其他声卡可能使用 Master/PCM
        QStringList controls = {"Master", "PCM", "Output 1", "Output 2"};

        for (const QString& ctrl : controls) {
            QStringList args;
            args << "-c" << cardNum << "sset" << ctrl << QString("%1%").arg(volume);

            QProcess proc;
            proc.start("amixer", args);
            if (proc.waitForFinished(1000) && proc.exitCode() == 0) {
                spdlog::debug("AudioManager: Volume set via {} to {}%", ctrl.toStdString(), volume);
                // Master/PCM 成功后就返回，Output 1/2 需要同时设置
                if (ctrl == "Master" || ctrl == "PCM") {
                    break;
                }
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), "onVolumeTaskFinished",
                                  Qt::QueuedConnection, Q_ARG(int, volume));
    });
}

void AudioManager::onVolumeTaskFinished(int appliedVolume) {
    int nextVolume = -1;
    {
        QMutexLocker locker(&mutex_);
        last_applied_volume_ = appliedVolume;
        volume_task_running_ = false;
        if (pending_volume_ != appliedVolume) {
            nextVolume = pending_volume_;
            // 不在这里设置 volume_task_running_ = true
            // applyAlsaVolume() 会自己设置该标志
        }
    }

    if (nextVolume >= 0) {
        applyAlsaVolume(nextVolume);
    }
}

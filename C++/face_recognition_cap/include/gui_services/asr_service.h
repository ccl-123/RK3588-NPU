/**
 * @file asr_service.h
 * @brief 云端 ASR 语音识别服务
 * @details 负责麦克风录音采集、WAV 编码、Base64 编码和 MiMo ASR API 调用。
 *          录音部分参考 VisionCast AudioCapture，使用 dlopen 动态加载 ALSA 库，
 *          避免编译期硬链接 libasound 依赖。
 */

#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QPointer>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <cstdint>

class AsrService : public QObject {
    Q_OBJECT

public:
    static AsrService* instance();

    /**
     * @brief 开始录音（点击开始/点击停止模式）
     * @return true 成功启动录音, false 录音设备不可用
     */
    bool startRecording();

    /**
     * @brief 停止录音，并自动开始语音识别
     */
    void stopRecording();

    /**
     * @brief 取消当前操作（录音或识别请求）
     */
    void cancel();

    /**
     * @brief 是否正在录音
     */
    bool isRecording() const;

    /**
     * @brief 是否正在进行语音识别（网络请求中）
     */
    bool isTranscribing() const;

    /**
     * @brief 检查 ASR API Key 是否已配置
     */
    bool isApiKeyConfigured() const;

signals:
    /// 语音识别结果就绪（流式增量文本）
    void transcriptionReady(const QString& text);

    /// 语音识别完成（全部文本）
    void transcriptionFinished(const QString& fullText);

    /// 错误发生
    void asrError(const QString& error);

    /// 录音状态变化
    void recordingStateChanged(bool recording);

    /// 识别状态变化
    void transcribingStateChanged(bool transcribing);

    /// 录音时长更新（秒）
    void recordingDurationChanged(int seconds);

private:
    explicit AsrService(QObject* parent = nullptr);
    ~AsrService();

    // ALSA 录音线程
    void captureLoop();
    void finishInitialization(bool success, const std::string& error = {});

    // WAV 编码
    QByteArray encodePcmToWav(const std::vector<uint8_t>& pcm_data,
                               int sample_rate, int channels, int bits_per_sample) const;

    // 网络请求
    void sendAsrRequest(const QByteArray& wav_data);
    void handleAsrResponse(QNetworkReply* reply);

    // 录音数据
    std::vector<uint8_t> pcm_buffer_;
    mutable std::mutex pcm_mutex_;

    // 录音线程控制
    std::thread capture_thread_;
    std::atomic<bool> recording_{false};
    std::atomic<bool> capture_running_{false};

    // 线程同步（ALSA 初始化）
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    bool initialization_done_ = false;
    bool initialization_success_ = false;
    std::string init_error_;

    // 网络
    QNetworkAccessManager* network_manager_ = nullptr;
    QPointer<QNetworkReply> current_reply_;
    QTimer* timeout_timer_ = nullptr;
    QTimer* duration_timer_ = nullptr;
    int recording_seconds_ = 0;

    // 识别状态
    std::atomic<bool> transcribing_{false};

    static constexpr int TIMEOUT_MS = 30000;  // 30秒请求超时
};


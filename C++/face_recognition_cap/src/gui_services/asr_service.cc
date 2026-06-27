/**
 * @file asr_service.cc
 * @brief ASR 语音识别服务核心及录音采集实现
 * @details 录音部分使用 dlopen 动态加载 ALSA 库，避免编译期硬链接依赖。
 */

#include "gui_services/asr_service.h"
#include "config/config.h"

#if ENABLE_LOCAL_SHERPA_ASR
#include "sherpa-onnx/c-api/cxx-api.h"
#endif

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QCoreApplication>
#include <QMetaObject>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <limits>

// ==================== ALSA 动态绑定（参考 VisionCast） ====================

namespace {

struct snd_pcm_t;
using snd_pcm_stream_t = int;
using snd_pcm_format_t = int;
using snd_pcm_access_t = int;
using snd_pcm_uframes_t = unsigned long;
using snd_pcm_sframes_t = long;

constexpr snd_pcm_stream_t kSndPcmStreamCapture = 1;
constexpr snd_pcm_format_t kSndPcmFormatS16Le = 2;
constexpr snd_pcm_access_t kSndPcmAccessRwInterleaved = 3;
constexpr int kEpipe = 32;

/**
 * @brief 动态加载 ALSA 运行库的包装类
 */
class AlsaRuntime {
public:
    using PcmOpen = int (*)(snd_pcm_t**, const char*, snd_pcm_stream_t, int);
    using PcmClose = int (*)(snd_pcm_t*);
    using PcmSetParams = int (*)(snd_pcm_t*, snd_pcm_format_t, snd_pcm_access_t,
                                  unsigned int, unsigned int, int, unsigned int);
    using PcmReadi = snd_pcm_sframes_t (*)(snd_pcm_t*, void*, snd_pcm_uframes_t);
    using PcmRecover = int (*)(snd_pcm_t*, int, int);
    using PcmPrepare = int (*)(snd_pcm_t*);
    using PcmDrop = int (*)(snd_pcm_t*);
    using StrError = const char* (*)(int);

    ~AlsaRuntime() {
        if (handle_) {
            dlclose(handle_);
        }
    }

    bool load(std::string& error) {
        if (handle_) {
            return true;
        }
        handle_ = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
        if (!handle_) {
            error = std::string("dlopen libasound.so.2: ") + dlerror();
            return false;
        }

        if (!load_sym(open, "snd_pcm_open", error) ||
            !load_sym(close, "snd_pcm_close", error) ||
            !load_sym(set_params, "snd_pcm_set_params", error) ||
            !load_sym(readi, "snd_pcm_readi", error) ||
            !load_sym(recover, "snd_pcm_recover", error) ||
            !load_sym(prepare, "snd_pcm_prepare", error) ||
            !load_sym(drop, "snd_pcm_drop", error) ||
            !load_sym(strerror, "snd_strerror", error)) {
            dlclose(handle_);
            handle_ = nullptr;
            return false;
        }
        return true;
    }

    PcmOpen open = nullptr;
    PcmClose close = nullptr;
    PcmSetParams set_params = nullptr;
    PcmReadi readi = nullptr;
    PcmRecover recover = nullptr;
    PcmPrepare prepare = nullptr;
    PcmDrop drop = nullptr;
    StrError strerror = nullptr;

private:
    template <typename Fn>
    bool load_sym(Fn& fn, const char* name, std::string& error) {
        dlerror();
        void* sym = dlsym(handle_, name);
        const char* dl_err = dlerror();
        if (dl_err || !sym) {
            error = std::string("dlsym ") + name + ": " + (dl_err ? dl_err : "missing");
            return false;
        }
        fn = reinterpret_cast<Fn>(sym);
        return true;
    }

    void* handle_ = nullptr;
};

/**
 * @brief 自动探测可用的 ALSA 录音设备
 */
std::string detect_capture_device() {
    AlsaRuntime alsa;
    std::string error;
    if (!alsa.load(error)) {
        spdlog::warn("ASR: cannot load ALSA: {}", error);
        return "default";
    }

    const std::vector<std::string> candidates = {
        "plughw:0,0", "plughw:1,0", "plughw:2,0", "default"
    };

    for (const auto& dev : candidates) {
        snd_pcm_t* handle = nullptr;
        int ret = alsa.open(&handle, dev.c_str(), kSndPcmStreamCapture, 0);
        if (ret >= 0) {
            alsa.close(handle);
            spdlog::info("ASR: detected capture device: {}", dev);
            return dev;
        }
    }

    spdlog::warn("ASR: no capture device found, using 'default'");
    return "default";
}

}  // namespace

// ==================== AsrService 实现 ====================

AsrService* AsrService::instance() {
    static AsrService s_instance;
    return &s_instance;
}

AsrService::AsrService(QObject* parent)
    : QObject(parent) {
    network_manager_ = new QNetworkAccessManager(this);

    timeout_timer_ = new QTimer(this);
    timeout_timer_->setSingleShot(true);
    connect(timeout_timer_, &QTimer::timeout, this, [this]() {
        if (current_reply_) {
            spdlog::warn("ASR: request timeout");
            current_reply_->abort();
            current_reply_->deleteLater();
            current_reply_.clear();
            transcribing_ = false;
            emit transcribingStateChanged(false);
            emit asrError(QStringLiteral("语音识别请求超时，请重试"));
        }
    });

    // 录音计时器
    duration_timer_ = new QTimer(this);
    duration_timer_->setInterval(1000);
    connect(duration_timer_, &QTimer::timeout, this, [this]() {
        recording_seconds_++;
        emit recordingDurationChanged(recording_seconds_);

        // 最大录音时长保护
        if (recording_seconds_ >= Config::MiMoASR::MAX_RECORD_SECONDS) {
            spdlog::info("ASR: max recording duration reached ({}s)", recording_seconds_);
            stopRecording();
        }
    });

    // 日志
    qRegisterMetaType<AsrBackendMode>("AsrBackendMode");

    const char* api_key = Config::MiMoASR::getApiKey();
    if (api_key[0] == '\0') {
        spdlog::warn("MIMO_ASR_API_KEY is not set; cloud ASR is disabled");
    } else {
        spdlog::info("MiMo ASR API Key loaded (length: {})", std::strlen(api_key));
    }

    // 默认开启本地离线 ASR，在系统初始化时自动触发出后台异步加载
#if ENABLE_LOCAL_SHERPA_ASR
    loadLocalRecognizer();
#endif
}

AsrService::~AsrService() {
    cancel();
    releaseLocalRecognizer();
    if (local_load_thread_.joinable()) {
        local_load_thread_.join();
    }
}

bool AsrService::isApiKeyConfigured() const {
    if (backend_mode_.load() == AsrBackendMode::Local) {
        return local_ready_.load();
    }
    return Config::MiMoASR::getApiKey()[0] != '\0';
}

bool AsrService::isRecording() const {
    return recording_.load();
}

bool AsrService::isTranscribing() const {
    return transcribing_.load();
}

AsrBackendMode AsrService::backendMode() const {
    return backend_mode_.load();
}

bool AsrService::isLocalReady() const {
    return local_ready_.load();
}

bool AsrService::isLocalLoading() const {
    return local_loading_.load();
}

void AsrService::setBackendMode(AsrBackendMode mode) {
    if (recording_.load() || transcribing_.load()) {
        emit asrError(QStringLiteral("请先停止当前语音识别，再切换 ASR 模式"));
        return;
    }

    const AsrBackendMode previous = backend_mode_.load();
    if (previous == mode) {
        if (mode == AsrBackendMode::Local && !local_ready_.load() && !local_loading_.load()) {
            loadLocalRecognizer();
        }
        return;
    }

    if (mode == AsrBackendMode::Local) {
        backend_mode_ = AsrBackendMode::Local;
        emit backendModeChanged(mode);
        if (!loadLocalRecognizer()) {
            backend_mode_ = AsrBackendMode::Cloud;
            emit backendModeChanged(AsrBackendMode::Cloud);
        }
        return;
    }

    backend_mode_ = AsrBackendMode::Cloud;
    emit backendModeChanged(mode);
    cancelLocalSession();
}

void AsrService::releaseForPageLeave() {
    cancel();
    releaseLocalRecognizer();
}

bool AsrService::startRecording() {
    if (recording_.load()) {
        spdlog::warn("ASR: already recording");
        return false;
    }

    if (backend_mode_.load() == AsrBackendMode::Cloud && !isApiKeyConfigured()) {
        emit asrError(QStringLiteral("未配置 MIMO_ASR_API_KEY，请设置环境变量后重启"));
        return false;
    }

    if (backend_mode_.load() == AsrBackendMode::Local) {
        if (!local_ready_.load() && !loadLocalRecognizer()) {
            return false;
        }
        if (!startLocalSession()) {
            return false;
        }
    }

    // 清空 PCM 缓冲区
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        pcm_buffer_.clear();
    }

    // 重置初始化状态
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        initialization_done_ = false;
        initialization_success_ = false;
        init_error_.clear();
    }

    recording_ = true;
    capture_running_ = true;
    recording_seconds_ = 0;

    // 启动录音线程
    capture_thread_ = std::thread(&AsrService::captureLoop, this);

    // 等待 ALSA 初始化完成（最长 5 秒）
    std::unique_lock<std::mutex> lock(state_mutex_);
    bool signaled = state_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
        return initialization_done_;
    });
    bool success = signaled && initialization_success_;
    if (!signaled) {
        init_error_ = "录音设备初始化超时";
    }
    lock.unlock();

    if (!success) {
        recording_ = false;
        capture_running_ = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        if (backend_mode_.load() == AsrBackendMode::Local) {
            cancelLocalSession();
        }
        QString error_msg = QString::fromStdString(init_error_);
        if (error_msg.isEmpty()) {
            error_msg = QStringLiteral("录音设备初始化失败");
        }
        emit asrError(error_msg);
        return false;
    }

    // 启动计时器
    duration_timer_->start();
    emit recordingStateChanged(true);
    spdlog::info("ASR: recording started");
    return true;
}

void AsrService::stopRecording() {
    if (!recording_.load()) {
        return;
    }

    spdlog::info("ASR: stopping recording ({}s)", recording_seconds_);

    // 停止录音
    recording_ = false;
    capture_running_ = false;
    duration_timer_->stop();

    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }

    emit recordingStateChanged(false);

    if (backend_mode_.load() == AsrBackendMode::Local) {
        stopLocalSession();
        return;
    }

    // 获取 PCM 数据（云端 ASR 停止后上传）
    std::vector<uint8_t> pcm_data;
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        pcm_data = std::move(pcm_buffer_);
        pcm_buffer_.clear();
    }

    if (pcm_data.empty()) {
        emit asrError(QStringLiteral("未录到有效音频，请检查麦克风"));
        return;
    }

    spdlog::info("ASR: captured {} bytes PCM data", pcm_data.size());

    // 编码为 WAV
    QByteArray wav_data = encodePcmToWav(pcm_data,
        Config::MiMoASR::SAMPLE_RATE,
        Config::MiMoASR::CHANNELS,
        Config::MiMoASR::SAMPLE_SIZE);

    // 发送 ASR 请求
    sendAsrRequest(wav_data);
}

void AsrService::cancel() {
    // 取消录音
    if (recording_.load()) {
        recording_ = false;
        capture_running_ = false;
        duration_timer_->stop();
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
        emit recordingStateChanged(false);
    }

    // 取消网络请求
    if (current_reply_) {
        timeout_timer_->stop();
        current_reply_->abort();
        current_reply_->deleteLater();
        current_reply_.clear();
        transcribing_ = false;
        emit transcribingStateChanged(false);
    }

    cancelLocalSession();

    // 清空 PCM
    {
        std::lock_guard<std::mutex> lock(pcm_mutex_);
        pcm_buffer_.clear();
    }
}

// ==================== ALSA 录音线程 ====================

void AsrService::finishInitialization(bool success, const std::string& error) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (initialization_done_) {
        return;
    }
    initialization_done_ = true;
    initialization_success_ = success;
    if (!error.empty()) {
        init_error_ = error;
    }
    state_cv_.notify_all();
}

void AsrService::captureLoop() {
    AlsaRuntime alsa;
    std::string error;

    // 1. 加载 ALSA
    if (!alsa.load(error)) {
        finishInitialization(false, "无法加载 ALSA 库: " + error);
        spdlog::error("ASR: {}", error);
        capture_running_ = false;
        return;
    }

    // 2. 探测并打开录音设备
    std::string device = detect_capture_device();
    snd_pcm_t* handle = nullptr;
    int ret = alsa.open(&handle, device.c_str(), kSndPcmStreamCapture, 0);
    if (ret < 0) {
        error = std::string("无法打开录音设备 ") + device + ": " + alsa.strerror(ret);
        finishInitialization(false, error);
        spdlog::error("ASR: {}", error);
        capture_running_ = false;
        return;
    }

    // 3. 配置采集参数
    const unsigned int sample_rate = static_cast<unsigned int>(Config::MiMoASR::SAMPLE_RATE);
    unsigned int capture_channels = static_cast<unsigned int>(Config::MiMoASR::CHANNELS);
    const unsigned int output_channels = capture_channels;

    // 周期帧数：20ms 一个周期
    constexpr int frame_ms = 20;
    snd_pcm_uframes_t period_frames = (sample_rate * frame_ms) / 1000U;

    ret = alsa.set_params(handle, kSndPcmFormatS16Le, kSndPcmAccessRwInterleaved,
                          capture_channels, sample_rate, 1,
                          static_cast<unsigned int>(frame_ms * 1000));

    // 如果 mono 失败，尝试 stereo 并软件降混
    if (ret < 0 && output_channels == 1) {
        capture_channels = 2;
        ret = alsa.set_params(handle, kSndPcmFormatS16Le, kSndPcmAccessRwInterleaved,
                              capture_channels, sample_rate, 1,
                              static_cast<unsigned int>(frame_ms * 1000));
        if (ret >= 0) {
            spdlog::warn("ASR: device rejected mono, capturing stereo for downmix");
        }
    }

    if (ret < 0) {
        error = std::string("设置音频参数失败: ") + alsa.strerror(ret);
        finishInitialization(false, error);
        spdlog::error("ASR: {}", error);
        alsa.close(handle);
        capture_running_ = false;
        return;
    }

    const size_t bytes_per_frame = capture_channels * sizeof(int16_t);
    std::vector<uint8_t> buffer(period_frames * bytes_per_frame);

    spdlog::info("ASR: capture started on {} (rate={}, ch={}/{})",
                 device, sample_rate, capture_channels, output_channels);
    finishInitialization(true);

    // 4. 读取循环
    while (capture_running_.load()) {
        snd_pcm_sframes_t frames = alsa.readi(handle, buffer.data(), period_frames);

        if (frames == -kEpipe) {
            spdlog::warn("ASR: ALSA overrun, preparing");
            alsa.prepare(handle);
            continue;
        }
        if (frames < 0) {
            ret = alsa.recover(handle, static_cast<int>(frames), 1);
            if (ret < 0) {
                spdlog::error("ASR: read error: {}", alsa.strerror(static_cast<int>(frames)));
                break;
            }
            continue;
        }
        if (frames == 0) {
            continue;
        }

        // 软件降混 stereo → mono（如果需要）
        size_t data_bytes = static_cast<size_t>(frames) * bytes_per_frame;
        if (capture_channels != output_channels && capture_channels == 2 && output_channels == 1) {
            // 双声道降混为单声道
            size_t out_bytes = static_cast<size_t>(frames) * sizeof(int16_t);
            std::vector<uint8_t> mono_data(out_bytes);
            const int16_t* stereo = reinterpret_cast<const int16_t*>(buffer.data());
            int16_t* mono = reinterpret_cast<int16_t*>(mono_data.data());
            for (snd_pcm_sframes_t i = 0; i < frames; ++i) {
                int32_t sum = static_cast<int32_t>(stereo[i * 2]) +
                              static_cast<int32_t>(stereo[i * 2 + 1]);
                mono[i] = static_cast<int16_t>(sum / 2);
            }

            handlePcmChunk(mono_data);
        } else {
            std::vector<uint8_t> chunk(buffer.data(), buffer.data() + data_bytes);
            handlePcmChunk(chunk);
        }
    }

    // 5. 清理
    alsa.drop(handle);
    alsa.close(handle);
    capture_running_ = false;
    spdlog::info("ASR: capture loop ended");
}

void AsrService::handlePcmChunk(const std::vector<uint8_t>& pcm_chunk) {
    if (backend_mode_.load() == AsrBackendMode::Local) {
        enqueueLocalPcm(pcm_chunk);
        return;
    }

    std::lock_guard<std::mutex> lock(pcm_mutex_);
    pcm_buffer_.insert(pcm_buffer_.end(), pcm_chunk.begin(), pcm_chunk.end());
}

// ==================== WAV 编码 ====================

QByteArray AsrService::encodePcmToWav(const std::vector<uint8_t>& pcm_data,
                                       int sample_rate, int channels,
                                       int bits_per_sample) const {
    const uint32_t data_size = static_cast<uint32_t>(pcm_data.size());
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * bits_per_sample / 8);
    const uint16_t block_align = static_cast<uint16_t>(channels * bits_per_sample / 8);
    const uint32_t chunk_size = 36 + data_size;

    QByteArray wav;
    wav.reserve(44 + static_cast<int>(data_size));

    // RIFF header
    wav.append("RIFF", 4);
    wav.append(reinterpret_cast<const char*>(&chunk_size), 4);
    wav.append("WAVE", 4);

    // fmt sub-chunk
    wav.append("fmt ", 4);
    const uint32_t fmt_size = 16;
    wav.append(reinterpret_cast<const char*>(&fmt_size), 4);
    const uint16_t audio_format = 1;  // PCM
    wav.append(reinterpret_cast<const char*>(&audio_format), 2);
    const uint16_t num_channels = static_cast<uint16_t>(channels);
    wav.append(reinterpret_cast<const char*>(&num_channels), 2);
    const uint32_t sr = static_cast<uint32_t>(sample_rate);
    wav.append(reinterpret_cast<const char*>(&sr), 4);
    wav.append(reinterpret_cast<const char*>(&byte_rate), 4);
    wav.append(reinterpret_cast<const char*>(&block_align), 2);
    const uint16_t bps = static_cast<uint16_t>(bits_per_sample);
    wav.append(reinterpret_cast<const char*>(&bps), 2);

    // data sub-chunk
    wav.append("data", 4);
    wav.append(reinterpret_cast<const char*>(&data_size), 4);
    wav.append(reinterpret_cast<const char*>(pcm_data.data()),
               static_cast<int>(pcm_data.size()));

    return wav;
}

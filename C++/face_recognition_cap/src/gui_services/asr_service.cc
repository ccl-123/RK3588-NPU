/**
 * @file asr_service.cc
 * @brief 云端 ASR 语音识别服务实现
 * @details 录音部分使用 dlopen 动态加载 ALSA 库（参考 VisionCast AudioCapture），
 *          录音数据编码为 WAV 并 Base64 上传至 MiMo ASR 云端接口。
 */

#include "gui_services/asr_service.h"
#include "config/config.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QCoreApplication>
#include <QThread>
#include <QMetaObject>
#include <spdlog/spdlog.h>

#if ENABLE_LOCAL_SHERPA_ASR
#include "sherpa-onnx/c-api/cxx-api.h"
#endif

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
 * @details 参考 VisionCast AudioCapture 的 AlsaRuntime 实现
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
 * @return 设备名称字符串（如 "plughw:0,0"）
 */
std::string detect_capture_device() {
    // 尝试常见的录音设备名称
    // 在 RK3588 上，es8388 声卡通常是 hw:0,0 或 hw:1,0
    AlsaRuntime alsa;
    std::string error;
    if (!alsa.load(error)) {
        spdlog::warn("ASR: cannot load ALSA: {}", error);
        return "default";
    }

    // 按优先级尝试不同设备
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

[[maybe_unused]] QString local_model_file(const char* file_name) {
    return QDir(QString::fromUtf8(Config::LocalASR::MODEL_DIR)).filePath(QString::fromUtf8(file_name));
}

#if ENABLE_LOCAL_SHERPA_ASR
bool local_model_files_exist(QString* missing_file) {
    const QStringList files = {
        local_model_file(Config::LocalASR::ENCODER),
        local_model_file(Config::LocalASR::DECODER),
        local_model_file(Config::LocalASR::JOINER),
        local_model_file(Config::LocalASR::TOKENS),
    };
    for (const QString& file : files) {
        if (!QFileInfo::exists(file)) {
            if (missing_file) {
                *missing_file = file;
            }
            return false;
        }
    }
    return true;
}
#endif

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
}

AsrService::~AsrService() {
    cancel();
    releaseLocalRecognizer();
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
    releaseLocalRecognizer();
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

// ==================== 网络请求 ====================

void AsrService::sendAsrRequest(const QByteArray& wav_data) {
    transcribing_ = true;
    emit transcribingStateChanged(true);

    // Base64 编码
    QString base64_audio = QString::fromLatin1(wav_data.toBase64());
    QString data_url = QStringLiteral("data:audio/wav;base64,") + base64_audio;

    spdlog::info("ASR: sending request, WAV size={}, Base64 size={}",
                 wav_data.size(), base64_audio.size());

    // 检查大小限制（10MB）
    if (base64_audio.size() > 10 * 1024 * 1024) {
        transcribing_ = false;
        emit transcribingStateChanged(false);
        emit asrError(QStringLiteral("录音过长，音频数据超过 10MB 限制"));
        return;
    }

    // 构建请求体
    QJsonObject input_audio;
    input_audio["data"] = data_url;

    QJsonObject content_item;
    content_item["type"] = QStringLiteral("input_audio");
    content_item["input_audio"] = input_audio;

    QJsonArray content_array;
    content_array.append(content_item);

    QJsonObject message;
    message["role"] = QStringLiteral("user");
    message["content"] = content_array;

    QJsonArray messages;
    messages.append(message);

    QJsonObject asr_options;
    asr_options["language"] = QString::fromUtf8(Config::MiMoASR::getLanguage());

    QJsonObject body;
    body["model"] = QString::fromUtf8(Config::MiMoASR::MODEL);
    body["messages"] = messages;
    body["asr_options"] = asr_options;

    // 发送请求
    QNetworkRequest request(QUrl(QString::fromUtf8(Config::MiMoASR::getBaseUrl())));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QByteArray apiKey = QByteArray(Config::MiMoASR::getApiKey());
    request.setRawHeader("api-key", apiKey);
    request.setRawHeader("Authorization", "Bearer " + apiKey);

    current_reply_ = network_manager_->post(request, QJsonDocument(body).toJson());
    QNetworkReply* reply = current_reply_;

    timeout_timer_->start(Config::MiMoASR::REQUEST_TIMEOUT_MS);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleAsrResponse(reply);
    });
}

void AsrService::handleAsrResponse(QNetworkReply* reply) {
    timeout_timer_->stop();

    if (!current_reply_ || current_reply_ != reply) {
        reply->deleteLater();
        return;
    }

    transcribing_ = false;
    emit transcribingStateChanged(false);

    if (reply->error() != QNetworkReply::NoError) {
        int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // 尝试解析错误响应体
        QByteArray response_data = reply->readAll();
        QString error_msg;

        if (!response_data.isEmpty()) {
            QJsonDocument doc = QJsonDocument::fromJson(response_data);
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                if (root.contains("error")) {
                    QJsonObject err = root["error"].toObject();
                    error_msg = err["message"].toString();
                }
            }
        }

        if (error_msg.isEmpty()) {
            error_msg = reply->errorString();
        }

        spdlog::error("ASR: request failed (HTTP {}): {}", http_status, error_msg.toStdString());
        emit asrError(QStringLiteral("语音识别失败: ") + error_msg);
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    // 解析成功响应
    QByteArray response_data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(response_data);

    if (!doc.isObject()) {
        spdlog::error("ASR: invalid JSON response");
        emit asrError(QStringLiteral("语音识别响应格式错误"));
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray choices = root["choices"].toArray();
    if (choices.isEmpty()) {
        spdlog::warn("ASR: empty choices in response");
        emit asrError(QStringLiteral("语音识别未返回结果"));
        reply->deleteLater();
        current_reply_.clear();
        return;
    }

    QJsonObject first_choice = choices[0].toObject();
    QJsonObject msg = first_choice["message"].toObject();
    QString text = msg["content"].toString().trimmed();

    spdlog::info("ASR: transcription result: '{}'", text.left(100).toStdString());

    if (text.isEmpty()) {
        emit asrError(QStringLiteral("未识别到语音内容，请重试"));
    } else {
        // 流式输出：逐字符发送以模拟流式填充效果
        emit transcriptionReady(text);
        emit transcriptionFinished(text);
    }

    reply->deleteLater();
    current_reply_.clear();
}

// ==================== 本地 Sherpa ASR ====================

bool AsrService::loadLocalRecognizer() {
#if !ENABLE_LOCAL_SHERPA_ASR
    emit asrError(QStringLiteral("当前构建未启用本地 ASR"));
    return false;
#else
    if (local_ready_.load()) {
        return true;
    }
    if (local_loading_.exchange(true)) {
        return true;
    }

    emit localLoadingChanged(true);
    QString missing;
    if (!local_model_files_exist(&missing)) {
        local_loading_ = false;
        emit localLoadingChanged(false);
        emit asrError(QStringLiteral("本地 ASR 模型文件缺失: ") + missing);
        return false;
    }

    std::thread([this]() {
        try {
            spdlog::info("Local ASR: Start loading model in background thread...");
            sherpa_onnx::cxx::OnlineRecognizerConfig config;
            config.feat_config.sample_rate = Config::LocalASR::SAMPLE_RATE;
            config.feat_config.feature_dim = Config::LocalASR::FEATURE_DIM;
            config.model_config.transducer.encoder = local_model_file(Config::LocalASR::ENCODER).toStdString();
            config.model_config.transducer.decoder = local_model_file(Config::LocalASR::DECODER).toStdString();
            config.model_config.transducer.joiner = local_model_file(Config::LocalASR::JOINER).toStdString();
            config.model_config.tokens = local_model_file(Config::LocalASR::TOKENS).toStdString();
            config.model_config.provider = "cpu";
            config.model_config.num_threads = Config::LocalASR::NUM_THREADS;
            config.model_config.modeling_unit = "cjkchar";
            config.decoding_method = "greedy_search";

            auto recognizer = sherpa_onnx::cxx::OnlineRecognizer::Create(config);
            if (!recognizer.Get()) {
                local_loading_ = false;
                QMetaObject::invokeMethod(this, [this]() {
                    emit localLoadingChanged(false);
                    if (backend_mode_.load() == AsrBackendMode::Local) {
                        emit asrError(QStringLiteral("本地 ASR 模型初始化失败"));
                        backend_mode_ = AsrBackendMode::Cloud;
                        emit backendModeChanged(AsrBackendMode::Cloud);
                    }
                }, Qt::QueuedConnection);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(local_mutex_);
                if (backend_mode_.load() == AsrBackendMode::Local) {
                    local_recognizer_ = std::make_unique<sherpa_onnx::cxx::OnlineRecognizer>(std::move(recognizer));
                    local_ready_ = true;
                    local_loading_ = false;
                    QMetaObject::invokeMethod(this, [this]() {
                        emit localLoadingChanged(false);
                        if (local_ready_.load()) {
                            emit localReadyChanged(true);
                        }
                    }, Qt::QueuedConnection);
                    spdlog::info("Local ASR loaded successfully from {}", Config::LocalASR::MODEL_DIR);
                    return;
                }
            }

            // User switched away from Local mode while loading
            spdlog::info("Local ASR: Loaded, but backend mode is no longer Local. Releasing loaded model.");
            local_loading_ = false;
            QMetaObject::invokeMethod(this, [this]() {
                emit localLoadingChanged(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QString errMsg = QString::fromUtf8(e.what());
            local_loading_ = false;
            local_ready_ = false;
            QMetaObject::invokeMethod(this, [this, errMsg]() {
                emit localLoadingChanged(false);
                emit localReadyChanged(false);
                if (backend_mode_.load() == AsrBackendMode::Local) {
                    emit asrError(QStringLiteral("本地 ASR 初始化异常: ") + errMsg);
                    backend_mode_ = AsrBackendMode::Cloud;
                    emit backendModeChanged(AsrBackendMode::Cloud);
                }
            }, Qt::QueuedConnection);
        }
    }).detach();

    return true;
#endif
}

void AsrService::releaseLocalRecognizer() {
    cancelLocalSession();

    bool was_loading = local_loading_.exchange(false);
    if (was_loading) {
        emit localLoadingChanged(false);
    }

    bool was_ready = false;
#if ENABLE_LOCAL_SHERPA_ASR
    {
        std::lock_guard<std::mutex> lock(local_mutex_);
        was_ready = local_ready_.exchange(false);
        local_stream_.reset();
        local_recognizer_.reset();
    }
#else
    was_ready = local_ready_.exchange(false);
#endif
    local_last_text_.clear();
    if (was_ready) {
        emit localReadyChanged(false);
        spdlog::info("Local ASR released");
    }
}

bool AsrService::startLocalSession() {
#if !ENABLE_LOCAL_SHERPA_ASR
    emit asrError(QStringLiteral("当前构建未启用本地 ASR"));
    return false;
#else
    std::lock_guard<std::mutex> lock(local_mutex_);
    if (!local_recognizer_) {
        emit asrError(QStringLiteral("本地 ASR 尚未就绪"));
        return false;
    }

    local_stream_ = std::make_unique<sherpa_onnx::cxx::OnlineStream>(
        local_recognizer_->CreateStream());
    if (!local_stream_ || !local_stream_->Get()) {
        local_stream_.reset();
        emit asrError(QStringLiteral("本地 ASR 创建流失败"));
        return false;
    }

    {
        std::lock_guard<std::mutex> qlock(local_queue_mutex_);
        std::queue<std::vector<int16_t>> empty;
        local_pcm_queue_.swap(empty);
        local_decode_running_ = true;
        local_input_finished_ = false;
    }
    local_last_text_.clear();
    local_session_active_ = true;
    local_decode_thread_ = std::thread(&AsrService::localDecodeLoop, this);
    return true;
#endif
}

void AsrService::stopLocalSession() {
    if (!local_session_active_.load()) {
        return;
    }
    transcribing_ = true;
    emit transcribingStateChanged(true);
    {
        std::lock_guard<std::mutex> lock(local_queue_mutex_);
        local_input_finished_ = true;
    }
    local_queue_cv_.notify_all();
    if (local_decode_thread_.joinable()) {
        local_decode_thread_.join();
    }
    local_session_active_ = false;
    transcribing_ = false;
    emit transcribingStateChanged(false);
    emit transcriptionFinished(local_last_text_);

#if ENABLE_LOCAL_SHERPA_ASR
    std::lock_guard<std::mutex> lock(local_mutex_);
    local_stream_.reset();
#endif
}

void AsrService::cancelLocalSession() {
    if (!local_session_active_.load() && !local_decode_running_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(local_queue_mutex_);
        local_decode_running_ = false;
        local_input_finished_ = true;
        std::queue<std::vector<int16_t>> empty;
        local_pcm_queue_.swap(empty);
    }
    local_queue_cv_.notify_all();
    if (local_decode_thread_.joinable()) {
        local_decode_thread_.join();
    }
    local_session_active_ = false;
    transcribing_ = false;
    emit transcribingStateChanged(false);
#if ENABLE_LOCAL_SHERPA_ASR
    std::lock_guard<std::mutex> lock(local_mutex_);
    local_stream_.reset();
#endif
}

void AsrService::enqueueLocalPcm(const std::vector<uint8_t>& pcm_chunk) {
    if (pcm_chunk.empty()) {
        return;
    }
    const size_t sample_count = pcm_chunk.size() / sizeof(int16_t);
    std::vector<int16_t> samples(sample_count);
    std::memcpy(samples.data(), pcm_chunk.data(), sample_count * sizeof(int16_t));
    {
        std::lock_guard<std::mutex> lock(local_queue_mutex_);
        if (!local_decode_running_) {
            return;
        }
        local_pcm_queue_.push(std::move(samples));
    }
    local_queue_cv_.notify_one();
}

void AsrService::localDecodeLoop() {
#if !ENABLE_LOCAL_SHERPA_ASR
    return;
#else
    while (true) {
        std::vector<int16_t> pcm;
        bool final_input = false;
        {
            std::unique_lock<std::mutex> lock(local_queue_mutex_);
            local_queue_cv_.wait(lock, [this] {
                return !local_pcm_queue_.empty() || local_input_finished_ || !local_decode_running_;
            });
            if (!local_decode_running_) {
                break;
            }
            if (!local_pcm_queue_.empty()) {
                pcm = std::move(local_pcm_queue_.front());
                local_pcm_queue_.pop();
            } else if (local_input_finished_) {
                final_input = true;
                local_decode_running_ = false;
            }
        }

        try {
            std::lock_guard<std::mutex> lock(local_mutex_);
            if (!local_recognizer_ || !local_stream_) {
                break;
            }

            if (!pcm.empty()) {
                std::vector<float> samples;
                samples.reserve(pcm.size());
                constexpr float kScale = 1.0f / 32768.0f;
                for (int16_t sample : pcm) {
                    samples.push_back(std::max(-1.0f, std::min(1.0f, sample * kScale)));
                }
                local_stream_->AcceptWaveform(Config::LocalASR::SAMPLE_RATE,
                                              samples.data(),
                                              static_cast<int32_t>(samples.size()));
            }

            if (final_input) {
                local_stream_->InputFinished();
            }

            while (local_recognizer_->IsReady(local_stream_.get())) {
                local_recognizer_->Decode(local_stream_.get());
            }

            auto result = local_recognizer_->GetResult(local_stream_.get());
            QString text = QString::fromStdString(result.text).trimmed();
            if (!text.isEmpty() && text != local_last_text_) {
                local_last_text_ = text;
                QMetaObject::invokeMethod(this, [this, text]() {
                    emit transcriptionReady(text);
                }, Qt::QueuedConnection);
            }

            if (final_input) {
                break;
            }
        } catch (const std::exception& e) {
            const QString error = QStringLiteral("本地 ASR 识别异常: ") + QString::fromUtf8(e.what());
            QMetaObject::invokeMethod(this, [this, error]() {
                emit asrError(error);
            }, Qt::QueuedConnection);
            break;
        }
    }
#endif
}

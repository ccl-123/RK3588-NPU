/**
 * @file asr_service_local.cc
 * @brief 本地 ASR 语音识别服务实现部分
 */

#include "gui_services/asr_service.h"
#include "config/config.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QCoreApplication>
#include <spdlog/spdlog.h>

#if ENABLE_LOCAL_SHERPA_ASR
#include "sherpa-onnx/c-api/cxx-api.h"
#endif

#include <algorithm>
#include <cstring>

namespace {

QString get_resolved_model_dir() {
    const QString model_rel_path = QString::fromUtf8(Config::LocalASR::MODEL_DIR);

    // Candidate 1: relative to current working directory
    QDir current_dir(QDir::currentPath());
    if (QFileInfo::exists(current_dir.filePath(model_rel_path))) {
        QString path = current_dir.absoluteFilePath(model_rel_path);
        spdlog::info("ASR: Resolved model directory relative to current working directory: {}", path.toStdString());
        return path;
    }

    // Candidate 2: relative to current working directory + install layout
    QString root_install_path = QStringLiteral("install/face_recognition_cap/") + model_rel_path;
    if (QFileInfo::exists(current_dir.filePath(root_install_path))) {
        QString path = current_dir.absoluteFilePath(root_install_path);
        spdlog::info("ASR: Resolved model directory via root install path: {}", path.toStdString());
        return path;
    }

    // Candidate 3: relative to application binary directory
    QDir app_dir(QCoreApplication::applicationDirPath());
    if (QFileInfo::exists(app_dir.filePath(model_rel_path))) {
        QString path = app_dir.absoluteFilePath(model_rel_path);
        spdlog::info("ASR: Resolved model directory relative to application binary: {}", path.toStdString());
        return path;
    }

    // Candidate 4: relative to application binary directory + build offsets
    QDir build_offset_dir(QCoreApplication::applicationDirPath() + QStringLiteral("/../../install/face_recognition_cap"));
    if (QFileInfo::exists(build_offset_dir.filePath(model_rel_path))) {
        QString path = build_offset_dir.absoluteFilePath(model_rel_path);
        spdlog::info("ASR: Resolved model directory via build offsets: {}", path.toStdString());
        return path;
    }

    // Fallback: just return absolute path relative to current path
    QString fallback = current_dir.absoluteFilePath(model_rel_path);
    spdlog::warn("ASR: Could not find model directory in candidates. Using fallback: {}", fallback.toStdString());
    return fallback;
}

[[maybe_unused]] QString local_model_file(const char* file_name) {
    static const QString resolved_model_dir = get_resolved_model_dir();
    return QDir(resolved_model_dir).filePath(QString::fromUtf8(file_name));
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

} // namespace

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

    // Abort any existing load task by incrementing load ID, and join the finished thread
    int current_id = ++local_load_id_;
    if (local_load_thread_.joinable()) {
        local_load_thread_.join();
    }

    local_load_thread_ = std::thread([this, current_id]() {
        try {
            spdlog::info("Local ASR: Start loading model in background thread (load_id: {})...", current_id);
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

            // Check if this thread has been cancelled/superseded before proceeding
            if (current_id != local_load_id_.load()) {
                spdlog::info("Local ASR: Model loaded but discarded because thread was superseded (load_id: {}).", current_id);
                return;
            }

            if (!recognizer.Get()) {
                local_loading_ = false;
                QMetaObject::invokeMethod(this, [this, current_id]() {
                    if (current_id != local_load_id_.load()) return;
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
                if (current_id == local_load_id_.load() && backend_mode_.load() == AsrBackendMode::Local) {
                    local_recognizer_ = std::make_unique<sherpa_onnx::cxx::OnlineRecognizer>(std::move(recognizer));
                    local_ready_ = true;
                    local_loading_ = false;
                    QMetaObject::invokeMethod(this, [this, current_id]() {
                        if (current_id != local_load_id_.load()) return;
                        emit localLoadingChanged(false);
                        if (local_ready_.load()) {
                            emit localReadyChanged(true);
                        }
                    }, Qt::QueuedConnection);
                    spdlog::info("Local ASR loaded successfully (load_id: {}).", current_id);
                    return;
                }
            }

            // User switched away from Local mode or loading was cancelled
            spdlog::info("Local ASR: Loaded, but backend mode is no longer Local or load was cancelled. Releasing loaded model (load_id: {}).", current_id);
            local_loading_ = false;
            QMetaObject::invokeMethod(this, [this, current_id]() {
                if (current_id != local_load_id_.load()) return;
                emit localLoadingChanged(false);
            }, Qt::QueuedConnection);
        } catch (const std::exception& e) {
            QString errMsg = QString::fromUtf8(e.what());
            local_loading_ = false;
            local_ready_ = false;
            QMetaObject::invokeMethod(this, [this, current_id, errMsg]() {
                if (current_id != local_load_id_.load()) return;
                emit localLoadingChanged(false);
                emit localReadyChanged(false);
                if (backend_mode_.load() == AsrBackendMode::Local) {
                    emit asrError(QStringLiteral("本地 ASR 初始化异常: ") + errMsg);
                    backend_mode_ = AsrBackendMode::Cloud;
                    emit backendModeChanged(AsrBackendMode::Cloud);
                }
            }, Qt::QueuedConnection);
        }
    });

    return true;
#endif
}

void AsrService::releaseLocalRecognizer() {
    cancelLocalSession();

    // Increment load ID to invalidate any active background loading thread
    ++local_load_id_;

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

// src/gui_services/tts_service_local.cc

#include "gui_services/tts_service_local.h"
#include "gui_utils/audio_manager.h"
#include "sherpa-onnx/c-api/c-api.h"

#include <iostream>
#include <chrono>
#include <QMetaObject>
#include <QString>
#include <spdlog/spdlog.h>

namespace gui_services {

TtsServiceLocal::TtsServiceLocal() = default;

TtsServiceLocal::~TtsServiceLocal() {
  if (running_) {
    running_ = false;
    stop_requested_ = true;
    cv_.notify_all();
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
  }
}

bool TtsServiceLocal::Initialize(const std::string &model_dir) {
  if (initialized_) return true;

  sherpa_onnx::cxx::OfflineTtsConfig config;
  config.model.vits.model = model_dir + "/zh_CN-huayan-medium.onnx";
  config.model.vits.tokens = model_dir + "/tokens.txt";
  config.model.vits.data_dir = model_dir + "/espeak-ng-data";
  config.model.num_threads = 2;
  config.max_num_sentences = 1;

  try {
    tts_ = std::make_unique<sherpa_onnx::cxx::OfflineTts>(sherpa_onnx::cxx::OfflineTts::Create(config));
    
    running_ = true;
    worker_thread_ = std::thread(&TtsServiceLocal::WorkerLoop, this);
    initialized_ = true;
    spdlog::info("[TtsServiceLocal] 独占 Worker 线程启动，离线 TTS 引擎初始化成功！采样率: {} Hz", tts_->SampleRate());
    return true;
  } catch (const std::exception &e) {
    spdlog::error("[TtsServiceLocal] 初始化失败: {}", e.what());
    return false;
  }
}

void TtsServiceLocal::WorkerLoop() {
  while (running_) {
    TtsTask task;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this]() {
        return !task_queue_.empty() || !running_;
      });

      if (!running_) break;

      task = task_queue_.front();
      task_queue_.pop();
      stop_requested_ = false;
    }

    if (task.text.empty() || !tts_) continue;

    try {
      sherpa_onnx::cxx::GenerationConfig gen_config;
      gen_config.sid = task.speaker_id;
      gen_config.speed = task.speed;

      // 生成合成音频
      auto audio = tts_->Generate(task.text, gen_config);
      if (stop_requested_ || !running_ || audio.samples.empty()) {
        continue;
      }

      // 写入 RAM 共享内存 tmpfs，极速无磨损
      std::string ram_wav_path = "/dev/shm/tts_prompt.wav";
      int32_t ok = SherpaOnnxWriteWave(audio.samples.data(), audio.samples.size(), audio.sample_rate, ram_wav_path.c_str());
      if (ok && !stop_requested_) {
        QMetaObject::invokeMethod(AudioManager::instance(), "playSound",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(ram_wav_path)));
      }
    } catch (const std::exception &e) {
      spdlog::error("[TtsServiceLocal] Worker 线程合成异常: {}", e.what());
    }
  }
}

void TtsServiceLocal::Stop() {
  stop_requested_ = true;
  std::lock_guard<std::mutex> lock(mutex_);
  std::queue<TtsTask> empty_q;
  std::swap(task_queue_, empty_q);
}

void TtsServiceLocal::SpeakAsync(const std::string &text, int32_t speaker_id, float speed) {
  if (!initialized_ || !tts_) {
    spdlog::warn("[TtsServiceLocal] 服务未初始化，无法合成");
    return;
  }

  // 打断当前工作，压入最新任务
  stop_requested_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    std::queue<TtsTask> empty_q;
    std::swap(task_queue_, empty_q);
    task_queue_.push({text, speaker_id, speed});
  }
  cv_.notify_one();
}

}  // namespace gui_services

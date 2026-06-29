// src/gui_services/tts_service_local.cc

#include "gui_services/tts_service_local.h"
#include "gui_utils/audio_manager.h"
#include "sherpa-onnx/c-api/c-api.h"

#include <iostream>
#include <chrono>
#include <cstdio>
#include <cstdint>
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

  model_dir_ = model_dir;
  running_ = true;
  initialized_ = true; // 开启服务状态，模型在 Worker 线程后台异步加载，0 阻塞主界面

  worker_thread_ = std::thread(&TtsServiceLocal::WorkerLoop, this);
  spdlog::info("[TtsServiceLocal] 异步 Worker 线程已启动，后台加载 TTS 模型: {}", model_dir_);
  return true;
}

void TtsServiceLocal::WorkerLoop() {
  // 在专职后台 Worker 线程中异步加载离线 TTS 模型，避免阻塞主线程及视频拉起
  if (!model_dir_.empty() && !tts_) {
    try {
      sherpa_onnx::cxx::OfflineTtsConfig config;
      config.model.vits.model = model_dir_ + "/zh_CN-huayan-medium.onnx";
      config.model.vits.tokens = model_dir_ + "/tokens.txt";
      config.model.vits.data_dir = model_dir_ + "/espeak-ng-data";
      config.model.num_threads = 2;
      config.max_num_sentences = 1;

      tts_ = std::make_unique<sherpa_onnx::cxx::OfflineTts>(sherpa_onnx::cxx::OfflineTts::Create(config));
      model_ready_ = true;
      spdlog::info("[TtsServiceLocal] 后台离线 TTS 引擎加载成功！采样率: {} Hz", tts_->SampleRate());
    } catch (const std::exception &e) {
      spdlog::error("[TtsServiceLocal] 后台加载 TTS 模型失败: {}", e.what());
    }
  }

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

    if (task.text.empty() || !tts_ || !model_ready_) continue;

    try {
      sherpa_onnx::cxx::GenerationConfig gen_config;
      gen_config.sid = task.speaker_id;
      gen_config.speed = task.speed;

      // 生成合成音频
      auto audio = tts_->Generate(task.text, gen_config);
      if (stop_requested_ || !running_ || audio.samples.empty()) {
        continue;
      }

      // 写入独立临时文件，避免 aplay 仍在读旧文件时被下一次 TTS 覆盖。
      std::string ram_wav_path = "/dev/shm/tts_prompt_" +
          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
          "_" + std::to_string(++wav_sequence_) + ".wav";
      int32_t ok = SherpaOnnxWriteWave(audio.samples.data(), audio.samples.size(), audio.sample_rate, ram_wav_path.c_str());
      if (ok && !stop_requested_) {
        QMetaObject::invokeMethod(AudioManager::instance(), "playSound",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromStdString(ram_wav_path)));
      } else if (ok) {
        std::remove(ram_wav_path.c_str());
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
  if (!initialized_) {
    spdlog::warn("[TtsServiceLocal] 服务未启动");
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

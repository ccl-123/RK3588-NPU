// include/gui_services/tts_service_local.h
// 打卡系统离线 TTS 语音合成服务 (绝对线程安全 + 后台异步加载模型 + 独占 Worker 工作线程机制)

#ifndef GUI_SERVICES_TTS_SERVICE_LOCAL_H_
#define GUI_SERVICES_TTS_SERVICE_LOCAL_H_

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>

#include "sherpa-onnx/c-api/cxx-api.h"

namespace gui_services {

struct TtsTask {
  std::string text;
  int32_t speaker_id = 0;
  float speed = 1.0f;
};

class TtsServiceLocal {
 public:
  TtsServiceLocal();
  ~TtsServiceLocal();

  // 初始化 TTS 模型
  bool Initialize(const std::string &model_dir);

  // 检查是否初始化完成及模型就绪
  bool IsInitialized() const { return initialized_ && model_ready_; }

  // 线程安全：请求异步合成并播报文本 (后来的任务自动抢占打断未播完的前任务)
  void SpeakAsync(const std::string &text, int32_t speaker_id = 0, float speed = 1.0f);

  // 线程安全：立即打断并清空播报任务
  void Stop();

 private:
  void WorkerLoop();

 private:
  bool initialized_ = false;
  std::atomic<bool> model_ready_{false};
  std::string model_dir_;
  std::unique_ptr<sherpa_onnx::cxx::OfflineTts> tts_;

  // 并发线程安全控制
  std::thread worker_thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<TtsTask> task_queue_;
  
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
};

}  // namespace gui_services

#endif  // GUI_SERVICES_TTS_SERVICE_LOCAL_H_

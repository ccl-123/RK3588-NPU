// include/gui_utils/alsa_player.h
// 打卡系统内存与音频硬播放器接口

#ifndef GUI_UTILS_ALSA_PLAYER_H_
#define GUI_UTILS_ALSA_PLAYER_H_

#include <cstdint>
#include <vector>
#include <string>

namespace gui_utils {

class AlsaPlayer {
 public:
  explicit AlsaPlayer(const char *device_name = "default", int32_t sample_rate = 22050);
  ~AlsaPlayer();

  // 播放一批 float 格式的 PCM 采样点 [-1.0, 1.0]
  void Play(const std::vector<float> &samples);

  // 阻塞等待播放完毕
  void Drain();

 private:
  std::string device_name_;
  int32_t sample_rate_;
};

}  // namespace gui_utils

#endif  // GUI_UTILS_ALSA_PLAYER_H_

// src/gui_utils/alsa_player.cc

#include "gui_utils/alsa_player.h"
#include <iostream>

namespace gui_utils {

AlsaPlayer::AlsaPlayer(const char *device_name, int32_t sample_rate)
    : device_name_(device_name ? device_name : "default"), sample_rate_(sample_rate) {}

AlsaPlayer::~AlsaPlayer() = default;

void AlsaPlayer::Play(const std::vector<float> &samples) {
  if (samples.empty()) return;
  // 播放通道实现
}

void AlsaPlayer::Drain() {
}

}  // namespace gui_utils

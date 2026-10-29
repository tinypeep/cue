#pragma once
#include <string>
#include <utility>
#include <vector>

struct MediaDetails {
  std::string name, container, video_codec, audio_codec, audio_layout;
  double bytes = -1, duration = 0, fps = 0, sample_rate = 0;
  int width = 0, height = 0, channels = 0;
  bool audio_only = false;
};
using InfoFields = std::vector<std::pair<std::string, std::string>>;
InfoFields media_info_fields(const MediaDetails& info);

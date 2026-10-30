#include "media_info.h"
#include "util.h"
#include "i18n.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
std::string number(double n, int places) { return i18n::decimal(n, places); }
std::string unavailable() { return tr(str::info_unavailable); }
std::string codec(std::string s) {
  if(s.empty()) return unavailable();
  if(s=="h264") return "H.264";
  if(s=="hevc") return "HEVC / H.265";
  for(char& c:s) if(c>='a' && c<='z') c-=32;
  return s;
}
std::string format(std::string s) {
  if(s.empty()) return unavailable();
  if(s.find("matroska")!=std::string::npos) return "Matroska / WebM";
  if(s.find("mov")!=std::string::npos && s.find("mp4")!=std::string::npos) return "MP4 / QuickTime";
  return s;
}
std::string file_size(double n) {
  if(!std::isfinite(n) || n<0) return unavailable();
  const str::Id units[]={str::unit_b,str::unit_kb,str::unit_mb,str::unit_gb,str::unit_tb};int k=0;
  while(n>=1000 && k<4) {n/=1000;++k;}
  return number(n,k?2:0)+" "+tr(units[k]);
}
}
InfoFields media_info_fields(const MediaDetails& m) {
  InfoFields fields={{tr(str::info_file),m.name.empty()?unavailable():m.name},{tr(str::info_size),file_size(m.bytes)},
    {tr(str::info_duration),std::isfinite(m.duration)&&m.duration>0&&m.duration<2147483647?fmt_time(m.duration):unavailable()},
    {tr(str::info_format),format(m.container)}};
  // Never report the generated audio waveform as the source video's resolution/codec.
  if(!m.audio_only) {
    fields.push_back({tr(str::info_resolution),m.width>0&&m.height>0?std::to_string(m.width)+" × "+std::to_string(m.height):unavailable()});
    fields.push_back({tr(str::info_fps),std::isfinite(m.fps)&&m.fps>0?number(m.fps,2):unavailable()});
    fields.push_back({tr(str::info_video),codec(m.video_codec)});
  }
  fields.push_back({tr(str::info_audio),m.audio_codec.empty()?std::string(tr(str::info_no_audio_track)):codec(m.audio_codec)});
  fields.push_back({tr(str::info_channels),!m.audio_layout.empty()?(m.audio_layout=="stereo"?std::string(tr(str::info_stereo)):m.audio_layout):m.channels>0?(m.channels==1?std::string(tr(str::info_mono)):m.channels==2?std::string(tr(str::info_stereo)):i18n::plural(str::info_channels_n,m.channels)):unavailable()});
  fields.push_back({tr(str::info_frequency),std::isfinite(m.sample_rate)&&m.sample_rate>0?number(m.sample_rate/1000,2)+" kHz":unavailable()});
  return fields;
}

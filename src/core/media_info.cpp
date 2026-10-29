#include "media_info.h"
#include "util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
std::string number(double n, int places) {
  char b[80]; std::snprintf(b, sizeof b, "%.*f", places, n);
  std::string s=b;
  if (s.find('.')!=std::string::npos) { while(s.back()=='0') s.pop_back(); if(s.back()=='.') s.pop_back(); }
  std::replace(s.begin(),s.end(),'.',','); return s;
}
std::string codec(std::string s) {
  if(s.empty()) return "indisponível";
  if(s=="h264") return "H.264";
  if(s=="hevc") return "HEVC / H.265";
  for(char& c:s) if(c>='a' && c<='z') c-=32;
  return s;
}
std::string format(std::string s) {
  if(s.empty()) return "indisponível";
  if(s.find("matroska")!=std::string::npos) return "Matroska / WebM";
  if(s.find("mov")!=std::string::npos && s.find("mp4")!=std::string::npos) return "MP4 / QuickTime";
  return s;
}
std::string file_size(double n) {
  if(!std::isfinite(n) || n<0) return "indisponível";
  const char* units[]={"B","KB","MB","GB","TB"};int k=0;
  while(n>=1000 && k<4) {n/=1000;++k;}
  return number(n,k?2:0)+" "+units[k];
}
}
InfoFields media_info_fields(const MediaDetails& m) {
  InfoFields fields={{"arquivo",m.name.empty()?"indisponível":m.name},{"tamanho",file_size(m.bytes)},
    {"duração",std::isfinite(m.duration)&&m.duration>0&&m.duration<2147483647?fmt_time(m.duration):"indisponível"},
    {"formato",format(m.container)}};
  // Never report the generated audio waveform as the source video's resolution/codec.
  if(!m.audio_only) {
    fields.push_back({"resolução",m.width>0&&m.height>0?std::to_string(m.width)+" × "+std::to_string(m.height):"indisponível"});
    fields.push_back({"FPS",std::isfinite(m.fps)&&m.fps>0?number(m.fps,2):"indisponível"});
    fields.push_back({"vídeo",codec(m.video_codec)});
  }
  fields.push_back({"áudio",m.audio_codec.empty()?"sem faixa selecionada":codec(m.audio_codec)});
  fields.push_back({"canais",!m.audio_layout.empty()?(m.audio_layout=="stereo"?"estéreo":m.audio_layout):m.channels>0?(m.channels==1?"mono":m.channels==2?"estéreo":std::to_string(m.channels)+" canais"):"indisponível"});
  fields.push_back({"frequência",std::isfinite(m.sample_rate)&&m.sample_rate>0?number(m.sample_rate/1000,2)+" kHz":"indisponível"});
  return fields;
}

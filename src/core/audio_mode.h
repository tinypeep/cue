#pragma once
#include "model.h"

// Decide from streams, never from the container extension (an MP4 may be audio-only).
// Album art is not a film and should not suppress the live waveform.
inline int64_t audio_visual_track(const std::vector<Track>& tracks, int64_t preferred = -1,
                                 const std::string& language = {}) {
  int64_t first = 0, def = 0, lang = 0, saved = 0;
  for (const auto& t : tracks) {
    if (t.type == "video" && !t.albumart) return 0;
    if (t.type != "audio") continue;
    if (!first) first = t.id;
    if (t.is_default && !def) def = t.id;
    if (!language.empty() && t.lang == language && !lang) lang = t.id;
    if (t.id == preferred && (language.empty() || t.lang == language)) saved = t.id;
  }
  return saved ? saved : lang ? lang : def ? def : first;
}

inline std::string audio_waveform_graph(int64_t track) {
  if (track <= 0) return {};
  // Split decoded samples, leaving the audible branch untouched. Only the tiny
  // visual branch is resampled; nearest-neighbour scaling preserves real pixels.
  return "[aid" + std::to_string(track) + "]asplit=2[ao][cue_wave];"
         "[cue_wave]aresample=48000,"
         "showwaves=s=160x48:r=30:mode=cline:scale=sqrt:draw=full:colors=0xdce2dc|0xdce2dc,"
         // showwaves needs two frames to prime its sample window after a flush.
         // Keep the previous image displayed until the first complete new frame.
         "trim=start_frame=2,"
         "format=rgb24,scale=640:192:flags=neighbor,drawgrid=w=4:h=4:t=1:c=black,"
         "pad=1024:576:192:192:color=black,setsar=1,format=yuv420p[vo]";
}

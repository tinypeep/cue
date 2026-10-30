#pragma once
#include "model.h"
#include <cmath>

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

// The waveform's visual branch (fed by [cue_wave]); the audible branch is never touched.
inline std::string audio_waveform_tail() {
  return "[cue_wave]aresample=48000,"
         "showwaves=s=160x48:r=30:mode=cline:scale=sqrt:draw=full:colors=0xdce2dc|0xdce2dc,"
         // showwaves needs two frames to prime its sample window after a flush.
         // Keep the previous image displayed until the first complete new frame.
         "trim=start_frame=2,"
         "format=rgb24,scale=640:192:flags=neighbor,drawgrid=w=4:h=4:t=1:c=black,"
         "pad=1024:576:192:192:color=black,setsar=1,format=yuv420p[vo]";
}

inline std::string audio_waveform_graph(int64_t track) {
  if (track <= 0) return {};
  // Split decoded samples, leaving the audible branch untouched. Only the tiny
  // visual branch is resampled; nearest-neighbour scaling preserves real pixels.
  return "[aid" + std::to_string(track) + "]asplit=2[ao][cue_wave];" + audio_waveform_tail();
}

// Every audio track at once, as recorded (game, microphone, voice chat...): amix scales each
// input by 1/n, the volume puts them back at their own level (works with any ffmpeg).
inline std::string audio_mix_graph(const std::vector<int64_t>& ids, bool waveform) {
  if (ids.size() < 2) return {};
  std::string g;
  for (int64_t id : ids) g += "[aid" + std::to_string(id) + "]";
  std::string n = std::to_string(ids.size());
  g += "amix=inputs=" + n + ":duration=longest:dropout_transition=0,volume=" + n;
  return waveform ? g + ",asplit=2[ao][cue_wave];" + audio_waveform_tail() : g + "[ao]";
}

inline std::string lower_ascii(std::string s) {
  for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  return s;
}

// A recording made in parts (OBS: game, microphone, voice chat on their own tracks) rather than
// a film with versions of the same sound (dubs, commentary): several audio tracks, none of them
// tagged with a language, none named like an alternative. Only then do they play together
// on their own; anything else waits for the person to choose it.
inline bool looks_like_multitrack_recording(const std::vector<Track>& audios) {
  int n = 0;
  for (const auto& t : audios) {
    if (t.external) continue;
    ++n;
    std::string lang = lower_ascii(t.lang);
    if (!lang.empty() && lang != "und" && lang != "unk" && lang != "mis" && lang != "zxx") return false;
    std::string title = lower_ascii(t.title);
    for (const char* w : {"comment", "dub", "director", "descri", "karaoke", "instrumental", "original", "stereo", "5.1", "7.1"})
      if (title.find(w) != std::string::npos) return false;
  }
  return n >= 2;
}

// Only rebuild/reselect here. The exact seek must be a separate operation AFTER
// playback-restart, otherwise the rebuild can discard it and land at a keyframe.
inline std::vector<std::vector<std::string>> audio_switch_commands(
    const std::string& graph, int64_t single_track) {
  std::vector<std::vector<std::string>> commands{{"set", "lavfi-complex", graph}};
  if (graph.empty() && single_track > 0) {
    // Release a formerly mixed input even when aid still names that same track.
    commands.push_back({"set", "aid", "no"});
    commands.push_back({"set", "aid", std::to_string(single_track)});
  }
  return commands;
}

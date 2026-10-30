// The pairing screen's sound: an airy chord that breathes with the light while Cue waits for a
// phone, and a small rising arpeggio when one connects. Synthesized here as WAV bytes (no audio
// files ship with Cue); the Windows side plays them from memory.
#pragma once
#include <vector>

namespace remote_sound {

enum class Kind {
  Opening,    // the "on": a soft glide up, while the chord fades in (one pass of the loop)
  Waiting,    // the chord, breathing; loops seamlessly (5.2 s, the light's own period)
  Connected,  // four notes up, then silence
};
constexpr double kLoopSeconds = 5.2;
std::vector<char> wav(Kind k);

}  // namespace remote_sound

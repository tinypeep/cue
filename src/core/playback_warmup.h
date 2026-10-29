#pragma once

// Background video work waits for playback, not just for the file to load.
// This clock is reset for every replacement video; media time/seek do not affect it.
class PlaybackWarmup {
 public:
  static constexpr double kDelay = 3.0;
  static constexpr double kAtmosphereDelay = 1.5;
  void reset() { first_frame_at_ = -1; }
  void first_frame(double now) { if (first_frame_at_ < 0) first_frame_at_ = now; }
  bool ready(double now, bool transitioning, double delay = kDelay) const {
    return first_frame_at_ >= 0 && now >= first_frame_at_ + delay && !transitioning;
  }
 private:
  double first_frame_at_ = -1;
};

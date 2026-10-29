#pragma once
#include <algorithm>
#include <cmath>

// Deadlines stay anchored to the 60 Hz phase. Scheduling from the last actual
// upload turns a 16/17 ms tick sequence into 32/34 ms gaps.
struct FrameCadence {
  double period = 1.0 / 60;  // the display's own interval when the app knows it
  double next = -1;
  void reset() { next = -1; }
  bool ready(double t, double slack = 0) const { return next < 0 || t + slack + 1e-9 >= next; }
  void mark(double t) {
    if (next < 0 || t - next > 4 * period) { next = (std::floor((t+1e-9)/period)+1) * period; return; }
    next += std::max(1.0, std::floor((t-next)/period)+1) * period;
  }
  unsigned wait_ms(double t) const {
    return (unsigned)std::clamp(std::ceil((next-t)*1000-1e-6), 1.0, std::max(17.0, std::ceil(period*1000)));
  }
};

#include "remote_sound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace remote_sound {

namespace {
constexpr int kRate = 22050;
constexpr double kPi = 3.14159265358979323846;

std::vector<char> pack(const std::vector<double>& s) {
  std::vector<char> d(44 + s.size() * 2, 0);
  auto put16 = [&](size_t o, unsigned v) { d[o] = (char)v; d[o + 1] = (char)(v >> 8); };
  auto put32 = [&](size_t o, unsigned v) { for (int i = 0; i < 4; ++i) d[o + i] = (char)(v >> (8 * i)); };
  std::memcpy(d.data(), "RIFF", 4);
  put32(4, (unsigned)(d.size() - 8));
  std::memcpy(d.data() + 8, "WAVEfmt ", 8);
  put32(16, 16); put16(20, 1); put16(22, 1); put32(24, kRate); put32(28, kRate * 2); put16(32, 2); put16(34, 16);
  std::memcpy(d.data() + 36, "data", 4);
  put32(40, (unsigned)(s.size() * 2));
  for (size_t i = 0; i < s.size(); ++i) put16(44 + i * 2, (unsigned short)(short)std::lround(std::clamp(s[i], -32000.0, 32000.0)));
  return d;
}

// One pass of the waiting chord. Every partial completes a whole number of cycles in 5.2 s, and
// the breathing is one slow cycle, so the end meets the beginning without a seam.
double chord(double t) {
  double breath = 0.62 + 0.38 * (0.5 - 0.5 * std::cos(2 * kPi * t / kLoopSeconds));  // softest at the seam
  double pad = 0.50 * std::sin(2 * kPi * 220.0 * t) + 0.18 * std::sin(2 * kPi * (1145.0 / 5.2) * t) +
               0.24 * std::sin(2 * kPi * 330.0 * t) + 0.16 * std::sin(2 * kPi * 440.0 * t) +
               0.05 * std::sin(2 * kPi * 660.0 * t);
  // a quiet high ping as each sonar ring leaves the code
  double ping = t < 0.012 ? t / 0.012 : std::exp(-(t - 0.012) / 0.42);
  return 2300 * breath * pad + 900 * ping * std::sin(2 * kPi * 1760.0 * t);
}
}  // namespace

std::vector<char> wav(Kind k) {
  std::vector<double> s;
  if (k == Kind::Waiting || k == Kind::Opening) {
    const int n = (int)std::lround(kLoopSeconds * kRate);
    s.resize(n);
    for (int i = 0; i < n; ++i) {
      double t = (double)i / kRate;
      double v = chord(t);
      if (k == Kind::Opening) {
        v *= std::min(1.0, t / 2.2);  // the chord arrives slowly
        // "on": a sine gliding up two octaves, a breath long
        if (t < 1.5) {
          double f0 = 220, f1 = 880, d = 0.9;
          double phase = t < d ? f0 * d / std::log(f1 / f0) * (std::pow(f1 / f0, t / d) - 1)
                               : f0 * d / std::log(f1 / f0) * (f1 / f0 - 1) + f1 * (t - d);
          double env = t < 0.15 ? t / 0.15 : std::exp(-(t - 0.15) / 0.35);
          v += 2600 * env * std::sin(2 * kPi * phase);
        }
      }
      s[i] = v;
    }
  } else {
    const double notes[] = {880.0, 1108.73, 1318.51, 1760.0};
    const int n = (int)(1.9 * kRate);
    s.assign(n, 0.0);
    for (int k2 = 0; k2 < 4; ++k2) {
      double start = k2 * 0.09;
      for (int i = (int)(start * kRate); i < n; ++i) {
        double t = (double)i / kRate - start;
        double env = t < 0.008 ? t / 0.008 : std::exp(-(t - 0.008) / 0.32);
        s[i] += 2000 * env * (std::sin(2 * kPi * notes[k2] * t) + 0.12 * std::sin(2 * kPi * notes[k2] * 2 * t));
      }
    }
    for (int i = n - 400; i < n; ++i) s[i] *= (double)(n - i) / 400;  // no click at the end
  }
  return pack(s);
}

}  // namespace remote_sound

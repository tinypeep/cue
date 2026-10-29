// Motion primitives. Omni language: poses change in steps, positions move with springs.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

inline bool& reduce_motion() { static bool v = false; return v; }

// Consumers draw `ease_out(v)`. Rising, `v` moves linearly, so the eased value
// leaves fast and lands softly. Falling used to mirror that, which reads as an
// ease-in: it lingered at full strength, then vanished. Exits now follow an
// ease-out of their own: the change starts on the same frame and settles gently.
// The step is stateless (derived from `v`), so a reversal mid-way stays continuous.
inline double eased_fall(double v, double dt, double duration) {
  if (v <= 0) return 0;
  double o = 1 - (1 - v) * (1 - v) * (1 - v);   // what is on screen now
  double u = std::cbrt(std::max(0.0, o)) - dt / std::max(1e-3, duration);
  if (u <= 0.004) return 0;
  o = u * u * u;
  return std::max(0.0, 1 - std::cbrt(1 - o));
}

// Hover: answers on the very first frame (a visible step), then eases the rest of the way.
// Leaving is a little slower, so quick passes do not flicker.
inline double hover_step(double v, bool on, double dt) {
  if (on) {
    if (v < 0.35) v = 0.35;
    return std::min(1.0, v + dt / (reduce_motion() ? 0.03 : 0.085));
  }
  return eased_fall(v, dt, reduce_motion() ? 0.03 : 0.18);
}

// Damped spring solved in closed form (no integration error, identical motion at
// 60, 120 or 144 Hz, and a dropped frame lands exactly where it should).
// `omega` is the natural frequency, `zeta` the damping ratio. SwiftUI's
// (response, dampingFraction) map to omega = 2*pi/response, zeta = damping.
struct Spring {
  double x = 0, v = 0, target = 0;
  double omega = 17, zeta = 0.86;  // Omni layout spring (response ~0.37 s)
  double eps = 0.05;

  Spring() = default;
  Spring(double w, double z, double e = 0.05) : omega(w), zeta(z), eps(e) {}
  // Apple-style parameters: `response` in seconds, `damping` 1 = no overshoot.
  static Spring rd(double response, double damping, double e = 0.05) {
    return Spring(6.283185307179586 / std::max(0.02, response), damping, e);
  }
  void snap(double val) { x = target = val; v = 0; }
  bool settled() const { return std::fabs(x - target) < eps && std::fabs(v) < eps * 4; }
  // returns true while moving
  bool step(double dt) {
    if (settled()) { x = target; v = 0; return false; }
    if (reduce_motion()) { snap(target); return false; }
    dt = std::clamp(dt, 0.0, 0.1);
    if (dt <= 0) return true;
    const double w = omega, z = zeta, d0 = x - target, v0 = v;
    double d, nv;
    if (std::fabs(z - 1) < 1e-4) {                 // critically damped
      double e = std::exp(-w * dt), c = v0 + w * d0;
      d = (d0 + c * dt) * e;
      nv = (v0 - w * c * dt) * e;
    } else if (z < 1) {                            // underdamped: a soft overshoot
      double wd = w * std::sqrt(1 - z * z), e = std::exp(-z * w * dt);
      double cs = std::cos(wd * dt), sn = std::sin(wd * dt);
      d = e * (d0 * cs + (v0 + z * w * d0) / wd * sn);
      nv = e * (v0 * cs - (z * w * v0 + w * w * d0) / wd * sn);
    } else {                                       // overdamped
      double r = std::sqrt(z * z - 1), r1 = -w * (z - r), r2 = -w * (z + r);
      double c2 = (v0 - r1 * d0) / (r2 - r1), c1 = d0 - c2;
      double e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
      d = c1 * e1 + c2 * e2;
      nv = r1 * c1 * e1 + r2 * c2 * e2;
    }
    x = target + d;
    v = nv;
    if (settled()) { x = target; v = 0; return false; }
    return true;
  }
};

inline double clamp01(double t) { return t < 0 ? 0 : (t > 1 ? 1 : t); }
inline double ease_out(double t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
inline double ease_in_out(double t) {
  t = clamp01(t);
  return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}
inline double smoothstep(double a, double b, double x) {
  double t = clamp01((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

// Finite, smooth motion. Position and velocity survive a mid-animation retarget;
// the destination has zero velocity and acceleration, without a spring's tail.
struct PolishedMotion {
  double x=0, v=0, target=0, start=0, velocity=0, elapsed=0, duration=.32;
  bool active=false;
  void snap(double value) { x=target=start=value; v=velocity=0; active=false; elapsed=0; }
  bool settled() const { return !active; }
  void to(double value, double seconds) {
    if (std::abs(value-target)<1e-7) return;
    start=x; velocity=v; target=value; elapsed=0; duration=std::max(.001,seconds); active=true;
    if (reduce_motion()) snap(target);
  }
  bool step(double dt) {
    if (!active) return false;
    if (reduce_motion()) { snap(target); return false; }
    elapsed=std::min(duration,elapsed+std::max(0.,dt));
    double k=elapsed/duration, k2=k*k, k3=k2*k, k4=k3*k, k5=k4*k;
    double ease=6*k5-15*k4+10*k3;
    double tangent=k-6*k3+8*k4-3*k5;
    x=start+(target-start)*ease+velocity*duration*tangent;
    v=(target-start)*30*k2*(1-k)*(1-k)/duration + velocity*(1-18*k2+32*k3-15*k4);
    if (elapsed>=duration-1e-9) snap(target);
    return active;
  }
};

// Fade toward a 0/1 target with separate in/out durations. Consumers draw
// `ease_out(v)`: entrances ease out, and exits now ease out too (see eased_fall).
struct Fade {
  double v = 0, target = 0, t_in = 0.14, t_out = 0.3;
  bool step(double dt) {
    if (v == target) return false;
    if (target > v) {
      v = std::min(target, v + dt / (reduce_motion() ? 0.06 : t_in));
    } else {
      double nv = eased_fall(v, dt, reduce_motion() ? 0.06 : t_out);
      v = nv <= target ? target : nv;
    }
    return true;
  }
};

// The step response of a critically damped spring, normalised to reach 1 at t = 1:
// a gentle start, a quick middle and a long, soft landing (SwiftUI's default feel).
inline double spring_ease(double t) {
  t = clamp01(t);
  constexpr double w = 7.0;
  const double end = 1 - (1 + w) * std::exp(-w);
  return (1 - (1 + w * t) * std::exp(-w * t)) / end;
}

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  explicit Rng(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ull + 1) {}
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
  double range(double a, double b) { return a + (b - a) * uni(); }
  int pick(int n) { return (int)(uni() * n) % n; }
};

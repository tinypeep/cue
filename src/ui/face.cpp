#include "face.h"

#include <cmath>

void Face::layout(double cx_, double cy_, double s_, double scale_) {
  cx = cx_;
  cy = cy_;
  s = s_;
  scale = scale_;
}

void Face::look(double ax, double ay) {
  gx.target = std::clamp(ax, -max_look, max_look);
  gy.target = std::clamp(ay, -max_look * 0.75, max_look * 0.75);
}

void Face::look_at(double px, double py, double strength) {
  double dx = (px - cx) / (36.0 * s), dy = (py - eye_y()) / (36.0 * s);
  double len = std::sqrt(dx * dx + dy * dy);
  double k = len > 1 ? 1 / len : 1;
  look(dx * k * max_look * strength, dy * k * max_look * strength);
}

void Face::set_mode(Mode m, double now) {
  if (m == mode) return;
  mode = m;
  thinking = false;
  wide = (m == Mode::Drag);
  if (m != Mode::Drag) { excite = 0; happy = false; mouth.target = 0; }
  if (m == Mode::Idle) { look(0, 0); next_idle = now + rng.range(1.5, 3.0); }
  if (m == Mode::Drag) next_blink = now + 1.2;  // wide eyes do not blink right away
  if (m == Mode::Nervous) stare_cycle = now;
}

void Face::notice(double now) {
  notice_t0 = now;
  hop.snap(1.0);  // crouch one px of art
  hop.target = 1.0;
}

void Face::gulp(double now) {
  gulp_t0 = now;
  happy = true;
  mouth.target = 1.0;
}

bool Face::tick(double now, double dt) {
  bool moving = false;
  if (mode == Mode::Drag) {
    // anxious and happy: little hops, faster and higher the closer the file gets
    happy = excite > 0.62;
    mouth.target = 0.22 + 0.78 * excite;
    if (now >= next_bounce) {
      hop.v = -(26 + 34 * excite);
      next_bounce = now + lerp(0.56, 0.30, excite) + rng.range(0.0, 0.08);
    }
    moving = true;
  }
  if (gulp_t0 > 0) {
    double g = now - gulp_t0;
    if (g >= 0.07 && mouth.target > 0) { mouth.target = 0; mouth.v = -26; hop.v = 46; }  // chomp + squash
    if (g > 0.6) gulp_t0 = -10;
    moving = true;
  }
  moving |= mouth.step(dt);
  if (now - notice_t0 < 0.4) {
    if (now - notice_t0 >= 0.055 && hop.target > 0) { hop.target = 0; hop.v = -60; }  // spring off the crouch
    moving = true;
  }
  moving |= hop.step(dt);
  if (!started) { started = true; next_blink = now + 1.6; next_idle = now + 3.5; }
  // blink: calm cadence, sometimes double
  if (mode != Mode::Stare && now >= next_blink && blink_until < now) {
    blink_until = now + tok::blink;
    double_blink_at = rng.uni() < 0.15 ? now + 0.26 : -1;
    // natural cadence: mostly 3-8 s, now and then a long stare
    next_blink = now + (rng.uni() < 0.12 ? rng.range(9.0, 13.0) : rng.range(3.0, 8.0));
    moving = true;
  }
  if (double_blink_at > 0 && now >= double_blink_at) {
    blink_until = now + tok::blink;
    double_blink_at = -1;
    moving = true;
  }
  if (mode == Mode::Idle && now >= next_idle) {
    // irregular on purpose: no two pauses alike, nothing that reads as a loop
    double r = rng.uni();
    thinking = false;
    if (now < rest_until) {
      // completely still: holds the gaze, no new idea
    } else if (r < 0.16) {
      rest_until = now + rng.range(7.0, 15.0);                                // just being there
      look(0, 0);
    } else if (r < 0.42) look(rng.range(-2.5, 2.5), rng.range(-1.0, 1.0));    // a small side glance
    else if (r < 0.58) look(0, 0);                                            // back to center
    else if (r < 0.70) { look(rng.uni() < 0.5 ? -2.0 : 2.0, -2.2); thinking = true; }  // thinking
    else if (r < 0.77 && now - last_thought > 40) {
      // rarely, a thought: two or three pixels appear one by one, then go
      thought_t0 = now;
      last_thought = now;
      thought_n = rng.uni() < 0.4 ? 2 : 3;
      look(1.8, -2.0);
      thinking = true;
    } else look(max_look * 0.85, rng.range(-0.5, 1.5));                       // peek at Recentes
    next_idle = now + rng.range(2.4, 7.5) + (thinking ? 1.2 : 0.0);
    moving = true;
  }
  if (now - thought_t0 < 2.8) moving = true;
  if (mode == Mode::Nervous) {
    // looks from one side to the other
    double ph = std::fmod(now - stare_cycle, 0.9);
    look(ph < 0.45 ? -max_look : max_look, -0.5);
  }
  moving |= gx.step(dt);
  moving |= gy.step(dt);
  bool b = mode != Mode::Stare && blink_until > now;
  moving |= (b != blink_now);
  blink_now = b;
  return moving || blink_now;
}

double Face::next_event(double now) const {
  double t = next_blink - now;
  if (double_blink_at > 0) t = std::min(t, double_blink_at - now);
  if (blink_until > now) t = std::min(t, blink_until - now);
  if (mode == Mode::Idle) t = std::min(t, next_idle - now);
  if (now - thought_t0 < 2.8) t = 0;
  if (mode == Mode::Nervous || mode == Mode::Drag || gulp_t0 > 0 || !mouth.settled()) t = 0;
  return std::max(0.0, t);
}

Rect Face::bounds() const {
  int half = (int)((8 + 6) * scale * s);
  int ey = (int)eye_y();
  return {(int)cx - half, ey - (int)(8 * scale * s), half * 2, (int)(16 * scale * s)};
}

void Face::render(Canvas& c, double alpha) const {
  bool blinking = closed || blink_now;
  const char* eye = wide ? "sprite:eye-wide" : (thinking ? "sprite:eye-flat" : "sprite:eye-open");
  if (happy) eye = "sprite:eye-happy";
  else if (blinking) eye = "sprite:eye-closed";
  const BitmapDef* b = bmp(eye);
  if (!b) return;
  int block = (int)std::lround(3 * scale) * s;
  double ox = gx.x * s, oy = gy.x * s;
  for (int side = -1; side <= 1; side += 2) {
    if (sad && !blinking && !happy) b = bmp(side < 0 ? "sprite:eye-sad-l" : "sprite:eye-sad-r");
    double ex = cx + side * 8 * scale * s + ox + (side < 0 ? lx_off : rx_off) * s;
    double ey = eye_y() + oy + (side < 0 ? ly_off : ry_off) * s + hop.x * s * scale * 0.6;
    int x = (int)std::lround(ex - b->w * block / 2.0);
    int y = (int)std::lround(ey - b->h * block / 2.0);
    c.bitmap(b, x, y, block, P().pixel, alpha);
  }
  // a thought: up to three pixels rising diagonally beside the face, one after another
  double tt = now_hint - thought_t0;
  if (tt >= 0 && tt < 2.8) {
    double fade = tt > 2.3 ? 1 - (tt - 2.3) / 0.5 : 1.0;
    int cell = std::max(1, (int)std::lround(scale * 0.7)) * s;
    for (int i = 0; i < thought_n; ++i) {
      double at = i * 0.42;
      if (tt < at) break;
      double a = std::min(1.0, (tt - at) / 0.18) * fade;
      int px0 = (int)std::lround(cx + (14 + i * 4) * scale * s * 0.75) / s * s;
      int py0 = (int)std::lround(eye_y() - (8 + i * 4) * scale * s * 0.75) / s * s;
      int sz = cell + (i == thought_n - 1 ? cell / 2 / s * s : 0);
      c.fill(px0, py0, sz, sz, P().dim, alpha * a);
    }
  }
  // mouth: flat top, rounded bottom, grows continuously (physical px), so it opens like a breath
  double m = std::max(0.0, mouth.x);
  int mh = (int)std::lround(m * 2.0 * block / s) * s;
  if (mh >= s) {
    int mw = (int)std::lround(lerp(1.6, 3.4, std::min(1.2, m)) * block / s) * s;
    mw = std::max(mw, (int)std::lround(2 * s));
    int mx0 = (int)std::lround(cx + ox * 0.6 - mw / 2.0) / s * s;
    int my0 = (int)std::lround(cy + 1.4 * block + oy * 0.5 + hop.x * s * scale * 0.6) / s * s;
    int r = std::min(mh / 2, block / 2) / s;  // corner radius in art px
    for (int row = 0; row < mh / s; ++row) {
      int from_bottom = mh / s - 1 - row;
      int inset = 0;
      if (from_bottom < r) {
        double yy = (r - from_bottom - 0.5) / r;  // 0..1 into the corner
        inset = (int)std::lround(r * (1 - std::sqrt(std::max(0.0, 1 - yy * yy))));
      }
      c.fill(mx0 + inset * s, my0 + row * s, mw - 2 * inset * s, s, P().pixel, alpha);
    }
  }
}

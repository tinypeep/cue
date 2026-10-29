// Window controls drawn in the Omni language: minimize, maximize/restore, close.
// Shared by the home (drawn into its canvas) and playback (an overlay layer).
#pragma once
#include <array>

#include "anim.h"
#include "gfx.h"

struct WindowChrome {
  enum Btn { None = -1, Min = 0, Max = 1, Close = 2 };
  double s = 2;
  bool maximized = false;
  Fade vis{0, 0, 0.14, 0.30};
  std::array<double, 3> hov{0, 0, 0};  // hover fade per button (Omni hover: 110 ms ease-out)
  int hot = None;

  static constexpr int kStep = 17, kSize = 14, kInset = 6;  // art px
  // Rects in the coordinate space of a window `w` px wide.
  Rect rect(int i, int w) const {
    int x = w - (kInset + kSize + (2 - i) * kStep) * s;
    return {x, kInset * s - 2 * s, kSize * s, kSize * s};
  }
  Rect area(int w) const { Rect a = rect(0, w); a.w = rect(2, w).r() - a.x; return a.grow(2 * s); }
  int hit(int x, int y, int w) const {
    if (vis.v < 0.2) return None;
    for (int i = 0; i < 3; ++i) if (rect(i, w).contains(x, y)) return i;
    return None;
  }
  bool tick(double dt) {
    bool moving = vis.step(dt);
    for (int i = 0; i < 3; ++i) {
      double v = hover_step(hov[i], hot == i, dt);
      if (v != hov[i]) { hov[i] = v; moving = true; }
    }
    return moving;
  }
  // Draws at window coordinates; `ox` shifts everything when drawing into a smaller layer.
  void draw(Canvas& c, int w, int ox = 0, int oy = 0) const {
    double a = ease_out(vis.v);
    if (a <= 0) return;
    const Palette& p = P();
    static const BitmapDef close_icon{"window-close", 5, 5, "#...#" ".#.#." "..#.." ".#.#." "#...#"};
    const char* icons[3] = {"win-min", maximized ? "win-restore" : "win-max", "x-small"};
    for (int i = 0; i < 3; ++i) {
      Rect r = rect(i, w);
      r.x -= ox; r.y -= oy;
      double h = ease_out(hov[i]);
      if (h > 0) c.cornered(r, s, p.hover, a * h);
      const BitmapDef* b = i == 2 ? &close_icon : bmp(icons[i]);
      if (!b) continue;
      double cell = 1.25 * s;
      int x = (int)std::lround(r.x + (r.w - b->w * cell) / 2), y = (int)std::lround(r.y + (r.h - b->h * cell) / 2);
      c.bitmap(b, x, y, cell, mix(p.dim, i == 2 ? p.pixel : p.fg, h), a);
    }
  }
};

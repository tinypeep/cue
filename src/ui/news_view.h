// "Saiu do forno": shown once to people who updated, over the home, before anything else.
// Cue's eyes with popcorn popping behind the heading, the version on a pill, then what arrived,
// one animated pixel icon and two short lines each. "experimentar" goes straight to the new
// thing; "continuar" (or Esc) to the home. Same pixel language as the welcome screens.
#pragma once
#include <string>
#include <vector>

#include "anim.h"
#include "face.h"
#include "gfx.h"
#include "scene_view.h"

enum class NewsAct { None = 0, Continue = 1, Try = 2 };

struct NewsView : SceneView {
  int w = 0, h = 0;
  double s = 2;        // its own comfortable pixel scale (like the welcome)
  double ui_s = 2;     // the interface scale (the floor)
  std::string version;

  void set_size(int w_, int h_, double s_) override;
  void open(double now) override;
  void close(double now) override { if (close_t0_ < 0) close_t0_ = now; }
  bool closing() const override { return close_t0_ > 0; }
  bool closed(double now) const override { return close_t0_ > 0 && now - close_t0_ >= 0.24; }
  bool tick(double now, double dt) override;
  void render(Canvas& c, double now) override;
  void pointer_move(int x, int y, double now) override;
  void pointer_leave(double now) override { hover_ = -1; (void)now; }
  SceneAct pointer_down(int x, int y, double now) override;
  SceneAct key_down(const std::string& k, bool shift, double now) override;
  int cursor() const override { return hover_ >= 0 ? 1 : 0; }
  bool fast(double now) const override { return now - open_t0_ < 1.8 || close_t0_ > 0 || !kernels_.empty(); }
  bool draggable(int x, int y) const override { return hit(x, y) < 0; }

 private:
  enum : int { kContinue = 0, kTry = 1 };
  Face face_;
  Rng rng_{11};
  double open_t0_ = -10, close_t0_ = -1, next_pop_ = 0;
  int hover_ = -1, focus_ = -1;
  bool kb_ = false;
  double hov_[2] = {0, 0};
  bool overflow_ = false;
  int cols_ = 2;
  // layout, physical px
  int eye_y_ = 0, head_y_ = 0, line_y_ = 0;
  Rect buttons_[2];
  std::vector<Rect> items_;
  std::vector<std::vector<std::string>> lines_;  // each item's description, wrapped
  struct Kernel { double x, y, vx, vy, t0; int spin; };
  std::vector<Kernel> kernels_;
  void layout();
  int hit(int x, int y) const;
  void pop(double now);
  void draw_icon(Canvas& c, int which, int x, int y, Rgb ink, double a, double now) const;
  void draw_button(Canvas& c, const Rect& r, const std::string& label, bool primary, double hv, double a, bool chevron) const;
};

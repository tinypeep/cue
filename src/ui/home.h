// Home: the only place the character lives. Asymmetric horizontal composition,
// character on the left (~42%), Recentes on the right.
#pragma once
#include <string>
#include <vector>

#include "anim.h"
#include "chrome.h"
#include "face.h"
#include "gfx.h"

struct HomeRow {
  std::string name;   // display name (file name)
  std::string right;  // "1:24:12"
  double progress = -1;  // 0..1 when in progress
};

enum class HomeAct { None, OpenDialog, OpenRecent, ClearRecents, EggSeen, WinMin, WinMax, WinClose, Update, SkipUpdate };
struct HomeAction { HomeAct a = HomeAct::None; int index = -1; };

struct HomeView {
  int w = 0, h = 0; double s = 2;
  std::vector<HomeRow> rows;
  std::string engine_problem;  // shown instead of the micro-instruction
  std::string notice;          // short-lived ("não deu pra abrir")
  double notice_until = 0;
  int eggs_seen = 0;
  // a newer version on GitHub: a quiet line in the bottom-left corner
  std::string update_version;   // "" = nothing to offer
  double update_progress = -1;  // 0..1 while downloading
  bool update_busy = false;     // checking / downloading / starting the installer

  Face face;
  WindowChrome chrome;

  void set_size(int w_, int h_, double s_);
  void set_rows(std::vector<HomeRow> r);
  void render(Canvas& c, double now);
  bool tick(double now, double dt);
  double next_event(double now) const;

  HomeAction mouse_move(int x, int y, double now);
  HomeAction mouse_down(int x, int y, double now);
  void mouse_leave(double now);
  HomeAction key(const std::string& k, bool shift, double now);
  // the cursor anywhere on screen (client coords, may be outside the window): the eyes follow it
  bool track(int x, int y, double now);  // true when the eyes need to move
  void drag_over(int x, int y, double now);
  void drag_leave(double now);
  bool dragging() const { return drag_; }
  // opening a video: the home bows out (swallow = a dropped file gets eaten first)
  void leave(double now, bool swallow);
  bool leaving() const { return leave_t0_ > 0; }
  bool leave_done(double now) const { return leave_t0_ > 0 && now - leave_t0_ >= leave_dur(); }
  void cancel_leave(double now);   // the file did not open: come back
  void enter(double now, bool keep_face = false);  // back from a video (or from the welcome, eyes already there)
  void show_notice(const std::string& n, double now) { notice = n; notice_until = now + 3.0; }
  // for previews/tests
  void force_egg(int variant, double now) { start_egg(now, variant); }
  void set_hover_row(int i, double now);
  // empty background: the window can be dragged from here (and snapped by Windows)
  bool is_background(int x, int y) const;

 private:
  enum : int { kOpen = 100, kDots = 101, kMenuClear = 102, kUpdate = 103, kUpdateSkip = 104 };
  int hover_ = -1, focus_ = -1;
  bool kb_ = false, menu_ = false, drag_ = false;
  int mx_ = -1, my_ = -1;
  int tx_ = -100000, ty_ = -100000;
  double track_moved_ = -10;
  std::vector<double> clicks_;
  int egg_ = 0;
  double egg_t0_ = 0;
  Rng rng_{42};
  struct Spot { double x, y, at; int size; };
  std::vector<Spot> spots_;

  Rect r_open_, r_title_, r_dots_, r_menu_, r_list_, r_update_, r_update_skip_;
  void layout_update();
  std::vector<Rect> r_rows_;

  void layout();
  int hit(int x, int y) const;
  int active() const { return kb_ ? focus_ : hover_; }
  void start_egg(double now, int forced = 0);
  void update_face_target(double now);
  void draw_hand(Canvas& c, int fx, int fy, double a) const;
  void draw_caligari_shapes(Canvas& c, double k) const;
  void distort(Canvas& c, double k) const;
  int egg_variant_ = 0;
  double leave_t0_ = -1, enter_t0_ = -10;
  bool keep_face_ = false;
  bool swallow_ = false;
  double leave_dur() const { return swallow_ ? 0.36 : 0.2; }
  void fades(double now, double& content, double& face) const;  // 1 = fully shown
  std::vector<double> hov_;  // hover fade per hit id (rows, open, dots, menu)
  double hov(int id) const;
};

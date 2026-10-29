// First run: four short screens before the home.
//   0  "oi, sou o Cue"      the character celebrates with a bucket of popcorn
//   1  iluminação dinâmica  what the ambient light does, with a pixel toggle
//   2  tamanho da interface the four sizes, applied live to this very screen
//   3  vem mais coisa       what is coming, ripening like popcorn (milho, panela, estoura)
// Drawn by the same pixel renderer as the home, into the same canvas.
#pragma once
#include <string>
#include <vector>

#include "anim.h"
#include "chrome.h"
#include "face.h"
#include "gfx.h"

enum class ObAct { None, Ambient, UiScale, Finish, WinMin, WinMax, WinClose };
struct ObAction { ObAct a = ObAct::None; int v = 0; };

struct OnboardingView {
  static constexpr int kPages = 4;
  int w = 0, h = 0;
  double s = 2;        // the welcome's own pixel scale (never smaller than a comfortable read)
  double ui_s = 2;     // the interface scale the person chose (the control sample uses it)
  bool ambient = true;  // what the toggle shows (the App owns the preference)
  int ui_scale = 2;     // persisted id: -1 pequena, 2 média (default), 0 grande, 1 maior
  Face face;
  WindowChrome chrome;

  void set_size(int w_, int h_, double s_);
  void start(double now);
  bool tick(double now, double dt);
  void render(Canvas& c, double now);
  double next_event(double now) const;

  ObAction mouse_move(int x, int y, double now);
  ObAction mouse_down(int x, int y, double now);
  void mouse_leave(double now);
  ObAction key(const std::string& k, bool shift, double now);
  bool track(int x, int y, double now);
  bool is_background(int x, int y) const;

  // the last screen's button: everything fades and the eyes fly to where the home keeps them
  void finish(double now, double face_x, double face_y, double face_scale);
  bool finishing() const { return finish_t0_ > 0; }
  bool finished(double now) const { return finish_t0_ > 0 && now - finish_t0_ >= 0.5; }
  int page() const { return page_; }
  bool face_handoff() const { return face_handoff_; }  // the home keeps the eyes where they landed
  void go(int p, double now);
  void set_ambient(bool on, double now);     // the App confirms (or a preview sets it)
  void set_scale_choice(int id, double now);

 private:
  enum Hit { kNone = -1, kNext = 1, kBack, kSkip, kToggle, kSeg0, kSeg1, kSeg2, kSeg3, kDot0, kDot1, kDot2, kDot3 };
  int page_ = 0;
  double t0_ = -1;
  Spring pos_ = Spring::rd(0.52, 0.88, 0.002);        // page position (0..2), slides between screens
  Spring knob_ = Spring::rd(0.30, 0.62, 0.004);       // the toggle's knob, a little bounce
  Spring glow_ = Spring::rd(0.55, 1.0, 0.004);        // demo light strength
  Spring seg_x_ = Spring::rd(0.36, 0.82, 0.05), seg_w_ = Spring::rd(0.36, 0.82, 0.05);  // size selector
  Spring dot_ = Spring::rd(0.42, 0.8, 0.004);         // page indicator
  Spring bucket_ = Spring::rd(0.5, 0.62, 0.01);      // the popcorn bucket's entrance, in its own cells
  Spring fx_ = Spring::rd(0.5, 0.82, 0.3), fy_ = Spring::rd(0.5, 0.82, 0.3), fs_ = Spring::rd(0.5, 0.9, 0.003);
  bool face_placed_ = false;
  bool face_handoff_ = false;
  double finish_t0_ = -1, finish_fx_ = 0, finish_fy_ = 0, finish_fs_ = 1;

  int hover_ = kNone, focus_ = kNone;
  bool kb_ = false;
  int mx_ = -1, my_ = -1;
  int tx_ = -100000, ty_ = -100000;
  double track_moved_ = -10;
  std::vector<double> hov_;  // hover fade per hit id
  double hov(int id) const { return id >= 0 && id < (int)hov_.size() ? ease_out(hov_[id]) : 0.0; }

  // popcorn
  struct Kernel { double x, y, vx, vy, t0, life; bool to_mouth; int spin; };
  std::vector<Kernel> kernels_;
  Rng rng_{2026};
  double next_pop_ = 0, burst_until_ = 0, happy_until_ = 0, page_enter_ = 0;
  int pops_ = 0;
  double toggle_t_ = -10;  // last flip (the face reacts)
  double scale_t_ = -10;   // last size change

  // layout (physical px), recomputed by layout()
  double K_ = 1;  // illustration unit, art px per design px
  struct Lay {
    Rect next, back, toggle, demo, dock, heading;
    std::vector<Rect> segs, dots;
    std::vector<Rect> items;            // page 3: one row per upcoming thing (icon + words)
    std::vector<int> item_lines;        // description lines per row
    int text_y = 0, bucket_x = 0, bucket_y = 0, bucket_cell = 1;
    double face_x = 0, face_y = 0, face_scale = 1;
  } L_[kPages];
  void layout();
  int hit(int x, int y) const;
  std::vector<int> focus_order() const;
  ObAction activate(int id, double now);
  void place_face(double now, bool snap);
  int scale_index() const;
  ObAction pick_scale(int index, double now);

  void pop(double now, bool to_mouth);
  void render_page(Canvas& c, int p, double ox, double a, double now, int stage);
  void draw_bucket(Canvas& c, int x, int y, int cell, double a) const;
  void draw_kernel(Canvas& c, double x, double y, int cell, int spin, double a) const;
  void draw_demo(Canvas& c, const Rect& r, double ox, double a, double now, int stage) const;
  void draw_toggle(Canvas& c, const Rect& r, double a) const;
  void draw_dock_sample(Canvas& c, const Rect& r, double a, double now) const;
  void draw_button(Canvas& c, const Rect& r, const std::string& label, bool primary, double hv, double a, bool chevron) const;
  void draw_footer(Canvas& c, double a, double now);
  void draw_upcoming(Canvas& c, double ox, double a, double now);
};

// Word wrap for the pixel font (spaces only), for previews and the onboarding text.
std::vector<std::string> wrap_text(const std::string& t, int max_w, double s);

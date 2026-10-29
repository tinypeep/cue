// The character: two pixel eyes, derived from Omni's Face. Calmer: no mouth,
// no coffee, no dance. Lives only on the home.
#pragma once
#include <string>

#include "anim.h"
#include "gfx.h"

struct Face {
  enum class Mode { Idle, Recent, Drag, Nervous, Stare, Follow };

  // layout (physical px)
  double cx = 0, cy = 0;
  double s = 2;
  double scale = 2;    // face scale: eye block = 3*scale px of art
  double max_look = 4; // px of art

  Mode mode = Mode::Idle;
  Spring gx{30, 0.82, 0.01}, gy{30, 0.82, 0.01};
  Rng rng{7};
  double next_blink = 2.0, blink_until = -1, double_blink_at = -1;
  double next_idle = 3.0, idle_until = -1;
  bool thinking = false;
  bool wide = false;
  bool happy = false;           // ^ ^ eyes
  bool sad = false;             // drooping eyes (the uninstaller asks if you are sure)
  // the little mouth only shows up for big feelings (a file coming in, swallowing it)
  Spring mouth{30, 0.55, 0.004};  // 0 = gone, 1 = wide open; springy on purpose
  double excite = 0;              // 0..1 while a file hovers: how close it is to the face
  double next_bounce = 0;
  // idle: alive, not animated
  double thought_t0 = -100;       // a rare "thought": 2-3 pixels rising beside the face
  int thought_n = 3;
  double last_thought = -100;
  double rest_until = -1;         // stretches of complete stillness
  bool closed = false;          // forced closed (horror blink)
  bool blink_now = false;
  bool started = false;
  double stare_cycle = 0;       // nervous look timing
  // per-eye offsets in art px (Caligari misalignment)
  double lx_off = 0, ly_off = 0, rx_off = 0, ry_off = 0;

  void layout(double cx_, double cy_, double s_, double scale_);
  void look_at(double px, double py, double strength = 1.0);
  void look(double ax, double ay);  // art px, clamped to max_look
  void set_mode(Mode m, double now);
  void notice(double now);
  void gulp(double now);    // the mouth snaps shut and the face squashes down
  double gulp_t0 = -10;  // crouch 55 ms, then a small hop (Omni `notice`, calmer)
  double notice_t0 = -10;
  Spring hop{24, 0.5, 0.01};
  bool tick(double now, double dt);        // true while something moves
  double next_event(double now) const;     // seconds until the next scheduled change
  void render(Canvas& c, double alpha = 1.0) const;
  double now_hint = 0;  // the time of the frame being drawn (for the thought)
  Rect bounds() const;
  double eye_y() const { return cy - 3 * scale * s; }
};

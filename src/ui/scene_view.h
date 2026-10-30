// A full-window scene drawn over the player or the home (the remote's pairing screen, the
// "what's new" splash). The view is portable; src/win/overlay hosts it in a layered window.
#pragma once
#include <string>

#include "gfx.h"

struct SceneAct { int a = 0; int index = -1; };  // 0: nothing; else the view's own action ids

struct SceneView {
  virtual ~SceneView() = default;
  virtual void set_size(int w, int h, double s) = 0;
  virtual void open(double now) = 0;
  virtual void close(double now) = 0;
  virtual bool closing() const = 0;
  virtual bool closed(double now) const = 0;
  virtual bool tick(double now, double dt) = 0;
  virtual void render(Canvas& c, double now) = 0;
  virtual void pointer_move(int x, int y, double now) = 0;
  virtual void pointer_leave(double now) = 0;
  virtual SceneAct pointer_down(int x, int y, double now) = 0;
  virtual SceneAct key_down(const std::string& k, bool shift, double now) = 0;
  virtual int cursor() const = 0;                               // 0 arrow, 1 hand
  virtual bool draggable(int x, int y) const { (void)x; (void)y; return false; }  // empty background moves the window
  // true while something moves fast (an entrance, an exit): drawn on every display frame;
  // otherwise a calm 30 frames a second is plenty for breathing light
  virtual bool fast(double now) const { (void)now; return true; }
};

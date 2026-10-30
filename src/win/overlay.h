// Hosts a SceneView in a layered popup laid exactly over the player's client area: the film (or
// the home) shows faintly underneath, the scene follows the window, and its empty background
// still moves the window like the rest of Cue does.
#pragma once
#include <windows.h>

#include <functional>

#include "../ui/scene_view.h"
#include "vsync.h"

class Overlay {
 public:
  ~Overlay() { pacer_.stop(); }
  std::function<void(SceneAct)> on_action;
  std::function<void()> on_closed;
  std::function<void(double now)> on_frame;  // before each frame (a scene's sound, its own timing)

  bool create(HINSTANCE inst);
  void show(SceneView* view, HWND owner, double s, double now);
  void close(double now);
  void follow(HWND owner, double s);
  bool visible() const { return wnd_ && IsWindowVisible(wnd_); }
  SceneView* view() const { return view_; }
  double now() const;

 private:
  HWND wnd_ = nullptr, owner_ = nullptr;
  SceneView* view_ = nullptr;
  HDC mem_ = nullptr;
  HBITMAP bmp_ = nullptr;
  uint32_t* bits_ = nullptr;
  int bw_ = 0, bh_ = 0;
  double s_ = 2;
  Canvas canvas_;
  double last_ = 0;
  bool tracking_ = false;
  bool fresh_ = false;  // shown, no frame yet
  VsyncPacer pacer_;    // frames on the display's own beat (60, 120, 144 Hz)
  double drawn_ = -1;   // when the last frame was presented
  void next();          // ask for the next frame
  LARGE_INTEGER qpf_{};
  void place(HWND owner);
  void frame();
  void present();
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT, WPARAM, LPARAM);
};

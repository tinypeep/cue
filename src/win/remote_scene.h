// The remote's pairing scene on screen: RemoteView in the shared overlay, with its own small
// sound (the "on", the chord that breathes with the light, the arpeggio when a phone arrives).
#pragma once
#include <windows.h>

#include <functional>

#include "../ui/remote_view.h"
#include "overlay.h"

class RemoteScene {
 public:
  RemoteView view;
  std::function<void(RemoteAction)> on_action;
  std::function<void()> on_closed;
  std::function<void()> on_leaving;         // the exit has begun (the film can come back)
  std::function<void(double now)> on_tick;  // every frame, on the scene's clock

  bool create(HINSTANCE inst);
  void show(HWND owner, double s, double now);
  // the scene runs on the overlay's own clock: the caller's time is not used
  void close(double) { host_.close(host_.now()); stop_sound(); }
  void follow(HWND owner, double s) { host_.follow(owner, s); }
  bool visible() const { return host_.visible(); }
  void connected(const std::string& device, double now);
  void set_sound(bool on);

 private:
  Overlay host_;
  double t_open_ = 0;
  bool looping_ = true;
  bool leaving_sent_ = false;
  void frame(double now);
  void stop_sound();
};

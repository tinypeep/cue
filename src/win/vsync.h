// Display-locked animation ticks.
// A worker waits for the compositor's next frame (DwmFlush) and posts one message
// to the UI thread, only when the UI asked for it. Frames land on the display's
// own rhythm (60, 120, 144 Hz...) instead of a millisecond timer that beats
// against it, and the system timer resolution never has to be raised.
#pragma once
#include <windows.h>

#include <atomic>
#include <memory>

class VsyncPacer {
 public:
  ~VsyncPacer() { stop(); }
  VsyncPacer() = default;
  VsyncPacer(const VsyncPacer&) = delete;
  VsyncPacer& operator=(const VsyncPacer&) = delete;
  bool start(HWND target, UINT msg);
  void stop();
  bool ok() const { return thread_ != nullptr; }
  // Ask for one tick on the next display frame. Cheap; repeated calls coalesce.
  void arm();
  // The UI thread handled the posted tick (allows the next one to be posted).
  void consumed();
  // Refresh interval of the monitor the window is on, in seconds (1/60 if unknown).
  double interval() const;
  void refresh_interval(HWND hwnd);

 private:
  static DWORD WINAPI main(void* self);
  struct State;
  std::shared_ptr<State> state_;
  HANDLE thread_ = nullptr;
};

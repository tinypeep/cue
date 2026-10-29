// Display-locked animation ticks.
// A worker waits for the compositor's next frame (DwmFlush) and posts one message
// to the UI thread, only when the UI asked for it. Frames land on the display's
// own rhythm (60, 120, 144 Hz...) instead of a millisecond timer that beats
// against it, and the system timer resolution never has to be raised.
#pragma once
#include <windows.h>

#include <atomic>

class VsyncPacer {
 public:
  bool start(HWND target, UINT msg);
  void stop();
  bool ok() const { return thread_ != nullptr; }
  // Ask for one tick on the next display frame. Cheap; repeated calls coalesce.
  void arm();
  // The UI thread handled the posted tick (allows the next one to be posted).
  void consumed() { posted_ = false; }
  // Refresh interval of the monitor the window is on, in seconds (1/60 if unknown).
  double interval() const { return interval_.load(); }
  void refresh_interval(HWND hwnd);

 private:
  static DWORD WINAPI main(void* self);
  HWND target_ = nullptr;
  UINT msg_ = 0;
  HANDLE thread_ = nullptr, wake_ = nullptr;
  std::atomic<bool> quit_{false}, posted_{false};
  std::atomic<double> interval_{1.0 / 60};
};

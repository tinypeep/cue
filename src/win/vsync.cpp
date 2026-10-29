#include "vsync.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace {
double seconds() {
  static LARGE_INTEGER f = [] { LARGE_INTEGER v; QueryPerformanceFrequency(&v); return v; }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart) / double(f.QuadPart);
}
}  // namespace

bool VsyncPacer::start(HWND target, UINT msg) {
  target_ = target;
  msg_ = msg;
  wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset: one request, one tick
  if (!wake_) return false;
  thread_ = CreateThread(nullptr, 0, main, this, 0, nullptr);
  if (!thread_) { CloseHandle(wake_); wake_ = nullptr; return false; }
  // the pacer only waits and posts: above normal so a busy machine does not delay frames
  SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
  refresh_interval(target);
  return true;
}

void VsyncPacer::stop() {
  if (!thread_) return;
  quit_ = true;
  SetEvent(wake_);
  WaitForSingleObject(thread_, 200);
  CloseHandle(thread_);
  CloseHandle(wake_);
  thread_ = wake_ = nullptr;
}

void VsyncPacer::arm() {
  if (wake_) SetEvent(wake_);
}

void VsyncPacer::refresh_interval(HWND hwnd) {
  double hz = 0;
  DEVMODEW dm{};
  dm.dmSize = sizeof dm;
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof mi;
  if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi) &&
      EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
    hz = dm.dmDisplayFrequency;
  if (hz < 30 || hz > 500) hz = 60;
  if (std::fabs(hz - 59) < 0.6) hz = 59.94;  // "59 Hz" modes are NTSC rates
  interval_ = 1.0 / hz;
}

DWORD WINAPI VsyncPacer::main(void* p) {
  auto* self = static_cast<VsyncPacer*>(p);
  double last = seconds();
  while (true) {
    WaitForSingleObject(self->wake_, INFINITE);
    if (self->quit_) break;
    // Waits for the compositor's next frame. While nothing is composed (minimized,
    // locked, remote session) DwmFlush can return at once: then pace by the clock.
    HRESULT hr = DwmFlush();
    double t = seconds(), want = self->interval_.load();
    if (FAILED(hr) || t - last < want * 0.45) {
      double rest = want - (t - last);
      if (rest > 0.0005) Sleep((DWORD)std::max(1.0, std::floor(rest * 1000)));
      t = seconds();
    }
    last = t;
    if (self->quit_) break;
    if (!self->posted_.exchange(true))
      if (!PostMessageW(self->target_, self->msg_, 0, 0)) self->posted_ = false;
  }
  return 0;
}

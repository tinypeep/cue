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

// The worker owns this state too. A timed-out DwmFlush must not read a destroyed
// VsyncPacer or an already-closed event after the window owner returns from stop().
struct VsyncPacer::State {
  HWND target = nullptr;
  UINT msg = 0;
  HANDLE wake = nullptr;
  std::atomic<bool> quit{false}, posted{false};
  std::atomic<double> interval{1.0 / 60};
  ~State() { if (wake) CloseHandle(wake); }
};

bool VsyncPacer::start(HWND target, UINT msg) {
  if (thread_) return true;
  auto state = std::make_shared<State>();
  state->target = target;
  state->msg = msg;
  state->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!state->wake) return false;
  auto* context = new std::shared_ptr<State>(state);
  thread_ = CreateThread(nullptr, 0, main, context, 0, nullptr);
  if (!thread_) { delete context; return false; }
  state_ = std::move(state);
  SetThreadPriority(thread_, THREAD_PRIORITY_ABOVE_NORMAL);
  refresh_interval(target);
  return true;
}

void VsyncPacer::stop() {
  if (!thread_) return;
  state_->quit = true;
  SetEvent(state_->wake);
  WaitForSingleObject(thread_, 200);
  CloseHandle(thread_);
  thread_ = nullptr;
  state_.reset(); // a stalled worker keeps its own event/state until it actually exits
}

void VsyncPacer::arm() { if (state_) SetEvent(state_->wake); }
void VsyncPacer::consumed() { if (state_) state_->posted = false; }
double VsyncPacer::interval() const { return state_ ? state_->interval.load() : 1.0 / 60; }

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
  if (state_) state_->interval = 1.0 / hz;
}

DWORD WINAPI VsyncPacer::main(void* p) {
  std::unique_ptr<std::shared_ptr<State>> context(static_cast<std::shared_ptr<State>*>(p));
  auto state = std::move(*context);
  context.reset();
  double last = seconds();
  while (true) {
    WaitForSingleObject(state->wake, INFINITE);
    if (state->quit) break;
    // Waits for the compositor's next frame. While nothing is composed (minimized,
    // locked, remote session) DwmFlush can return at once: then pace by the clock.
    HRESULT hr = DwmFlush();
    double t = seconds(), want = state->interval.load();
    if (FAILED(hr) || t - last < want * 0.45) {
      double rest = want - (t - last);
      if (rest > 0.0005) Sleep((DWORD)std::max(1.0, std::floor(rest * 1000)));
      t = seconds();
    }
    last = t;
    if (state->quit) break;
    if (!state->posted.exchange(true))
      if (!PostMessageW(state->target, state->msg, 0, 0)) state->posted = false;
  }
  return 0;
}

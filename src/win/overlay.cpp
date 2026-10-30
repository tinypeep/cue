#include "overlay.h"

#include <windowsx.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr UINT_PTR kTimer = 1;
constexpr UINT kVsync = WM_APP + 7;
}

double Overlay::now() const {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart) / double(qpf_.QuadPart);
}

bool Overlay::create(HINSTANCE inst) {
  QueryPerformanceFrequency(&qpf_);
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.lpszClassName = L"tinyoffice.Cue.scene";
  RegisterClassExW(&wc);  // a second Overlay finds the class already there
  wnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, wc.lpszClassName, L"Cue", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                         inst, this);
  if (wnd_) pacer_.start(wnd_, kVsync);
  return wnd_ != nullptr;
}

void Overlay::place(HWND owner) {
  RECT rc;
  GetClientRect(owner, &rc);
  POINT tl{0, 0};
  ClientToScreen(owner, &tl);
  int w = rc.right - rc.left, h = rc.bottom - rc.top;
  if (w <= 0 || h <= 0) return;
  if (w != bw_ || h != bh_) {
    if (bmp_) { DeleteObject(bmp_); bmp_ = nullptr; }
    if (!mem_) mem_ = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    bmp_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bits_ = static_cast<uint32_t*>(bits);
    SelectObject(mem_, bmp_);
    bw_ = w;
    bh_ = h;
  }
  SetWindowPos(wnd_, nullptr, tl.x, tl.y, w, h, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
}

void Overlay::show(SceneView* view, HWND owner, double s, double t) {
  if (!wnd_ || !view) return;
  view_ = view;
  owner_ = owner;
  s_ = s;
  SetWindowLongPtrW(wnd_, GWLP_HWNDPARENT, (LONG_PTR)owner);  // above the player; minimizes with it
  place(owner);
  view_->set_size(bw_, bh_, s);
  view_->open(t);
  last_ = t;
  fresh_ = true;
  present();
  ShowWindow(wnd_, SW_SHOW);
  SetForegroundWindow(wnd_);
  SetFocus(wnd_);
  drawn_ = t;
  pacer_.refresh_interval(owner);
  next();
}

void Overlay::close(double t) {
  if (visible() && view_ && !view_->closing()) view_->close(t);
}

void Overlay::follow(HWND owner, double s) {
  if (!visible() || !view_) return;
  s_ = s;
  place(owner);
  view_->set_size(bw_, bh_, s);
  present();
}

void Overlay::next() {
  if (pacer_.ok()) pacer_.arm();
  else SetTimer(wnd_, kTimer, 15, nullptr);  // no compositor beat: the fastest plain timer
}

void Overlay::frame() {
  double t = now();
  double dt = std::min(0.1, t - last_);
  last_ = t;
  if (on_frame) on_frame(t);
  if (!view_) return;
  // whatever the caller did right after showing (starting the server, loading a sound) must not
  // eat the entrance: the scene's clock starts with its first real frame
  if (fresh_) {
    fresh_ = false;
    if (dt > 0.05) { view_->open(t); dt = 0; }
  }
  view_->tick(t, dt);
  if (view_->closed(t)) {
    KillTimer(wnd_, kTimer);
    view_ = nullptr;
    ShowWindow(wnd_, SW_HIDE);
    if (owner_) SetForegroundWindow(owner_);
    if (on_closed) on_closed();
    return;
  }
  // calm moments at 30 Hz (20 on a 4K screen); entrances, exits and celebrations on every frame
  long long px = (long long)bw_ * bh_;
  double calm = px > 16000000 ? 0.066 : px > 4000000 ? 0.05 : 0.033;  // 8K breathes at 15 Hz
  if (view_->fast(t) || t - drawn_ >= calm - 0.004) { present(); drawn_ = t; }
  next();
}

void Overlay::present() {
  if (!bits_ || bw_ <= 0 || !view_) return;
  view_->render(canvas_, now());
  if (canvas_.w != bw_ || canvas_.h != bh_) return;
  std::memcpy(bits_, canvas_.px.data(), canvas_.px.size() * 4);
  RECT wr;
  GetWindowRect(wnd_, &wr);
  POINT dst{wr.left, wr.top}, src{0, 0};
  SIZE sz{bw_, bh_};
  BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(wnd_, nullptr, &dst, &sz, mem_, &src, 0, &bf, ULW_ALPHA);
}

LRESULT CALLBACK Overlay::proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
  }
  auto* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (self && self->wnd_ == h) return self->handle(m, w, l);
  return DefWindowProcW(h, m, w, l);
}

LRESULT Overlay::handle(UINT m, WPARAM w, LPARAM l) {
  double t = now();
  switch (m) {
    case WM_TIMER:
      if (w == kTimer) { KillTimer(wnd_, kTimer); frame(); }
      return 0;
    case kVsync:
      pacer_.consumed();
      if (view_ && visible()) frame();
      return 0;
    case WM_MOUSEMOVE:
      if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd_, 0};
        TrackMouseEvent(&tme);
        tracking_ = true;
      }
      if (view_) view_->pointer_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), t);
      return 0;
    case WM_MOUSELEAVE:
      tracking_ = false;
      if (view_) view_->pointer_leave(t);
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT) {
        SetCursor(LoadCursor(nullptr, view_ && view_->cursor() ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN: {
      if (!view_ || view_->closing()) return 0;
      int x = GET_X_LPARAM(l), y = GET_Y_LPARAM(l);
      SceneAct a = view_->pointer_down(x, y, t);
      if (a.a && on_action) { on_action(a); return 0; }
      if (view_->draggable(x, y) && owner_ && !IsZoomed(owner_)) {
        // the empty background moves the window, as anywhere else in Cue (Snap included)
        ReleaseCapture();
        SendMessageW(owner_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      return 0;
    }
    case WM_KEYDOWN: {
      std::string k = w == VK_ESCAPE ? "esc" : w == VK_TAB ? "tab" : w == VK_RETURN ? "enter" : w == VK_SPACE ? "space"
                    : w == VK_LEFT ? "left" : w == VK_RIGHT ? "right" : "";
      if (k.empty() || !view_ || view_->closing()) return 0;
      SceneAct a = view_->key_down(k, GetKeyState(VK_SHIFT) < 0, t);
      if (a.a && on_action) on_action(a);
      return 0;
    }
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
  }
  return DefWindowProcW(wnd_, m, w, l);
}

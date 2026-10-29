#include "ambient.h"
#include "../ui/ambient_raster.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

static LRESULT CALLBACK glow_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCHITTEST) return HTTRANSPARENT;
  if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
  return DefWindowProcW(h, m, w, l);
}

bool Ambient::create(HINSTANCE inst) {
  if (failed_) return false;
  if (st_[0].wnd) return true;
  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = glow_proc;
  wc.hInstance = inst;
  wc.lpszClassName = L"tinyoffice.Cue.ambient";
  RegisterClassExW(&wc);
  for (auto& s : st_) {
    s.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
                            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    if (!s.wnd) { destroy(); failed_ = true; return false; }
  }
  return true;
}

void Ambient::free_surfaces() {
  for (auto& s : st_) {
    if (s.mem) DeleteDC(s.mem);
    if (s.bmp) DeleteObject(s.bmp);
    s.mem = nullptr;
    s.bmp = nullptr;
    s.bits = nullptr;
    s.shape.clear();
  }
  built_ = false;
}

void Ambient::destroy() {
  free_surfaces();
  for (auto& s : st_) { if (s.wnd) DestroyWindow(s.wnd); s.wnd = nullptr; }
}

void Ambient::hide() {
  for (auto& s : st_) if (s.wnd && IsWindowVisible(s.wnd)) ShowWindow(s.wnd, SW_HIDE);
}

void Ambient::reset() {
  // A transient layered-window error must not disable the next video forever.
  if (failed_) { destroy(); failed_ = false; }
  want_ = false;
  alpha_ = 0;
  last_a_ = -1;
  colors_dirty_ = true;
  since_paint_ = 1;
  hide();
}

// The visible edge of the player (without the invisible resize borders).
static RECT visible_rect(HWND owner) {
  RECT r{};
  if (FAILED(DwmGetWindowAttribute(owner, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(owner, &r);
  return r;
}

// Once per size: where the halo is, how strong at each pixel, and which point of the window's
// edge lights it. Everything later is a lookup.
void Ambient::build_shape(const RECT& r, int dpi) {
  free_surfaces();
  rect_ = r;
  int ww = r.right - r.left, wh = r.bottom - r.top;
  if (ww <= 0 || wh <= 0) return;
  dpi_ = dpi;
  int reach = (int)std::lround(std::min(ww, wh) * 0.14);
  M_ = std::clamp(reach, MulDiv(48, dpi, 96), MulDiv(120, dpi, 96));
  RECT sr[4];
  layout_strips(r, M_, sr);
  for (int k = 0; k < 4; ++k) {
    st_[k].x = sr[k].left; st_[k].y = sr[k].top;
    st_[k].w = std::max(0L, sr[k].right - sr[k].left); st_[k].h = std::max(0L, sr[k].bottom - sr[k].top);
  }
  since_build_ = 0;
  rebuild_pending_ = false;
  HDC screen = GetDC(nullptr);
  for (auto& s : st_) {
    if (s.w <= 0 || s.h <= 0) continue;  // entirely off screen: nothing to make
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = s.w;
    bi.bmiHeader.biHeight = -s.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    s.mem = CreateCompatibleDC(screen);
    s.bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!s.mem || !s.bmp || !bits) { ReleaseDC(nullptr, screen); free_surfaces(); failed_ = true; return; }
    SelectObject(s.mem, s.bmp);
    s.bits = static_cast<uint32_t*>(bits);
    std::memset(s.bits, 0, (size_t)s.w * s.h * 4);
    s.shape.reserve((size_t)s.w * s.h / 2);
  }
  ReleaseDC(nullptr, screen);
  const double P = 2.0 * (ww + wh);
  for (int k = 0; k < 4; ++k) {
    Strip& s = st_[k];
    for (int ly = 0; ly < s.h; ++ly)
      for (int lx = 0; lx < s.w; ++lx) {
        int x = s.x + lx, y = s.y + ly;
        int qx = std::clamp(x, M_, M_ + ww - 1), qy = std::clamp(y, M_, M_ + wh - 1);
        double dx = x - qx, dy = y - qy;
        double u = std::sqrt(dx * dx + dy * dy) / M_;
        if (u >= 1) continue;
        // a soft light: strong at the edge, fading like a lamp on a wall, ending at zero
        double f = std::exp(-2.8 * u * u) * (1 - u);
        int a = (int)std::lround(f * 235);
        if (a <= 1) continue;
        // which point of the window's edge this pixel faces, clockwise from the top-left corner
        double p;
        int ex = qx - M_, ey = qy - M_;
        if (y < M_) p = ex;
        else if (x >= M_ + ww) p = ww + ey;
        else if (y >= M_ + wh) p = ww + wh + (ww - 1 - ex);
        else p = 2.0 * ww + wh + (wh - 1 - ey);
        int li = std::clamp((int)(p / P * kLut), 0, kLut - 1);
        s.shape.push_back({(uint32_t)(ly * s.w + lx), (uint16_t)li, (uint8_t)a});
      }
  }
  built_ = true;
  colors_dirty_ = true;
}

// Strip rects in halo coordinates (origin = player's top-left minus the reach), cut to the
// monitor the player is on. top/bottom carry the corners, left/right sit between them.
void Ambient::layout_strips(const RECT& r, int M, RECT out[4]) const {
  int ww = r.right - r.left, wh = r.bottom - r.top, W = ww + 2 * M;
  RECT full[4] = {{0, 0, W, M}, {0, M + wh, W, M + wh + M}, {0, M, M, M + wh}, {M + ww, M, M + ww + M, M + wh}};
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
  RECT mon = mi.rcMonitor;
  OffsetRect(&mon, -(r.left - M), -(r.top - M));  // into halo coordinates
  for (int k = 0; k < 4; ++k)
    if (!IntersectRect(&out[k], &full[k], &mon)) SetRectEmpty(&out[k]);
}

// The zone colours spread around the edge, blended softly between zones (like the TV's LEDs).
void Ambient::build_lut() {
  if (tint_.empty()) tint_.resize(kLut * 256, 0);
  int ww = rect_.right - rect_.left, wh = rect_.bottom - rect_.top;
  double P = 2.0 * (ww + wh);
  if (P <= 0) return;
  double pos[kZones];
  for (int i = 0; i < 8; ++i) pos[i] = (i + 0.5) / 8 * ww;
  for (int i = 0; i < 5; ++i) pos[8 + i] = ww + (i + 0.5) / 5 * wh;
  for (int i = 0; i < 8; ++i) pos[13 + i] = ww + wh + (i + 0.5) / 8 * ww;
  for (int i = 0; i < 5; ++i) pos[21 + i] = 2.0 * ww + wh + (i + 0.5) / 5 * wh;
  static double r[kLut], g[kLut], b[kLut], l[kLut];
  for (int k = 0; k < kLut; ++k) {
    double p = (k + 0.5) / kLut * P;
    int j = kZones - 1;
    for (int z = 0; z < kZones; ++z) if (pos[z] <= p) j = z;
    int n = (j + 1) % kZones;
    double a = pos[j], bpos = n == 0 ? pos[0] + P : pos[n];
    if (p < pos[0]) { a = pos[kZones - 1] - P; bpos = pos[0]; }  // wrapping past the top-left corner
    double t = std::clamp((p - a) / std::max(1.0, bpos - a), 0.0, 1.0);
    t = t * t * (3 - 2 * t);
    r[k] = zone_[j].r + (zone_[n].r - zone_[j].r) * t;
    g[k] = zone_[j].g + (zone_[n].g - zone_[j].g) * t;
    b[k] = zone_[j].b + (zone_[n].b - zone_[j].b) * t;
    l[k] = light_[j] + (light_[n] - light_[j]) * t;
  }
  // a little extra blur around the ring, so no zone has a hard border
  static double tr[kLut], tg[kLut], tb[kLut], tl[kLut];
  const int R = 10;
  for (int k = 0; k < kLut; ++k) {
    double sr = 0, sg = 0, sb = 0, sl = 0;
    for (int d = -R; d <= R; ++d) { int q = (k + d + kLut) % kLut; sr += r[q]; sg += g[q]; sb += b[q]; sl += l[q]; }
    tr[k] = sr / (2 * R + 1); tg[k] = sg / (2 * R + 1); tb[k] = sb / (2 * R + 1); tl[k] = sl / (2 * R + 1);
  }
  for (int k = 0; k < kLut; ++k) {
    auto quantize = [](double v) { return (uint8_t)std::lround(std::clamp(v, 0.0, 255.0)); };
    uint8_t nr = quantize(tr[k]), ng = quantize(tg[k]), nb = quantize(tb[k]), na = quantize(tl[k] * 255);
    if (nr != lr_[k] || ng != lg_[k] || nb != lb_[k] || na != la_[k]) {
      lr_[k] = nr; lg_[k] = ng; lb_[k] = nb; la_[k] = na;
      ambient_ramp(nr, ng, nb, na, tint_.data() + k * 256);
    }
  }
}

void Ambient::place(HWND owner, bool show) {
  HDWP dw = BeginDeferWindowPos(4);
  for (auto& s : st_) {
    if (!s.wnd || !s.bits) continue;
    UINT fl = SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | (show ? SWP_SHOWWINDOW : 0);
    if (dw) dw = DeferWindowPos(dw, s.wnd, owner, rect_.left - M_ + s.x, rect_.top - M_ + s.y, 0, 0, fl);
  }
  if (dw) EndDeferWindowPos(dw);
}

void Ambient::paint(HWND owner) {
  if (!built_) return;
  build_lut();
  bool needs_show = false;
  HDC screen = GetDC(nullptr);
  BLENDFUNCTION bf{AC_SRC_OVER, 0, blend(), AC_SRC_ALPHA};
  for (auto& s : st_) {
    if (!s.bits) { if (s.wnd && IsWindowVisible(s.wnd)) ShowWindow(s.wnd, SW_HIDE); continue; }
    needs_show |= !IsWindowVisible(s.wnd);
    for (const Px& q : s.shape) {
      s.bits[q.idx] = tint_[(size_t)q.lut * 256 + q.a];
    }
    POINT dst{rect_.left - M_ + s.x, rect_.top - M_ + s.y}, src{0, 0};
    SIZE sz{s.w, s.h};
    if (!UpdateLayeredWindow(s.wnd, screen, &dst, &sz, s.mem, &src, 0, &bf, ULW_ALPHA)) failed_ = true;
  }
  ReleaseDC(nullptr, screen);
  if (needs_show) place(owner, true); // colour changes do not need another move/z-order batch
  last_a_ = blend();
  colors_dirty_ = false;
  since_paint_ = 0;
}

void Ambient::follow(HWND owner, bool defer_geometry) {
  if (alpha_ <= 0.001 || !built_) return;
  RECT r = visible_rect(owner);
  if (r.right - r.left != rect_.right - rect_.left || r.bottom - r.top != rect_.bottom - rect_.top) {
    if (defer_geometry) { rebuild_pending_ = true; return; }
    alpha_ = 0;
    build_shape(r, GetDpiForWindow(owner));
    paint(owner);
    return;
  }
  // moved: same halo, just somewhere else. If the monitor now cuts it differently, it is
  // rebuilt a moment later (never on every step of a drag)
  RECT sr[4];
  layout_strips(r, M_, sr);
  for (int k = 0; k < 4; ++k)
    if (sr[k].left != st_[k].x || sr[k].top != st_[k].y || sr[k].right - sr[k].left != st_[k].w ||
        sr[k].bottom - sr[k].top != st_[k].h)
      rebuild_pending_ = true;
  rect_ = r;
  place(owner, false);
}

BYTE Ambient::blend() const { return (BYTE)std::lround(std::clamp(alpha_ * strength_, 0.0, 1.0) * 255); }

bool Ambient::update(HWND owner, const Rgb* zones, const double* light, bool visible, double strength, double dt, bool defer_geometry) {
  if (!st_[0].wnd || failed_) return false;
  want_ = visible;
  strength_ = std::clamp(strength, 0.0, 1.0);
  double target = visible && !defer_geometry ? 1.0 : 0.0;
  double na = alpha_ + std::clamp(target - alpha_, -dt / 0.45, dt / 0.8);  // in slowly, out a bit faster
  bool moving = std::fabs(na - alpha_) > 1e-4;
  alpha_ = na;
  since_paint_ += dt;
  for (int k = 0; k < kZones; ++k) {
    if (zones[k].r != zone_[k].r || zones[k].g != zone_[k].g || zones[k].b != zone_[k].b ||
        std::fabs(light[k] - light_[k]) > 1.0 / 1024) {
      zone_[k] = zones[k];
      light_[k] = light[k];
      colors_dirty_ = true;
    }
  }
  if (alpha_ <= 0.001) {
    hide();
    last_a_ = -1;
    return moving;
  }
  RECT r = visible_rect(owner);
  if (!built_ || r.right - r.left != rect_.right - rect_.left || r.bottom - r.top != rect_.bottom - rect_.top) {
    if (defer_geometry) {
      rebuild_pending_ = true;
      int a=blend();BLENDFUNCTION bf{AC_SRC_OVER,0,(BYTE)a,AC_SRC_ALPHA};
      if(a!=last_a_)for(auto& strip:st_)if(strip.bits)UpdateLayeredWindow(strip.wnd,nullptr,nullptr,nullptr,nullptr,nullptr,0,&bf,ULW_ALPHA);
      last_a_=a;return moving;
    }
    build_shape(r, GetDpiForWindow(owner));
    paint(owner);
    return moving;
  }
  rect_ = r;
  since_build_ += dt;
  if (rebuild_pending_ && since_build_ > 0.15 && !defer_geometry) {
    build_shape(r, GetDpiForWindow(owner));
    paint(owner);
    return moving;
  }
  int a = blend();
  // T_POLL is 33 ms: allow timer jitter rather than accidentally painting every
  // second tick when 33 ms falls just short of 33.333 ms.
  if (colors_dirty_ && (since_paint_ >= 1.0 / 30 - 0.002 || last_a_ < 0)) paint(owner);
  else if (a != last_a_) {
    // only the fade changed: no new pixels, just the strips' overall opacity
    BLENDFUNCTION bf{AC_SRC_OVER, 0, (BYTE)a, AC_SRC_ALPHA};
    for (auto& s : st_)
      if (s.bits) UpdateLayeredWindow(s.wnd, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &bf, ULW_ALPHA);
    last_a_ = a;
  }
  return moving;
}

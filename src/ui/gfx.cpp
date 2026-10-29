#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <type_traits>

#include "assets.inc"
#include "action_icons.inc"

static Palette g_pal;
static bool g_hc = false;
const Palette& P() { return g_pal; }
void set_high_contrast(bool on) {
  g_hc = on;
  g_pal = Palette{};
  if (on) {
    g_pal.faint = {120, 120, 120};
    g_pal.dim = {200, 200, 200};
    g_pal.edge = {120, 120, 120};
  }
}

// ── lookups ─────────────────────────────────────────────────────────
const BitmapDef* bmp(std::string_view name) {
  static std::unordered_map<std::string_view, const BitmapDef*> map = [] {
    std::unordered_map<std::string_view, const BitmapDef*> m;
    for (auto& b : kBitmaps) m[b.name] = &b;
    for (auto& b : kActionIcons) m[b.name] = &b;
    // icons drawn for Cue's own menus, on the same 1-art-px grid as "check" and "clock"
    static const BitmapDef extra[] = {
        {"text-style", 9, 7, ".##......" "#..#....." "#..#.###." "####....#" "#..#.####" "#..#.#..#" "#..#..###"},
    };
    for (auto& b : extra) m[b.name] = &b;
    return m;
  }();
  auto it = map.find(name);
  return it == map.end() ? nullptr : it->second;
}

void action_icon(Canvas& c, std::string_view name, const Rect& box, double s, Rgb colour, double alpha) {
  if (const auto* b = bmp(name)) {
    double cell = name.starts_with("cue:") ? s / 2 : s;
    c.bitmap(b, box.x + (int)std::lround((box.w - b->w * cell) / 2),
             box.y + (int)std::lround((box.h - b->h * cell) / 2), cell, colour, alpha);
  }
}

const MaskDef* label_mask(std::string_view label, double s) {
  const MaskDef* best = nullptr;
  for (auto& m : kMasks) {
    if (label != m.label) continue;
    if (!best || std::abs(m.s - s) < std::abs(best->s - s)) best = &m;
  }
  return best;
}

uint32_t next_cp(std::string_view s, size_t& i) {
  auto b = [&](size_t k) { return (uint32_t)(uint8_t)s[k]; };
  uint32_t c = b(i);
  if (c < 0x80) { i += 1; return c; }
  if ((c >> 5) == 6 && i + 1 < s.size()) { uint32_t r = ((c & 31) << 6) | (b(i + 1) & 63); i += 2; return r; }
  if ((c >> 4) == 14 && i + 2 < s.size()) { uint32_t r = ((c & 15) << 12) | ((b(i + 1) & 63) << 6) | (b(i + 2) & 63); i += 3; return r; }
  if ((c >> 3) == 30 && i + 3 < s.size()) {
    uint32_t r = ((c & 7) << 18) | ((b(i + 1) & 63) << 12) | ((b(i + 2) & 63) << 6) | (b(i + 3) & 63);
    i += 4; return r;
  }
  i += 1;
  return 0xFFFD;
}

static const GlyphDef* glyph(uint32_t cp) {
  static std::unordered_map<uint32_t, const GlyphDef*> map = [] {
    std::unordered_map<uint32_t, const GlyphDef*> m;
    for (auto& g : kGlyphs) { size_t i = 0; m[next_cp(g.ch, i)] = &g; }
    return m;
  }();
  auto it = map.find(cp);
  return it == map.end() ? nullptr : it->second;
}
bool has_glyph(uint32_t cp) { return glyph(cp) != nullptr; }

// ── canvas ──────────────────────────────────────────────────────────
static inline uint32_t premul(Rgb c, double a) {
  if (a <= 0) return 0;
  if (a > 1) a = 1;
  uint32_t A = (uint32_t)(a * 255 + 0.5);
  uint32_t R = (c.r * A + 127) / 255, G = (c.g * A + 127) / 255, B = (c.b * A + 127) / 255;
  return (A << 24) | (R << 16) | (G << 8) | B;
}

static inline uint32_t over(uint32_t src, uint32_t dst) {
  uint32_t sa = src >> 24;
  if (sa == 255 || dst == 0) return src;
  if (sa == 0) return dst;
  uint32_t inv = 255 - sa;
  auto ch = [&](int sh) {
    uint32_t s = (src >> sh) & 255, d = (dst >> sh) & 255;
    uint32_t v = s + (d * inv + 127) / 255;
    return std::min<uint32_t>(v, 255) << sh;
  };
  return ch(24) | ch(16) | ch(8) | ch(0);
}

void Canvas::resize(int nw, int nh) {
  w = std::max(0, nw);
  h = std::max(0, nh);
  px.assign((size_t)w * h, 0);
}
void Canvas::clear(Rgb c, double a) { std::fill(px.begin(), px.end(), premul(c, a)); }
void Canvas::clear_transparent() { std::fill(px.begin(), px.end(), 0u); }

void Canvas::fill(double x, double y, double fw, double fh, Rgb c, double a) {
  const int ix = (int)std::lround(x), iy = (int)std::lround(y);
  fill_pixels(ix, iy, (int)std::lround(x + fw) - ix, (int)std::lround(y + fh) - iy, c, a);
}

void Canvas::fill_pixels(int x, int y, int fw, int fh, Rgb c, double a) {
  int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(w, x + fw), y1 = std::min(h, y + fh);
  if (x0 >= x1 || y0 >= y1) return;
  uint32_t v = premul(c, a);
  if (!v) return;
  bool opaque = (v >> 24) == 255;
  for (int yy = y0; yy < y1; ++yy) {
    uint32_t* row = &px[(size_t)yy * w];
    if (opaque) std::fill(row + x0, row + x1, v);
    else for (int xx = x0; xx < x1; ++xx) row[xx] = over(v, row[xx]);
  }
}

void Canvas::cornered(const Rect& r, int cut, Rgb c, double a) {
  if (r.w <= 2 * cut || r.h <= 2 * cut) { fill(r, c, a); return; }
  fill(r.x + cut, r.y, r.w - 2 * cut, cut, c, a);
  fill(r.x, r.y + cut, r.w, r.h - 2 * cut, c, a);
  fill(r.x + cut, r.b() - cut, r.w - 2 * cut, cut, c, a);
}

void Canvas::cells(const char* grid, int gw, int gh, int x, int y, double cell, Rgb c, double a) {
  const int whole = (int)cell;
  if (cell == whole) {
    for (int j = 0; j < gh; ++j) {
      for (int i = 0; i < gw;) {
        if (grid[j * gw + i] != '#') { ++i; continue; }
        const int first = i++;
        while (i < gw && grid[j * gw + i] == '#') ++i;
        fill_pixels(x + first * whole, y + j * whole, (i - first) * whole, whole, c, a);
      }
    }
    return;
  }
  for (int j = 0; j < gh; ++j) {
    const int top = (int)std::lround(y + j * cell);
    const int height = (int)std::lround(y + (j + 1) * cell) - top;
    for (int i = 0; i < gw;) {
      if (grid[j * gw + i] != '#') { ++i; continue; }
      const int first = i++;
      while (i < gw && grid[j * gw + i] == '#') ++i;
      const int left = (int)std::lround(x + first * cell);
      fill_pixels(left, top, (int)std::lround(x + i * cell) - left, height, c, a);
    }
  }
}

void Canvas::bitmap(const BitmapDef* b, int x, int y, double cell, Rgb c, double a, bool flip_x) {
  if (!b) return;
  if (!flip_x) { cells(b->cells, b->w, b->h, x, y, cell, c, a); return; }
  for (int j = 0; j < b->h; ++j)
    for (int i = 0; i < b->w; ++i)
      if (b->cells[j * b->w + i] == '#') {
        int xi = flip_x ? (b->w - 1 - i) : i;
        fill(x + xi * cell, y + j * cell, cell, cell, c, a);
      }
}

void Canvas::mask(const MaskDef* m, int x, int y, Rgb c, double a) {
  if (!m) return;
  for (int j = 0; j < m->h; ++j) {
    int yy = y + j;
    if (yy < 0 || yy >= h) continue;
    for (int i = 0; i < m->w; ++i) {
      int xx = x + i;
      if (xx < 0 || xx >= w) continue;
      uint8_t k = (uint8_t)m->a[j * m->w + i];
      if (!k) continue;
      uint32_t v = premul(c, a * k / 255.0);
      px[(size_t)yy * w + xx] = over(v, px[(size_t)yy * w + xx]);
    }
  }
}

void Canvas::blit(const Canvas& src, int x, int y, double a) {
  uint32_t A = (uint32_t)(std::clamp(a, 0.0, 1.0) * 255 + 0.5);
  if (A == 0) return;
  // clip once, then walk whole rows: no per-pixel bounds checks
  const int i0 = std::max(0, -x), i1 = std::min(src.w, w - x);
  const int j0 = std::max(0, -y), j1 = std::min(src.h, h - y);
  if (i0 >= i1 || j0 >= j1) return;
  for (int j = j0; j < j1; ++j) {
    const uint32_t* s = &src.px[(size_t)j * src.w];
    uint32_t* d = &px[(size_t)(y + j) * w + x];
    if (A == 255) {
      for (int i = i0; i < i1; ++i) {
        uint32_t v = s[i];
        uint32_t sa = v >> 24;
        if (sa == 255) d[i] = v;           // opaque: a plain copy
        else if (sa) d[i] = over(v, d[i]);  // transparent pixels are skipped
      }
    } else {
      for (int i = i0; i < i1; ++i) {
        uint32_t v = s[i];
        if (!v) continue;
        auto ch = [&](int sh) { return ((((v >> sh) & 255) * A + 127) / 255) << sh; };
        d[i] = over(ch(24) | ch(16) | ch(8) | ch(0), d[i]);
      }
    }
  }
}

void Canvas::scale_alpha(double a) {
  if (a >= 1) return;
  uint32_t A = (uint32_t)(std::clamp(a, 0.0, 1.0) * 255 + 0.5);
  for (auto& s : px) {
    auto ch = [&](int sh) { return ((((s >> sh) & 255) * A + 127) / 255) << sh; };
    s = ch(24) | ch(16) | ch(8) | ch(0);
  }
}

template<class Scale>
static void draw_panel_impl(Canvas& c, int x, int y, int aw, int ah, Scale s, const std::vector<int>& profile, Rgb fill,
                double fill_a, Rgb edge, double edge_a) {
  auto fill_rect = [&](auto xx, auto yy, auto ww, auto hh, Rgb color, double alpha) {
    if constexpr (std::is_integral_v<Scale>) c.fill_pixels(xx, yy, ww, hh, color, alpha);
    else c.fill(xx, yy, ww, hh, color, alpha);
  };
  if (aw <= 0 || ah <= 0) return;
  auto inset = [&](int row) {
    int k = std::min(row, ah - 1 - row);
    return k < (int)profile.size() ? profile[k] : 0;
  };
  auto inside = [&](int i, int j) {
    if (j < 0 || j >= ah) return false;
    int in = inset(j);
    return i >= in && i < aw - in;
  };
  for (int j = 0; j < ah; ++j) {
    int in = inset(j);
    // run-length: fill the interior span in one go, edges cell by cell
    int i0 = in, i1 = aw - in;
    for (int i = i0; i < i1; ++i) {
      bool border = !inside(i - 1, j) || !inside(i + 1, j) || !inside(i, j - 1) || !inside(i, j + 1);
      if (border) { fill_rect(x + i * s, y + j * s, s, s, edge, edge_a); continue; }
      int run = i;
      while (run < i1 && inside(run, j - 1) && inside(run, j + 1) && inside(run + 1, j)) ++run;
      if (run == i) { fill_rect(x + i * s, y + j * s, s, s, fill, fill_a); continue; }
      fill_rect(x + i * s, y + j * s, (run - i) * s, s, fill, fill_a);
      i = run - 1;
    }
  }
}

void draw_panel(Canvas& c, int x, int y, int aw, int ah, double s, const std::vector<int>& profile,
                Rgb fill, double fill_a, Rgb edge, double edge_a) {
  if (s == (int)s) draw_panel_impl(c, x, y, aw, ah, (int)s, profile, fill, fill_a, edge, edge_a);
  else draw_panel_impl(c, x, y, aw, ah, s, profile, fill, fill_a, edge, edge_a);
}

// ── text ────────────────────────────────────────────────────────────
int text_w(std::string_view str, double s) {
  int w = 0;
  for (size_t i = 0; i < str.size();) {
    const GlyphDef* g = glyph(next_cp(str, i));
    if (g) w += g->w;
  }
  return (int)std::lround(w * s);
}

int text(Canvas& c, std::string_view str, int x, int y, double s, Rgb col, double a) {
  double cx = x;
  for (size_t i = 0; i < str.size();) {
    const GlyphDef* g = glyph(next_cp(str, i));
    if (!g) continue;  // what the font does not draw disappears (Omni rule)
    c.cells(g->rows, g->w, 12, cx, y, s, col, a);
    cx += g->w * s;
  }
  return (int)std::lround(cx - x);
}

static std::vector<std::string_view> split_cps(std::string_view s) {
  std::vector<std::string_view> out;
  for (size_t i = 0; i < s.size();) {
    size_t st = i;
    next_cp(s, i);
    out.push_back(s.substr(st, i - st));
  }
  return out;
}

std::string fit_end(std::string_view t, int max_w, double s) {
  if (text_w(t, s) <= max_w) return std::string(t);
  auto cps = split_cps(t);
  int ell = text_w("…", s);
  std::string out;
  int w = 0;
  for (auto cp : cps) {
    int cw = text_w(cp, s);
    if (w + cw + ell > max_w) break;
    out += cp;
    w += cw;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out + "…";
}

std::string fit_middle(std::string_view name, int max_w, double s) {
  if (text_w(name, s) <= max_w) return std::string(name);
  auto cps = split_cps(name);
  size_t dot = name.rfind('.');
  size_t keep_tail_bytes = (dot != std::string_view::npos && name.size() - dot <= 6) ? name.size() - dot : 0;
  // grow the tail a bit beyond the extension so "S02E03" style endings survive
  int ell = text_w("…", s);
  std::string tail(name.substr(name.size() - keep_tail_bytes));
  auto tail_cps = split_cps(name.substr(0, name.size() - keep_tail_bytes));
  int budget = max_w - ell - text_w(tail, s);
  if (budget <= 0) return fit_end(name, max_w, s);
  int head_budget = (budget * 2) / 3, tail_budget = budget - head_budget;
  std::string head, extra;
  int w = 0;
  size_t hi = 0;
  for (; hi < tail_cps.size(); ++hi) {
    int cw = text_w(tail_cps[hi], s);
    if (w + cw > head_budget) break;
    head += tail_cps[hi];
    w += cw;
  }
  int tw = 0;
  for (size_t k = tail_cps.size(); k > hi; --k) {
    int cw = text_w(tail_cps[k - 1], s);
    if (tw + cw > tail_budget) break;
    extra = std::string(tail_cps[k - 1]) + extra;
    tw += cw;
  }
  return head + "…" + extra + tail;
}

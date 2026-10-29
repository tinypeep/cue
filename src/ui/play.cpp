#include "play.h"
#include "../core/version.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../core/sub_metrics.h"
#include "../core/sub_style.h"
#include "../core/util.h"
#include "../core/ui_scale.h"

namespace {
constexpr int kDockH = 38;         // art px
constexpr int kRowH = 13;          // Omni row-sheet
constexpr int kMargin = 14;        // art px from the window edge
const std::vector<int> kDockProfile = {3, 2, 1};
const std::vector<int> kPanelProfile = {2, 1};
constexpr double kDockFill = 0.92, kPanelFill = 0.95;

// Play/pause as column spans (7 columns, centered on row 3). Morphing the
// spans keeps every frame a real pixel shape: nothing is resampled.
const double kPlayCols[7] = {7, 7, 5, 5, 3, 3, 1};
const double kPauseCols[7] = {7, 7, 0, 0, 0, 7, 7};

std::string speed_label(double v) {
  char b[16];
  std::snprintf(b, sizeof b, "%.2f", v);
  std::string s = b;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s + "\xc3\x97";  // ×
}

// Speed: the slider covers 0.25..3 in steps of 0.05; the chips are the usual stops.
constexpr double kSpeedMin = 0.25, kSpeedMax = 3.0;
const double kSpeedChips[6] = {0.5, 1.0, 1.25, 1.5, 2.0, 3.0};
const double kSpeedLadder[10] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};  // [ and ]

std::string speed_plain(double v) {  // "1.25", "2", "0.5": no sign, for the chips
  std::string s = speed_label(v);
  return s.substr(0, s.size() - 2);
}

void draw_playpause(Canvas& c, int x, int y, double s, double t, Rgb col, double a) {
  t = clamp01(t);
  double dip = 1.0 - 0.22 * std::sin(t * 3.14159265);
  int shift = (int)std::lround((1 - t) * s * 0.5);  // optical centre of the triangle
  bool rest = t < 0.001 || t > 0.999;
  for (int i = 0; i < 7; ++i) {
    double hgt = lerp(kPlayCols[i], kPauseCols[i], ease_in_out(t));
    if (rest) {
      // at rest: exact art pixels, spans odd so they stay centred
      int hh = (int)std::lround(hgt);
      if (hh % 2 == 0) hh += (hgt > hh) ? 1 : -1;
      if (hh <= 0) continue;
      c.fill(x + i * s + shift, y + (3 - hh / 2) * s, s, hh * s, col, a);
    } else {
      // in motion: physical px, so the morph glides instead of stepping
      int hp = (int)std::lround(hgt * s);
      if (hp <= 0) continue;
      c.fill(x + i * s + shift, y + (7 * s - hp) / 2, s, hp, col, a * dip);
    }
  }
}
}  // namespace

// ── layout ──────────────────────────────────────────────────────────
void PlayView::resize(int w, int h, double s) {
  bool first = w_ == 0;
  if (first) { style_mix_ = in.controls_free ? 1 : 0; style_motion_.snap(style_mix_); }
  int ow = w_, oh = h_;
  double ox = px_.x, oy = py_.x;
  style_y_ = -1;
  w_ = w; h_ = h; s_ = std::max(1.0, s);
  in.w = w; in.h = h; in.s = s_;
  layout_dock();
  double x, y;
  place_target(x, y);
  if (glide_ && !first && !dragging_dock_ && !reduce_motion() && ow > 0 && oh > 0) {
    // fullscreen in/out: the dock starts where it was (relative to the old frame) and travels
    px_.snap(ox * w / ow);
    py_.snap(oy * h / oh);
    px_.target = x;
    py_.target = y;
  } else if (!first && w == ow && h == oh) {
    if (!dragging_dock_) { px_.target = x; py_.target = y; }  // same size (frame refresh): keep any glide going
  } else if (first || !dragging_dock_) { px_.snap(x); py_.snap(y); }
  if (first || w != ow || h != oh) glide_ = false;
  dock_dirty_ = panel_dirty_ = true;
  if (panel_ != Panel::None) layout_rows();
}

void PlayView::layout_dock() {
  int W = w_ / s_;
  // narrow windows (portrait video): same dock, recomposed. Core stays -15 / play / +15
  compact_ = W < 300;
  // grows with the window (a longer timeline is easier to aim on big screens), within reason
  double DW = std::min(std::clamp((int)std::lround(W * 0.44), 290, 340), std::max(196, W - 2 * kMargin));
  if (compact_) DW = std::min(std::max(196, W - 2 * kMargin), kCompactDockW);  // centred, never edge to edge
  if (!compact_) {
    int free_width = std::min(W - 2 * kMargin, std::clamp((int)(W * 0.85), 196, 800));
    DW = lerp(DW, free_width, style_mix_);
  }
  layout_free_ = in.controls_free;
  dw_ = (int)std::lround(DW * s_);
  dh_ = (compact_ ? 62 : kDockH) * s_;
  hots_.clear();
  auto add = [&](int id, double x, double y, double ww, double hh) { hots_.push_back({id, {x * s_, y * s_, ww * s_, hh * s_}}); };
  const int y1 = 13;
  const int audio_y = y1 - 8;
  const int volume_w = 49;
  double cx = DW / 2;
  int playback_y = compact_ ? 26 : y1 - 8;
  add(B_VOL, 6, audio_y, 17, 16);
  add(B_VOLBAR, 25, audio_y, volume_w, 16);  // longer volume track, with space for speed and More
  // playback speed lives in the toolbar itself: one click, the choices open right above it
  const int sw = text_w("1.75\xc3\x97", 1) + 8;
  if (compact_) add(B_SPEED, DW - 20 - sw, audio_y, sw, 16);
  // right side, from the edge, in groups by meaning: more · view (full screen, audio,
  // subtitles) · repeat. One slot width and one step inside a group, a wider step between.
  const int slot = 17, step = 20, group = 5;
  double x = DW - 5;
  int controls_y = compact_ ? 26 : y1 - 8;
  add(B_MORE, x - slot, audio_y, slot, 16); x -= step;
  add(B_FS, compact_ ? DW - 24 : x - slot, controls_y, slot, 16); x -= step;
  if (in.audios.size() > 1) {
    add(B_AUDIO, compact_ ? 25 : x - slot, controls_y, slot, 16); x -= step;
  }
  add(B_CC, compact_ ? 6 : x - slot, controls_y, slot, 16); x -= step;
  double left_end = 25 + volume_w;
  if (!compact_) {
    // more stands apart from the view group, and repeat apart from the view
    for (auto& hb : hots_) if (hb.id == B_FS || hb.id == B_CC || hb.id == B_AUDIO) hb.r.x -= group * s_;
    x -= 2 * group;
    add(B_LOOP, x - slot, controls_y, slot, 16); x -= step;
    // speed is "how it plays", like volume: it sits right after the volume, its slot hugging
    // the words ("1×", "1.25×") so it never floats
    int ssw = text_w(speed_plain(in.speed), 1) + 4 + 8;
    add(B_SPEED, left_end + 7, y1 - 8, ssw, 16);
    left_end += 7 + ssw;
  } else {
    add(B_LOOP, DW - 44, controls_y, slot, 16);
  }
  if (!compact_ && (cx + 28 + 8 > x || cx - 27 - 8 < left_end)) cx = std::max((left_end + x) / 2, left_end + 8 + 27);
  int gap = compact_ ? 1 : 3;
  add(B_REW, cx - 8 - gap - 16, playback_y, 16, 16);
  add(B_PLAY, cx - 8, playback_y, 17, 16);
  add(B_FWD, cx + 9 + gap, playback_y, 16, 16);
  // timeline row
  bool hours = in.dur >= 3600;
  layout_hours_ = hours;
  layout_audio_count_ = in.audios.size();
  int tw = text_w(hours ? "-0:00:00" : "-00:00", 1);
  int timeline_y = compact_ ? 51 : 28;
  double tx0 = 8 + tw + 7, tx1 = DW - 8 - tw - 7;
  tl_ = {tx0 * s_, timeline_y * s_, (tx1 - tx0) * s_, s_};
  add(B_TIMELINE, tx0 - 3, timeline_y - 7, tx1 - tx0 + 6, 14);
  add(B_TIME, 4, timeline_y - 7, tw + 8, 14);
  add(B_TIME, DW - tw - 12, timeline_y - 7, tw + 8, 14);
}

void PlayView::anchor_pos(DockAnchor a, double& x, double& y) const {
  double m = kMargin * s_;
  switch (a) {
    case DockAnchor::TopCenter: x = (w_ - dw_) / 2.0; y = m; break;
    case DockAnchor::BottomLeft: x = m; y = h_ - dh_ - std::max(m, h_ * 0.12); break;
    case DockAnchor::BottomRight: x = w_ - dw_ - m; y = h_ - dh_ - std::max(m, h_ * 0.12); break;
    default: x = (w_ - dw_) / 2.0; y = h_ - dh_ - std::max(m, h_ * 0.12); break;
  }
}

void PlayView::place_target(double& x, double& y) const {
  if (place.anchor != DockAnchor::Free) { anchor_pos(place.anchor, x, y); }
  else {
    double m = kMargin * s_;
    x = m + place.fx * std::max(0.0, w_ - dw_ - 2 * m);
    y = m + place.fy * std::max(0.0, h_ - dh_ - 2 * m);
  }
  x = std::round(x);
  y = std::round(y);
}

Rect PlayView::dock_rect() const {
  // enters on a spring (6 px of art, a hair of overshoot), leaves with a small eased descent
  double art = vis_.target > 0 ? rise_.x : (1 - ease_out(vis_.v)) * 3;
  if (viewport_transition_) art += (1 - ease_out(viewport_vis_.v)) * 4;  // stepping aside: sinks as it fades
  int rise = (int)std::lround(art * s_);
  bool top = py_.x + dh_ / 2.0 < h_ / 2.0;
  return {(int)std::lround(px_.x), (int)std::lround(py_.x) + (top ? -rise : rise), dw_, dh_};
}

int PlayView::dock_btn_at(int lx, int ly) const {
  for (auto& hb : hots_)
    if (hb.r.contains(lx, ly)) return hb.id;
  return (!in.controls_free && lx >= 0 && ly >= 0 && lx < dw_ && ly < dh_) ? B_DOCKBG : B_NONE;
}

int PlayView::hit_dock(int x, int y) const {
  if (viewport_transition_ || vis_.v <= 0.01) return B_NONE;
  Rect d = dock_rect();
  return dock_btn_at(x - d.x, y - d.y);
}

double PlayView::time_at(int x) const {
  Rect d = dock_rect();
  double f = (x - d.x - tl_.x) / (double)std::max(1, tl_.w);
  return clamp01(f) * std::max(0.0, in.dur);
}

// ── render: dock ────────────────────────────────────────────────────
void PlayView::render_dock() {
  Palette p = P();
  const double ink_white = style_mix_;
  p.dim = mix(p.dim, {255,255,255}, ink_white);
  p.fg = mix(p.fg, {255,255,255}, ink_white);
  p.pixel = {255,255,255};
  p.faint = mix(p.faint, {255,255,255}, ink_white);
  Canvas& c = dock_base_;
  c.resize(dw_, dh_);
  // the film tints the whole toolbar a little: its glass, its edge, what is active
  double ak = std::clamp(in.accent_k, 0.0, 1.0) * (1 - ink_white);
  double s = s_;
  const double secondary = compact_ ? lerp(.8, 1., style_mix_) : 1.;
  // every button fades its hover in and out (Omni: 110 ms ease-out), nothing snaps
  auto hv = [&](int id) {
    double h = ease_out(hov_[id]);
    if ((id == B_CC || id == B_AUDIO || id == B_MORE) && panel_btn_ == id) h = 1.0;
    return h;
  };
  auto rect_of = [&](int id) {
    for (auto& hb : hots_) if (hb.id == id) return hb.r;
    return Rect{};
  };
  auto bg = [&](int id, double h) {
    if (h <= 0 || style_mix_ >= 1) return;
    Rect r = rect_of(id).inset(s);
    c.cornered(r, s, mix(p.hover, in.accent, 0.22 * std::clamp(in.accent_k, 0.0, 1.0)), h * (1-style_mix_));
  };
  // magnetic hover: the glyph leans a pixel or two toward the cursor; the hitbox never moves
  auto lean = [&](int id, int& x, int& y) {
    x += (int)std::lround(magx_[id].x);
    y += (int)std::lround(magy_[id].x);
  };
  auto icon = [&](int id, const char* name, Rgb rest = Rgb{0, 0, 0}, bool use_rest = false) {
    const BitmapDef* b = bmp(name);
    if (!b) return;
    Rect r = rect_of(id);
    if (r.w == 0) return;
    double h = hv(id);
    bg(id, h);
    double cell = std::string_view(name).starts_with("cue:") ? s / 2 : s;
    int x = style_animating_ ? (int)std::lround(r.x + (r.w - b->w * cell) / 2) : (int)((r.x + (r.w - b->w * cell) / 2) / s) * s, y = (r.y + (r.h - b->h * cell) / 2) / s * s;
    lean(id, x, y);
    // hovered glyphs warm toward the film's colour, at rest they stay neutral
    Rgb hot = mix(p.fg, in.accent, 0.35 * ak);
    c.bitmap(b, x, y, cell, mix(use_rest ? rest : p.dim, hot, h), 1.0);
  };
  // the film's atmosphere, kept subtle: only what shows progress and activity takes it
  Rgb acc = mix(p.fg, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * .85 * (1-style_mix_));
  Rgb acc_px = mix(p.pixel, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * .85 * (1-style_mix_));

  bool muted = in.muted || in.volume <= 0;
  icon(B_VOL, muted ? "sound-off" : "sound-on");
  {
    Rect r = rect_of(B_VOLBAR);
    int bx = r.x + 2 * s, bw = r.w - 4 * s, by = r.cy() / s * s - s;
    c.fill(bx, by, bw, 2 * s, p.faint, lerp(1., .28, ink_white));
    double v = muted ? 0 : std::clamp(in.volume / kVolMax, 0.0, 1.0);
    int fw = (int)std::lround(bw * v / s) * s;
    int f100 = (int)std::lround(bw * 100 / kVolMax / s) * s;  // where the natural level ends
    double h = vol_drag_ ? 1.0 : std::max(hv(B_VOLBAR), hv(B_VOL));
    Rgb fillc = mix(mix(p.dim, acc, 0.5), acc, h);
    c.fill(bx, by, std::min(fw, f100), 2 * s, fillc, 1.0);
    // the booster stretch: past 100% the line turns warm, so you always see you are pushing it
    Rgb boost = mix(Rgb{255, 170, 90}, in.accent, 0.25 * std::clamp(in.accent_k, 0.0, 1.0));
    if (fw > f100) c.fill(bx + f100, by, fw - f100, 2 * s, mix(mix(p.dim, boost, 0.6), boost, h), 1.0);
    // a small notch marks 100%, a point the handle sticks to
    c.fill(bx + f100, by - s, s, s, fw > f100 ? boost : p.faint, (fw > f100 ? 0.9 : 0.6 + 0.4 * h));
    int handle_h = (7 + (int)std::lround(2 * h)) * s;
    c.fill(bx + fw - s, r.cy() - handle_h / 2, 2 * s, handle_h,
           fw > f100 ? mix(acc_px, boost, 0.7) : mix(p.dim, acc_px, 0.65 + 0.35 * h));
  }
  icon(B_REW, "rewind");
  icon(B_FWD, "forward");
  {
    // repeat: lit and underlined by a pixel while it is on
    icon(B_LOOP, "loop", in.loop ? acc : p.dim, true);
    Rect r = rect_of(B_LOOP);
    if (in.loop && r.w > 0) {
      int dx = (int)((r.x + r.w / 2) / s) * s + (int)std::lround(magx_[B_LOOP].x);
      int dy = (int)((r.y + (r.h + 7 * s) / 2) / s) * s + 2 * s + (int)std::lround(magy_[B_LOOP].x);
      c.fill(dx, dy, s, s, acc, 1.0);
    }
  }
  {
    Rect r = rect_of(B_PLAY);
    double h = hv(B_PLAY);
    bg(B_PLAY, h);
    double play_cell = (1 + 0.25 * style_mix_) * s;
    int x = (int)std::lround(r.x + (r.w - 7 * play_cell) / 2), y = (int)std::lround(r.y + (r.h - 7 * play_cell) / 2);
    lean(B_PLAY, x, y);
    draw_playpause(c, x, y, play_cell, morph_.x, mix(p.fg, p.pixel, h), 1.0);
  }
  bool cc_on = in.sid != 0 && in.sub_visible;
  icon(B_CC, "cc", cc_on ? acc : p.dim, true);  // an active state takes the accent
  
  if (in.audios.size() > 1) icon(B_AUDIO, "k-audio");
  icon(B_FS, in.fullscreen ? "fs-exit" : "fs-enter");
  {
    Rect r = rect_of(B_MORE);
    double h = hv(B_MORE);
    bg(B_MORE, h);
    const BitmapDef* dots = bmp("more");
    int dx = style_animating_ ? (int)std::lround(r.x + (r.w - dots->w * s) / 2) : (int)((r.x + (r.w - dots->w * s) / 2) / s) * s;
    int dy = (r.y + (r.h - dots->h * s) / 2) / s * s;
    lean(B_MORE, dx, dy);
    c.bitmap(dots, dx, dy, s, mix(p.dim, p.fg, h), secondary);
  }

  // time · timeline · duration
  double t = scrubbing_ ? scrub_t_ : in.time;
  bool hours = in.dur >= 3600;
  std::string ts = remaining_ ? "-" + fmt_time(std::max(0.0, in.dur - t), hours) : fmt_time(t, hours), ds = fmt_time(in.dur, hours);
  shown_time_ = ts;
  int ty = tl_.y - 6 * s;
  text(c, ts, 8 * s, ty, s, p.dim);
  text(c, ds, dw_ - 8 * s - text_w(ds, s), ty, s, p.dim);
  {
    Rect sr = rect_of(B_SPEED);
    if (sr.w > 0) {
      double h = hv(B_SPEED);
      bg(B_SPEED, h);
      bool other = std::fabs(in.speed - 1.0) > 0.01;  // not 1x: a touch brighter, so it is noticed
      // the number, then a 3x3 pixel × on the digits' middle (the font's × sits low and small)
      std::string num = speed_plain(in.speed);
      int nw = text_w(num, s);
      int lx = sr.x + (sr.w - nw - 4 * s) / 2 / s * s + (int)std::lround(magx_[B_SPEED].x);
      int ly = sr.y + (sr.h - 12 * s) / 2 / s * s;
      if (compact_) lx = sr.r() - 3 * s - nw - 4 * s;
      Rgb col = mix(other ? acc : p.dim, acc, h);
      text(c, num, lx, ly, s, col, secondary);
      c.cells("#.#.#.#.#", 3, 3, lx + nw + s, ly + 4 * s, s, col, secondary);
    }
  }
  double f = in.dur > 0 ? clamp01(t / in.dur) : 0;
  int played = (int)std::lround(tl_.w * f / s) * s;
  shown_tl_px_ = played;
  // under the cursor the track thickens and the handle grows, in physical px so it is smooth
  double e = scrubbing_ ? 1.0 : ease_out(tl_near_);
  int th = s + (int)std::lround(e * s);
  int cy2 = 2 * tl_.y + s;  // track centre, doubled to stay exact
  int ty0 = (cy2 - th) / 2;
  c.fill(tl_.x, ty0, tl_.w, th, p.faint, lerp(1., .28, ink_white));
  if (e > 0 && !scrubbing_ && hover_ == B_TIMELINE && mx_ >= 0) {
    // the stretch you would jump to, lit a little ahead of the playhead
    int gx = std::clamp(mx_ - dock_rect().x - tl_.x, 0, tl_.w);
    if (gx > played) c.fill(tl_.x + played, ty0, gx - played, th, p.dim, 0.55 * e);
  }
  c.fill(tl_.x, ty0, played, th, acc);
  int hs = (int)std::lround((3 + 2 * e) * s);
  int hx = tl_.x + played - hs / 2;
  c.fill(hx, (cy2 - hs) / 2, hs, hs, mix(acc_px, p.pixel, 0.25 * (1 - e)));
  if (style_mix_ > 0) {
    // Ink brightens continuously with the same motion as geometry and backdrop.
    for (auto& pixel : c.px) {
      unsigned a = pixel >> 24;
      auto white = [&](unsigned channel) { return (unsigned)std::lround(lerp(channel, a, style_mix_)); };
      pixel = (a << 24) | (white((pixel>>16)&255) << 16) | (white((pixel>>8)&255) << 8) | white(pixel&255);
    }
  }
  if (style_mix_ > 0) white_control_shadow(c, style_mix_);
  if (style_mix_ < 1) {
    Canvas panel; panel.resize(dw_, dh_);
    draw_panel(panel, 0, 0, dw_ / s_, dh_ / s_, s_, kDockProfile,
               mix(p.bg, in.accent, 0.10 * ak), kDockFill * (1 - style_mix_),
               mix(p.edge, in.accent, 0.45 * ak), 0.9 * (1 - style_mix_));
    // The pixel-art panel renderer accepts whole art cells. Fill the fractional
    // width by extending its flat centre, so the right edge travels in screen
    // pixels instead of jumping one whole cell during the morph.
    int drawn_w = (int)std::lround((int)(dw_/s_) * s_);
    if (drawn_w > 1 && drawn_w < dw_) {
      int extra=dw_-drawn_w, middle=drawn_w/2;
      for(int y=0;y<dh_;++y) {
        auto row=panel.px.begin()+(size_t)y*dw_;
        auto flat=row[middle];
        std::move_backward(row+middle,row+drawn_w,row+dw_);
        std::fill(row+middle,row+middle+extra,flat);
      }
    }
    panel.blit(c, 0, 0); c = std::move(panel);
  }
  dock_dirty_ = false;
  ++dock_gen_;
}

// ── panel ───────────────────────────────────────────────────────────
bool PlayView::selectable(const Row& r) const {
  return r.k != RK::Hint && r.k != RK::Separator && r.k != RK::Brand && r.k != RK::Credit && r.k != RK::Info;
}

void PlayView::set_speed(double v) {
  v = std::clamp(std::round(v * 20.0) / 20.0, kSpeedMin, kSpeedMax);
  if (std::fabs(v - in.speed) < 0.001) return;
  act(PA::Speed, v);
  in.speed = v;
  dock_dirty_ = true;
  if (panel_ == Panel::Speed) {
    int keep = pfocus_;
    build_rows();
    layout_rows();
    pfocus_ = keep;
  }
}

void PlayView::build_rows() {
  rows_.clear();
  auto R = [&](RK k, RowId id, std::string label = "") { Row r; r.k = k; r.id = id; r.label = std::move(label); rows_.push_back(r); return &rows_.back(); };
  if (panel_ == Panel::CC) {
    if (in.subs.empty()) {
      R(RK::Hint, R_NONE, "nenhuma legenda");
    } else {
      // one switch turns subtitles on and off; the tracks below only pick which one
      bool on = in.sid != 0 && in.sub_visible;
      R(RK::Toggle, R_SUB_TOGGLE, "legenda")->checked = on;
      for (auto& t : in.subs) {
        Row* r = R(RK::Option, R_SUB_TRACK, track_label(t, in.subs));
        r->track = t.id;
        r->checked = in.sid == t.id;
        r->enabled = on;
      }
    }
    Row* add = R(RK::Action, R_ADD_SUB, "carregar legenda");
    add->icon = "cue:open-file";
    if (in.sid != 0) {
      R(RK::Separator, R_NONE);
      // every action row shares one icon column, so the labels line up
      R(RK::Submenu, R_STYLE, "estilo")->icon = "text-style";
      Row* d = R(RK::Stepper, R_DELAY, "sincronia");
      d->right = fmt_delay(in.sub_delay);
      d->icon = "clock";
    }
  } else if (panel_ == Panel::Style) {
    R(RK::Header, R_BACK, "estilo");
    StyleCaps caps = style_caps(in.style, in.kind);
    if (caps.ass_toggle) {
      Row* r = R(RK::Segmented, R_ASS);
      r->segs = {"original", "meu estilo"};
      r->seg = in.style.ass_mode;
    }
    if (caps.font) {
      Row* r = R(RK::Fonts, R_FONTS);
      r->seg = in.style.font;
    }
    if (caps.size) {
      Row* r = R(RK::Slider, R_SIZE, "tamanho");
      r->value = in.style.size; r->minv = 28; r->maxv = 72;
      r->right = std::to_string(in.style.size);
    }
    if (caps.size) {
      // typography: space between lines, and between letters
      Row* g = R(RK::Slider, R_LINEGAP, "entrelinha");
      g->value = in.style.line_gap; g->minv = kLineGapMin; g->maxv = kLineGapMax;
      g->right = in.style.line_gap > 0 ? "+" + std::to_string(in.style.line_gap) : std::to_string(in.style.line_gap);
      g = R(RK::Slider, R_TRACK, "kerning");
      g->value = in.style.tracking; g->minv = -3; g->maxv = 10;
      g->right = in.style.tracking > 0 ? "+" + std::to_string(in.style.tracking) : std::to_string(in.style.tracking);
    }
    if (caps.opacity) {
      Row* r = R(RK::Slider, R_TEXT, "texto");
      r->value = in.style.text_op; r->minv = 10; r->maxv = 100;
      r->right = std::to_string(in.style.text_op) + "%";
      r = R(RK::Slider, R_BG, "fundo");
      r->value = in.style.bg_op; r->minv = 0; r->maxv = 100;
      r->right = std::to_string(in.style.bg_op) + "%";
      // the outline lives on transparent backgrounds; with a box it has nothing to draw
      r = R(RK::Slider, R_OUTLINE, "contorno");
      r->value = in.style.outline; r->minv = 0; r->maxv = 6;
      r->enabled = in.style.bg_op == 0;
      r->right = !r->enabled ? "-" : (in.style.outline == 0 ? "sem" : std::to_string(in.style.outline));
    }
    if (caps.position) R(RK::Hint, R_POS, "arraste a legenda");
    if (in.kind == SubKind::Bitmap) R(RK::Hint, R_NONE, "legenda em imagem");
    else if (in.kind == SubKind::Ass && in.style.ass_mode == 0) R(RK::Hint, R_NONE, "estilo do arquivo");
    // always present, so the panel never changes height under the cursor
    SubStyle factory;
    Row* r = R(RK::Links, R_LINKS);
    r->segs = {"usar como padrão", "resetar"};
    r->seg = (in.style != in.default_style ? 1 : 0) | (in.style != factory ? 2 : 0);  // enabled bits
  } else if (panel_ == Panel::Audio) {
    for (auto& t : in.audios) {
      Row* r = R(RK::Option, R_AUDIO_TRACK, track_label(t, in.audios));
      r->track = t.id;
      r->checked = in.aid == t.id;
    }
  } else if (panel_ == Panel::More) {

    Row* open = R(RK::Action, R_OPEN, "abrir arquivo");
    open->right = "ctrl+o"; open->icon = "cue:open-file";
    R(RK::Action, R_CLOSE, in.audio_only ? "fechar áudio" : "fechar vídeo")->icon = "cue:close-video";
    R(RK::Separator, R_NONE);
    if (!in.audio_only) {
      Row* amb = R(RK::Toggle, R_AMBIENT, "luz ambiente");
      amb->checked = in.ambient; amb->icon = "sun";
      // its strength; always present so the panel never jumps, usable when the light is on
      Row* g = R(RK::Slider, R_AMBIENT_STR, "força");
      g->value = std::lround(in.ambient_strength * 100);
      g->minv = 5; g->maxv = 100;
      g->right = std::to_string((int)std::lround(in.ambient_strength * 100)) + "%";
      g->enabled = in.ambient;
    }
    R(RK::Hint, R_NONE, "interface");
    Row* u = R(RK::Segmented, R_UISCALE);
    u->segs = {"pequena", "média", "grande", "maior"};
    u->seg = ui_size_index(in.ui_scale);
    R(RK::Hint, R_NONE, "fundo dos controles");
    Row* appearance = R(RK::Segmented, R_CONTROLS_FREE);
    appearance->segs = {"com fundo", "sem fundo"};
    appearance->seg = in.controls_free ? 1 : 0;
    R(RK::Separator, R_NONE);
    if (in.update_ready && !in.update_label.empty()) R(RK::Action, R_UPDATE, in.update_label)->icon = "cue:open-file";
    R(RK::Submenu, R_ABOUT, "sobre o Cue")->icon = "cue:about-cue";
  } else if (panel_ == Panel::About) {
    R(RK::Header, R_BACK_MORE, "sobre o Cue");
    R(RK::Brand, R_NONE);
    R(RK::Credit, R_NONE);
    R(RK::Profile, R_LINKEDIN, "Gabriel Benzoni");
    R(RK::Separator, R_NONE);
    R(RK::Profile, R_LINKEDIN, "LinkedIn");
    R(RK::Profile, R_GITHUB, "GitHub");
    R(RK::Profile, R_LICENSES, "licenças");
    if (!in.update_label.empty()) R(RK::Profile, R_UPDATE, in.update_label);
  } else if (panel_ == Panel::Speed) {
    // Keep the value on the title line, at the same scale as the dock.
    R(RK::Hint, R_NONE, "velocidade")->right = speed_label(in.speed);
    Row* bar = R(RK::SpeedBar, R_SPEED_BAR);
    bar->value = in.speed; bar->minv = kSpeedMin; bar->maxv = kSpeedMax;
    Row* ch = R(RK::Chips, R_SPEED_CHIPS);
    ch->seg = -1;
    for (int i = 0; i < 6; ++i) {
      ch->segs.push_back(speed_plain(kSpeedChips[i]));
      if (std::fabs(in.speed - kSpeedChips[i]) < 0.01) ch->seg = i;
    }
  } else if (panel_ == Panel::Context) {
    R(RK::Action, R_FILE_INFO, "sobre o arquivo")->icon = "cue:info";
    R(RK::Separator, R_NONE);
    if (!in.audio_only) {
      R(RK::Action, R_FRAME, "salvar frame")->icon = "cue:save-frame";  // the picture only, never the subtitle or the UI
      R(RK::Separator, R_NONE);
    }
    R(RK::Action, R_CLOSE, in.audio_only ? "fechar áudio" : "fechar vídeo")->icon = "cue:close-video";
  } else if (panel_ == Panel::FileInfo) {
    R(RK::CloseHeader, R_DISMISS, "sobre o arquivo");
    if (in.file_info.empty()) R(RK::Info, R_NONE, "carregando...");
    for (const auto& field : in.file_info) R(RK::Info, R_NONE, field.first)->right = field.second;
  }
}

// The label column fits the longest slider name, so no label ever touches its track.
bool PlayView::icon_grid() const {
  for (auto& r : rows_) if (r.icon) return true;
  return false;
}

int PlayView::slider_label_w() const {
  int w = 40;
  for (auto& r : rows_) if (r.k == RK::Slider) w = std::max(w, text_w(r.label, 1));
  return w + 14;
}

void PlayView::layout_rows() {
  double s = s_;
  int W = 0;
  for (auto& r : rows_) {
    int need = 0;
    switch (r.k) {
      case RK::Toggle: need = 8 + text_w(r.label, 1) + 14 + 13 + 6; break;
      case RK::Option: case RK::Action: case RK::Submenu: case RK::Hint: case RK::Header:
        need = 26 + text_w(r.label, 1) + 10 + (r.right.empty() ? 0 : text_w(r.right, 1) + 10) + (r.k == RK::Submenu ? 12 : 0);
        break;
      case RK::Stepper: need = (r.icon ? 25 : 8) + text_w(r.label, 1) + 14 + 7 + text_w("+0.0s", 1) + 7 + 7 + 8; break;
      case RK::Fonts: need = 3 * 46 + 8; break;
      case RK::Segmented: { int sw = 8; for (auto& sg : r.segs) sw += text_w(sg, 1) + 14; need = sw; break; }
      case RK::Links: { int lw = 8; for (auto& sg : r.segs) lw += text_w(sg, 1) + 14; need = lw; break; }
      case RK::Slider: need = slider_label_w() + 110 + (icon_grid() ? 17 : 0); break;
      case RK::SpeedBar: need = 4 + 13 + 4 + 90 + 4 + 13 + 4; break;
      case RK::Chips: need = 4 + 6 * (text_w("1.25", 1) + 4) + 5 * 2 + 4; break;
      default: break;
    }
    W = std::max(W, need);
  }
  W = std::max(W, 112);
  if (panel_ == Panel::About) W = 168;
  if (panel_ == Panel::FileInfo) W = std::max(150, std::min(264, (int)(w_ / s) - 16));
  pw_ = W * s;
  double y = 4 * s;
  for (auto& r : rows_) {
    int rh = r.k == RK::Separator ? 5 : (r.k == RK::Fonts ? 17 : (r.k == RK::Links ? 15 : (r.k == RK::Slider ? 17 : kRowH)));
    // menus breathe when the window has room (the same rhythm in ···, CC and audio)
    if (panel_ == Panel::More || panel_ == Panel::CC || panel_ == Panel::Audio) rh += h_ / s_ < 230 ? 0 : 5;
    if (r.k == RK::Hint && panel_ == Panel::More) rh += 3;  // a section starts: a little air above its name
    if (r.k == RK::SpeedBar) rh = 17;
    if (r.k == RK::Chips) rh = 18;
    if (r.k == RK::Brand) rh = 46;
    if (r.k == RK::Credit) rh = 16;
    if (r.k == RK::Profile) rh = 20;
    if (r.k == RK::Info) rh = 13;
    if (r.k == RK::CloseHeader) rh = 18;
    r.r = {2 * s, y, pw_ - 4 * s, rh * s};
    r.parts.clear();
    int x0 = r.r.x, x1 = r.r.r();
    if (r.k == RK::Slider) {
      // the whole strip around the track is clickable, not only the handle
      int bx = x0 + (slider_label_w() + (icon_grid() ? 17 : 0)) * s, bw = (x1 - 32 * s) - bx;
      r.parts.push_back({bx - 6 * s, r.r.y, bw + 12 * s, r.r.h});
    } else if (r.k == RK::Stepper) {
      int vw = text_w("+0.0s", s);
      int px = x1 - 4 * s - 9 * s;
      int vx = px - 3 * s - vw;
      int mxx = vx - 3 * s - 9 * s;
      r.parts.push_back({mxx, r.r.y, 9 * s, r.r.h});
      r.parts.push_back({px, r.r.y, 9 * s, r.r.h});
    } else if (r.k == RK::Fonts) {
      int cw = (r.r.w - 4 * s) / 3 / s * s;
      for (int i = 0; i < 3; ++i) r.parts.push_back({x0 + 2 * s + i * cw, r.r.y + s, cw, r.r.h - 2 * s});
    } else if (r.k == RK::Segmented) {
      // cells as wide as their words plus an even share of the room left: equal breathing
      int n = std::max(1, (int)r.segs.size());
      int words = 0;
      for (auto& sg : r.segs) words += text_w(sg, s);
      int room = std::max(0, (int)((r.r.w - 4 * s) - words)) / n;
      int cx0 = x0 + 2 * s;
      for (int i = 0; i < n; ++i) {
        int cw = (text_w(r.segs[i], s) + room) / (int)s * (int)s;
        if (i == n - 1) cw = x1 - 2 * s - cx0;
        r.parts.push_back({cx0, r.r.y + s, cw, r.r.h - 2 * s});
        cx0 += cw;
      }
    } else if (r.k == RK::SpeedBar) {
      // - · track · +   (the strip around the track is all slider)
      Rect minus{x0 + 2 * s, r.r.y + 2 * s, 13 * s, 13 * s}, plus{x1 - 15 * s, r.r.y + 2 * s, 13 * s, 13 * s};
      r.parts.push_back(minus);
      r.parts.push_back({minus.r() + 2 * s, r.r.y, plus.x - minus.r() - 4 * s, r.r.h});
      r.parts.push_back(plus);
    } else if (r.k == RK::Chips) {
      int n = std::max(1, (int)r.segs.size());
      int cw = (r.r.w - 4 * s - (n - 1) * 2 * s) / n / s * s;
      int left = x0 + 2 * s + (r.r.w - 4 * s - (n * cw + (n - 1) * 2 * s)) / 2 / s * s;
      for (int i = 0; i < n; ++i) r.parts.push_back({left + i * (cw + 2 * s), r.r.y + 2 * s, cw, 14 * s});
    } else if (r.k == RK::Links) {
      int lx = x0 + 4 * s;
      for (auto& sg : r.segs) {
        int lw = text_w(sg, s) + 8 * s;
        r.parts.push_back({lx, r.r.y + s, lw, r.r.h - 2 * s});
        lx += lw + 4 * s;
      }
    }
    y += rh * s;
  }
  ph_ = y + 4 * s;
  panel_dirty_ = true;
}

Rect PlayView::panel_rect() const {
  Rect d{(int)std::lround(px_.x), (int)std::lround(py_.x), dw_, dh_};
  if (panel_ == Panel::About) return {(w_ - pw_) / 2, (h_ - ph_) / 2, pw_, ph_};
  if (panel_ == Panel::FileInfo) {
    int margin = 8 * s_;
    int y = std::min((int)(26 * s_), std::max(margin, h_ - ph_ - margin));
    return {std::max(0, w_ - pw_ - margin), std::max(0, y), pw_, ph_};
  }
  if (panel_ == Panel::Style) {
    // beside the picture, never on top of the subtitle being edited
    // tries the middle, then the top, then just above the dock: the first place that touches
    // neither the dock nor the subtitle wins. Frozen while the subtitle is being dragged.
    // decided when the editor opens (or after the subtitle is moved), then it never moves under the cursor
    if (style_y_ >= 0) return {style_x_, style_y_, pw_, ph_};
    int m = kMargin * s_;
    auto area = [](const Rect& a, const Rect& b) {
      int w = std::min(a.r(), b.r()) - std::max(a.x, b.x), h = std::min(a.b(), b.b()) - std::max(a.y, b.y);
      return (w > 0 && h > 0) ? (long)w * h : 0L;
    };
    Rect sub = sub_rect().grow(4 * s_);
    int xs[2] = {std::max(0, w_ - pw_ - m), m};  // right side first, then left
    double ys[6] = {(h_ - ph_) / 2 - 10 * s_, (double)m, sub.b() + 2 * s_, sub.y - ph_ - 2 * s_, d.y - ph_ - 4 * s_, d.b() + 4 * s_};
    long best_cost = -1;
    Rect best{xs[0], 0, pw_, ph_};
    for (int xi : xs)
      for (int yi : ys) {
        Rect r{xi / s_ * s_, std::clamp(yi, 0, std::max(0, h_ - ph_)) / s_ * s_, pw_, ph_};
        long cost = area(r, sub) * 4 + area(r, d) * 2;  // hiding the subtitle is worst
        if (best_cost < 0 || cost < best_cost) { best_cost = cost; best = r; }
        if (cost == 0) goto done;
      }
  done:
    style_x_ = best.x;
    style_y_ = best.y;
    return best;
  }
  if (panel_ == Panel::Context) {
    int x = std::clamp(ctx_x_, 0, std::max(0, w_ - pw_)) / s_ * s_;
    int y = ctx_y_ + ph_ > h_ ? ctx_y_ - ph_ : ctx_y_;
    y = std::clamp(y, 0, std::max(0, h_ - ph_)) / s_ * s_;
    return {x, y, pw_, ph_};
  }
  if (panel_ == Panel::None) return closing_rect_;
  int bx = d.x + d.w / 2;
  for (auto& hb : hots_) if (hb.id == panel_btn_) bx = d.x + hb.r.cx();
  // centred on its button, but never past the dock's edges: one clean silhouette
  double want = std::clamp<double>(bx - pw_ / 2, d.x, std::max(d.x, d.r() - pw_));
  int x = std::clamp<double>(want, kMargin * s_ / 2, std::max(kMargin * s_ / 2, w_ - pw_ - kMargin * s_ / 2));
  if (panel_btn_ == B_NONE) x = std::clamp(d.x + d.w - pw_, 0, std::max(0, w_ - pw_));
  bool above = d.y + d.h / 2 > h_ / 2;
  int gap = 4 * s_;
  int y = above ? d.y - gap - ph_ : d.b() + gap;
  y = std::clamp(y, 0, std::max(0, h_ - ph_));
  x = x / s_ * s_; y = y / s_ * s_;
  return {x, y, pw_, ph_};
}

void PlayView::render_panel() {
  const Palette& p = P();
  double s = s_;
  Canvas& c = panel_base_;
  c.resize(pw_, ph_);
  draw_panel(c, 0, 0, pw_ / s, ph_ / s, s, kPanelProfile, p.bg, kPanelFill, p.edge, 0.95);
  int act_row = kb_ ? pfocus_ : phover_;
  const int text_col = icon_grid() ? 23 : 6;  // with any icon, every label starts on the same column
  for (int i = 0; i < (int)rows_.size(); ++i) {
    Row& r = rows_[i];
    bool hot = act_row == i;
    double ha = i < (int)phov_.size() ? ease_out(phov_[i]) : (hot ? 1.0 : 0.0);
    int ty = r.r.y + (r.r.h - 12 * s) / 2 + s;  // text top, cap centered
    switch (r.k) {
      case RK::CloseHeader:
        if (ha > 0) c.cornered(r.r, s, p.hover, ha);
        text(c, r.label, r.r.x + 6 * s, ty, s, p.fg);
        c.bitmap(bmp("x-small"), r.r.r() - 12 * s, r.r.y + 6 * s, s, p.dim);
        break;
      case RK::Info: {
        int label_width = r.label == "arquivo" || r.right.empty() ? 0 : 68 * s;
        if (label_width) text(c, r.label, r.r.x + 6 * s, ty, s, p.dim);
        std::string value = r.right.empty() ? r.label : r.right;
        value = fit_middle(value, std::max(0, r.r.w - (int)(12 * s) - label_width), s);
        text(c, value, r.r.x + 6 * s + label_width, ty, s, p.fg);
        break;
      }
      case RK::Brand:
        text(c, "Cue", (pw_ - text_w("Cue", 2 * s)) / 2, r.r.y + 6 * s, 2 * s, p.fg);
        {
          const std::string line = "v" CUE_VERSION_SHORT " · .tinypeep";
          text(c, line, (pw_ - text_w(line, s)) / 2, r.r.y + 31 * s, s, p.dim);
        }
        break;
      case RK::Credit: {
        const std::string left = "made with", right = "by";
        int width = text_w(left, s) + text_w(right, s) + 15 * s;
        int x = (pw_ - width) / 2;
        text(c, left, x, ty, s, p.dim);
        x += text_w(left, s) + 4 * s;
        // Draw the heart directly on the art grid, without a font or image dependency.
        c.cells(".##.##." "#######" "#######" ".#####." "..###.." "...#...", 7, 6,
                x, ty + 2 * s, s, mix(p.fg, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * 0.85));
        text(c, right, x + 11 * s, ty, s, p.dim);
        break;
      }
      case RK::Profile:
        if (ha > 0) c.cornered(r.r, s, p.hover, ha);
        text(c, r.label, (pw_ - text_w(r.label, s)) / 2, ty, s,
             mix(r.label == "Gabriel Benzoni" ? p.fg : p.dim, p.pixel, ha));
        if (hot) c.fill((pw_ - text_w(r.label, s)) / 2, ty + 10 * s, text_w(r.label, s), s, p.dim);
        break;
      case RK::Separator:
        c.fill(r.r.x + 4 * s, r.r.y + 2 * s, r.r.w - 8 * s, s, p.edge);
        break;
      case RK::Hint:
        text(c, r.label, r.r.x + text_col * s, ty + (panel_ == Panel::More ? 2 * s : 0), s, mix(p.faint, p.dim, 0.55));
        if (!r.right.empty()) text(c, r.right, r.r.r() - 6 * s - text_w(r.right, s), ty, s, p.fg);
        break;
      case RK::Header: {
        if (ha > 0) c.cornered(r.r, s, p.hover, ha);
        action_icon(c, "cue:return", {r.r.x + 3 * s, r.r.y, 12 * s, r.r.h}, s, mix(p.dim, p.fg, ha));
        text(c, r.label, r.r.x + 23 * s, ty, s, p.fg);
        break;
      }
      case RK::Toggle: {
        if (ha > 0) c.cornered(r.r, s, p.hover, ha);
        double k = std::clamp(sw_.x, -0.15, 1.15);   // overshoots a hair, like a real switch
        double kc = clamp01(k);
        if (r.icon) action_icon(c, r.icon, {r.r.x + 3 * s, r.r.y, 12 * s, r.r.h}, s, mix(mix(p.dim, p.fg, kc), p.fg, ha));
        text(c, r.label, r.r.x + text_col * s, ty, s, mix(mix(p.dim, p.fg, kc), p.fg, ha));
        int sx = r.r.r() - 5 * s - 13 * s, sy = (r.r.y + (r.r.h - 7 * s) / 2) / s * s;
        Rect tr{sx, sy, 13 * s, 7 * s};
        Rgb on = mix(p.fg, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * 0.85);
        c.cornered(tr, s, mix(p.faint, on, kc), 1.0);
        if (kc < 1) c.cornered(tr.grow(-s), s, mix(p.bg, on, kc), 1.0);  // off: an outlined track
        int kx = sx + (int)std::lround((1 + 7 * k) * s);
        c.fill(kx, sy + s, 4 * s, 5 * s, mix(mix(p.dim, p.fg, ha), p.bg, kc));
        break;
      }
      case RK::Option: case RK::Action: case RK::Submenu: {
        if (ha > 0) c.cornered(r.r, s, p.hover, ha);
        Rgb on_col = r.enabled ? p.fg : p.dim;
        if (r.checked) action_icon(c, "check", {r.r.x + 3 * s, r.r.y, 12 * s, r.r.h}, s, r.enabled ? p.fg : p.faint);
        if (r.icon) action_icon(c, r.icon, {r.r.x + 3 * s, r.r.y, 12 * s, r.r.h}, s, mix(p.dim, p.fg, ha));
        text(c, r.label, r.r.x + 23 * s, ty, s,
             r.checked ? on_col : mix(r.enabled ? p.dim : p.faint, p.fg, ha));
        if (!r.right.empty())
          text(c, r.right, r.r.r() - (r.k == RK::Submenu ? 13 : 5) * s - text_w(r.right, s), ty, s, p.faint);
        if (r.k == RK::Submenu) c.bitmap(bmp("chev-right5"), r.r.r() - 8 * s, r.r.y + (r.r.h - 5 * s) / 2 / s * s, s, mix(p.dim, p.fg, ha));
        break;
      }
      case RK::Stepper: {
        if (r.icon) action_icon(c, r.icon, {r.r.x + 3 * s, r.r.y, 12 * s, r.r.h}, s, p.dim);
        text(c, r.label, r.r.x + (r.icon ? 23 : 6) * s, ty, s, p.dim);
        for (int k = 0; k < 2; ++k) {
          bool ph = hot && ppart_ == k;
          if (ph) c.cornered(r.parts[k], s, p.hover);
          const BitmapDef* b = bmp(k == 0 ? "minus" : "plus");
          c.bitmap(b, r.parts[k].x + 2 * s, r.parts[k].y + (r.parts[k].h - 5 * s) / 2 / s * s, s, ph ? p.fg : p.dim);
        }
        int vw = text_w(r.right, s);
        int vx = r.parts[0].r() + (r.parts[1].x - r.parts[0].r() - vw) / 2;
        text(c, r.right, vx, ty, s, std::abs(in.sub_delay) > 0.04 ? p.fg : p.dim);
        break;
      }
      case RK::Slider: {
        text(c, r.label, r.r.x + text_col * s, ty, s, !r.enabled ? p.faint : (hot ? p.fg : p.dim));
        Rect bar = r.parts[0];
        int bx = bar.x + 6 * s, bw = bar.w - 12 * s, by = (r.r.cy() / s - 1) * s;
        double f = clamp01((r.value - r.minv) / (r.maxv - r.minv));
        int fw = (int)std::lround(bw * f / s) * s;
        bool live = (hot || slider_row_ == i) && r.enabled;
        c.fill(bx, by, bw, 2 * s, r.enabled ? p.faint : p.edge);  // track, 2 px of art
        if (r.enabled) {
          c.fill(bx, by, fw, 2 * s, live ? mix(p.fg, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * 0.85) : p.dim);
          int hw = 3, hh = live ? 11 : 9;                          // handle, 3 × 9 (11 while active)
          int hx = std::clamp(bx + fw - s, bx - s, bx + bw - 2 * s);
          c.fill(hx - s, by + s - (hh / 2) * s, hw * s, hh * s, live ? p.pixel : p.fg);
        }
        text(c, r.right, r.r.r() - 5 * s - text_w(r.right, s), ty, s, p.dim);
        break;
      }
      case RK::Fonts: {
        for (int k = 0; k < 3; ++k) {
          const Rect& cell = r.parts[k];
          bool sel = r.seg == k, ph = hot && ppart_ == k;
          if (sel) c.cornered(cell, s, p.hover);
          Rgb col = sel ? p.fg : (ph ? p.fg : p.dim);
          int cy = cell.y + (cell.h - 12 * s) / 2;
          if (k == 0) {
            int tw = text_w(kSubFontLabel[0], s);
            text(c, kSubFontLabel[0], cell.x + (cell.w - tw) / 2, cy, s, col);
          } else {
            const MaskDef* m = label_mask(kSubFontLabel[k], s);
            if (m) c.mask(m, cell.x + (cell.w - m->w) / 2, cy, col);
          }
          if (ph && !sel) c.fill(cell.x + 4 * s, cell.b() - s, cell.w - 8 * s, s, p.faint);
        }
        break;
      }
      case RK::Segmented: {
        for (int k = 0; k < (int)r.segs.size(); ++k) {
          const Rect& cell = r.parts[k];
          bool sel = r.seg == k, ph = hot && ppart_ == k;
          if (sel) c.cornered(cell, s, p.hover);
          int tw = text_w(r.segs[k], s);
          text(c, r.segs[k], cell.x + (cell.w - tw) / 2, ty, s, sel || ph ? p.fg : p.dim);
        }
        break;
      }
      case RK::SpeedBar: {
        Rgb acc = mix(p.fg, in.accent, std::clamp(in.accent_k, 0.0, 1.0) * 0.85);
        for (int k : {0, 2}) {
          bool ph = hot && ppart_ == k;
          const Rect& b = r.parts[k];
          c.cornered(b, s, p.hover, ph ? 1.0 : 0.55);
          const BitmapDef* ic = bmp(k == 0 ? "minus" : "plus");
          c.bitmap(ic, b.x + (b.w - 5 * s) / 2 / s * s, b.y + (b.h - 5 * s) / 2 / s * s, s, ph ? p.fg : p.dim);
        }
        Rect bar = r.parts[1];
        int bx = bar.x + 4 * s, bw = bar.w - 8 * s, by = (r.r.cy() / s - 1) * s;
        double f = clamp01((r.value - r.minv) / (r.maxv - r.minv));
        int fw = (int)std::lround(bw * f / s) * s;
        bool live = (hot && ppart_ == 1) || slider_row_ == i || (kb_ && pfocus_ == i);
        c.fill(bx, by, bw, 2 * s, p.faint);
        c.fill(bx, by, fw, 2 * s, live ? acc : p.fg);
        // a notch at 1x, the point people come back to (the handle sticks to it)
        int n1 = bx + (int)std::lround(bw * (1.0 - r.minv) / (r.maxv - r.minv) / s) * s;
        c.fill(n1, by + 3 * s, s, s, p.faint);
        int hh = live ? 9 : 7;
        int hx = std::clamp(bx + fw - s, bx - s, bx + bw - 2 * s);
        c.fill(hx - s, by + s - (hh / 2) * s, 3 * s, hh * s, live ? p.pixel : p.fg);
        break;
      }
      case RK::Chips: {
        for (int k = 0; k < (int)r.segs.size(); ++k) {
          const Rect& cell = r.parts[k];
          bool sel = r.seg == k, ph = hot && ppart_ == k;
          c.cornered(cell, s, p.hover, sel ? 1.0 : (ph ? 0.85 : 0.4));
          int tw = text_w(r.segs[k], s);
          text(c, r.segs[k], cell.x + (cell.w - tw) / 2 / s * s, cell.y + (cell.h - 12 * s) / 2 / s * s + s, s,
               sel || ph ? p.fg : p.dim);
        }
        break;
      }
      case RK::Links: {
        for (int k = 0; k < (int)r.segs.size(); ++k) {
          const Rect& cell = r.parts[k];
          if (k == 0 && ack_now_) {  // Omni ack: the action becomes a check for 1.3 s
            c.bitmap(bmp("check"), cell.x + 4 * s, cell.y + (cell.h - 5 * s) / 2 / s * s, s, p.fg);
            text(c, "salvo", cell.x + 14 * s, cell.y + (cell.h - 12 * s) / 2 + s, s, p.fg);
            continue;
          }
          bool on = (r.seg >> k) & 1;
          bool ph = hot && ppart_ == k && on;
          if (ph) c.cornered(cell, s, p.hover);
          text(c, r.segs[k], cell.x + 4 * s, cell.y + (cell.h - 12 * s) / 2 + s, s, ph ? p.fg : (on ? p.dim : p.faint));
        }
        break;
      }
    }
  }
  panel_dirty_ = false;
  ++panel_gen_;
}

void PlayView::open_panel(Panel pn, int btn, double now) {
  bool was = panel_ != Panel::None;
  style_y_ = -1;  // re-place the style editor each time it opens
  panel_ = pn;
  panel_btn_ = btn;
  phover_ = -1; ppart_ = -1; slider_row_ = -1;
  build_rows();
  layout_rows();
  sw_.snap(switch_on() ? 1 : 0);
  pfocus_ = -1;
  if (kb_) for (int i = 0; i < (int)rows_.size(); ++i) if (selectable(rows_[i])) { pfocus_ = i; break; }
  if (!was) { pvis_.v = 0; prise_.snap(5); }
  else { prise_.snap(2); }               // switching CC <-> estilo: the new content rises in
  prise_.target = 0;
  content_t0_ = now;
  pvis_.target = 1;
  dock_dirty_ = true;
  activity(now);
}

void PlayView::close_panel() {
  if (panel_ == Panel::None) return;
  panel_ = Panel::None;
  panel_btn_ = B_NONE;
  pvis_.target = 0;
  slider_row_ = -1;
  sub_hover_ = false;
  dock_dirty_ = true;
}

int PlayView::hit_panel(int x, int y, int* part) const {
  if (panel_ == Panel::None) return -1;
  Rect pr = panel_rect();
  if (!pr.contains(x, y)) return -1;
  int lx = x - pr.x, ly = y - pr.y;
  for (int i = 0; i < (int)rows_.size(); ++i) {
    if (!rows_[i].r.contains(lx, ly)) continue;
    if (part) {
      *part = -1;
      for (int k = 0; k < (int)rows_[i].parts.size(); ++k)
        if (rows_[i].parts[k].contains(lx, ly)) *part = k;
    }
    return i;
  }
  return -2;  // inside the panel, on padding
}

void PlayView::style_changed(bool live) {
  in.style.size = std::clamp(in.style.size, 24, 80);
  act(live ? PA::StyleLive : PA::Style);
  if (panel_ == Panel::Style) {
    int keep_f = pfocus_;
    build_rows();
    layout_rows();
    pfocus_ = std::min(keep_f, (int)rows_.size() - 1);
  }
}

void PlayView::slider_set(int row, int x, bool live) {
  Row& r = rows_[row];
  Rect pr = panel_rect();
  if (r.id == R_SPEED_BAR) {
    Rect bar = r.parts[1];
    int bx = pr.x + bar.x + 4 * s_, bw = bar.w - 8 * s_;
    double v = r.minv + clamp01((x - bx) / (double)std::max(1, bw)) * (r.maxv - r.minv);
    if (std::fabs(v - 1.0) < 0.06) v = 1.0;  // normal speed is easy to land back on
    set_speed(v);
    return;
  }
  Rect bar = r.parts[0];
  int bx = pr.x + bar.x + 6 * s_, bw = bar.w - 12 * s_;
  double f = clamp01((x - bx) / (double)std::max(1, bw));
  double v = r.minv + f * (r.maxv - r.minv);
  if (r.id == R_SIZE) in.style.size = (int)std::lround(v / 2) * 2;
  if (r.id == R_TEXT) in.style.text_op = (int)std::lround(v / 5) * 5;
  if (r.id == R_BG) in.style.bg_op = (int)std::lround(v / 5) * 5;
  if (r.id == R_OUTLINE) in.style.outline = (int)std::lround(v);
  if (r.id == R_LINEGAP) in.style.line_gap = (int)std::lround(v);
  if (r.id == R_TRACK) in.style.tracking = (int)std::lround(v);
  if (r.id == R_AMBIENT_STR) {
    in.ambient_strength = std::clamp(std::round(v) / 100.0, 0.05, 1.0);
    act(PA::AmbientStrength, in.ambient_strength, live ? 1 : 0);
    build_rows();
    layout_rows();
    return;
  }
  style_changed(live);
}

void PlayView::panel_adjust(int row, int dir, double now) {
  (void)now;
  Row& r = rows_[row];
  switch (r.id) {
    case R_SIZE: in.style.size = std::clamp(in.style.size + 2 * dir, 28, 72); style_changed(false); break;
    case R_TEXT: in.style.text_op = std::clamp(in.style.text_op + 5 * dir, 10, 100); style_changed(false); break;
    case R_BG: in.style.bg_op = std::clamp(in.style.bg_op + 5 * dir, 0, 100); style_changed(false); break;
    case R_LINEGAP: in.style.line_gap = std::clamp(in.style.line_gap + dir, kLineGapMin, kLineGapMax); style_changed(false); break;
    case R_TRACK: in.style.tracking = std::clamp(in.style.tracking + dir, -3, 10); style_changed(false); break;
    case R_OUTLINE: if (r.enabled) { in.style.outline = std::clamp(in.style.outline + dir, 0, 6); style_changed(false); } break;
    case R_FONTS: in.style.font = (in.style.font + dir + 3) % 3; style_changed(false); break;
    case R_ASS: in.style.ass_mode = dir > 0 ? 1 : 0; style_changed(false); break;
    case R_DELAY: act(PA::SubDelay, 0.1 * dir); break;
    case R_SUB_TOGGLE: if ((dir > 0) != r.checked) { act(PA::SubVisible, dir > 0 ? 1 : 0); sw_.target = dir > 0 ? 1 : 0; } break;
    case R_AMBIENT: if ((dir > 0) != r.checked) { act(PA::Ambient, dir > 0 ? 1 : 0); sw_.target = dir > 0 ? 1 : 0; } break;
    case R_CONTROLS_FREE: act(PA::ControlsFree, dir > 0 ? 1 : 0); break;
    case R_UISCALE: act(PA::UiScale, next_ui_size(in.ui_scale, dir)); break;
    case R_SPEED_BAR: set_speed(in.speed + 0.05 * dir); break;
    case R_SPEED_CHIPS: {
      int i = r.seg;
      if (i < 0) {  // between stops: the next one in that direction
        i = dir > 0 ? 5 : 0;
        for (int k = 0; k < 6; ++k)
          if (dir > 0 ? kSpeedChips[k] > in.speed + 0.001 : kSpeedChips[k] < in.speed - 0.001) { i = k; if (dir > 0) break; }
      } else {
        i = std::clamp(i + dir, 0, 5);
      }
      set_speed(kSpeedChips[i]);
      break;
    }
    case R_AMBIENT_STR:
      if (r.enabled) {
        in.ambient_strength = std::clamp(in.ambient_strength + 0.05 * dir, 0.05, 1.0);
        act(PA::AmbientStrength, in.ambient_strength, 0);
        build_rows();
        layout_rows();
      }
      break;
    default: break;
  }
}

void PlayView::panel_click(int row, int part, int x, double now) {
  if (row < 0 || row >= (int)rows_.size()) return;
  Row& r = rows_[row];
  switch (r.id) {
    case R_SUB_TOGGLE: act(PA::SubVisible, r.checked ? 0 : 1); sw_.target = r.checked ? 0 : 1; break;
    case R_SUB_TRACK: act(PA::SelectSub, 0, r.track); break;  // picking a track also turns it on
    case R_ADD_SUB: act(PA::AddSubFile); close_panel(); break;
    case R_STYLE: open_panel(Panel::Style, panel_btn_, now); break;
    case R_BACK: open_panel(Panel::CC, panel_btn_, now); break;
    case R_DELAY: if (part >= 0) act(PA::SubDelay, part == 0 ? -0.1 : 0.1); break;
    case R_FONTS: if (part >= 0) { in.style.font = part; style_changed(false); } break;
    case R_ASS: if (part >= 0) { in.style.ass_mode = part; style_changed(false); } break;
    case R_SIZE: case R_TEXT: case R_BG: case R_OUTLINE: case R_LINEGAP: case R_TRACK: case R_AMBIENT_STR:
      if (part == 0 && r.enabled) { slider_row_ = row; slider_set(row, x, true); }
      break;
    case R_LINKS:
      if (part >= 0 && part < (int)r.segs.size() && ((r.seg >> part) & 1)) {
        if (r.segs[part] == "resetar") { in.style = SubStyle{}; style_changed(false); }
        else { act(PA::StyleDefault); ack_until_ = now + 1.3; }
      }
      break;
    case R_AUDIO_TRACK: act(PA::SelectAudio, 0, r.track); break;
    case R_SPEED: open_panel(Panel::Speed, panel_btn_, now); break;
    case R_BACK_MORE: open_panel(Panel::More, panel_btn_, now); break;
    case R_ABOUT: open_panel(Panel::About, panel_btn_, now); break;
    case R_LINKEDIN: act(PA::OpenLinkedIn); break;
    case R_GITHUB: act(PA::OpenGitHub); break;
    case R_FILE_INFO: act(PA::FileInfo); open_panel(Panel::FileInfo, B_NONE, now); break;
    case R_DISMISS: close_panel(); break;
    case R_LICENSES: act(PA::OpenLicenses); break;
    case R_UPDATE: act(PA::Update); break;
    case R_AUDIO_ROW: open_panel(Panel::Audio, panel_btn_, now); break;
    case R_SPEED_OPT: act(PA::Speed, r.opt); in.speed = r.opt; layout_dock(); dock_dirty_ = true; close_panel(); break;
    case R_SPEED_BAR:
      if (part == 0 || part == 2) set_speed(in.speed + (part == 0 ? -0.05 : 0.05));
      else if (part == 1) { slider_row_ = row; slider_set(row, x, true); }
      break;
    case R_SPEED_CHIPS: if (part >= 0 && part < 6) set_speed(kSpeedChips[part]); break;
    case R_AMBIENT: act(PA::Ambient, r.checked ? 0 : 1); sw_.target = r.checked ? 0 : 1; break;
    case R_CONTROLS_FREE: if (part >= 0) act(PA::ControlsFree, part == 1 ? 1 : 0); break;
    case R_UISCALE: if (part >= 0) act(PA::UiScale, ui_size_choice(part)); break;
    case R_FRAME: act(PA::SaveFrame, 0); close_panel(); break;
    case R_OPEN: act(PA::OpenFile); close_panel(); break;
    case R_CLOSE: close_on_release_ = true; close_panel(); break;  // fires on release, never with a button held
    default: break;
  }
  panel_dirty_ = true;
}

// ── subtitle position ───────────────────────────────────────────────
// The whole horizontal strip around the subtitle is a handle: easy to grab.
// Where the hand appears: the subtitle's own block with a small margin, never the whole width.
Rect PlayView::sub_band() const {
  Rect r = sub_rect();
  int px = 4 * s_, py = 3 * s_;
  return {r.x - px, r.y - py, r.w + 2 * px, r.h + 2 * py};
}

// Where libass draws each line, computed the same way it does (see sub_metrics.h):
//   line pitch = font size + line spacing; block height = n lines minus one spacing;
//   a bottom subtitle's top = (height - bottom margin - block height) * sub-pos / 100.
bool PlayView::sub_line_rects(double pos, std::vector<Rect>& out) const {
  out.clear();
  if (in.sub_text.empty() || (in.kind == SubKind::Ass && in.style.ass_mode == 0) || in.kind == SubKind::Bitmap)
    return false;
  double k = h_ / 720.0;                       // the engine's sizes are given at a 720 px window
  double fs = in.style.size * k;
  double em = in.style.size / 44.0;            // spacing follows the size (sub_style.cpp)
  double gap = in.style.line_gap * em * k, track = in.style.tracking * em * k;
  double margin = 22 * k;                      // sub-margin-y default
  int n = (int)in.sub_text.size();
  double bh = n * fs + (n - 1) * gap;
  double top = (h_ - margin - bh) * pos / 100.0;
  int font = std::clamp(in.style.font, 0, 2);
  for (int i = 0; i < n; ++i) {
    double lw = sub_line_width(in.sub_text[i], font, fs, track);
    double y = top + i * (fs + gap);
    out.push_back({(int)std::lround(w_ / 2.0 - lw / 2.0), (int)std::lround(y), (int)std::lround(lw), (int)std::lround(fs)});
  }
  return true;
}

// Each line is its own target: the hand never shows beside a short line or in the empty
// corner of a two-line block.
bool PlayView::sub_hit(int x, int y) const {
  std::vector<Rect> rs;
  if (!sub_line_rects(sub_drag_ ? in.style.pos : lifted_pos(), rs)) return sub_band().contains(x, y);
  for (const Rect& r : rs)
    if (r.grow(3 * s_).contains(x, y)) return true;
  return false;
}

Rect PlayView::sub_rect() const { return sub_rect_at(sub_drag_ ? in.style.pos : lifted_pos()); }

Rect PlayView::sub_rect_at(double pos) const {
  std::vector<Rect> rs;
  if (sub_line_rects(pos, rs) && !rs.empty()) {
    Rect u = rs[0];
    for (auto& r : rs) {
      int x0 = std::min(u.x, r.x), y0 = std::min(u.y, r.y), x1 = std::max(u.r(), r.r()), y1 = std::max(u.b(), r.b());
      u = {x0, y0, x1 - x0, y1 - y0};
    }
    return u.grow((int)std::lround(4 * h_ / 720.0));  // the background box pads it a little
  }
  double fs = in.style.size * h_ / 720.0;
  int lines = std::max(1, in.sub_lines);
  double em = fs / 44.0;  // spacing follows the size, like the engine applies it
  double bh = lines * fs * 1.15 + std::max(0, lines - 1) * in.style.line_gap * em * h_ / 720.0 + fs * 0.2;
  double bw = std::clamp(in.sub_chars * (fs * 0.5 + in.style.tracking * em * h_ / 720.0) + fs * 0.4, fs * 3, w_ * 0.92);
  double bottom = pos / 100.0 * h_;
  return {(int)(w_ / 2 - bw / 2), (int)(bottom - bh), (int)bw, (int)bh};
}

double PlayView::snap_sub(double pos) const {
  const double snaps[3] = {kSubPosTop, kSubPosCenter, kSubPosBottom};
  for (double sn : snaps) {
    double d = pos - sn;
    if (std::fabs(d) < 3.0) return sn + d * smoothstep(0, 3.0, std::fabs(d));  // magnetic
  }
  return pos;
}

void PlayView::begin_sub_drag_for_preview(double now) {
  sub_drag_ = true;
  sub_start_pos_ = in.style.pos;
  activity(now);
  update_sub_layers();
}

void PlayView::update_sub_layers() {
  const Palette& p = P();
  double s = s_;
  bool show_guides = sub_drag_;
  const double snaps[3] = {kSubPosTop, kSubPosCenter, kSubPosBottom};
  for (int i = 0; i < 3; ++i) {
    Layer& L = layers[L_G0 + i];
    bool vis = show_guides;
    if (vis) {
      int lw = (int)(w_ * 0.56) / s * s;
      L.c.resize(lw, s);
      for (int x = 0; x < lw; x += 6 * s) L.c.fill(x, 0, 3 * s, s, p.faint, 0.55);  // only while dragging
      L.x = (w_ - lw) / 2 / s * s;
      L.y = (int)(snaps[i] / 100.0 * h_) / s * s;
    }
    if (vis != L.visible || vis) L.dirty = true;
    L.visible = vis;
  }
  // always draggable, never visually selected: the cursor (open / closed hand) is the only sign
  Layer& H = layers[L_HINT];
  bool hint = false;
  if (hint) {
    Rect r = sub_rect().grow(6 * s);
    r.x = r.x / s * s; r.y = r.y / s * s;
    int cw = r.w / s * s + 10 * s, ch = r.h / s * s;
    H.c.resize(cw, ch);
    Rgb col = sub_drag_ ? p.fg : p.dim;
    int L = 4 * s;
    int x0 = 10 * s, x1 = cw - s, y1 = ch - s;
    // corner brackets around the block, and a grip on the left
    H.c.fill(x0, 0, L, s, col); H.c.fill(x0, 0, s, L, col);
    H.c.fill(x1 - L + s, 0, L, s, col); H.c.fill(x1, 0, s, L, col);
    H.c.fill(x0, y1, L, s, col); H.c.fill(x0, y1 - L + s, s, L, col);
    H.c.fill(x1 - L + s, y1, L, s, col); H.c.fill(x1, y1 - L + s, s, L, col);
    H.c.bitmap(bmp("grip"), 2 * s, (ch - 5 * s) / 2 / s * s, s, col);
    H.x = r.x - 10 * s;
    H.y = r.y;
  }
  if (hint != H.visible || hint) H.dirty = true;
  H.visible = hint;
}

// ── composition ─────────────────────────────────────────────────────
// Re-scales and marks a layer for upload only when its content, alpha or
// position really changed. A still dock costs nothing.
static bool restage(Layer& L, const Canvas& base, uint64_t gen, double alpha, int x, int y) {
  int a = (int)std::lround(std::clamp(alpha, 0.0, 1.0) * 255);
  if (L.visible && L.lgen == gen && L.lsrc == &base && L.la == a && L.lx == x && L.ly == y) return false;
  // Moving a cached layer changes its placement, not its pixels.
  if (L.lgen != gen || L.lsrc != &base || L.la != a || L.c.px.empty()) {
    L.c = base;
    if (a < 255) L.c.scale_alpha(a / 255.0);
  }
  L.x = x; L.y = y;
  L.lgen = gen; L.lsrc = &base; L.la = a; L.lx = x; L.ly = y;
  L.visible = L.c.w > 0;
  L.dirty = true;
  return true;
}

static constexpr double kIrisDur = 0.36;

void PlayView::begin_cover() {
  iris_ = 1;
  iris_drawn_ = -1;
  iris_frame_ = -1;
  vis_.v = 0;
  vis_.target = 0;
}

void PlayView::start_iris(double now) {
  if (iris_ != 1) return;
  iris_ = 2;
  iris_t0_ = now;
  iris_frame_ = -1;
}

// The picture opens through a pixel-art ellipse with the window's proportions.
// Chunky cells (3 px of art), black outside; ends a little past the corners.
void PlayView::render_iris(double k) {
  Layer& L = layers[L_IRIS];
  if (L.c.w != w_ || L.c.h != h_) L.c.resize(w_, h_);
  const uint32_t black = 0xFF000000u;
  int cell = 3 * s_;
  double cx = w_ / 2.0, cy = h_ / 2.0;
  double rx = std::max(1.5 * cell, k * cx * 1.45), ry = std::max(1.5 * cell, k * cy * 1.45);
  for (int y0 = 0; y0 < h_; y0 += cell) {
    int y1 = std::min(h_, y0 + cell);
    double yc = (y0 + cell / 2.0) - cy;
    int a = w_, b = w_;  // [a, b) is open (transparent) on this band
    if (k > 0 && std::fabs(yc) < ry) {
      double half = rx * std::sqrt(1 - (yc / ry) * (yc / ry));
      int hx = (int)std::lround(half / cell) * cell;
      a = std::max(0, (int)std::lround(cx) - hx);
      b = std::min(w_, (int)std::lround(cx) + hx);
      if (a >= b) a = b = w_;
    }
    for (int y = y0; y < y1; ++y) {
      uint32_t* row = L.c.px.data() + (size_t)y * w_;
      if (a >= b) { std::fill(row, row + w_, black); continue; }
      std::fill(row, row + a, black);
      std::fill(row + a, row + b, 0u);
      std::fill(row + b, row + w_, black);
    }
  }
  L.x = L.y = 0;
  L.visible = true;
  L.dirty = true;
}

void PlayView::compose(double now) {
  const Palette& p = P();
  double s = s_;
  {
    Layer& IL = layers[L_IRIS];
    if (iris_ == 1) { if (iris_drawn_ != 0 || IL.c.w != w_ || IL.c.h != h_ || !IL.visible) { render_iris(0); iris_drawn_ = 0; } }
    else if (iris_ == 2) {
      double t = reduce_motion() ? 1.0 : std::clamp((now - iris_t0_) / kIrisDur, 0.0, 1.0);
      double k = 1 - std::pow(1 - t, 3);  // quick to open, soft landing
      // This is a full-window bitmap: rasterizing it at every input/property
      // event (or 120 Hz tick) starves playback, especially on a large window.
      int frame = (int)std::floor(std::max(0.0, now - iris_t0_) * 60);
      if (frame != iris_frame_ || IL.c.w != w_ || IL.c.h != h_) {
        render_iris(k);
        iris_drawn_ = k;
        iris_frame_ = frame;
      }
    } else if (IL.visible) { IL.visible = false; IL.dirty = true; IL.c = Canvas{}; }
  }
  // dock
  Layer& D = layers[L_DOCK];
  if (vis_.v > 0 && viewport_vis_.v > 0) {
    if (dock_dirty_) render_dock();
    Rect d = dock_rect();
    restage(D, dock_base_, dock_gen_, ease_out(vis_.v) * ease_out(viewport_vis_.v) * (1 - ease_out(edit_.x)), d.x, d.y);
  } else if (D.visible) { D.visible = false; D.dirty = true; D.lgen = 0; }

  // panel
  Layer& PL = layers[L_PANEL];
  if (pvis_.v > 0 && (panel_ != Panel::None || pvis_.target == 0)) {
    if (panel_ != Panel::None && panel_dirty_) render_panel();
    Rect pr = panel_rect();
    if (panel_ != Panel::None) closing_rect_ = pr;
    bool above = py_.x + dh_ / 2.0 > h_ / 2.0;
    double enter = ease_out((now - content_t0_) / tok::enter);
    restage(PL, panel_base_, panel_gen_, ease_out(pvis_.v) * std::max(0.0, vis_.v) * (0.35 + 0.65 * enter) * (1 - ease_out(edit_.x)), pr.x,
            pr.y + (int)std::lround(prise_.x * s) * (above ? 1 : -1));
  } else if (PL.visible) { PL.visible = false; PL.dirty = true; PL.lgen = 0; }

  // toast
  Layer& T = layers[L_TOAST];
  double ta = 0, tt = now - toast_t0_;
  if (!toast_text_.empty()) {
    if (tt < 0.14) ta = tt / 0.14;
    else if (tt < 0.14 + toast_hold_) ta = 1;
    else if (tt < 0.40 + toast_hold_) ta = 1 - (tt - 0.14 - toast_hold_) / 0.26;
    if (reduce_motion() && ta > 0) ta = 1;
  }
  toast_alpha_ = ta;
  if (ta > 0) {
    if (toast_dirty_ || T.c.w == 0) {
      const BitmapDef* ib = toast_icon_ ? bmp(toast_icon_) : nullptr;
      int iw = ib ? (ib->w + 4) * s : 0;
      int tw = text_w(toast_text_, s);
      int bw = toast_btn_ ? 19 * s : 0;  // divider + folder button
      int W = tw + iw + 14 * s + bw, H = 17 * s;
      T.c.resize(W, H);
      draw_panel(T.c, 0, 0, W / s, H / s, s, {1}, p.tip_bg, 0.92, p.edge, 1.0);
      int x = 7 * s;
      if (ib && !toast_icon_after_) { T.c.bitmap(ib, x, (H - ib->h * s) / 2 / s * s, s, p.fg); x += iw; }
      text(T.c, toast_text_, x, 3 * s, s, p.fg);
      x += tw + 4 * s;
      if (ib && toast_icon_after_) T.c.bitmap(ib, x, (H - ib->h * s) / 2 / s * s, s, p.fg);
      if (toast_btn_) {
        int dx = W - bw;
        T.c.fill(dx, 3 * s, s, H - 6 * s, p.edge);
        toast_btn_rect_ = {dx + 2 * s, 2 * s, 15 * s, H - 4 * s};
        if (toast_hot_) T.c.cornered(toast_btn_rect_, s, p.hover, 1.0);
        const BitmapDef* fb = bmp("folder");
        if (fb)
          T.c.bitmap(fb, toast_btn_rect_.x + (toast_btn_rect_.w - fb->w * s) / 2 / s * s,
                     toast_btn_rect_.y + (toast_btn_rect_.h - fb->h * s) / 2 / s * s, s, toast_hot_ ? p.fg : p.dim);
      }
      toast_base_ = T.c;
      toast_dirty_ = false;
      ++toast_gen_;
    }
    int tx = (w_ - toast_base_.w) / 2 / s * s, ty = kMargin * s + (int)std::lround((1 - ta) * -3) * s;
    toast_rect_ = {tx, ty, toast_base_.w, toast_base_.h};
    restage(T, toast_base_, toast_gen_, ta, tx, ty);
  } else if (T.visible) {
    T.visible = false; T.dirty = true; T.lgen = 0; toast_text_.clear();
    toast_btn_ = toast_hot_ = false;
  }

  // tiny centred feedback for keyboard actions and skips
  {
    Layer& FL = layers[L_FLASH];
    double ft = now - flash_t0_;
    double fa = ft < 0.07 ? ft / 0.07 : ft < 0.42 ? 1.0 : ft < 0.7 ? 1 - (ft - 0.42) / 0.28 : 0.0;
    if (reduce_motion() && fa > 0) fa = ft < 0.6 ? 1 : 0;
    if (fa > 0) {
      if (flash_base_.w == 0 || FL.lgen != flash_gen_) {
        int W = flash_icon_ >= 0 ? 21 * s : text_w(flash_text_, s) + 14 * s, H = 19 * s;
        flash_base_.resize(W, H);
        draw_panel(flash_base_, 0, 0, W / s, H / s, s, {1}, p.tip_bg, 0.9, p.edge, 1.0);
        if (flash_icon_ >= 0) draw_playpause(flash_base_, 7 * s, 6 * s, s, flash_icon_, p.fg, 1.0);
        else text(flash_base_, flash_text_, 7 * s, 4 * s, s, p.fg);
      }
      int rise = (int)std::lround((1 - std::min(1.0, ft / 0.12)) * 2) * s;
      restage(FL, flash_base_, flash_gen_, ease_out(fa), (w_ - flash_base_.w) / 2 / s * s,
              (h_ - flash_base_.h) / 2 / s * s + rise);
    } else if (FL.visible) { FL.visible = false; FL.dirty = true; FL.lgen = 0; }
  }

  // window controls (own buttons, Omni style), with the rest of the UI
  {
    Layer& WL = layers[L_WIN];
    chrome.s = s;
    chrome.vis.target = (vis_.target > 0 && !in.fullscreen && !sub_drag_) ? 1 : 0;
    if (chrome.vis.v > 0) {
      Rect a = chrome.area(w_);
      std::string sig = std::to_string(a.x) + "|" + std::to_string((int)(chrome.vis.v * 255)) + "|" +
                        std::to_string((int)(chrome.hov[0] * 50)) + std::to_string((int)(chrome.hov[1] * 50)) +
                        std::to_string((int)(chrome.hov[2] * 50)) + (chrome.maximized ? "m" : "");
      if (sig != chrome_sig_ || !WL.visible) {
        chrome_sig_ = sig;
        WL.c.resize(a.w, a.h);
        chrome.draw(WL.c, w_, a.x, a.y);
        WL.x = a.x; WL.y = a.y;
        WL.visible = true;
        WL.dirty = true;
      }
    } else if (WL.visible) { WL.visible = false; WL.dirty = true; chrome_sig_.clear(); }
  }

  // Back to home: icon only, with the same soft hover highlight.
  {
    Layer& BL = layers[L_BACK];
    if (back_vis_.v > 0) {
      double a = ease_out(back_vis_.v), h = ease_out(back_hov_);
      std::string sig = std::to_string((int)(a * 255)) + "|" + std::to_string((int)(h * 50)) + "|" + std::to_string(s);
      if (sig != back_sig_ || !BL.visible) {
        back_sig_ = sig;
        Rect br = back_rect();
        BL.c.resize(br.w, br.h);
        if (h > 0) BL.c.cornered({0, 0, br.w, br.h}, s, p.hover, a * h);
        action_icon(BL.c, "cue:return", {0, 0, br.w, br.h}, s, mix(p.dim, p.fg, h), a);
        BL.x = br.x; BL.y = br.y;
        BL.visible = true;
        BL.dirty = true;
      }
    } else if (BL.visible) { BL.visible = false; BL.dirty = true; back_sig_.clear(); }
  }

  // timeline tooltip, or the name of a button after 450 ms still (Omni)
  Layer& TP = layers[L_TIP];
  {
    static const char* names[] = {"", "volume", "", "voltar 15s", "", "avançar 15s", "legendas", "áudio", "", "mais"};
    bool volbar = vol_drag_ || (hover_ == B_VOLBAR);
    bool btn = (((hover_ > B_NONE && hover_ <= B_MORE) || hover_ == B_LOOP) && panel_btn_ != hover_ && !kb_) || volbar;
    double still = vol_drag_ ? 1e9 : now - hover_since_;
    if (btn && still > tok::tip_delay && vis_.v > 0.9) {
      std::string label = volbar ? std::to_string((int)std::lround(in.muted ? 0 : in.volume)) + "%"
                        : hover_ == B_PLAY ? (in.paused ? "tocar" : "pausar")
                        : hover_ == B_LOOP ? (in.loop ? "repetir: ligado" : "repetir")
                        : hover_ == B_FS ? (in.fullscreen ? "sair da tela cheia" : "tela cheia")
                        : hover_ == B_VOL ? (in.muted ? "tirar do mudo" : "mudo")
                        : names[hover_];
      double k = ease_out((still - tok::tip_delay) / tok::tip_in);
      Rect d = dock_rect(), br;
      for (auto& hb : hots_) if (hb.id == (volbar ? B_VOLBAR : hover_)) br = hb.r;
      if (volbar) {  // the value rides above the handle
        int bw = br.w - 4 * s, fw = (int)std::lround(bw * std::clamp((in.muted ? 0 : in.volume) / kVolMax, 0.0, 1.0) / s) * s;
        br = {br.x + 2 * s + fw - s, br.y, s, br.h};
      }
      int W = text_w(label, s) + 10 * s, H = 16 * s;
      int nx = std::clamp(d.x + br.cx() - W / 2, 0, std::max(0, w_ - W)) / s * s;
      bool top = d.y + d.h / 2 < h_ / 2;
      int ny = top ? d.b() + 3 * s : d.y - H - 3 * s;
      ny += (int)std::lround((1 - k) * 2 * s) * (top ? -1 : 1);  // slides 2 px of art into place
      std::string sig = label + "|" + std::to_string((int)(k * 20));
      if (sig != tip_text_ || nx != TP.x || ny != TP.y) {
        tip_text_ = sig;
        TP.c.resize(W, H);
        draw_panel(TP.c, 0, 0, W / s, H / s, s, {1}, p.tip_bg, 0.95, p.edge, 1.0);
        text(TP.c, label, 5 * s, 2 * s, s, p.fg);
        TP.c.scale_alpha(k);
        TP.x = nx; TP.y = ny;
        TP.visible = true;
        TP.dirty = true;
      }
      return;
    }
  }
  if (tipvis_.v > 0) {
    // the preview: fades and rises in, glides after the cursor, frames dissolve into each other
    double k = ease_out(tipvis_.v);
    if (tip_want()) tip_ts_ = fmt_time(scrubbing_ ? scrub_t_ : time_at(mx_), in.dur >= 3600);
    bool thumb = thumb_.w > 0;
    int W, H;
    tip_size(W, H);
    int pad = thumb ? 2 * s : 0;
    Rect dd = dock_rect();
    bool top = dd.y + dd.h / 2 < h_ / 2;
    int ny = top ? dd.b() + 3 * s : dd.y - H - 3 * s;
    if (ny < 0) ny = dd.b() + 3 * s;
    ny += (int)std::lround((1 - k) * 3 * s) * (top ? -1 : 1);
    int nx = (int)std::lround(tipx_.x);
    std::string sig = tip_ts_ + "|" + std::to_string(thumb_gen_) + "|" + std::to_string((int)(k * 40)) + "|" +
                      std::to_string((int)(thumb_xfade_ * 20));
    if (TP.visible && sig == tip_text_ && nx == TP.x && ny == TP.y) return;
    if (sig != tip_text_ || TP.c.w != W || TP.c.h != H) {
      tip_text_ = sig;
      TP.c.resize(W, H);
      draw_panel(TP.c, 0, 0, W / s, H / s, s, {1}, p.tip_bg, 0.96, p.edge, 1.0);
      if (thumb) {
        double xf = ease_out(thumb_xfade_);
        bool prev = xf < 1 && thumb_prev_.w == thumb_.w && thumb_prev_.h == thumb_.h;
        if (prev) TP.c.blit(thumb_prev_, pad, pad);
        TP.c.blit(thumb_, pad, pad, prev ? xf : 1.0);
      }
      int lw = text_w(tip_ts_, s);
      text(TP.c, tip_ts_, (W - lw) / 2 / s * s, thumb ? H - 13 * s : 2 * s, s, p.fg);
      if (k < 1) TP.c.scale_alpha(k);
    }
    TP.x = nx;
    TP.y = ny;
    TP.visible = true;
    TP.dirty = true;
  } else if (TP.visible) { TP.visible = false; TP.dirty = true; tip_text_.clear(); }
  if (viewport_transition_) {
    for(int i=0;i<L_COUNT;++i) if(i!=L_DOCK && layers[i].visible) {
      layers[i].visible=false; layers[i].dirty=true; layers[i].lgen=0;
    }
  }
}

bool PlayView::tip_want() const {
  return (hover_ == B_TIMELINE || scrubbing_) && vis_.v > 0.5 && in.dur > 0 && mx_ >= 0;
}

void PlayView::tip_size(int& W, int& H) const {
  double s = s_;
  bool thumb = thumb_.w > 0;
  int pad = thumb ? 2 * s : 0;
  W = thumb ? thumb_.w + 2 * pad : text_w(in.dur >= 3600 ? "0:00:00" : "00:00", s) + 10 * s;
  H = thumb ? thumb_.h + 2 * pad + 14 * s : 16 * s;
  W = (int)std::ceil(std::ceil(W / s) * s);
  H = (int)std::ceil(std::ceil(H / s) * s);

}

// ── public behaviour ────────────────────────────────────────────────
void PlayView::begin_viewport_transition(double now, bool instant) {
  viewport_transition_=true; viewport_vis_.target=0; glide_=false;
  pending_click_=press_=P_NONE; second_click_=false; last_click_t_=-10;
  close_panel(); pvis_.v=0; tip_on_=false; tipvis_.v=tipvis_.target=0;
  hover_=B_NONE; phover_=-1; back_hot_=false; chrome.hot=WindowChrome::None;
  scrubbing_=vol_drag_=dragging_dock_=drag_pending_=false;
  flash_t0_=-10; activity(now);
  if(reduce_motion() || instant) viewport_vis_.v=0;
}

void PlayView::end_viewport_transition(double now) {
  viewport_transition_=false; viewport_vis_.target=1;
  double x,y;place_target(x,y);px_.snap(x);py_.snap(y);
  // back in with the dock's own entrance: up from 8 px of art on a soft spring
  if(!reduce_motion()){ rise_.snap(8); rise_.target=0; }
  last_vis_target_=1;
  dock_dirty_=panel_dirty_=true; activity(now);
}

void PlayView::reset(double now) {
  close_panel();
  viewport_transition_=false; viewport_vis_.v=viewport_vis_.target=1;
  pvis_.v = 0;
  scrubbing_ = vol_drag_ = dragging_dock_ = drag_pending_ = sub_drag_ = false;
  press_ = pending_click_ = P_NONE;
  win_drag_ = close_on_release_ = swallow_up_ = second_click_ = false;
  last_click_t_ = -10;
  morph_.snap(in.paused ? 0 : 1);
  edit_.snap(0);
  subtitle_lift_.snap(0); last_lift_ = -1;
  style_mix_ = in.controls_free ? 1 : 0; style_motion_.snap(style_mix_); style_animating_ = false;
  layout_dock();
  activity(now);
}

void PlayView::changed(double /*now*/) {
  // the dock re-renders only when something visible on it changed
  bool hours = in.dur >= 3600;
  if (in.controls_free != layout_free_) {
    style_motion_.to(in.controls_free ? 1 : 0, .32);
    style_animating_ = !style_motion_.settled();
    layout_free_ = in.controls_free;
    if (!style_animating_) { style_mix_ = in.controls_free ? 1 : 0; resize(w_, h_, s_); }
    dock_state_valid_ = false;
  }
  int speed_w = text_w(speed_plain(in.speed), 1);  // the speed slot hugs its words
  // (never while the speed panel is open: its button must not slide under the slider)
  bool speed_moved = speed_w != layout_speed_w_ && panel_ != Panel::Speed;
  if (hours != layout_hours_ || in.audios.size() != layout_audio_count_ || speed_moved || layout_speed_w_ < 0) {
    layout_speed_w_ = speed_w;
    layout_dock();
    dock_dirty_ = true;
  }
  morph_.target = in.paused ? 0 : 1;
  // Compare display values directly. Formatting and allocating strings at video
  // frame rate used to happen even when the displayed second/pixel was unchanged.
  double shown = scrubbing_ ? scrub_t_ : in.time;
  if (!std::isfinite(shown) || shown < 0) shown = 0;
  double duration = std::isfinite(in.dur) && in.dur > 0 ? in.dur : 0;
  double f = duration > 0 ? clamp01(shown / duration) : 0;
  int played = (int)std::lround(tl_.w * f / s_) * s_;
  std::array<int64_t, 17> state{(int64_t)shown, played, (int64_t)duration, (int)in.volume,
      in.muted, in.fullscreen, in.sub_visible, in.sid, (int64_t)in.audios.size(),
      (int64_t)std::lround(in.speed * 100),
      (in.controls_free ? 0 : (in.accent.r >> 3) << 10 | (in.accent.g >> 3) << 5 | (in.accent.b >> 3)),
      in.controls_free ? 0 : (int)(in.accent_k * 32), hours, in.audio_only, in.controls_free, in.loop, in.ended};
  if (!dock_state_valid_ || state != dock_state_) {
    dock_state_ = state;
    dock_state_valid_ = true;
    ++dock_revision_;
    dock_dirty_ = true;
  }
  if (panel_ != Panel::None) {
    // the panel is rebuilt only if something it shows changed (never on every tick of the clock)
    std::string ps = std::to_string(in.sid) + "|" + std::to_string(in.aid) + "|" + std::to_string(in.subs.size()) + "|" +
                     std::to_string(in.audios.size()) + "|" + fmt_delay(in.sub_delay) + (in.sub_visible ? "v" : "") +
                     std::to_string((int)in.kind) + (in.style == in.default_style ? "d" : "") +
                     (in.controls_free ? "F" : "B") + (in.ambient ? "A" : "") + (in.audio_only ? "a" : "v") + std::to_string(in.ui_scale) + speed_label(in.speed);
    for (auto& t : in.subs) ps += "|" + t.lang + t.title;
    if (panel_ == Panel::FileInfo) ps += "|info:" + std::to_string(in.file_info_revision);
    ps += "|up:" + in.update_label + (in.update_ready ? "!" : "");
    if (ps != panel_sig_) {
      panel_sig_ = ps;
      build_rows();
      layout_rows();
      sw_.target = switch_on() ? 1 : 0;
      if (pfocus_ >= (int)rows_.size()) pfocus_ = -1;
    }
  }
}

void PlayView::activity(double now) { last_activity_ = now; vis_.target = 1; still_x_ = -1; }

int PlayView::cursor_kind() const {
  if (panel_ == Panel::About && phover_ >= 0 && phover_ < (int)rows_.size() && rows_[phover_].k == RK::Profile) return 1;
  if (sub_drag_ || press_ == P_SUB) return 3;
  if (sub_hover_ && panel_ == Panel::None) return 2;
  return 0;
}

void PlayView::flash(const std::string& t, double now) {
  // over the dock the result is already visible; elsewhere a quiet echo in the middle
  flash_text_ = t;
  flash_icon_ = -1;
  flash_t0_ = now;
  ++flash_gen_;
}

void PlayView::flash_pause(bool paused, double now) {
  flash_text_.clear();
  flash_icon_ = paused ? 1 : 0;  // Feedback describes the action just performed: pause or play.
  flash_t0_ = now;
  ++flash_gen_;
}

void PlayView::set_thumb(const std::vector<uint32_t>& px, int w, int h, double t) {
  if (w <= 0 || h <= 0) { thumb_ = Canvas{}; thumb_prev_ = Canvas{}; thumb_xfade_ = 1; ++thumb_gen_; return; }  // new file
  if (px.size() < (size_t)w * h) return;
  if (thumb_.w > 0 && thumb_t_ != t) { std::swap(thumb_prev_, thumb_); thumb_xfade_ = 0; }
  // nearest-neighbour enlargement to the preview size: crisp blocks, no filtering cost
  int f = std::max(1, (int)std::lround(thumb_width() / (double)w));
  thumb_.w = w * f;
  thumb_.h = h * f;
  thumb_.px.resize((size_t)thumb_.w * thumb_.h);
  for (int y = 0; y < thumb_.h; ++y) {
    const uint32_t* src = px.data() + (size_t)(y / f) * w;
    uint32_t* dst = thumb_.px.data() + (size_t)y * thumb_.w;
    for (int x = 0; x < thumb_.w; ++x) dst[x] = src[x / f];
  }
  thumb_t_ = t;
  ++thumb_gen_;
  tip_text_.clear();  // forces the preview tooltip to redraw
}

bool PlayView::dragging() const {
  return scrubbing_ || vol_drag_ || dragging_dock_ || drag_pending_ || sub_drag_ || slider_row_ >= 0 ||
         press_ != P_NONE || win_drag_;
}

void PlayView::handoff_window_drag(double now) {
  win_drag_ = false;
  press_ = P_NONE;
  swallow_up_ = true; // a late release from the video child is not a click to pause
  last_click_t_ = -10;
  pending_click_ = P_NONE;
  second_click_ = false;
  activity(now);
}

bool PlayView::sub_draggable() const {
  return in.sid != 0 && in.sub_visible && in.sub_shown && in.kind != SubKind::Bitmap &&
         style_caps(in.style, in.kind).position;
}

void PlayView::begin_sub_edit(double /*now*/) {
  if (sub_drag_) return;
  act(PA::SubEdit, 1);
  hover_ = B_NONE;
  tip_text_.clear();
  dock_dirty_ = true;
}

void PlayView::toggle_loop(double now) {
  in.loop = !in.loop;
  act(PA::Loop, in.loop ? 1 : 0);
  if (vis_.v < 0.5) flash(in.loop ? "repetir" : "sem repetir", now);
  dock_dirty_ = true;
}

bool PlayView::pinned() const {
  return in.audio_only || in.ended || scrubbing_ || vol_drag_ || dragging_dock_ || drag_pending_ || sub_drag_ || panel_ != Panel::None ||
         slider_row_ >= 0 || hover_ != B_NONE || kb_ || back_hot_;
}

void PlayView::toast(const std::string& t, const char* icon, double now, bool icon_after) {
  toast_text_ = t;
  toast_icon_ = icon;
  toast_icon_after_ = icon_after;
  toast_t0_ = now;
  toast_dirty_ = true;
  toast_btn_ = toast_hot_ = false;
  toast_hold_ = 1.1;
}

void PlayView::toast_folder(const std::string& t, double now) {
  toast(t, "check", now, true);
  toast_btn_ = true;
  toast_hold_ = 3.2;  // time to reach for the folder
}

bool PlayView::toast_hit(int x, int y, bool* on_btn) const {
  if (toast_text_.empty() || toast_alpha_ < 0.5 || !toast_rect_.contains(x, y)) return false;
  if (on_btn) {
    Rect b = toast_btn_rect_;
    b.x += toast_rect_.x; b.y += toast_rect_.y;
    *on_btn = toast_btn_ && b.grow(s_).contains(x, y);
  }
  return true;
}

Rect PlayView::back_rect() const {
  // mirrors the window controls on the other side
  return {WindowChrome::kInset * s_, WindowChrome::kInset * s_ - 2 * s_, WindowChrome::kSize * s_, WindowChrome::kSize * s_};
}

bool PlayView::back_hit(int x, int y) const { return back_vis_.v > 0.2 && back_rect().grow(2 * s_).contains(x, y); }

// While the dock sits over the subtitle, the subtitle rises above it.
double PlayView::lifted_pos() const {
  return in.style.pos - (sub_drag_ ? 0 : subtitle_lift_.x);
}

double PlayView::wanted_subtitle_lift() const {
  if (sub_drag_ || sub_settling_ || vis_.target < 1 || in.sid == 0 || in.kind == SubKind::Bitmap) return 0;
  const double pos = in.style.pos;
  Rect d = {(int)px_.target, (int)py_.target, dw_, dh_};
  if (d.y + d.h / 2 < h_ / 2) return 0;
  Rect sr = sub_rect_at(pos);
  bool overlap = !(sr.r() < d.x || d.r() < sr.x || sr.b() < d.y || d.b() < sr.y);
  if (!overlap) return 0;
  return std::max(0.0, pos - (d.y - 4.0 * s_) * 100.0 / std::max(1, h_));
}

bool PlayView::tick(double now, double dt) {
  bool moving = viewport_vis_.step(dt);
  if (style_animating_) {
    style_motion_.step(dt);
    style_mix_ = std::clamp(style_motion_.x, 0., 1.);
    layout_dock();
    double x, y; place_target(x, y);
    if (!dragging_dock_) { px_.snap(x); py_.snap(y); }
    dock_dirty_ = true;
    if (panel_ != Panel::None) { layout_rows(); panel_dirty_ = true; }
    style_animating_ = !style_motion_.settled();
    moving = true;
  }
  last_now_ = now;
  if (pending_click_ != P_NONE) {
    if (!second_click_ && now - last_click_t_ >= double_click_time_) commit_click();
    else moving = true;
  }
  {
    // hover fades: dock buttons
    int hot = kb_ ? focus_ : hover_;
    for (int id = 0; id < (int)hov_.size(); ++id) {
      double v = hover_step(hov_[id], hot == id, dt);
      if (v != hov_[id]) { hov_[id] = v; dock_dirty_ = true; moving = true; }
    }
    // panel rows
    int act_row = kb_ ? pfocus_ : phover_;
    if (phov_.size() != rows_.size()) phov_.assign(rows_.size(), 0.0);
    for (int i = 0; i < (int)phov_.size(); ++i) {
      double v = hover_step(phov_[i], act_row == i, dt);
      if (v != phov_[i]) { phov_[i] = v; panel_dirty_ = true; moving = true; }
    }
    if ((panel_ == Panel::CC || panel_ == Panel::More) && sw_.step(dt)) { panel_dirty_ = true; moving = true; }
    bool ack = now < ack_until_;
    if (ack != ack_now_) { ack_now_ = ack; panel_dirty_ = true; }
    if (ack) moving = true;
    if (now - content_t0_ < tok::enter) moving = true;
    // tooltips need frames while they wait and fade in
    if (((hover_ > B_NONE && hover_ <= B_MORE) || hover_ == B_LOOP) && now - hover_since_ < tok::tip_delay + tok::tip_in + 0.05) moving = true;
  }
  moving |= chrome.tick(dt);
  {
    // magnetic hover: the hovered glyph leans toward the cursor, the rest settle back
    Rect d = dock_rect();
    for (auto& hb : hots_) {
      int id = hb.id;
      if (id <= B_NONE || id >= (int)magx_.size() || id == B_VOLBAR || id == B_TIMELINE || id == B_DOCKBG) continue;
      Spring& sx = magx_[id];
      Spring& sy = magy_[id];
      sx.omega = sy.omega = 34; sx.zeta = sy.zeta = 0.9; sx.eps = sy.eps = 0.05;
      double tx = 0, ty = 0;
      if (hover_ == id && !kb_ && !reduce_motion() && mx_ >= 0) {
        double reach = (id == B_PLAY ? 1.5 : 1.0) * s_;  // play/pause answers a touch more
        double fx = (mx_ - (d.x + hb.r.cx())) / std::max(1.0, hb.r.w / 2.0);
        double fy = (my_ - (d.y + hb.r.cy())) / std::max(1.0, hb.r.h / 2.0);
        tx = std::clamp(fx, -1.0, 1.0) * reach;
        ty = std::clamp(fy, -1.0, 1.0) * reach * 0.6;
      }
      sx.target = tx; sy.target = ty;
      if (sx.step(dt) | sy.step(dt)) { dock_dirty_ = true; moving = true; }
    }
    // the timeline wakes up as the cursor approaches, not only inside its hitbox
    double near = 0;
    if (mx_ >= 0 && vis_.target > 0 && panel_ == Panel::None) {
      int lx = mx_ - d.x, ly = my_ - d.y;
      if (lx >= tl_.x - 4 * s_ && lx <= tl_.r() + 4 * s_) {
        double dist = std::fabs(ly - (tl_.y + s_ / 2.0)) / s_;  // art px
        near = std::clamp(1.0 - (dist - 5.0) / 8.0, 0.0, 1.0);
      }
    }
    if (scrubbing_) near = 1;
    double nv = hover_step(tl_near_, near > 0.5, dt);
    if (near > 0 && near <= 0.5) nv = std::max(tl_near_ - dt / 0.16, std::min(nv, near));
    if (nv != tl_near_) { tl_near_ = nv; dock_dirty_ = true; moving = true; }
    if (now - flash_t0_ < 0.9) moving = true;
  }
  {
    bool want = tip_want();
    tipvis_.target = want ? 1 : 0;
    if (want) {
      int W, H;
      tip_size(W, H);
      double tx = std::clamp(mx_ - W / 2, 0, std::max(0, w_ - W));
      if (tipvis_.v <= 0) tipx_.snap(tx); else tipx_.target = tx;
    }
    moving |= tipvis_.step(dt);
    moving |= tipx_.step(dt);
    if (thumb_xfade_ < 1) { thumb_xfade_ = std::min(1.0, thumb_xfade_ + dt / 0.09); moving = true; }
  }
  // the dock rises a few px with a soft spring when it appears, sinks a little when it leaves

  if (!pinned() && now - last_activity_ > tok::dock_idle) {
    if (vis_.target > 0) { still_x_ = mx_; still_y_ = my_; }  // remember the resting point
    vis_.target = 0;
  }
  if (pinned()) vis_.target = 1;
  if (iris_ == 2) {
    moving = true;
    if (reduce_motion() || now - iris_t0_ >= kIrisDur) { iris_ = 0; activity(now); }  // then the dock rises in
  }
  if (iris_) { vis_.target = 0; vis_.v = 0; }
  if (vis_.target > 0 && last_vis_target_ <= 0) {
    // an entrance: a fresh one rises 6 px of art; one that interrupts a departure starts where it is
    rise_.snap(vis_.v <= 0 ? 6.0 : (1 - ease_out(vis_.v)) * 3);
    rise_.target = 0;
  }
  last_vis_target_ = vis_.target;
  moving |= vis_.step(dt);
  moving |= rise_.step(dt);
  if (!dragging_dock_) moving |= px_.step(dt) | py_.step(dt);
  if (morph_.step(dt)) { moving = true; dock_dirty_ = true; }
  edit_.target = sub_drag_ ? 1 : 0;
  moving |= edit_.step(dt);
  moving |= pvis_.step(dt);
  moving |= prise_.step(dt);
  if (sub_settling_) {
    moving = true;
    if (!sub_spring_.step(dt)) { sub_settling_ = false; in.style.pos = sub_spring_.target; act(PA::Style); }
    else { in.style.pos = sub_spring_.x; act(PA::StyleLive); }
    update_sub_layers();
  }
  if (toast_hot_ && now - toast_t0_ > 0.14) toast_t0_ = now - 0.14;  // stays while the cursor is on it
  bool toast_live = !toast_text_.empty() && now - toast_t0_ < toast_hold_ + 0.5;
  back_vis_.target = (vis_.target > 0 && iris_ == 0 && !sub_drag_) ? 1 : 0;
  moving |= back_vis_.step(dt);
  {
    double v = hover_step(back_hov_, back_hot_ && back_vis_.target > 0, dt);
    if (v != back_hov_) { back_hov_ = v; moving = true; }
  }
  moving |= toast_live || dragging();  // keep frames coming while something is held
  // The subtitle settles independently after the dock fades; no target-state snap.
  double wanted_lift = wanted_subtitle_lift();
  subtitle_lift_.to(wanted_lift, wanted_lift < subtitle_lift_.x ? .42 : .32);
  if (subtitle_lift_.step(dt)) moving = true;
  if (in.sid != 0 && in.kind != SubKind::Bitmap && !sub_settling_) {
    double lp = lifted_pos();
    if (std::fabs(lp - last_lift_) > 0.02 || (!subtitle_lift_.settled() && last_lift_ < 0) || (subtitle_lift_.settled() && std::fabs(lp-last_lift_) > 0.0001)) { last_lift_ = lp; act(PA::SubLift, lp); }
  }
  compose(now);
  if (pvis_.v <= 0 && panel_ == Panel::None) panel_base_.resize(0, 0);
  return moving || vis_.target != vis_.v;
}

std::vector<int> PlayView::focus_order() const {
  std::vector<int> o = {B_VOL, B_REW, B_PLAY, B_FWD, B_SPEED, B_LOOP, B_CC};
  if (in.audios.size() > 1) o.push_back(B_AUDIO);
  o.push_back(B_FS);
  o.push_back(B_MORE);
  o.push_back(B_TIME);
  o.push_back(B_TIMELINE);
  return o;
}

void PlayView::activate(int b, double now) {
  switch (b) {
    case B_VOL: act(PA::ToggleMute); break;
    case B_REW: act(PA::SeekRel, -15); flash("-15", now); break;
    case B_FWD: act(PA::SeekRel, 15); flash("+15", now); break;
    case B_TIME: remaining_ = !remaining_; break;
    case B_LOOP: toggle_loop(now); break;
    case B_PLAY:
      if (in.ended) {  // watch again: from the start, playing
        act(PA::TogglePause, 0);
        in.paused = in.ended = false;
        morph_.target = 1;
        break;
      }
      act(PA::TogglePause, !in.paused);
      in.paused = !in.paused;            // action first: the morph starts this frame,
      morph_.target = in.paused ? 0 : 1;  // the engine confirms a moment later
      flash_pause(in.paused, now);
      break;
    case B_SPEED: if (panel_ == Panel::Speed) close_panel(); else open_panel(Panel::Speed, B_SPEED, now); break;
    case B_FS: act(PA::Fullscreen); break;
    case B_CC: if (panel_ == Panel::CC || panel_ == Panel::Style) close_panel(); else open_panel(Panel::CC, B_CC, now); break;
    case B_AUDIO: if (panel_ == Panel::Audio) close_panel(); else open_panel(Panel::Audio, B_AUDIO, now); break;
    case B_MORE: if (panel_ == Panel::More) close_panel(); else open_panel(Panel::More, B_MORE, now); break;
    default: break;
  }
  dock_dirty_ = true;
}

void PlayView::mouse_move(int x, int y, double now) {
  bool moved = x != mx_ || y != my_;
  mx_ = x; my_ = y;
  if (!moved) return;
  // a sensor twitch must not bring the dock back: it takes a real move (3 px of art)
  if (vis_.target == 0 && !dragging() && still_x_ >= 0 &&
      std::abs(x - still_x_) + std::abs(y - still_y_) < 3 * s_) {
    bool sh = panel_ == Panel::None && sub_draggable() && sub_hit(x, y);
    if (sh != sub_hover_) sub_hover_ = sh;
    return;
  }
  still_x_ = -1;
  if (kb_) { kb_ = false; focus_ = B_NONE; pfocus_ = -1; dock_dirty_ = panel_dirty_ = true; }
  activity(now);
  if (dragging_dock_ || drag_pending_) {
    if (drag_pending_ && std::abs(x - down_x_) + std::abs(y - down_y_) > 3 * s_) { drag_pending_ = false; dragging_dock_ = true; }
    if (dragging_dock_) {
      double rx = x - grab_x_, ry = y - grab_y_;
      rx = std::clamp(rx, 0.0, (double)std::max(0, w_ - dw_));
      ry = std::clamp(ry, 0.0, (double)std::max(0, h_ - dh_));
      // magnetic pull toward useful places
      double best = 1e9, bx = rx, by = ry;
      for (int a = 0; a < 4; ++a) {
        double ax, ay;
        anchor_pos((DockAnchor)a, ax, ay);
        double d = std::hypot(ax - rx, ay - ry);
        if (d < best) { best = d; bx = ax; by = ay; }
      }
      double R = 40.0 * s_;
      double k = best < R ? std::pow(1 - best / R, 2) * 0.7 : 0;
      px_.snap(lerp(rx, bx, k));
      py_.snap(lerp(ry, by, k));
      if (panel_ != Panel::None) panel_dirty_ = true;
    }
    return;
  }
  if (scrubbing_) {
    seek_scrub(x, false);
    if (std::fabs(scrub_t_ - last_preview_req_) > std::max(0.5, in.dur / 400)) { last_preview_req_ = scrub_t_; act(PA::Preview, scrub_t_); }
    return;
  }
  if (vol_drag_) { set_volume_from(x); return; }
  if (slider_row_ >= 0) { slider_set(slider_row_, x, true); return; }
  if (press_ != P_NONE && std::abs(x - press_x_) + std::abs(y - press_y_) > 4 * s_) {
    pending_click_ = P_NONE;
    second_click_ = false;
    if (press_ == P_SUB) {
      in.style.pos = lifted_pos();
      subtitle_lift_.snap(0);
      begin_sub_edit(now);
      sub_drag_ = true;
      sub_settling_ = false;
      sub_start_pos_ = in.style.pos;
      sub_start_y_ = press_y_;
      update_sub_layers();
    } else if (press_ == P_STRIP) {
      win_drag_ = true;
      act(PA::WinDragBegin);
    }
    press_ = P_NONE;
  }
  if (win_drag_) return;
  if (sub_drag_) {
    double raw = sub_start_pos_ + (y - sub_start_y_) * 100.0 / std::max(1, h_);
    raw = std::clamp(raw, kSubPosMin, kSubPosMax);
    in.style.pos = snap_sub(raw);
    act(PA::StyleLive);
    update_sub_layers();
    return;
  }
  {
    // the toast's folder button and the back arrow sit outside the dock: they take the pointer first
    bool on_btn = false;
    bool on_toast = toast_hit(x, y, &on_btn);
    if (on_btn != toast_hot_) { toast_hot_ = on_btn; toast_dirty_ = true; }
    back_hot_ = !on_toast && hit_panel(x, y, nullptr) == -1 && back_hit(x, y);
    if (on_toast || back_hot_) {
      if (hover_ != B_NONE) { hover_ = B_NONE; dock_dirty_ = true; }
      if (phover_ != -1) { phover_ = -1; panel_dirty_ = true; }
      chrome.hot = WindowChrome::None;
      sub_hover_ = false;
      return;
    }
  }
  int part = -1;
  int pr = hit_panel(x, y, &part);
  if (pr != phover_ || part != ppart_) { phover_ = pr; ppart_ = part; panel_dirty_ = true; }
  int b = pr == -1 ? hit_dock(x, y) : B_NONE;
  if (b != hover_) { hover_ = b; dock_dirty_ = true; }
  hover_since_ = now;  // tooltips wait for the mouse to rest
  if (hover_ == B_TIMELINE && in.dur > 0) {
    dock_dirty_ = true;
    double t = time_at(x);
    if (std::fabs(t - last_preview_req_) > std::max(0.5, in.dur / 400)) { last_preview_req_ = t; act(PA::Preview, t); }
  }
  chrome.hot = (pr == -1 && b == B_NONE) ? chrome.hit(x, y, w_) : WindowChrome::None;
  bool sh = pr == -1 && b == B_NONE && chrome.hot == WindowChrome::None && panel_ == Panel::None && sub_draggable() &&
            sub_hit(x, y);
  if (sh != sub_hover_) sub_hover_ = sh;  // nothing is drawn: only the cursor changes
}

void PlayView::mouse_down(int x, int y, double now) {
  if (viewport_transition_) return;
  mx_ = x; my_ = y;
  activity(now);
  int part = -1;
  swallow_up_ = false;
  press_ = P_NONE;
  second_click_ = false;
  if (pending_click_ != P_NONE && now - last_click_t_ >= double_click_time_) commit_click();
  Press prior_click = pending_click_;
  pending_click_ = P_NONE; // controls, menus and subtitles consume the pending gesture
  {
    bool on_btn = false;
    if (toast_hit(x, y, &on_btn)) {  // a click on the toast never pauses; its button opens the folder
      if (on_btn) act(PA::OpenFrameFolder);
      swallow_up_ = true;
      return;
    }
  }
  int pr = hit_panel(x, y, &part);
  if (pr >= 0) { panel_click(pr, part, x, now); return; }
  if (pr == -2) return;
  int b = hit_dock(x, y);
  if (panel_ != Panel::None && !(panel_ == Panel::Style && b == B_NONE && sub_hit(x, y))) {
    // first outside click dismisses the layer and is consumed: no pause, no seek, nothing behind
    close_panel();
    swallow_up_ = true;
    dock_dirty_ = true;
    return;
  }
  if (b == B_NONE && panel_ == Panel::Style && sub_hit(x, y) && style_caps(in.style, in.kind).position) {
    in.style.pos = lifted_pos();  // grab it where it is on screen, no jump
    subtitle_lift_.snap(0);
    begin_sub_edit(now);
    sub_drag_ = true;
    sub_settling_ = false;
    sub_start_pos_ = in.style.pos;
    sub_start_y_ = y;
    update_sub_layers();
    return;
  }
  if (b == B_NONE && back_hit(x, y)) { close_on_release_ = true; return; }  // home, on release (like "fechar vídeo")
  switch (b == B_NONE ? chrome.hit(x, y, w_) : WindowChrome::None) {
    case WindowChrome::Min: act(PA::WinMin); return;
    case WindowChrome::Max: act(PA::WinMax); return;
    case WindowChrome::Close: act(PA::WinClose); return;
    default: break;
  }
  switch (b) {
    case B_NONE:
      // decided on release: a click pauses, a drag moves the subtitle (or the window, from the top strip)
      press_ = sub_draggable() && sub_hit(x, y) ? P_SUB : (y < kStrip * s_ && !in.fullscreen ? P_STRIP : P_VIDEO);
      press_x_ = x; press_y_ = y;
      if (press_ != P_SUB) pending_click_ = prior_click;
      second_click_ = pending_click_ == press_ && now - last_click_t_ < double_click_time_ &&
                      std::abs(x - last_click_x_) < 6 * s_ && std::abs(y - last_click_y_) < 6 * s_;
      if (!second_click_ && pending_click_ != P_NONE) commit_click();
      break;
    case B_TIMELINE: scrubbing_ = true; seek_scrub(x, false); break;
    case B_VOLBAR: vol_drag_ = true; set_volume_from(x); break;
    case B_DOCKBG: {
      Rect d = dock_rect();
      drag_pending_ = true;
      grab_x_ = x - d.x; grab_y_ = y - d.y;
      down_x_ = x; down_y_ = y;
      break;
    }
    default: activate(b, now);
  }
  if (press_ == P_NONE || press_ == P_SUB) pending_click_ = P_NONE;
  dock_dirty_ = true;
}

void PlayView::commit_click() { pending_click_ = P_NONE; }

void PlayView::mouse_up(int x, int y, double now) {
  if (viewport_transition_) return;
  activity(now);
  if (close_on_release_) { close_on_release_ = false; act(PA::CloseVideo); return; }
  if (swallow_up_) { swallow_up_ = false; return; }
  if (win_drag_) { win_drag_ = false; act(PA::WinDragEnd); }
  if (press_ == P_SUB) press_ = P_NONE;  // a click on the subtitle belongs to it: it never pauses
  if (press_ != P_NONE) {
    // The first click already responded. A double click explicitly restores
    // the pre-gesture state, rather than cycling a possibly stale engine state.
    if (second_click_ && pending_click_ == press_) {
      pending_click_ = P_NONE;
      if (press_ == P_VIDEO) {
        in.paused=click_was_paused_; act(PA::TogglePause,in.paused);
        morph_.target=in.paused?0:1; flash_t0_=-10; dock_dirty_=true;
      }
      act(press_ == P_STRIP ? PA::WinMax : PA::Fullscreen);
      last_click_t_ = -10;
    } else {
      click_was_paused_=in.paused;
      if (press_ == P_VIDEO) activate(B_PLAY,now);
      pending_click_ = press_;
      last_click_t_ = now; last_click_x_ = x; last_click_y_ = y;
    }
    second_click_ = false;
    press_ = P_NONE;
  }
  if (scrubbing_) { seek_scrub(x, true); scrubbing_ = false; dock_dirty_ = true; }
  if (vol_drag_) { vol_drag_ = false; dock_dirty_ = true; }
  if (slider_row_ >= 0) { slider_set(slider_row_, x, false); slider_row_ = -1; panel_dirty_ = true; }
  if (sub_drag_) {
    sub_drag_ = false;
    // settle softly on the nearest guide if close, else stay where released
    double target = in.style.pos;
    const double snaps[3] = {kSubPosTop, kSubPosCenter, kSubPosBottom};
    for (double sn : snaps) if (std::fabs(target - sn) < 3.0) target = sn;
    sub_spring_.snap(in.style.pos);
    sub_spring_.target = target;
    sub_settling_ = true;
    style_y_ = -1;  // the subtitle moved: the editor may pick a better place once
    update_sub_layers();
    act(PA::SubEdit, 0);  // colour and playback come back
    if (panel_ == Panel::None) {
      // the controls stay away until the mouse moves again, so the subtitle is seen where it stays
      vis_.v = vis_.target = 0;
      still_x_ = x; still_y_ = y;
      last_activity_ = now - 1e3;
      edit_.snap(0);
    }
  }
  drag_pending_ = false;
  if (dragging_dock_) {
    dragging_dock_ = false;
    double best = 1e9;
    int ba = -1;
    for (int a = 0; a < 4; ++a) {
      double ax, ay;
      anchor_pos((DockAnchor)a, ax, ay);
      double d = std::hypot(ax - px_.x, ay - py_.x);
      if (d < best) { best = d; ba = a; }
    }
    if (best < 56.0 * s_) place.anchor = (DockAnchor)ba;
    else {
      double m = kMargin * s_;
      place.anchor = DockAnchor::Free;
      place.fx = clamp01((px_.x - m) / std::max(1.0, w_ - dw_ - 2 * m));
      place.fy = clamp01((py_.x - m) / std::max(1.0, h_ - dh_ - 2 * m));
    }
    double tx, ty;
    place_target(tx, ty);
    px_.target = tx;
    py_.target = ty;
    act(PA::Placement);
  }
}

void PlayView::right_click(int x, int y, double now) {
  pending_click_ = P_NONE;
  second_click_ = false;
  mx_ = x; my_ = y;
  activity(now);
  if (panel_ != Panel::None) { close_panel(); return; }  // outside click: only dismisses
  ctx_x_ = x;
  ctx_y_ = y;
  open_panel(Panel::Context, B_NONE, now);
}

void PlayView::wheel(int dir, double now) {
  activity(now);
  act(PA::Volume, std::clamp(in.volume + 5 * dir, 0.0, kVolMax));
}

void PlayView::seek_scrub(int x, bool final) {
  scrub_t_ = time_at(x);
  // Audio keeps playing/drawing while the hand chooses a new point. Repeated
  // decoder flushes would keep restarting the waveform; commit once on release.
  if (!in.audio_only || final) act(final ? PA::SeekAbsExact : PA::SeekAbs, scrub_t_);
  dock_dirty_ = true;
}

void PlayView::set_volume_from(int x) {
  Rect d = dock_rect();
  for (auto& hb : hots_)
    if (hb.id == B_VOLBAR) {
      int bx = d.x + hb.r.x + 2 * s_, bw = hb.r.w - 4 * s_;
      double v = std::round(clamp01((x - bx) / (double)bw) * kVolMax);
      if (std::fabs(v - 100) <= 3) v = 100;  // the natural level is easy to land back on
      act(PA::Volume, v);
      in.volume = v;
      if (in.muted && v > 0) act(PA::ToggleMute);
    }
  dock_dirty_ = true;
}

void PlayView::open_panel_for_preview(int which, double now) {
  Panel pn = which == 0 ? Panel::CC : which == 1 ? Panel::Style : which == 2 ? Panel::Audio : which == 4 ? Panel::Speed : which == 5 ? Panel::About : which == 6 ? Panel::FileInfo : Panel::More;
  int btn = which == 2 ? B_AUDIO : which == 3 ? B_MORE : which == 4 ? B_SPEED : B_CC;
  activity(now);
  open_panel(pn, btn, now);
}

void PlayView::key(const std::string& k, bool shift, bool ctrl, double now) {
  if (viewport_transition_ && k=="esc") return; // never close media during a pending fullscreen change
  pending_click_ = P_NONE;
  second_click_ = false;
  (void)ctrl;
  // the keyboard controls playback silently; only navigating the UI brings it up
  bool nav = k == "tab" || panel_ != Panel::None || (kb_ && (k == "enter" || k == "left" || k == "right"));
  if (nav) activity(now);
  if (k == "esc") {
    if (panel_ == Panel::About) open_panel(Panel::More, B_MORE, now);
    else if (panel_ == Panel::Style) open_panel(Panel::CC, panel_btn_, now);
    else if (panel_ == Panel::Speed && panel_btn_ == B_MORE) open_panel(Panel::More, panel_btn_, now);
    else if (panel_ != Panel::None) close_panel();
    else if (kb_) { kb_ = false; focus_ = B_NONE; dock_dirty_ = true; }
    else if (in.fullscreen) act(PA::Fullscreen);
    else act(PA::CloseVideo);  // last step back: the home
    return;
  }
  if (k == "tab") {
    if (panel_ != Panel::None) {
      kb_ = true;
      std::vector<int> sel;
      for (int i = 0; i < (int)rows_.size(); ++i) if (selectable(rows_[i])) sel.push_back(i);
      if (sel.empty()) return;
      auto it = std::find(sel.begin(), sel.end(), pfocus_);
      int i = it == sel.end() ? -1 : (int)(it - sel.begin());
      int n = (int)sel.size();
      i = i < 0 ? 0 : (i + (shift ? n - 1 : 1)) % n;
      pfocus_ = sel[i];
      ppart_ = -1;
      panel_dirty_ = true;
      return;
    }
    kb_ = true;
    auto o = focus_order();
    auto it = std::find(o.begin(), o.end(), focus_);
    int i = it == o.end() ? -1 : (int)(it - o.begin());
    int n = (int)o.size();
    i = i < 0 ? (shift ? n - 1 : 0) : (i + (shift ? n - 1 : 1)) % n;
    focus_ = o[i];
    dock_dirty_ = true;
    return;
  }
  // panels own the arrows and Enter while open
  if (panel_ != Panel::None) {
    std::vector<int> sel;
    for (int i = 0; i < (int)rows_.size(); ++i) if (selectable(rows_[i])) sel.push_back(i);
    if ((k == "up" || k == "down") && !sel.empty()) {
      kb_ = true;
      auto it = std::find(sel.begin(), sel.end(), pfocus_);
      int i = it == sel.end() ? (k == "down" ? -1 : (int)sel.size()) : (int)(it - sel.begin());
      i = std::clamp(i + (k == "down" ? 1 : -1), 0, (int)sel.size() - 1);
      pfocus_ = sel[i];
      panel_dirty_ = true;
      return;
    }
    if ((k == "left" || k == "right") && pfocus_ >= 0) {
      Row& r = rows_[pfocus_];
      if (r.k == RK::Slider || r.k == RK::Fonts || r.k == RK::Segmented || r.k == RK::Stepper || r.k == RK::SpeedBar ||
          r.k == RK::Chips) {
        panel_adjust(pfocus_, k == "right" ? 1 : -1, now);
        panel_dirty_ = true;
        return;
      }
      if (r.k == RK::Header && k == "left") { panel_click(pfocus_, -1, 0, now); return; }
      if (r.k == RK::Submenu && k == "right") { panel_click(pfocus_, -1, 0, now); return; }
    }
    if (k == "enter" && pfocus_ >= 0) {
      Row& r = rows_[pfocus_];
      int part = (r.k == RK::Links && !r.parts.empty()) ? 0 : -1;
      if (r.k == RK::Slider || r.k == RK::Stepper || r.k == RK::Fonts || r.k == RK::Segmented || r.k == RK::SpeedBar ||
          r.k == RK::Chips) return;
      panel_click(pfocus_, part, 0, now);
      if (close_on_release_) { close_on_release_ = false; act(PA::CloseVideo); }
      return;
    }
  }
  if (k == "enter" && kb_ && focus_ != B_NONE) { activate(focus_, now); return; }
  if (k == "space") {
    act(PA::TogglePause, !in.paused);
    in.ended = false;
    in.paused = !in.paused;
    morph_.target = in.paused ? 0 : 1;
    flash_pause(in.paused, now);
    return;
  }
  if (k == "left" || k == "right") {
    int dir = k == "right" ? 1 : -1;
    if (kb_ && focus_ == B_VOL) { act(PA::Volume, std::clamp(in.volume + 5 * dir, 0.0, kVolMax)); return; }
    int sec = shift ? 30 : 5;
    act(PA::SeekRel, dir * sec);
    if (vis_.v < 0.5) flash((dir > 0 ? "+" : "-") + std::to_string(sec), now);
    return;
  }
  if (k == "[" || k == "]" || k == "\\") {
    // the next usual stop up or down (from any value the slider left)
    double v = 1.0;
    if (k == "]") { v = kSpeedLadder[9]; for (double st : kSpeedLadder) if (st > in.speed + 0.001) { v = st; break; } }
    if (k == "[") { v = kSpeedLadder[0]; for (double st : kSpeedLadder) if (st < in.speed - 0.001) v = st; }
    set_speed(v);
    flash(speed_label(v), now);
    return;
  }
  if (k == "up" || k == "down") { act(PA::Volume, std::clamp(in.volume + (k == "up" ? 5 : -5), 0.0, kVolMax)); return; }
  if (k == "c" && !ctrl) { activity(now); open_panel(Panel::CC, B_CC, now); return; }
  if (k == "m") { act(PA::ToggleMute); return; }
  if (k == "l") { toggle_loop(now); return; }
  if (k == "f") { act(PA::Fullscreen); return; }
  if (k == "s") { act(PA::SubVisible, in.sub_visible ? 0 : 1); return; }
  if (k == "z") { act(PA::SubDelay, -0.1); return; }
  if (k == "x") { act(PA::SubDelay, 0.1); return; }
}

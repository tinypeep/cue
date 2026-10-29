#include "onboarding.h"

#include <algorithm>
#include <cmath>

#include "../core/ui_scale.h"

namespace {
const char* kSizeNames[4] = {"pequena", "média", "grande", "maior"};
const char* kIntroHeading = "oi, sou o Cue";
const char* kAmbHeading = "iluminação dinâmica";
const char* kAmbLine = "a janela brilha com as cores do vídeo";
const char* kSizeHeading = "tamanho da interface";
const char* kNextHeading = "vem mais coisa por aí";

// What is coming, ripening like the popcorn of the first screen.
struct Upcoming { const char* title; const char* tag; const char* line; int icon; };
const Upcoming kUpcoming[3] = {
    {"controle pelo celular", "estoura na próxima build", "o celular vira controle pela rede. o sofá agradece.", 0},
    {"transmissão sem fio", "ainda na panela", "do Cue direto pra TV, sem caçar cabo.", 1},
    {"macOS e Linux", "ainda é milho", "o Cue quer conhecer outros sistemas.", 2},
};
constexpr int kItemTextW = 300;  // art px for the words of a row
constexpr int kIconGap = 14;     // between the icon and the words
constexpr int kTitleH = 18, kLineH = 12, kRowGap = 14;  // title line (with its tag), description lines
// physical px per icon cell: 1.5 art px, whole pixels
inline int icon_cell(double s) { return std::max(1, (int)std::lround(1.5 * s)); }
constexpr int kIconCols = 18, kIconRows = 14;
// Icons, 18 x 14 cells: '#' the drawing, digits light up in turn (signal), 'p' the phone's
// screen glyph, 'c' a blinking cursor.
const char* kIcons[3][kIconRows] = {
    {  // a phone sending a signal
        "..#######.........",
        ".#.......#........",
        ".#.......#.....3..",
        ".#.......#......3.",
        ".#.......#...2...3",
        ".#.......#....2..3",
        ".#.......#.1..2..3",
        ".#.......#..1.2..3",
        ".#.......#.1..2..3",
        ".#.......#....2..3",
        ".#.......#...2...3",
        ".#...#...#......3.",
        ".#.......#.....3..",
        "..#######.........",
    },
    {  // a screen being cast to
        "..................",
        "..................",
        "##############....",
        "#............#....",
        "#............#....",
        "#............#....",
        ".............#....",
        "333..........#....",
        "...3.........#....",
        "22..3........#....",
        "..2.3........#....",
        "1.2.3.########....",
        "....######........",
        "..................",
    },
    {  // a laptop with a prompt
        "..................",
        "..................",
        "..############....",
        "..#..........#....",
        "..#.#........#....",
        "..#..#.......#....",
        "..#.#.cc.....#....",
        "..#..........#....",
        "..#..........#....",
        "..############....",
        "################..",
        ".##############...",
        "..................",
        "..................",
    },
};

constexpr double kGravity = 64;   // popcorn, bucket cells per second squared
constexpr double kIntroType = 1.25, kCharDur = 0.055;
constexpr int kBucketW = 17, kBucketH = 19;

// The bucket, 17 x 19 cells: popcorn heap (rows 0-4), rim (5), striped body (6-18).
// '#' kernel, '+' kernel shade, 'R' rim, 'a'/'b' alternating stripes.
const char* kBucket[kBucketH] = {
    "......##...#.....",
    "...##.###.###....",
    "..#####+##+####..",
    ".###+######+####.",
    "#####+##########+",
    "RRRRRRRRRRRRRRRRR",
    ".aaabbbaaabbbaaa.",
    ".aaabbbaaabbbaaa.",
    ".aaabbbaaabbbaaa.",
    "..aabbbaaabbbaa..",
    "..aabbbaaabbbaa..",
    "..aabbbaaabbbaa..",
    "..aabbbaaabbbaa..",
    "...abbbaaabbba...",
    "...abbbaaabbba...",
    "...abbbaaabbba...",
    "...abbbaaabbba...",
    "....bbbaaabbb....",
    "....bbbaaabbb....",
};
// Two kernel poses, 3 x 3, so a flying kernel tumbles.
const char* kKernel[2] = {".#.###.#.", "##.###.##"};

int cps(const std::string& s) {
  int n = 0;
  for (size_t i = 0; i < s.size();) { next_cp(s, i); ++n; }
  return n;
}
std::string first_cps(const std::string& s, int n) {
  size_t i = 0;
  for (int k = 0; k < n && i < s.size(); ++k) next_cp(s, i);
  return s.substr(0, i);
}
double ease_k(double t) { return ease_out(clamp01(t)); }
}  // namespace

std::vector<std::string> wrap_text(const std::string& t, int max_w, double s) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush_word = [&] {
    if (word.empty()) return;
    std::string cand = line.empty() ? word : line + " " + word;
    if (!line.empty() && text_w(cand, s) > max_w) { lines.push_back(line); line = word; }
    else line = cand;
    word.clear();
  };
  for (char ch : t) {
    if (ch == ' ') flush_word();
    else word += ch;
  }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

// The same number of lines, but as even as possible: no lonely last word.
static std::vector<std::string> wrap_balanced(const std::string& t, int max_w, double s) {
  auto lines = wrap_text(t, max_w, s);
  if (lines.size() < 2) return lines;
  int lo = max_w / (int)lines.size(), hi = max_w;
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (wrap_text(t, mid, s).size() <= lines.size()) hi = mid;
    else lo = mid + 1;
  }
  return wrap_text(t, hi, s);
}

// ── layout ──────────────────────────────────────────────────────────
void OnboardingView::set_size(int w_, int h_, double s_) {
  if (w_ <= 0 || h_ <= 0) return;  // minimized or not created yet: keep the last layout
  bool first = w == 0;
  w = w_;
  h = h_;
  ui_s = std::max(1.0, s_);
  // A first impression reads at a comfortable size, close to the chosen interface size:
  // about 720 x 405 px of art fill the window, never more than 1.5x the interface scale.
  // The control sample shows the real size.
  double fit = std::floor(std::min(w / 720.0, h / 405.0) * 2) / 2;
  s = std::max(ui_s, std::min(std::max(1.0, fit), ui_s * 1.5));
  chrome.s = ui_s;  // the window buttons stay exactly where the home has them
  layout();
  if (first) { pos_.snap(page_); dot_.snap(page_); }
  place_face(0, !face_placed_);
}

int OnboardingView::scale_index() const { return ui_size_index(ui_scale); }

void OnboardingView::layout() {
  if (w <= 0 || h <= 0) return;
  const double W = w / s, H = h / s;
  const double CW = std::min(W - 32, 560.0), ox = (W - CW) / 2;
  const double top = 24, foot_y = H - 14 - 16, bottom = foot_y - 10, AH = bottom - top;
  auto R = [&](double x, double y, double ww, double hh) { return Rect(x * s, y * s, ww * s, hh * s); };

  // footer: secondary on the left, page dots in the middle, primary on the right
  for (int p = 0; p < kPages; ++p) {
    Lay& L = L_[p];
    std::string nl = p == kPages - 1 ? "começar" : "continuar";
    double nw = text_w(nl, 1) + 9 + 22;
    L.next = R(ox + CW - nw, foot_y, nw, 16);
    std::string bl = p == 0 ? "pular" : "voltar";
    double bw = text_w(bl, 1) + 16;
    L.back = R(ox, foot_y, bw, 16);
    L.dots.clear();
    double dx = W / 2 - (kPages * 6 + (kPages - 1) * 4) / 2.0;
    for (int i = 0; i < kPages; ++i) L.dots.push_back(R(dx + i * 10 - 2, foot_y + 4, 10, 8));
  }

  // 0 · intro: face, bucket, heading, one line
  {
    Lay& L = L_[0];
    double fixed = 24 + 6;
    double fs = std::clamp(std::min((AH - fixed) / 32.0, CW / 90.0), 1.0, 3.34);
    fs = std::floor(fs * 3) / 3.0;
    int bc = std::max(1, (int)std::lround(fs * 0.8));
    double total = 3 * fs + 13 * fs + kBucketH * bc + 8 + fixed;
    double y = top + std::max(0.0, (AH - total) / 2);
    double eye_y = y + 3 * fs;
    L.face_scale = fs;
    L.face_x = W / 2 * s;
    L.face_y = (eye_y + 3 * fs) * s;  // Face::cy: the eyes sit 3 * scale above it
    L.bucket_cell = bc;
    L.bucket_x = (int)std::lround((W / 2 - kBucketW * bc / 2.0) * s);
    L.bucket_y = (int)std::lround((eye_y + 13 * fs) * s);
    L.text_y = (int)std::lround((eye_y + 13 * fs + kBucketH * bc + 10) * s);
    L.heading = R(ox, L.text_y / s, CW, 24);
  }

  // 1 · ambient light: the demo window glowing, heading, text, the toggle
  {
    Lay& L = L_[1];
    double lines = 1;
    double fixed = 24 + 4 + 13 * lines + 16 + 16;
    // demo (54 K tall) + room for its light above (12 K) and below (22 K): 88 K in all
    K_ = std::clamp(std::min((AH - fixed) / 88.0, CW * 0.42 / 96.0), 0.5, 2.2);
    double dw = std::round(96 * K_), dh = std::round(54 * K_);
    double total = 12 * K_ + dh + 22 * K_ + fixed;
    double y = top + std::max(0.0, (AH - total) / 2) + 12 * K_;
    L.demo = R(std::round(W / 2 - dw / 2), std::round(y), dw, dh);
    double ty = y + dh + 22 * K_;  // clear of the light
    L.heading = R(ox, ty, CW, 24);
    L.text_y = (int)std::lround((ty + 28) * s);
    double tgy = ty + 28 + 13 * lines + 16;
    double tw = 28 + 8 + text_w("desligada", 1);
    L.toggle = R(std::round(W / 2 - tw / 2), tgy, 28, 16);
  }

  // 2 · interface size: a sample of the real controls, heading, text, the four sizes
  {
    Lay& L = L_[2];
    double segw = 8;
    for (auto* n : kSizeNames) segw += text_w(n, 1) + 16;
    double fixed = 26 + 22 + 24 + 14 + 16;
    double y = top + std::max(0.0, (AH - fixed) / 2) + 6;
    // the sample is drawn at the real interface scale: its size in this page's art px varies
    double k = ui_s / s;
    double dkw = std::min(CW, 176.0 * k), dkh = 26 * k;
    L.dock = R(std::round(W / 2 - dkw / 2), y + (26 - dkh), dkw, dkh);
    double ty = y + 26 + 22;
    L.heading = R(ox, ty, CW, 24);
    L.text_y = (int)std::lround((ty + 28) * s);
    double sy = ty + 24 + 14;
    L.segs.clear();
    double sx = std::round(W / 2 - segw / 2) + 4;
    for (auto* n : kSizeNames) {
      double sw = text_w(n, 1) + 16;
      L.segs.push_back(R(sx, sy, sw, 16));
      sx += sw;
    }
    const Rect& sel = L.segs[std::clamp(scale_index(), 0, 3)];
    seg_x_.snap(sel.x);
    seg_w_.snap(sel.w);
  }

  // 3 · what is coming: heading, then a row per item (icon, title and tag, a short line)
  {
    Lay& L = L_[3];
    int ic = icon_cell(s);
    double icon_w = kIconCols * ic / s, icon_h = kIconRows * ic / s;
    double rw = std::min(CW, icon_w + kIconGap + kItemTextW);
    L.items.clear(); L.item_lines.clear();
    // every row the same height (the tallest), so the list reads as one even column
    double rh = icon_h;
    for (auto& it : kUpcoming) {
      int n = (int)wrap_text(it.line, (int)std::lround((rw - icon_w - kIconGap) * s), s).size();
      L.item_lines.push_back(n);
      rh = std::max(rh, (double)kTitleH + kLineH * n);
    }
    double total = 24 + 18 + 3 * rh + 2 * kRowGap;
    double y = top + std::max(0.0, (AH - total) / 2);
    L.heading = R(ox, y, CW, 24);
    y += 24 + 18;
    for (int i = 0; i < 3; ++i) { L.items.push_back(R(std::round(W / 2 - rw / 2), std::round(y), rw, rh)); y += rh + kRowGap; }
  }

  // The footer (back, page dots, continue) is never covered: content that would reach it
  // moves up, as far as the top margin allows.
  const int limit = (int)std::lround(bottom * s);
  auto lift = [&](Lay& L, int content_bottom) {
    int over = content_bottom - limit;
    if (over <= 0) return;
    int room = std::max(0, (int)std::lround(L.heading.y - top * s) - 1);
    int d = std::min(over, room);
    L.heading.y -= d; L.text_y -= d; L.toggle.y -= d; L.demo.y -= d; L.dock.y -= d;
    L.bucket_y -= d; L.face_y -= d;
    for (auto& r : L.segs) r.y -= d;
    for (auto& r : L.items) r.y -= d;
  };
  lift(L_[0], L_[0].text_y + (int)std::lround(24 * s));
  lift(L_[1], L_[1].toggle.b());
  if (!L_[2].segs.empty()) lift(L_[2], L_[2].segs[0].b());
  if (!L_[3].items.empty()) lift(L_[3], L_[3].items.back().b());
}

void OnboardingView::place_face(double now, bool snap) {
  (void)now;
  double tx, ty, ts;
  if (finish_t0_ > 0) { tx = finish_fx_; ty = finish_fy_; ts = finish_fs_; }
  else { const Lay& L = L_[0]; tx = L.face_x; ty = L.face_y; ts = L.face_scale; }
  if (snap) { fx_.snap(tx); fy_.snap(ty); fs_.snap(ts); face_placed_ = true; }
  else { fx_.target = tx; fy_.target = ty; fs_.target = ts; }
  double fs = finish_t0_ > 0 ? ui_s : s;
  face.layout(fx_.x, fy_.x, fs, std::max(0.34, std::round(fs_.x * 3) / 3.0));
  face.max_look = 3.0 * face.scale / 1.5;
}

void OnboardingView::start(double now) {
  t0_ = now;
  page_enter_ = now;
  face.set_mode(Face::Mode::Follow, now);
  face.look(0, 0);
  bucket_.snap(16);
  bucket_.target = 16;
  next_pop_ = now + 0.95;
  burst_until_ = now + 1.5;
  if (reduce_motion()) { t0_ = now - 10; bucket_.snap(0); }
  knob_.snap(ambient ? 1 : 0);
  glow_.snap(ambient ? 1 : 0);
}

void OnboardingView::go(int p, double now) {
  p = std::clamp(p, 0, kPages - 1);
  if (p == page_) return;
  page_ = p;
  page_enter_ = now;
  pos_.target = p;
  dot_.target = p;
  if (kb_) focus_ = kNext;
  place_face(now, false);
  face.notice(now);
}

void OnboardingView::set_ambient(bool on, double now) {
  if (on == ambient) return;
  ambient = on;
  knob_.target = on ? 1 : 0;
  knob_.v += on ? 5 : -5;  // the flick that starts it
  glow_.target = on ? 1 : 0;
  toggle_t_ = now;
  if (on) { happy_until_ = now + 0.9; face.hop.v = -34; }
}

void OnboardingView::set_scale_choice(int id, double now) {
  ui_scale = id;
  if (L_[2].segs.size() == 4) {
    const Rect& sel = L_[2].segs[std::clamp(scale_index(), 0, 3)];
    seg_x_.target = sel.x;
    seg_w_.target = sel.w;
  }
  scale_t_ = now;
  face.hop.v = -30;
  happy_until_ = now + 0.6;
}

void OnboardingView::finish(double now, double face_x, double face_y, double face_scale) {
  if (finish_t0_ > 0) return;
  finish_t0_ = now;
  finish_fx_ = face_x;
  finish_fy_ = face_y;
  finish_fs_ = face_scale;
  face_handoff_ = pos_.x < 0.5;  // only when the eyes are on screen do they fly to the home
  fx_ = Spring::rd(0.36, 0.9, 0.3); fx_.snap(face.cx);
  fy_ = Spring::rd(0.36, 0.9, 0.3); fy_.snap(face.cy);
  fs_ = Spring::rd(0.36, 1.0, 0.003); fs_.snap(face.scale * s / ui_s);  // same size on screen, in home units
  place_face(now, false);
  happy_until_ = 0;
  face.mouth.target = 0;
  face.look(0, 0);
  kernels_.clear();
}

// ── popcorn ─────────────────────────────────────────────────────────
void OnboardingView::pop(double now, bool to_mouth) {
  const Lay& L = L_[0];
  Kernel k{};
  k.t0 = now;
  k.spin = rng_.pick(2);
  k.x = rng_.range(-4.0, 4.0);
  k.y = 1;
  if (to_mouth) {
    // a ballistic arc that ends in the mouth
    double cell = L.bucket_cell * s;
    double mx = (face.cx - (L.bucket_x + kBucketW * cell / 2.0)) / cell;
    double my = (face.cy + 1.9 * std::lround(3 * face.scale) * s - L.bucket_y) / cell;
    double T = 0.62;
    k.vx = (mx - k.x) / T;
    k.vy = (my - k.y - 0.5 * kGravity * T * T) / T;
    k.life = T;
    k.to_mouth = true;
  } else {
    k.vx = rng_.range(-15.0, 15.0);
    if (std::fabs(k.vx) < 3) k.vx = k.vx < 0 ? -4 : 4;
    k.vy = -rng_.range(28.0, 40.0);
    k.life = 2.2;
  }
  kernels_.push_back(k);
  ++pops_;
}

// ── tick ────────────────────────────────────────────────────────────
bool OnboardingView::tick(double now, double dt) {
  bool moving = false;
  double t = now - t0_;
  // the bucket rises in once the eyes are there
  if (t >= 0.35 && bucket_.target != 0 && finish_t0_ < 0) { bucket_.target = 0; bucket_.v = -6; }
  moving |= bucket_.step(dt);
  moving |= pos_.step(dt);
  moving |= dot_.step(dt);
  moving |= knob_.step(dt);
  moving |= glow_.step(dt);
  moving |= seg_x_.step(dt) | seg_w_.step(dt);
  place_face(now, false);
  moving |= fx_.step(dt) | fy_.step(dt) | fs_.step(dt);
  place_face(now, false);

  bool intro_visible = pos_.x < 0.999 && finish_t0_ < 0;
  if (intro_visible && !reduce_motion()) {
    if (now >= next_pop_) {
      bool burst = now < burst_until_;
      bool mouth = (pops_ == 3) || (!burst && rng_.uni() < 0.3);
      pop(now, mouth);
      if (burst) { happy_until_ = std::max(happy_until_, now + 1.4); face.hop.v = -30; }
      next_pop_ = now + (burst ? 0.11 : rng_.range(1.3, 3.0));
    }
    moving = true;
  }
  // kernels: fly, and either fall back into the bucket, out of the picture, or into the mouth
  for (auto& k : kernels_) {
    double kt = now - k.t0;
    if (k.to_mouth) {
      if (kt >= k.life - 0.24 && face.gulp_t0 < 0) face.mouth.target = std::max(face.mouth.target, 0.85);
      if (kt >= k.life) { face.gulp(now); k.life = -1; }
    }
  }
  for (auto& k : kernels_) {
    if (k.life < 0) continue;
    double kdt = std::min(dt, 0.05);
    k.vy += kGravity * kdt;
    k.x += k.vx * kdt;
    k.y += k.vy * kdt;
    if (!k.to_mouth && k.vy > 0 && std::fabs(k.x) < kBucketW / 2.0 - 2 && k.y > 2) k.life = -1;  // back in
    if (!k.to_mouth && now - k.t0 > k.life) k.life = -1;
  }
  kernels_.erase(std::remove_if(kernels_.begin(), kernels_.end(), [](const Kernel& k) { return k.life < 0; }),
                 kernels_.end());
  if (!kernels_.empty()) moving = true;

  // where the eyes go: the newest kernel, the control being talked about, or the cursor
  if (finish_t0_ < 0) {
    bool cursor = tx_ > -100000 && now - track_moved_ < 2.5;
    if (page_ == 0 && !kernels_.empty()) {
      const Lay& L = L_[0];
      const Kernel& k = kernels_.back();
      double cell = L.bucket_cell * s;
      face.look_at(L.bucket_x + (kBucketW / 2.0 + k.x) * cell, L.bucket_y + k.y * cell, 0.9);
    } else if (cursor) {
      face.look_at(tx_, ty_, 0.7);
    } else if (page_ == 1) {
      face.look_at(L_[1].toggle.cx(), L_[1].toggle.cy(), 0.8);
    } else if (page_ == 2 && L_[2].segs.size() == 4) {
      face.look_at(seg_x_.x + seg_w_.x / 2, L_[2].segs[0].cy(), 0.8);
    } else {
      face.look(0, 0);
    }
  }
  // peeking over the picture, a downward glance would sink the eyes behind it
  if (page_ != 0 && finish_t0_ < 0) face.gy.target = std::min(face.gy.target, 0.0);
  face.happy = now < happy_until_ || face.gulp_t0 > 0;
  face.thinking = page_ == 1 && !ambient && now - toggle_t_ < 1.2;
  moving |= face.tick(now, dt);
  moving |= chrome.tick(dt);

  hov_.resize(kDot3 + 1, 0.0);
  int act = kb_ ? focus_ : hover_;
  for (int id = 0; id <= kDot3; ++id) {
    double v = hover_step(hov_[id], act == id, dt);
    if (v != hov_[id]) { hov_[id] = v; moving = true; }
  }
  double typed_end = kIntroType + cps(kIntroHeading) * kCharDur + 0.9;
  if (t < typed_end) moving = true;
  if (finish_t0_ > 0 && now - finish_t0_ < 0.6) moving = true;
  if (now - toggle_t_ < 1.3 || now - scale_t_ < 0.7) moving = true;
  if (pos_.x > 2.001 && finish_t0_ < 0 && !reduce_motion()) moving = true;  // the icons keep signalling
  return moving;
}

double OnboardingView::next_event(double now) const {
  if (pos_.x < 0.999 && finish_t0_ < 0) return 0;  // the popcorn keeps going on the first screen
  if (chrome.vis.v != chrome.vis.target) return 0;
  return face.next_event(now);
}

// ── input ───────────────────────────────────────────────────────────
int OnboardingView::hit(int x, int y) const {
  if (finish_t0_ > 0) return kNone;
  const Lay& L = L_[page_];
  if (L.next.grow(2 * s).contains(x, y)) return kNext;
  if (L.back.grow(2 * s).contains(x, y)) return page_ == 0 ? kSkip : kBack;
  for (int i = 0; i < (int)L.dots.size(); ++i)
    if (L.dots[i].contains(x, y)) return kDot0 + i;
  if (page_ == 1 && L.toggle.grow(3 * s).contains(x, y)) return kToggle;
  if (page_ == 1) {  // the label next to the toggle flips it too
    Rect lab{L.toggle.r(), L.toggle.y, (int)(8 * s) + text_w("desligada", s), L.toggle.h};
    if (lab.contains(x, y)) return kToggle;
  }
  if (page_ == 2)
    for (int i = 0; i < (int)L.segs.size(); ++i)
      if (L.segs[i].contains(x, y)) return kSeg0 + i;
  return kNone;
}

bool OnboardingView::is_background(int x, int y) const {
  if (hit(x, y) != kNone || chrome.area(w).contains(x, y)) return false;
  return !face.bounds().contains(x, y);
}

std::vector<int> OnboardingView::focus_order() const {
  std::vector<int> o;
  if (page_ == 1) o.push_back(kToggle);
  if (page_ == 2) o.push_back(kSeg0 + std::clamp(scale_index(), 0, 3));
  o.push_back(page_ == 0 ? kSkip : kBack);
  o.push_back(kNext);
  return o;
}

ObAction OnboardingView::pick_scale(int index, double now) {
  int id = ui_size_choice(std::clamp(index, 0, 3));
  if (id == ui_scale) return {};
  set_scale_choice(id, now);
  return {ObAct::UiScale, id};
}

ObAction OnboardingView::activate(int id, double now) {
  switch (id) {
    case kNext:
      if (page_ == kPages - 1) return {ObAct::Finish, 0};
      go(page_ + 1, now);
      return {};
    case kBack: go(page_ - 1, now); return {};
    case kSkip: return {ObAct::Finish, 0};
    case kToggle: {
      bool on = !ambient;
      set_ambient(on, now);
      return {ObAct::Ambient, on ? 1 : 0};
    }
    case kSeg0: case kSeg1: case kSeg2: case kSeg3: return pick_scale(id - kSeg0, now);
    case kDot0: case kDot1: case kDot2: case kDot3: go(id - kDot0, now); return {};
    default: return {};
  }
}

ObAction OnboardingView::mouse_move(int x, int y, double now) {
  (void)now;
  mx_ = x; my_ = y;
  kb_ = false;
  chrome.vis.target = 1;
  chrome.hot = chrome.hit(x, y, w);
  hover_ = hit(x, y);
  return {};
}

void OnboardingView::mouse_leave(double now) {
  (void)now;
  hover_ = kNone;
  mx_ = my_ = -1;
  chrome.vis.target = 0;
  chrome.hot = -1;
}

ObAction OnboardingView::mouse_down(int x, int y, double now) {
  switch (chrome.hit(x, y, w)) {
    case WindowChrome::Min: return {ObAct::WinMin, 0};
    case WindowChrome::Max: return {ObAct::WinMax, 0};
    case WindowChrome::Close: return {ObAct::WinClose, 0};
    default: break;
  }
  if (face.bounds().contains(x, y) && finish_t0_ < 0) {  // a poke: it hops and smiles
    face.hop.v = -40;
    happy_until_ = now + 0.7;
    if (page_ == 0) pop(now, true);
    return {};
  }
  return activate(hit(x, y), now);
}

ObAction OnboardingView::key(const std::string& k, bool shift, double now) {
  if (finish_t0_ > 0) return {};
  if (k == "esc") return {ObAct::Finish, 0};
  if (k == "tab") {
    kb_ = true;
    auto o = focus_order();
    auto it = std::find(o.begin(), o.end(), focus_);
    int i = it == o.end() ? (shift ? (int)o.size() - 1 : 0)
                          : ((int)(it - o.begin()) + (shift ? (int)o.size() - 1 : 1)) % (int)o.size();
    focus_ = o[i];
    return {};
  }
  if (page_ == 2 && (k == "left" || k == "right")) {
    kb_ = true;
    focus_ = kSeg0 + std::clamp(scale_index() + (k == "right" ? 1 : -1), 0, 3);
    return pick_scale(focus_ - kSeg0, now);
  }
  if (k == "space" && page_ == 1 && (!kb_ || focus_ == kToggle || focus_ == kNone)) return activate(kToggle, now);
  if (k == "enter" || k == "space") {
    // enter on a choice confirms it and moves on; on a button it presses that button
    bool choice = focus_ == kToggle || (focus_ >= kSeg0 && focus_ <= kSeg3);
    if (kb_ && focus_ != kNone && !(k == "enter" && choice)) return activate(focus_, now);
    return activate(kNext, now);
  }
  if (k == "right") return activate(kNext, now);
  if (k == "left" || k == "backspace") { go(page_ - 1, now); return {}; }
  return {};
}

bool OnboardingView::track(int x, int y, double now) {
  if (x == tx_ && y == ty_) return false;
  tx_ = x; ty_ = y;
  track_moved_ = now;
  return true;
}

// ── drawing ─────────────────────────────────────────────────────────
void OnboardingView::draw_bucket(Canvas& c, int x, int y, int cell, double a) const {
  const Palette& p = P();
  int px = cell * (int)std::lround(s);
  if (s != std::lround(s)) px = (int)std::lround(cell * s);
  for (int j = 0; j < kBucketH; ++j)
    for (int i = 0; i < kBucketW;) {
      char ch = kBucket[j][i];
      if (ch == '.') { ++i; continue; }
      int run = 1;
      while (i + run < kBucketW && kBucket[j][i + run] == ch) ++run;
      Rgb col = ch == '#' ? p.pixel : ch == '+' ? p.dim : ch == 'R' ? p.fg : ch == 'a' ? p.fg : p.faint;
      c.fill(x + i * px, y + j * px, run * px, px, col, a);
      i += run;
    }
}

void OnboardingView::draw_kernel(Canvas& c, double x, double y, int cell, int spin, double a) const {
  int px = (int)std::lround(cell * s);
  c.cells(kKernel[spin & 1], 3, 3, (int)std::lround(x - 1.5 * px), (int)std::lround(y - 1.5 * px), px, P().pixel, a);
}

void OnboardingView::draw_button(Canvas& c, const Rect& r, const std::string& label, bool primary, double hv,
                                 double a, bool chevron) const {
  const Palette& p = P();
  if (primary) c.cornered(r, (int)s, mix(p.hover, p.press, hv), a);
  else if (hv > 0) c.cornered(r, (int)s, p.hover, a * hv);
  int tw = text_w(label, s) + (chevron ? (int)std::lround(9 * s) : 0);
  int x = r.x + (r.w - tw) / 2, y = r.y + (int)std::lround(3 * s);
  Rgb col = primary ? mix(p.fg, p.pixel, hv) : mix(p.dim, p.fg, hv);
  text(c, label, x, y, s, col, a);
  if (chevron) {
    int nudge = (int)std::lround(hv * s);  // leans toward where it goes
    c.bitmap(bmp("chev-right5"), x + tw - (int)std::lround(3 * s) + nudge, y + (int)std::lround(2 * s), s, col, a);
  }
}

void OnboardingView::draw_toggle(Canvas& c, const Rect& r, double a) const {
  // a 14 x 8 grid of big pixels: stepped corners, a square knob that slides on a spring
  const Palette& p = P();
  double cell = r.w / 14.0;
  double k = std::clamp(knob_.x, -0.12, 1.12), kc = clamp01(k);
  double hv = std::max(hov(kToggle), 0.0);
  Rgb on = p.fg;
  auto box = [&](double gx, double gy, double gw, double gh, Rgb col, double al) {
    c.fill(r.x + gx * cell, r.y + gy * cell, gw * cell, gh * cell, col, al);
  };
  auto stepped = [&](double gx, double gy, double gw, double gh, Rgb col, double al) {
    box(gx + 1, gy, gw - 2, 1, col, al);
    box(gx, gy + 1, gw, gh - 2, col, al);
    box(gx + 1, gy + gh - 1, gw - 2, 1, col, al);
  };
  stepped(0, 0, 14, 8, mix(mix(p.faint, p.dim, hv * 0.6), on, kc), a);
  if (kc < 1) stepped(1, 1, 12, 6, p.bg, a * (1 - kc));  // off: an outlined track
  // tiny pixel marks inside the track: a dash when on, a ring when off
  if (kc > 0.5) box(3, 3, 1, 2, p.bg, a * (kc - 0.5) * 2);  // a little 'on' tick
  else {
    double ka = a * (1 - kc * 2);
    box(9.5, 3, 2, 1, p.faint, ka); box(9.5, 4, 2, 1, p.faint, ka);
  }
  double kx = 1 + 6 * k;
  Rgb knob = mix(mix(p.dim, p.fg, hv), p.bg, kc);
  stepped(kx, 1, 6, 6, knob, a);
}

void OnboardingView::draw_demo(Canvas& c, const Rect& r, double ox, double a, double now, int stage) const {
  const Palette& p = P();
  // three film moods the scene drifts through, each with the light it would cast
  struct Mood { Rgb top, bot, sun, far, near, glow_top, glow_bot; };
  static const Mood moods[3] = {
      {{38, 44, 104}, {232, 128, 72}, {255, 218, 150}, {74, 52, 92}, {26, 18, 38}, {96, 92, 190}, {244, 140, 76}},
      {{10, 54, 84}, {58, 186, 176}, {224, 255, 240}, {20, 88, 98}, {8, 36, 46}, {40, 120, 200}, {64, 206, 186}},
      {{30, 12, 64}, {186, 62, 168}, {255, 186, 238}, {58, 22, 82}, {18, 8, 30}, {120, 60, 210}, {214, 76, 190}},
  };
  double cyc = std::fmod(std::max(0.0, now) / 3.4, 3.0);
  int i0 = (int)cyc % 3, i1 = (i0 + 1) % 3;
  double f = smoothstep(0.72, 1.0, cyc - std::floor(cyc));
  auto M = [&](Rgb Mood::*m) { return mix(moods[i0].*m, moods[i1].*m, f); };
  Rgb top = M(&Mood::top), bot = M(&Mood::bot), sun = M(&Mood::sun), far = M(&Mood::far), near = M(&Mood::near);
  Rgb gtop = M(&Mood::glow_top), gbot = M(&Mood::glow_bot);

  int x0 = (int)std::lround(r.x + ox), y0 = r.y;
  int cell = std::max(1, (int)std::lround(K_)) * std::max(1, (int)std::lround(s));
  // the light: stepped rings of big pixels, ordered dithering at the edge (pixel language, no blur)
  double g = std::clamp(glow_.x, 0.0, 1.2);
  if (stage == 0 && g > 0.003) {
    int R = (int)std::lround(18 * K_ * s);
    int gc = cell * 2;
    static const int bayer[4] = {0, 2, 3, 1};
    for (int yy = y0 - R; yy < y0 + r.h + R; yy += gc)
      for (int xx = x0 - R; xx < x0 + r.w + R; xx += gc) {
        double cx = xx + gc / 2.0, cy = yy + gc / 2.0;
        double dx = std::max({(double)x0 - cx, 0.0, cx - (x0 + r.w)}), dy = std::max({(double)y0 - cy, 0.0, cy - (y0 + r.h)});
        double d = std::sqrt(dx * dx + dy * dy) / R;
        if (d <= 0 || d >= 1) continue;
        double fall = (1 - d) * (1 - d);
        double level = fall * 4;
        int th = bayer[((yy / gc) & 1) * 2 + ((xx / gc) & 1)];
        double q = std::floor(level + th / 4.0) / 4.0;  // 4 quantised steps, dithered between them
        if (q <= 0) continue;
        double ny = clamp01((cy - (y0 - R)) / (r.h + 2.0 * R));
        c.fill(xx, yy, gc, gc, mix(gtop, gbot, ny), a * g * std::min(1.0, q) * 0.62);
      }
  }
  if (stage == 0) return;
  // the little window and its film
  int ww = r.w, hh = r.h;
  c.fill(x0, y0, ww, hh, p.screen, a);
  int bands = 9;
  int sky_h = (int)(hh * 0.72);
  for (int b = 0; b < bands; ++b) {
    int by0 = y0 + sky_h * b / bands, by1 = y0 + sky_h * (b + 1) / bands;
    by0 = y0 + (by0 - y0) / cell * cell;
    by1 = y0 + (by1 - y0) / cell * cell;
    c.fill(x0, by0, ww, by1 - by0, mix(top, bot, b / (bands - 1.0)), a);
  }
  // sun, a pixel disc sinking slowly behind the hills
  double sr = 9 * K_ * s;
  double scx = x0 + ww * 0.62, scy = y0 + sky_h * 0.78 + std::sin(now * 0.6) * cell;
  for (int yy = (int)(scy - sr); yy < scy + sr; yy += cell)
    for (int xx = (int)(scx - sr); xx < scx + sr; xx += cell) {
      int qx = x0 + (xx - x0) / cell * cell, qy = y0 + (yy - y0) / cell * cell;
      double dx = qx + cell / 2.0 - scx, dy = qy + cell / 2.0 - scy;
      if (dx * dx + dy * dy <= sr * sr && qy < y0 + hh) c.fill(qx, qy, cell, cell, sun, a);
    }
  // two ridges of hills, drawn column by column on the cell grid
  for (int xx = 0; xx < ww; xx += cell) {
    double u = (double)xx / ww;
    double far_h = hh * (0.40 + 0.10 * std::sin(u * 7.0 + 1.3) + 0.05 * std::sin(u * 17.0));
    double near_h = hh * (0.26 + 0.08 * std::sin(u * 5.0 + 4.0) + 0.03 * std::sin(u * 23.0 + 2.0));
    int fy = y0 + hh - (int)far_h / cell * cell, ny = y0 + hh - (int)near_h / cell * cell;
    int cw = std::min(cell, ww - xx);
    c.fill(x0 + xx, fy, cw, y0 + hh - fy, far, a);
    c.fill(x0 + xx, ny, cw, y0 + hh - ny, near, a);
  }
  // a whisper of the player: a progress line along the bottom
  int ly = y0 + hh - 4 * cell / 2;
  double prog = std::fmod(std::max(0.0, now) / 24.0, 1.0);
  int lx0 = x0 + 4 * cell / 2, lw = ww - 8 * cell / 2;
  c.fill(lx0, ly, lw, std::max(1, cell / 2), p.pixel, a * 0.25);
  c.fill(lx0, ly, (int)(lw * (0.18 + 0.7 * prog)), std::max(1, cell / 2), p.pixel, a * 0.85);
  // frame: one art px of edge, stepped corners
  int e = (int)std::lround(s);
  c.fill(x0 + e, y0 - e, ww - 2 * e, e, p.edge, a);
  c.fill(x0 + e, y0 + hh, ww - 2 * e, e, p.edge, a);
  c.fill(x0 - e, y0 + e, e, hh - 2 * e, p.edge, a);
  c.fill(x0 + ww, y0 + e, e, hh - 2 * e, p.edge, a);
  // light off: the scene itself stays, only the room goes dark
  (void)gtop;
}

void OnboardingView::draw_dock_sample(Canvas& c, const Rect& r, double a, double now) const {
  // the real controls, at the real size: what the chosen size will look like while watching
  const Palette& p = P();
  const double s = ui_s;
  draw_panel(c, r.x, r.y, (int)std::lround(r.w / s), (int)std::lround(r.h / s), s, {2, 1}, p.sheet, a * 0.96, p.edge, a);
  int cy = r.y + (int)std::lround(6 * s);
  int x = r.x + (int)std::lround(8 * s);
  c.bitmap(bmp("rewind"), x, cy + (int)std::lround(1 * s), s, p.dim, a);
  x += (int)std::lround(17 * s);
  c.bitmap(bmp("pause"), x, cy, s, p.fg, a);
  x += (int)std::lround(13 * s);
  c.bitmap(bmp("forward"), x, cy + (int)std::lround(1 * s), s, p.dim, a);
  int rx = r.r() - (int)std::lround(8 * s);
  const BitmapDef* fs = bmp("fs-enter");
  const BitmapDef* cc = bmp("cc");
  if (fs) { rx -= (int)std::lround(fs->w * s); c.bitmap(fs, rx, cy, s, p.dim, a); }
  if (cc) { rx -= (int)std::lround((cc->w + 8) * s); c.bitmap(cc, rx, cy, s, p.dim, a); }
  // the timeline on the second row, with the time
  int ty = r.y + (int)std::lround(18 * s);
  std::string t0 = "0:42", t1 = "1:23:20";
  int lx = r.x + (int)std::lround(8 * s);
  text(c, t0, lx, ty - (int)std::lround(4 * s), s, p.dim, a);
  int tx1 = r.r() - (int)std::lround(8 * s) - text_w(t1, s);
  text(c, t1, tx1, ty - (int)std::lround(4 * s), s, p.dim, a);
  int bx = lx + text_w(t0, s) + (int)std::lround(8 * s), bw = tx1 - (int)std::lround(8 * s) - bx;
  if (bw > 10 * s) {
    double k = 0.36 + 0.02 * std::sin(now * 0.5);
    c.fill(bx, ty, bw, (int)std::max(1.0, s), p.faint, a);
    c.fill(bx, ty, (int)(bw * k), (int)std::max(1.0, s), p.fg, a);
    c.fill(bx + (int)(bw * k) - (int)s, ty - (int)s, (int)std::lround(2 * s), (int)std::lround(3 * s), p.pixel, a);
  }
}

void OnboardingView::render_page(Canvas& c, int pg, double ox, double a, double now, int stage) {
  if (a <= 0.003) return;
  if (stage == 0) {  // what sits behind the character: the light around the little window
    if (pg == 1) draw_demo(c, L_[1].demo, ox, a, now, 0);
    return;
  }
  const Palette& p = P();
  const Lay& L = L_[pg];
  const double W = w / s;
  const double CW = std::min(W - 32, 560.0);
  auto center_text = [&](const std::string& t, int y, double ts, Rgb col, double al) {
    int tw = text_w(t, ts);
    text(c, t, (int)std::lround(w / 2.0 - tw / 2.0 + ox), y, ts, col, al);
  };
  auto paragraph = [&](const char* body, int y, Rgb col, double al) {
    for (auto& line : wrap_balanced(body, (int)(CW * 0.86 * s), s)) { center_text(line, y, s, col, al); y += (int)std::lround(13 * s); }
    return y;
  };
  double t = now - t0_;
  if (pg == 0) {
    // the bucket comes up from below on a spring, then the words are typed
    int by = L.bucket_y + (int)std::lround(bucket_.x * L.bucket_cell * s);
    double ba = a * ease_k((t - 0.35) / 0.2);
    if (ba > 0) draw_bucket(c, (int)std::lround(L.bucket_x + ox), by, L.bucket_cell, ba);
    for (auto& k : kernels_) {
      double cell = L.bucket_cell * s;
      double kx = L.bucket_x + ox + (kBucketW / 2.0 + k.x) * cell, ky = by + k.y * cell;
      if (ky > L.next.y - 6 * s) continue;  // they fall out of sight before the footer
      double ka = k.to_mouth ? 1.0 : std::min(1.0, (k.life - (now - k.t0)) / 0.3);
      draw_kernel(c, kx, ky, L.bucket_cell, k.spin ^ ((int)((now - k.t0) / 0.09) & 1), a * std::max(0.0, ka));
    }
    std::string head = kIntroHeading;
    int n = cps(head);
    int shown = reduce_motion() ? n : std::clamp((int)std::floor((t - kIntroType) / kCharDur) + 1, 0, n);
    std::string part = first_cps(head, shown);
    double hs = 2 * s;
    int tw = text_w(head, hs);
    int hx = (int)std::lround(w / 2.0 - tw / 2.0 + ox);
    int pw = text(c, part, hx, L.text_y, hs, p.pixel, a);
    double done_at = kIntroType + n * kCharDur;
    // a caret while typing, blinking a moment after
    bool caret = t >= kIntroType - 0.3 && (t < done_at || (t < done_at + 0.9 && std::fmod(t - done_at, 0.45) < 0.24));
    if (caret && !reduce_motion())
      c.fill(hx + pw + (int)std::lround(2 * s), L.text_y + (int)std::lround(4 * hs / 2), (int)std::lround(hs * 1.5),
             (int)std::lround(hs * 7), p.dim, a);
  } else if (pg == 1) {
    draw_demo(c, L.demo, ox, a, now, 1);
    center_text(kAmbHeading, L.heading.y, 2 * s, p.pixel, a);
    paragraph(kAmbLine, L.text_y, p.dim, a);
    Rect tr = L.toggle;
    tr.x += (int)std::lround(ox);
    draw_toggle(c, tr, a);
    double kc = clamp01(knob_.x);
    int lx = tr.r() + (int)std::lround(8 * s), ly = tr.y + (int)std::lround(3 * s);
    // the label crossfades with the knob
    text(c, "ligada", lx, ly, s, p.fg, a * kc);
    text(c, "desligada", lx, ly, s, mix(p.dim, p.fg, hov(kToggle)), a * (1 - kc));
  } else if (pg == 3) {
    center_text(kNextHeading, L.heading.y, 2 * s, p.pixel, a);
    draw_upcoming(c, ox, a, now);
  } else {
    Rect dr = L.dock;
    dr.x += (int)std::lround(ox);
    draw_dock_sample(c, dr, a, now);
    center_text(kSizeHeading, L.heading.y, 2 * s, p.pixel, a);
    // the selector: a sliding highlight behind the chosen size
    Rect hl{(int)std::lround(seg_x_.x + ox), L.segs[0].y, (int)std::lround(seg_w_.x), L.segs[0].h};
    c.cornered(hl, (int)s, p.press, a);
    int sel = std::clamp(scale_index(), 0, 3);
    for (int i = 0; i < 4; ++i) {
      Rect r = L.segs[i];
      r.x += (int)std::lround(ox);
      double hv = hov(kSeg0 + i);
      if (i != sel && hv > 0) c.cornered(r, (int)s, p.hover, a * hv);
      int tw = text_w(kSizeNames[i], s);
      text(c, kSizeNames[i], r.x + (r.w - tw) / 2, r.y + (int)std::lround(3 * s), s,
           i == sel ? p.pixel : mix(p.dim, p.fg, hv), a);
    }
  }
  // footer buttons belong to the page (they crossfade with it, but do not slide)
  draw_button(c, L.back, pg == 0 ? "pular" : "voltar", false, hov(pg == 0 ? kSkip : kBack), a, false);
  draw_button(c, L.next, pg == kPages - 1 ? "começar" : "continuar", true, hov(kNext), a, true);
}

void OnboardingView::draw_upcoming(Canvas& c, double ox, double a, double now) {
  const Palette& p = P();
  const Lay& L = L_[3];
  int ic = icon_cell(s);
  double since = now - page_enter_;
  for (int i = 0; i < 3 && i < (int)L.items.size(); ++i) {
    const Upcoming& it = kUpcoming[i];
    // rows arrive one after the other, rising 6 px of art
    double k = reduce_motion() || page_ != 3 ? 1.0 : ease_out(clamp01((since - 0.10 - 0.08 * i) / 0.34));
    double ra = a * k;
    if (ra <= 0.003) continue;
    Rect r = L.items[i];
    r.x += (int)std::lround(ox);
    r.y += (int)std::lround((1 - k) * 6 * s);
    bool next = i == 0;  // the next build stands out; later things are quieter
    Rgb ink = next ? p.fg : p.dim;
    // the icon, centred in its box and on the row, its signal lighting up in turn
    int c0 = kIconCols, c1 = 0;
    for (int yy = 0; yy < kIconRows; ++yy)
      for (int xx = 0; xx < kIconCols; ++xx)
        if (kIcons[it.icon][yy][xx] != '.') { c0 = std::min(c0, xx); c1 = std::max(c1, xx); }
    int ix = r.x + (kIconCols - (c1 - c0 + 1)) * ic / 2 - c0 * ic;
    int iy = r.y + (r.h - kIconRows * ic) / 2;
    double cyc = std::fmod(std::max(0.0, now) * (next ? 1.4 : 1.0) + i * 0.37, 1.0);
    for (int yy = 0; yy < kIconRows; ++yy)
      for (int xx = 0; xx < kIconCols; ++xx) {
        char ch = kIcons[it.icon][yy][xx];
        if (ch == '.') continue;
        double ca = 1.0;
        if (ch >= '1' && ch <= '3') {
          int g = ch - '1';  // 1, 2, 3: each arc lights up a beat after the last
          double ph = cyc * 4 - g;
          ca = reduce_motion() ? 0.8 : 0.25 + 0.75 * clamp01(ph < 0 ? 0 : ph < 1.6 ? 1 : 0);
        } else if (ch == 'c') {
          ca = reduce_motion() || std::fmod(now, 1.0) < 0.55 ? 1.0 : 0.0;
        }
        if (ca <= 0) continue;
        c.fill(ix + xx * ic, iy + yy * ic, ic, ic, ink, ra * ca);
      }
    if (it.icon == 0) {
      // the phone's screen: the remote in the hand, play and pause in turn
      int sx = ix + 4 * ic, sy = iy + 4 * ic;
      bool playing = reduce_motion() || std::fmod(now, 2.4) < 1.2;
      if (playing) { c.fill(sx, sy, ic, 5 * ic, ink, ra); c.fill(sx + ic, sy + ic, ic, 3 * ic, ink, ra); c.fill(sx + 2 * ic, sy + 2 * ic, ic, ic, ink, ra); }
      else { c.fill(sx, sy, ic, 5 * ic, ink, ra); c.fill(sx + 2 * ic, sy, ic, 5 * ic, ink, ra); }
    }
    // words, centred on the row: title and its ripeness tag, then the description
    int n = i < (int)L.item_lines.size() ? L.item_lines[i] : 1;
    int block = (int)std::lround((kTitleH + kLineH * n - 3) * s);
    int tx = r.x + (int)std::lround((kIconCols * ic / s + kIconGap) * s);
    int ty = r.y + (r.h - block) / 2 + (int)std::lround(3 * s);
    int tw = text(c, it.title, tx, ty, s, next ? p.pixel : p.fg, ra);
    // the tag is a small button-sized pill: 16 art px tall, text 3 px from its top (room for descenders)
    int gx = tx + tw + (int)std::lround(8 * s);
    int gw = text_w(it.tag, s) + (int)std::lround(12 * s), gh = (int)std::lround(16 * s);
    Rect tag{gx, ty - (int)std::lround(3 * s), gw, gh};
    int pad = (int)std::lround(6 * s);
    if (next) {
      c.cornered(tag, (int)s, p.fg, ra);
      text(c, it.tag, gx + pad, ty, s, p.bg, ra);
    } else {
      c.cornered(tag, (int)s, p.faint, ra * 0.55);
      c.cornered(tag.inset((int)std::max(1.0, std::round(s))), (int)s, p.bg, ra);
      text(c, it.tag, gx + pad, ty, s, p.dim, ra);
    }
    int ly = ty + (int)std::lround(kTitleH * s);
    for (auto& line : wrap_text(it.line, r.r() - tx, s)) { text(c, line, tx, ly, s, p.dim, ra * (next ? 1.0 : 0.85)); ly += (int)std::lround(kLineH * s); }
  }
}

void OnboardingView::draw_footer(Canvas& c, double a, double now) {
  (void)now;
  const Palette& p = P();
  const Lay& L = L_[page_];
  for (int i = 0; i < kPages; ++i) {
    // the current page's dot stretches into a dash (and slides there on a spring)
    double on = clamp01(1 - std::fabs(i - dot_.x));
    double dw = (2 + 4 * on) * s, dh = 2 * s;
    const Rect& r = L.dots[i];
    double cx = r.x + r.w / 2.0;
    double hv = hov(kDot0 + i);
    c.fill(cx - dw / 2, r.y + (r.h - dh) / 2, dw, dh, mix(mix(p.faint, p.dim, hv), p.fg, on), a);
  }
}

void OnboardingView::render(Canvas& c, double now) {
  const Palette& p = P();
  if (c.w != w || c.h != h) c.resize(w, h);
  c.clear(p.bg);
  double t0 = now - t0_;
  double face_a = reduce_motion() ? 1.0 : ease_k((t0 - 0.1) / 0.3);
  face.now_hint = now;
  double fade = finish_t0_ > 0 ? 1 - ease_k((now - finish_t0_) / 0.22) : 1.0;
  auto alpha_of = [&](int pg) {
    // the page leaving fades quickly, the one arriving takes over: they barely overlap
    double d = std::fabs(pg - pos_.x);
    return d >= 1 ? 0.0 : ease_out(clamp01(1 - d * 1.7));
  };
  for (int pg = 0; pg < kPages; ++pg) render_page(c, pg, (pg - pos_.x) * 56 * s, alpha_of(pg) * fade, now, 0);
  double t = now - t0_;
  double intro_in = reduce_motion() ? 1.0 : ease_k((t - kIntroType - cps(kIntroHeading) * kCharDur - 0.2) / 0.4);
  for (int pg = 0; pg < kPages; ++pg) {
    double d = pg - pos_.x;
    if (std::fabs(d) >= 1) continue;
    double a = alpha_of(pg) * fade;
    if (pg == 0 && page_ == 0) {
      // the first screen's footer waits for the greeting
      double ox = d * 56 * s;
      render_page(c, pg, ox, a, now, 1);
      if (intro_in < 1) {
        // redraw the footer region over with the background, to fade the buttons in
        const Lay& L = L_[0];
        Rect band{0, L.next.y - (int)s, w, L.next.h + 2 * (int)s};
        c.fill(band, p.bg, 1 - intro_in);
      }
      continue;
    }
    render_page(c, pg, d * 56 * s, a, now, 1);
  }
  draw_footer(c, fade * (page_ == 0 && pos_.x < 0.5 ? intro_in : 1.0), now);
  chrome.draw(c, w);
  // the character belongs to the first screen: it slides and fades with it, and only flies
  // to the home when the welcome ends while it is on screen
  double fa = finish_t0_ > 0 ? (face_handoff_ ? 1.0 : 0.0) : face_a * alpha_of(0);
  if (fa > 0.003) {
    double keep = face.cx;
    if (finish_t0_ < 0) face.cx += (0 - pos_.x) * 56 * s;
    face.render(c, fa);
    face.cx = keep;
  }
}

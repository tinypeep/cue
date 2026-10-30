#include "home.h"

#include <algorithm>
#include <cmath>

#include "../core/i18n.h"

namespace {
constexpr int kRowPitch = 16;  // art px
constexpr int kMaxRows = 6;
constexpr double kEggDur[5] = {0, 4.2, 3.4, 4.4, 3.6};
}  // namespace

void HomeView::set_size(int w_, int h_, double s_) {
  w = w_;
  h = h_;
  s = std::max(1.0, s_);
  chrome.s = s;
  layout();
}

void HomeView::set_rows(std::vector<HomeRow> r) {
  if ((int)r.size() > kMaxRows) r.resize(kMaxRows);
  rows = std::move(r);
  if (focus_ >= (int)rows.size() && focus_ < kOpen) focus_ = -1;
  layout();
}

void HomeView::layout() {
  const int W = w / s, H = h / s;
  // the composition keeps its proportions on wide or huge windows: it never spreads past
  // 700 px of art and stays centred (ultrawide, 1440p, 4K at 100% look like a poster, not a sparse grid)
  const int CW = std::min(W, 700), ox = (W - CW) / 2;
  const int split = (int)std::lround(CW * 0.42);
  double scale = std::clamp(std::min(CW, H * 16 / 9) / 180.0, 1.34, 3.34);
  face.layout((ox + split * 0.56) * s, (H * 0.47) * s, s, std::round(scale * 3) / 3.0);
  face.max_look = 3.0 * face.scale / 1.5;

  // micro-instruction button under the eyes
  int iw = 11 + 6 + text_w(tr(str::home_open_file), 1);
  int ix = (int)std::lround(ox + split * 0.56 - iw / 2.0);
  int iy = (int)std::lround(face.eye_y() / s + 9 * face.scale + 14);
  r_open_ = {(ix - 5) * s, (iy - 3) * s, (iw + 10) * s, 18 * s};

  // Recentes column
  int x0 = ox + (int)std::lround(CW * 0.49);
  int x1 = ox + CW - (int)std::lround(CW * 0.09);
  int n = std::max(1, (int)rows.size());
  int block = 12 + 9 + n * kRowPitch;
  int y0 = (int)std::lround(H * 0.47 - block / 2.0 - 3);
  r_title_ = {x0 * s, y0 * s, (x1 - x0) * s, 12 * s};
  r_dots_ = {(x1 - 9) * s, (y0 + 1) * s, 11 * s, 10 * s};
  r_rows_.clear();
  for (int i = 0; i < (int)rows.size(); ++i)
    r_rows_.push_back({(x0 - 5) * s, (y0 + 21 + i * kRowPitch) * s, (x1 - x0 + 10) * s, 14 * s});
  r_list_ = {(x0 - 8) * s, y0 * s - 6 * s, (x1 - x0 + 16) * s, (block + 12) * s};
  layout_menu();
  layout_update();
}

std::string HomeView::menu_label(int id) const {
  if (id == kMenuLang) return tr(str::more_language);
  if (id == kMenuClear) return tr(str::home_clear_recents);
  if (id == kMenuRemote) return tr(str::remote_title);
  if (id == kLangOpt) return tr(str::language_auto);
  if (id > kLangOpt && id <= kLangOpt + i18n::kLangCount) return i18n::native_name((i18n::Lang)(id - kLangOpt - 1));
  return "";
}

// The menu hangs under the ··· at the end of the Recentes title, as wide as its longest word.
void HomeView::layout_menu() {
  menu_ids_.clear();
  r_menu_items_.clear();
  if (menu_lang_) {
    for (int i = 0; i <= i18n::kLangCount; ++i) menu_ids_.push_back(kLangOpt + i);
  } else {
    menu_ids_.push_back(kMenuLang);
    menu_ids_.push_back(kMenuRemote);
    if (!rows.empty()) menu_ids_.push_back(kMenuClear);
  }
  int words = 0;
  for (int id : menu_ids_) words = std::max(words, text_w(menu_label(id), 1));
  const int lead = menu_lang_ ? 14 : 4, trail = menu_lang_ ? 6 : 14;  // check column / chevron
  const int W = std::max(84, 2 + lead + words + trail + 2), H = 4 + 13 * (int)menu_ids_.size();
  const int x1 = r_title_.r() / s, y0 = r_title_.y / s;
  r_menu_ = {(x1 + 2 - W) * s, (y0 + 12) * s, W * s, H * s};
  for (size_t i = 0; i < menu_ids_.size(); ++i)
    r_menu_items_.push_back({r_menu_.x + 2 * s, r_menu_.y + (2 + 13 * (int)i) * s, (W - 4) * s, 13 * s});
}

void HomeView::open_menu(bool lang) {
  menu_ = true;
  menu_lang_ = lang;
  layout_menu();
  if (kb_) focus_ = menu_ids_.empty() ? -1 : (lang ? kLangOpt + language + 1 : menu_ids_[0]);
}

void HomeView::layout_update() {
  r_update_ = r_update_skip_ = r_news_ = r_news_close_ = {};
  const int H = h / s;
  int x = 10;
  if (!update_version.empty()) {
    std::string label = i18n::format(str::home_update_offer, {{"version", update_version}});
    int tw = 11 + 6 + text_w(label, 1);
    r_update_ = {x * s, (H - 24) * s, (tw + 10) * s, 16 * s};
    r_update_skip_ = {r_update_.r() + 2 * s, r_update_.y, 14 * s, 16 * s};
    x += tw + 10 + 2 + 14 + 10;
  }
  if (news) {
    int tw = 11 + 6 + text_w(tr(str::home_news), 1);
    r_news_ = {x * s, (H - 24) * s, (tw + 10) * s, 16 * s};
    r_news_close_ = {r_news_.r() + 2 * s, r_news_.y, 14 * s, 16 * s};
  }
}

int HomeView::hit(int x, int y) const {
  if (menu_ && r_menu_.contains(x, y)) {
    for (size_t i = 0; i < r_menu_items_.size(); ++i)
      if (r_menu_items_[i].contains(x, y)) return menu_ids_[i];
    return kMenuPad;
  }
  if (!update_version.empty() && !update_busy) {
    if (r_update_.contains(x, y)) return kUpdate;
    if (r_update_skip_.contains(x, y)) return kUpdateSkip;
  }
  if (news) {
    if (r_news_.contains(x, y)) return kNews;
    if (r_news_close_.contains(x, y)) return kNewsClose;
  }
  if (r_dots_.contains(x, y)) return kDots;
  for (int i = 0; i < (int)r_rows_.size(); ++i)
    if (r_rows_[i].contains(x, y)) return i;
  if (r_open_.contains(x, y) && engine_problem.empty()) return kOpen;
  return -1;
}

void HomeView::set_hover_row(int i, double now) {
  hover_ = i;
  update_face_target(now);
}

void HomeView::update_face_target(double now) {
  if (egg_ != 0) return;
  if (leave_t0_ > 0) return;
  if (drag_) {
    face.set_mode(Face::Mode::Drag, now);
    face.look_at(mx_, my_);
    // the closer the file gets to the face, the more it wants it
    double d = std::hypot(mx_ - face.cx, my_ - face.cy) / std::max(1.0, w * 0.42);
    face.excite = std::clamp(1.0 - d, 0.0, 1.0);
    face.excite *= face.excite * (3 - 2 * face.excite);  // smoothstep
    return;
  }
  int a = active();
  if (a >= 0 && a < (int)r_rows_.size()) {
    face.set_mode(Face::Mode::Recent, now);
    // looks a few px to the right, following the row a little
    face.look_at(r_rows_[a].x + r_rows_[a].w * 0.2, r_rows_[a].cy(), 0.9);
  } else if (tx_ > -100000 && now - track_moved_ < 6.0 &&
             std::hypot(tx_ - face.cx, ty_ - face.eye_y()) < std::max(w, h) * 0.55) {
    // follows the cursor discreetly, and only when it comes near
    face.set_mode(Face::Mode::Follow, now);
    double d = std::hypot(tx_ - face.cx, ty_ - face.eye_y()) / (double)std::max(1, w);
    face.look_at(tx_, ty_, std::clamp(0.45 + d * 0.5, 0.45, 0.7));
  } else {
    face.set_mode(Face::Mode::Idle, now);
  }
}

bool HomeView::is_background(int x, int y) const {
  if (hit(x, y) != -1 || chrome.area(w).contains(x, y)) return false;
  if (face.bounds().contains(x, y)) return false;  // the eyes take clicks (and keep a secret)
  return true;
}

double HomeView::hov(int id) const {
  int k = id >= 100 ? (int)rows.size() + (id - 100) : id;
  return (k >= 0 && k < (int)hov_.size()) ? ease_out(hov_[k]) : 0.0;
}

HomeAction HomeView::mouse_move(int x, int y, double now) {
  mx_ = x;
  my_ = y;
  kb_ = false;
  chrome.vis.target = 1;
  chrome.hot = chrome.hit(x, y, w);
  int nh = hit(x, y);
  if (nh != hover_) { hover_ = nh; update_face_target(now); }
  if (egg_ == 4) face.look_at(x, y, 1.0);
  return {};
}

void HomeView::mouse_leave(double now) {
  hover_ = -1;
  chrome.vis.target = 0;
  chrome.hot = -1;
  mx_ = my_ = -1;
  update_face_target(now);
}

HomeAction HomeView::mouse_down(int x, int y, double now) {
  switch (chrome.hit(x, y, w)) {
    case WindowChrome::Min: return {HomeAct::WinMin, -1};
    case WindowChrome::Max: return {HomeAct::WinMax, -1};
    case WindowChrome::Close: return {HomeAct::WinClose, -1};
    case WindowChrome::Pin: return {HomeAct::WinPin, -1};
    default: break;
  }
  int hh = hit(x, y);
  bool in_menu = hh == kMenuPad || hh == kMenuClear || hh == kMenuLang || hh == kMenuRemote || (hh >= kLangOpt && hh <= kLangOpt + i18n::kLangCount);
  if (menu_ && !in_menu && hh != kDots) { menu_ = false; return {}; }
  if (hh == kMenuPad) return {};
  if (hh == kMenuClear) { menu_ = false; return {HomeAct::ClearRecents, -1}; }
  if (hh == kMenuRemote) { menu_ = false; return {HomeAct::Remote, -1}; }
  if (hh == kMenuLang) { open_menu(true); return {}; }
  if (hh >= kLangOpt && hh <= kLangOpt + i18n::kLangCount) return {HomeAct::Language, hh - kLangOpt - 1};
  if (hh == kDots) { if (menu_) menu_ = false; else open_menu(false); return {}; }
  if (hh == kOpen) return {HomeAct::OpenDialog, -1};
  if (hh == kUpdate) return {HomeAct::Update, -1};
  if (hh == kUpdateSkip) return {HomeAct::SkipUpdate, -1};
  if (hh == kNews) return {HomeAct::News, -1};
  if (hh == kNewsClose) return {HomeAct::NewsClose, -1};
  if (hh >= 0 && hh < (int)rows.size()) return {HomeAct::OpenRecent, hh};
  if (face.bounds().contains(x, y)) {
    clicks_.push_back(now);
    clicks_.erase(std::remove_if(clicks_.begin(), clicks_.end(), [&](double t) { return now - t > 2.2; }), clicks_.end());
    if (clicks_.size() >= 7 && egg_ == 0) {
      clicks_.clear();
      start_egg(now);
      return {HomeAct::EggSeen, egg_variant_};
    }
  }
  return {};
}

HomeAction HomeView::key(const std::string& k, bool shift, double now) {
  std::vector<int> order;
  if (engine_problem.empty()) order.push_back(kOpen);
  for (int i = 0; i < (int)rows.size(); ++i) order.push_back(i);
  order.push_back(kDots);
  if (menu_) for (int id : menu_ids_) order.push_back(id);
  if (!update_version.empty() && !update_busy) order.push_back(kUpdate);
  if (news) { order.push_back(kNews); order.push_back(kNewsClose); }
  auto idx = [&] { auto it = std::find(order.begin(), order.end(), focus_); return it == order.end() ? -1 : (int)(it - order.begin()); };
  if (k == "tab") {
    kb_ = true;
    int i = idx();
    int n = (int)order.size();
    if (n == 0) return {};
    i = i < 0 ? (shift ? n - 1 : 0) : (i + (shift ? n - 1 : 1)) % n;
    focus_ = order[i];
  } else if (k == "down" || k == "up") {
    kb_ = true;
    if (rows.empty()) return {};
    if (focus_ < 0 || focus_ >= (int)rows.size()) focus_ = k == "down" ? 0 : (int)rows.size() - 1;
    else focus_ = std::clamp(focus_ + (k == "down" ? 1 : -1), 0, (int)rows.size() - 1);
  } else if (k == "enter" || (k == "space" && kb_)) {
    if (focus_ == kOpen) return {HomeAct::OpenDialog, -1};
    if (focus_ == kUpdate) return {HomeAct::Update, -1};
    if (focus_ == kNews) return {HomeAct::News, -1};
    if (focus_ == kNewsClose) { focus_ = -1; return {HomeAct::NewsClose, -1}; }
    if (focus_ == kDots) { if (menu_) menu_ = false; else open_menu(false); }
    else if (focus_ == kMenuClear) { menu_ = false; focus_ = -1; return {HomeAct::ClearRecents, -1}; }
    else if (focus_ == kMenuRemote) { menu_ = false; focus_ = -1; return {HomeAct::Remote, -1}; }
    else if (focus_ == kMenuLang) open_menu(true);
    else if (focus_ >= kLangOpt && focus_ <= kLangOpt + i18n::kLangCount) return {HomeAct::Language, focus_ - kLangOpt - 1};
    else if (focus_ >= 0 && focus_ < (int)rows.size()) return {HomeAct::OpenRecent, focus_};
  } else if (k == "esc") {
    if (menu_ && menu_lang_) { open_menu(false); if (kb_) focus_ = kMenuLang; }  // one level back
    else { menu_ = false; kb_ = false; focus_ = -1; }
  }
  update_face_target(now);
  return {};
}

bool HomeView::track(int x, int y, double now) {
  if (x == tx_ && y == ty_) {
    // still for a while: back to contemplating on its own
    if (face.mode == Face::Mode::Follow && now - track_moved_ > 6.0) { face.set_mode(Face::Mode::Idle, now); return true; }
    return false;
  }
  tx_ = x; ty_ = y;
  track_moved_ = now;
  update_face_target(now);
  return true;
}

void HomeView::drag_over(int x, int y, double now) {
  mx_ = x;
  my_ = y;
  if (!drag_) face.notice(now);  // Omni: it notices, crouches and hops
  drag_ = true;
  update_face_target(now);
}
void HomeView::drag_leave(double now) {
  drag_ = false;
  update_face_target(now);
}

void HomeView::leave(double now, bool swallow) {
  if (leave_t0_ > 0) return;
  swallow_ = swallow;
  leave_t0_ = now;
  menu_ = false;
  if (swallow) { face.set_mode(Face::Mode::Idle, now); face.look(0, 0); face.gulp(now); }
  else { face.closed = true; }  // lights off, the film starts
}

void HomeView::cancel_leave(double now) {
  if (leave_t0_ < 0) return;
  leave_t0_ = -1;
  enter(now);
}

void HomeView::enter(double now, bool keep_face) {
  leave_t0_ = -1;
  enter_t0_ = now;
  keep_face_ = keep_face;
  face.closed = false;
  face.happy = false;
  face.mouth.snap(0);
  face.notice(now);
  update_face_target(now);
}

void HomeView::fades(double now, double& content, double& fa) const {
  content = fa = 1;
  if (leave_t0_ > 0) {
    double t = now - leave_t0_;
    if (swallow_) {
      content = 1 - ease_out(std::clamp(t / 0.16, 0.0, 1.0));
      fa = 1 - ease_in_out(std::clamp((t - 0.2) / 0.16, 0.0, 1.0));  // eats it, then goes
    } else {
      content = 1 - ease_out(std::clamp(t / 0.16, 0.0, 1.0));
      fa = 1 - ease_out(std::clamp((t - 0.04) / 0.16, 0.0, 1.0));
    }
  } else if (now - enter_t0_ < 0.4) {
    double t = now - enter_t0_;
    fa = keep_face_ ? 1.0 : ease_out(std::clamp(t / 0.22, 0.0, 1.0));
    content = ease_out(std::clamp((t - 0.06) / 0.26, 0.0, 1.0));
  }
}

// ── easter egg ──────────────────────────────────────────────────────
void HomeView::start_egg(double now, int forced) {
  int v = forced;
  if (!v) {
    std::vector<int> unseen;
    for (int i = 1; i <= 3; ++i)
      if (!(eggs_seen & (1 << i))) unseen.push_back(i);
    if (!unseen.empty()) v = unseen[rng_.pick((int)unseen.size())];
    else v = rng_.uni() < 0.16 ? 4 : 1 + rng_.pick(3);
  }
  egg_ = v;
  egg_variant_ = v;
  eggs_seen |= (1 << v);
  egg_t0_ = now;
  spots_.clear();
  if (v == 3) {
    // little pairs of eyes in the dark, all looking at the character
    Rect avoid = face.bounds().grow(14 * s);
    for (int i = 0; spots_.size() < 11 && i < 400; ++i) {
      double x = rng_.range(0.04, 0.96) * w, y = rng_.range(0.08, 0.92) * h;
      Rect r{(int)x - 10 * s, (int)y - 6 * s, 20 * s, 12 * s};
      bool bad = r.x < 0 || r.r() > w || r.y < 0 || r.b() > h;
      auto inter = [&](const Rect& a) { return !(r.r() < a.x || a.r() < r.x || r.b() < a.y || a.b() < r.y); };
      if (inter(avoid) || inter(r_list_) || inter(r_open_.grow(4 * s))) bad = true;
      for (auto& sp : spots_) if (std::hypot(sp.x - x, sp.y - y) < 26 * s) bad = true;
      if (bad) continue;
      spots_.push_back({x, y, 0.25 + spots_.size() * 0.22, rng_.uni() < 0.3 ? 1 : 2});
    }
  }
  if (v == 4) { face.set_mode(Face::Mode::Stare, now); face.max_look = 3.0 * face.scale; }
}

bool HomeView::tick(double now, double dt) {
  bool moving = false;
  if (egg_) {
    double t = now - egg_t0_;
    moving = true;
    switch (egg_) {
      case 1:  // Nosferatu: the eyes watch the shadow, then blink it away
        if (t > 1.6) face.look(face.max_look, 0.4);
        face.wide = t > 2.6 && t < 3.4;
        face.closed = t >= 3.4 && t < 3.66;
        break;
      case 2: {
        double k = t < 0.6 ? ease_out(t / 0.6) : (t < 2.6 ? 1.0 : 1.0 - ease_out((t - 2.6) / 0.7));
        face.lx_off = -2.0 * k; face.ly_off = -3.0 * k;
        face.rx_off = 1.0 * k;  face.ry_off = 2.0 * k;
        break;
      }
      case 3: {
        int shown = 0;
        for (auto& sp : spots_) if (t >= sp.at) ++shown;
        if (shown >= 4 && t < 3.4) face.set_mode(Face::Mode::Nervous, now);
        if (t >= 3.4) { face.set_mode(Face::Mode::Idle, now); face.look(0, 0); }
        break;
      }
      case 4:
        if (mx_ >= 0) face.look_at(mx_, my_, 1.0);
        break;
    }
    if (t >= kEggDur[egg_]) {
      egg_ = 0;
      face.wide = false;
      face.closed = false;
      face.lx_off = face.ly_off = face.rx_off = face.ry_off = 0;
      face.max_look = 3.0 * face.scale / 1.5;
      face.mode = Face::Mode::Recent;  // force a refresh
      face.set_mode(Face::Mode::Idle, now);
      update_face_target(now);
    }
  }
  if (!notice.empty() && now > notice_until) { notice.clear(); moving = true; }
  moving |= face.tick(now, dt);
  moving |= chrome.tick(dt);
  if (leave_t0_ > 0 && now - leave_t0_ < leave_dur() + 0.05) moving = true;
  if (now - enter_t0_ < 0.45) moving = true;
  size_t n = rows.size() + kHitIds;
  if (hov_.size() != n) hov_.assign(n, 0.0);
  for (size_t k = 0; k < n; ++k) {
    int id = k < rows.size() ? (int)k : 100 + (int)(k - rows.size());
    double v = hover_step(hov_[k], active() == id, dt);
    if (v != hov_[k]) { hov_[k] = v; moving = true; }
  }
  return moving;
}

double HomeView::next_event(double now) const {
  if (egg_ || chrome.vis.v != chrome.vis.target || leave_t0_ > 0 || now - enter_t0_ < 0.45) return 0;
  double t = face.next_event(now);
  if (!notice.empty()) t = std::min(t, notice_until - now);
  return std::max(0.0, t);
}

// A long-fingered shadow, built from rectangles on the art grid. (fx, fy) is the
// tip of the longest finger; the arm extends to the right, off screen.
void HomeView::draw_hand(Canvas& c, int fx, int fy, double a) const {
  Rgb col = P().edge;
  const int k = 2 * s;  // the shadow is drawn on a coarser grid, like a projected silhouette
  auto R = [&](int x, int y, int ww, int hh) { c.fill(fx + x * k, fy + y * k, ww * k, hh * k, col, a); };
  R(40, -2, 400, 10);   // forearm
  R(30, -5, 14, 14);    // palm
  R(28, 7, 8, 3);       // thumb base
  R(22, 10, 8, 2);      // thumb
  R(19, 11, 4, 2);
  const int fy0[4] = {-5, -1, 3, 7};
  const int len[4] = {26, 32, 29, 22};
  for (int i = 0; i < 4; ++i) {
    int x0 = 30 - len[i];
    R(x0 + 4, fy0[i], len[i] - 4, 2);       // long finger
    R(x0 + 1, fy0[i] + 1, 3, 2);            // last joint bends down
    R(x0, fy0[i] + 3, 2, 2);                // nail
    R(x0 + len[i] / 2, fy0[i] - 1, 3, 1);   // knuckle
  }
}

void HomeView::draw_caligari_shapes(Canvas& c, double k) const {
  if (k <= 0) return;
  // monochrome diagonal flats, like painted sets
  auto tri = [&](double ax, double ay, double bx, double by, double cx, double cy, Rgb col) {
    int minx = (int)std::min({ax, bx, cx}), maxx = (int)std::max({ax, bx, cx});
    int miny = (int)std::min({ay, by, cy}), maxy = (int)std::max({ay, by, cy});
    for (int y = std::max(0, miny); y < std::min(h, maxy); y += s)
      for (int x = std::max(0, minx); x < std::min(w, maxx); x += s) {
        double px = x + s / 2.0, py = y + s / 2.0;
        double d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
        double d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
        double d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
        bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
        if (!(neg && pos)) c.fill(x, y, s, s, col, k);
      }
  };
  tri(0, 0, w * 0.34, 0, 0, h * 0.78, P().band);
  tri(w, h, w * 0.52, h, w, h * 0.16, P().band);
  tri(w * 0.18, h, w * 0.30, h * 0.30, w * 0.40, h, P().hover);
  tri(w * 0.62, 0, w * 0.70, h * 0.62, w * 0.80, 0, P().hover);
}

void HomeView::distort(Canvas& c, double k) const {
  if (k <= 0) return;
  Canvas src = c;
  for (int y = 0; y < h; ++y) {
    double ny = (double)y / h - 0.5;
    double shear = k * ny * 0.30 * w * 0.25;
    double wob = k * std::sin(y / (23.0 * s)) * 2.0 * s;
    double sx = 1.0 + k * 0.10 * ny;  // perspective that does not add up
    int dx = (int)std::lround((shear + wob) / s) * s;
    for (int x = 0; x < w; ++x) {
      double fx = (x - w / 2.0) / sx + w / 2.0 - dx;
      int qx = (int)std::floor(fx / s) * s;
      uint32_t v = (qx >= 0 && qx < w) ? src.px[(size_t)y * w + qx] : src.px[(size_t)y * w];
      c.px[(size_t)y * w + x] = v;
    }
  }
}

// ── render ──────────────────────────────────────────────────────────
void HomeView::render(Canvas& c, double now) {
  const Palette& p = P();
  if (c.w != w || c.h != h) c.resize(w, h);
  c.clear(p.bg);
  double et = egg_ ? now - egg_t0_ : 0;

  double cal_k = 0;
  if (egg_ == 2) cal_k = et < 0.6 ? ease_out(et / 0.6) : (et < 2.6 ? 1.0 : 1.0 - ease_out((et - 2.6) / 0.7));
  draw_caligari_shapes(c, cal_k * 0.9);

  if (egg_ == 1 && et < 3.5) {
    // the shadow creeps in from the right, almost reaching the eyes
    double pr = ease_in_out(std::min(1.0, et / 3.3));
    int tip_end = (int)(face.cx + 8 * face.scale * s + 9 * face.scale * s);
    int tip = (int)std::lround(lerp(w + 8.0 * s, tip_end, pr) / s) * s;
    draw_hand(c, tip, (int)face.eye_y() - 4 * s, 1.0);
  }

  if (egg_ == 3) {
    for (auto& sp : spots_) {
      if (et < sp.at || et >= 3.4) continue;
      double a = std::min(1.0, (et - sp.at) / 0.35);
      int cell = sp.size * s;
      const BitmapDef* e = bmp("sprite:eye-open");
      double dx = face.cx - sp.x, dy = face.eye_y() - sp.y, l = std::hypot(dx, dy);
      int ox = (int)std::lround(dx / l * 1.0) * s, oy = (int)std::lround(dy / l * 1.0) * s;
      Rgb col = sp.size == 2 ? p.dim : p.faint;
      c.bitmap(e, (int)sp.x - 4 * cell + ox, (int)sp.y + oy, cell, col, a);
      c.bitmap(e, (int)sp.x + 2 * cell + ox, (int)sp.y + oy, cell, col, a);
    }
  }

  // micro-instruction / state line
  {
    std::string label = tr(str::home_open_file);
    Rgb col = p.dim;
    bool show_icon = true;
    if (!engine_problem.empty()) { label = engine_problem; show_icon = false; }
    else if (drag_) { label = tr(str::home_drop_here); col = p.fg; show_icon = false; }
    else if (!notice.empty()) { label = notice; show_icon = false; }
    double ha = show_icon ? hov(kOpen) : 0.0;
    int tw = text_w(label, s) + (show_icon ? 17 * s : 0);
    int x = (int)face.cx - tw / 2;
    int y = r_open_.y + 3 * s;
    if (ha > 0) c.cornered({x - 5 * s, r_open_.y, tw + 10 * s, r_open_.h}, s, p.hover, ha);
    if (show_icon) {
      action_icon(c, "cue:open-file", {x, y, 11 * s, 12 * s}, s, mix(p.dim, p.pixel, ha));
      x += 17 * s;
    }
    text(c, label, x, y, s, mix(col, p.fg, ha));
  }

  // Recentes
  text(c, tr(str::home_recents), r_title_.x, r_title_.y, s, p.fg);
  bool show_dots = menu_ || r_list_.contains(mx_, my_) || r_dots_.contains(mx_, my_) || (kb_ && focus_ == kDots);
  if (show_dots) {
    double on = menu_ ? 1.0 : hov(kDots);
    if (on > 0) c.cornered(r_dots_, s, p.hover, on);
    c.bitmap(bmp("dots"), r_dots_.x + 3 * s, r_dots_.y + 5 * s, s, mix(p.dim, p.fg, on));
  }
  if (rows.empty()) {
    text(c, tr(str::home_recents_empty), r_title_.x, r_title_.y + 21 * s, s, p.faint);
  }
  for (int i = 0; i < (int)rows.size(); ++i) {
    const Rect& r = r_rows_[i];
    double ha = hov(i);
    bool on = ha > 0.5;
    if (ha > 0) c.cornered(r, s, p.hover, ha);
    int x0 = r.x + 5 * s, x1 = r.r() - 5 * s;
    c.bitmap(bmp("k-video"), x0, r.y + 2 * s, s, mix(p.dim, p.fg, ha));
    const HomeRow& row = rows[i];
    int rw = text_w(row.right, s);
    bool resumable = row.progress >= 0;
    text(c, row.right, x1 - rw, r.y, s, resumable ? mix(p.dim, p.fg, ha) : p.faint);
    int nx = x0 + 13 * s;
    std::string nm = fit_middle(row.name, x1 - rw - 10 * s - nx, s);
    text(c, nm, nx, r.y, s, mix(p.dim, p.fg, ha));
    if (resumable) {
      // a 1 px whisper of progress under the name; the track only shows on hover
      int lw = x1 - nx, ly = r.y + 12 * s;
      int pw = std::max(s, (int)std::lround(lw * std::clamp(row.progress, 0.0, 1.0) / s) * s);
      if (on) c.fill(nx, ly, lw, s, p.edge);
      c.fill(nx, ly, pw, s, on ? p.dim : p.faint);
    }
  }
  if (menu_) {
    draw_panel(c, r_menu_.x, r_menu_.y, r_menu_.w / s, r_menu_.h / s, s, {1}, p.sheet, 1.0, p.edge, 1.0);
    for (size_t i = 0; i < menu_ids_.size(); ++i) {
      int id = menu_ids_[i];
      const Rect& ir = r_menu_items_[i];
      bool on = active() == id;
      if (on) c.cornered(ir, s, p.hover);
      bool chosen = id >= kLangOpt && id - kLangOpt - 1 == language;
      if (chosen) c.bitmap(bmp("check"), ir.x + 3 * s, ir.y + 4 * s, s, p.fg);
      text(c, menu_label(id), ir.x + (menu_lang_ ? 14 : 4) * s, ir.y + 1 * s, s, on || chosen ? p.fg : p.dim);
      if (id == kMenuLang) c.bitmap(bmp("chev-right5"), ir.r() - 8 * s, ir.y + 4 * s, s, on ? p.fg : p.dim);
    }
  }

  // a newer version: one quiet line, bottom left; a progress whisper while it downloads
  if (!update_version.empty()) {
    layout_update();
    double ha = update_busy ? 0.0 : hov(kUpdate);
    std::string label;
    if (update_progress >= 0)
      label = i18n::format(str::home_update_downloading, {{"version", update_version}, {"percent", std::to_string((int)std::lround(update_progress * 100))}});
    else if (update_busy) label = tr(str::home_update_preparing);
    else label = i18n::format(str::home_update_offer, {{"version", update_version}});
    Rect r = r_update_;
    if (ha > 0) c.cornered({r.x, r.y, 11 * s + 6 * s + text_w(label, s) + 10 * s, r.h}, s, p.hover, ha);
    int x = r.x + 5 * s, y = r.y + 3 * s;
    // a small pixel arrow pointing down: "comes in"
    static const char* kDown = "..#.." "..#.." "#.#.#" ".###." "..#..";
    c.cells(kDown, 5, 5, x + 3 * s, y + 3 * s, s, mix(update_busy ? p.dim : p.fg, p.pixel, ha));
    text(c, label, x + 17 * s, y, s, mix(update_busy ? p.dim : p.fg, p.pixel, ha));
    if (update_progress >= 0) {
      int lw = text_w(label, s), lx = x + 17 * s, ly = y + 12 * s;
      c.fill(lx, ly, lw, s, p.edge);
      c.fill(lx, ly, (int)std::lround(lw * std::clamp(update_progress, 0.0, 1.0) / s) * s, s, p.fg);
    }
    if (!update_busy) {
      double hs = hov(kUpdateSkip);
      bool show = hs > 0 || r_update_.grow(4 * s).contains(mx_, my_) || r_update_skip_.contains(mx_, my_);
      if (show) {
        if (hs > 0) c.cornered(r_update_skip_, s, p.hover, hs);
        c.bitmap(bmp("x-small"), r_update_skip_.x + 5 * s, r_update_skip_.y + 6 * s, s, mix(p.faint, p.fg, hs));
      }
    }
  }

  // what's new: the same quiet line, a little spark that twinkles now and then
  if (news) {
    layout_update();
    double ha = hov(kNews);
    std::string label = tr(str::home_news);
    Rect r = r_news_;
    if (ha > 0) c.cornered(r, s, p.hover, ha);
    int x = r.x + 5 * s, y = r.y + 3 * s;
    bool twinkle = !reduce_motion() && std::fmod(now, 3.2) < 0.18;
    static const char* kSpark = "..#.." "..#.." "##.##" "..#.." "..#..";
    static const char* kSpark2 = "....." "..#.." ".###." "..#.." ".....";
    c.cells(twinkle ? kSpark2 : kSpark, 5, 5, x + 3 * s, y + 3 * s, s, mix(p.fg, p.pixel, std::max(ha, twinkle ? 1.0 : 0.0)));
    text(c, label, x + 17 * s, y, s, mix(p.fg, p.pixel, ha));
    double hs = hov(kNewsClose);
    bool show = hs > 0 || r_news_.grow(4 * s).contains(mx_, my_) || r_news_close_.contains(mx_, my_) || (kb_ && (focus_ == kNews || focus_ == kNewsClose));
    if (show) {
      if (hs > 0) c.cornered(r_news_close_, s, p.hover, hs);
      c.bitmap(bmp("x-small"), r_news_close_.x + 5 * s, r_news_close_.y + 6 * s, s, mix(p.faint, p.fg, hs));
    }
  }

  chrome.draw(c, w);
  double ca, fa;
  fades(now, ca, fa);
  if (ca < 1) c.fill(0, 0, w, h, p.bg, 1 - ca);
  face.now_hint = now;
  if (fa > 0) face.render(c, fa);
  if (egg_ == 2) distort(c, cal_k);
  if (egg_ == 3 && et >= 3.4 && et < 3.72) c.clear(p.bg);  // the screen goes dark for a moment
}

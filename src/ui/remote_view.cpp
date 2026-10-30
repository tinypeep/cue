#include "remote_view.h"

#include <algorithm>
#include <cmath>

#include "../core/i18n.h"

namespace {
constexpr int kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
constexpr double kPeriod = 5.2;  // the light breathes, and the sound with it
double bayer(int x, int y) { return kBayer[(y & 3) * 4 + (x & 3)] / 16.0; }
}  // namespace

void RemoteView::set_size(int w_, int h_, double s_) {
  w = w_;
  h = h_;
  s = s_;
  layout();
}

void RemoteView::set_link(const std::string& url) {
  qr_ = url.empty() ? QrCode{} : qr_encode(url, QrEcc::M);
  layout();
}

void RemoteView::open(double now) {
  open_t0_ = now;
  close_t0_ = conn_t0_ = -1;
  device_.clear();
  hover_ = focus_ = -1;
  kb_ = false;
  face_ = Face{};
  face_.set_mode(Face::Mode::Idle, now);
  landed_ = reduce_motion();
  layout();
}

void RemoteView::close(double now) {
  if (close_t0_ < 0) close_t0_ = now;
}

void RemoteView::connected(const std::string& device, double now) {
  device_ = device;
  conn_t0_ = now;
  hops_ = 0;
  face_.wide = true;  // first a start: who's there?
  face_.happy = false;
  face_.look(0, 0);
}

// The scene opens and closes like an iris of big pixels around the code: each cell of the light's
// grid comes (or goes) when the wave reaches it, dithered so the edge is never a straight line.
void RemoteView::iris(Canvas& c, double q, bool opening) const {
  const int cell = 8 * (int)s;
  const double cx = tile_.cx(), cy = tile_.cy();
  const double far = std::sqrt((double)w * w + (double)h * h) / 2 + cell;
  for (int gy = 0; gy * cell < h; ++gy)
    for (int gx = 0; gx * cell < w; ++gx) {
      double px = gx * cell + cell / 2.0 - cx, py = gy * cell + cell / 2.0 - cy;
      double d = std::sqrt(px * px + py * py) / far * 0.82 + bayer(gx, gy) * 0.18;
      bool shown = opening ? d <= q : d <= 1 - q;
      if (shown) continue;
      int x0 = gx * cell, y0 = gy * cell, x1 = std::min(w, x0 + cell), y1 = std::min(h, y0 + cell);
      for (int y = y0; y < y1; ++y) std::fill(c.px.begin() + (size_t)y * w + x0, c.px.begin() + (size_t)y * w + x1, 0u);
    }
}

void RemoteView::layout() {
  hots_.clear();
  if (w <= 0 || h <= 0) return;
  const int a = (int)s;  // one art pixel
  int H = h / a;
  // the code: three art px per module on a normal window, two when it is short
  module_ = (H < 250 ? 2 : 3) * a;
  quiet_ = 3;
  int n = qr_.size > 0 ? qr_.size : 29;
  int tile = (n + 2 * quiet_) * module_;

  hots_.push_back({kClose, {w - 20 * a, 4 * a, 14 * a, 14 * a}});
  // folders: a row of chips at the bottom, centred, then the + to add another. It shares the
  // bottom line with the sound and "turn off" when there is room, else it sits just above them,
  // and in a narrow window long names are cut.
  const int W = w / a;
  int ow = text_w(tr(str::remote_turn_off), 1) + 12;
  std::vector<int> widths;
  int total = 0;
  chip_label_ = 110;
  for (;;) {
    widths.clear();
    total = 18;
    for (auto& c : chips) {
      int tw = std::min(text_w(c.label, 1), chip_label_);
      widths.push_back(6 + 9 + 5 + tw + 7);
      total += widths.back() + 4;
    }
    if (total <= W - 16 || chip_label_ <= 24) break;
    chip_label_ -= 8;
  }
  bool shared_line = total <= W - 2 * (std::max(ow, 26) + 12);
  int y = shared_line ? H - 30 : H - 52;
  int x = (W - total) / 2;

  // eyes, code, address, code line; a short window brings them closer together
  int eyes = 26, gap = 10;
  int bottom = (shared_line ? 34 : 56) * a;
  if ((eyes + gap + 32 + 18) * a + tile + bottom > h) { eyes = 16; gap = 6; }
  int block = eyes * a + tile + gap * a + 18 * a + 14 * a;
  int top = std::max(14 * a, (h - bottom - block) / 2);
  tile_ = {w / 2 - tile / 2, top + eyes * a, tile, tile};
  addr_gap_ = gap;
  // eyes peek over the code
  face_.layout(w / 2.0, tile_.y - 11.0 * a + 3 * 2.5 * a, s, 2.5);
  face_.max_look = 3;
  for (size_t i = 0; i < chips.size(); ++i) {
    hots_.push_back({(int)i, {x * a, y * a, widths[i] * a, 18 * a}});
    x += widths[i] + 4;
  }
  hots_.push_back({kAdd, {x * a, y * a, 18 * a, 18 * a}});
  hots_.push_back({kSound, {8 * a, (H - 30) * a, 18 * a, 18 * a}});
  hots_.push_back({kOff, {w - (8 + ow) * a, (H - 30) * a, ow * a, 18 * a}});
}

int RemoteView::hit(int x, int y) const {
  for (auto& hh : hots_) if (hh.r.contains(x, y)) return hh.id;
  return -1;
}

int RemoteView::hov_index(int id) const {
  if (id >= 0 && id < 1000) return id;
  return (int)chips.size() + (id - 1000);
}

double RemoteView::hov(int id) const {
  int k = hov_index(id);
  return k >= 0 && k < (int)hov_.size() ? hov_[k] : 0.0;
}

bool RemoteView::tick(double now, double dt) {
  size_t n = chips.size() + 4;
  if (hov_.size() != n) hov_.assign(n, 0.0);
  int active = kb_ ? focus_ : hover_;
  for (size_t k = 0; k < n; ++k) {
    int id = k < chips.size() ? (int)k : 1000 + (int)(k - chips.size());
    hov_[k] = hover_step(hov_[k], active == id, dt);
  }
  // the opening: the eyes drop onto the code and land with a little hop
  if (!landed_ && now - open_t0_ >= 0.62) { landed_ = true; face_.notice(now); }
  // a phone came in: wide eyes, then a jump into the phone, a happy landing and two more hops
  if (conn_t0_ > 0) {
    double bt = now - conn_t0_;
    face_.wide = bt < 0.26 && !reduce_motion();
    face_.happy = bt >= 0.62 || reduce_motion();
    static const double kHops[3] = {0.78, 1.1, 1.38};
    while (hops_ < 3 && bt >= kHops[hops_]) { face_.notice(now); ++hops_; }
  }
  // leaving: the eyes close first, like a goodbye
  face_.closed = close_t0_ > 0 && now - close_t0_ > 0.04;
  face_.tick(now, dt);
  if (conn_t0_ < 0 && face_.mode == Face::Mode::Idle && !face_.wide) {
    // waiting: the eyes keep coming back to the code, now and then a glance aside
    double ph = std::fmod(now, 7.0);
    if (ph < 4.5) face_.look(0, 2.2);
  }
  return true;  // the light is always breathing while the scene is up
}

void RemoteView::render(Canvas& c, double now) {
  const Palette& p = P();
  const int a = (int)s;
  if (c.w != w || c.h != h) c.resize(w, h);
  double enter = reduce_motion() ? 1.0 : clamp01((now - open_t0_) / 0.12);
  double leave = close_t0_ > 0 ? (reduce_motion() ? 1.0 - clamp01((now - close_t0_) / 0.2) : 1.0) : 1.0;
  c.clear(p.bg, veil);

  // the light: two dithered fields breathing out of phase, and sonar rings leaving the code
  const int cell = 8 * a;
  const double cx = w / 2.0, cy = tile_.y + tile_.h / 2.0;
  // the light blooms out of the code as the scene opens
  double bloom = reduce_motion() ? 1.0 : ease_out((now - open_t0_) / 0.7);
  const double R = std::max(tile_.w * 1.05, 120.0 * a) * (0.45 + 0.55 * bloom);
  double ph = reduce_motion() ? 0.0 : std::fmod(now - open_t0_, kPeriod) / kPeriod;
  double breathe = 0.5 - 0.5 * std::cos(2 * 3.14159265358979 * ph);
  bool waiting = conn_t0_ < 0;
  Rgb col = waiting ? glow : mix(glow, p.fg, clamp01((now - conn_t0_) / 0.6) * 0.6);
  for (int gy = 0; gy * cell < h; ++gy)
    for (int gx = 0; gx * cell < w; ++gx) {
      double px = gx * cell + cell / 2.0, py = gy * cell + cell / 2.0;
      double nx = (px - cx) / (R * 1.6), ny = (py - cy) / (R * 1.05);
      double v1 = 1 - std::sqrt(nx * nx + ny * ny);
      nx = (px - cx) / (R * 2.6); ny = (py - cy) / (R * 1.7);
      double v2 = 1 - std::sqrt(nx * nx + ny * ny);
      double alpha = 0;
      if (v1 > 0 && v1 * 1.5 >= bayer(gx, gy)) alpha = std::max(alpha, (0.06 + 0.62 * std::pow(v1, 1.1)) * (1.0 - 0.42 * breathe));
      if (v2 > 0 && v2 * 1.5 >= bayer(gx + 1, gy + 2)) alpha = std::max(alpha, (0.04 + 0.34 * std::pow(v2, 1.1)) * (0.45 + 0.55 * breathe));
      if (waiting && !reduce_motion()) {
        double d = std::sqrt((px - cx) * (px - cx) + (py - cy) * 1.25 * (py - cy) * 1.25);
        for (int k = 0; k < 2; ++k) {
          double q = std::fmod((now - open_t0_) / kPeriod + k * 0.5, 1.0) * 2.0;  // each ring lives half a period
          if (q > 1) continue;
          double rr = tile_.w * 0.62 + q * R * 1.5;
          double band = 1 - std::fabs(d - rr) / (cell * 1.4);
          if (band > 0 && band >= bayer(gx + 2, gy + 1)) alpha = std::max(alpha, 0.42 * (1 - q));
        }
      }
      alpha *= bloom;
      if (alpha > 0.01) c.fill_pixels(gx * cell, gy * cell, cell, cell, col, std::min(1.0, alpha));
    }

  // title, close
  text(c, tr(str::remote_title), 12 * a, 8 * a, s, p.dim);
  {
    Rect r = hots_.empty() ? Rect{} : hots_[0].r;
    double ha = hov(kClose);
    if (ha > 0) c.cornered(r, a, p.hover, ha);
    c.bitmap(bmp("cross"), r.x + 4 * a + a / 2, r.y + 4 * a + a / 2, s, mix(p.dim, p.fg, ha));
  }

  // the phones holding the remote right now: an icon, its little signal, its name
  if (!phones.empty()) {
    std::vector<std::pair<std::string, int>> groups;
    for (auto& ph : phones) {
      auto it = std::find_if(groups.begin(), groups.end(), [&](auto& g) { return g.first == ph; });
      if (it == groups.end()) groups.push_back({ph, 1}); else ++it->second;
    }
    std::vector<std::string> labels;
    int total = 0;
    for (auto& g : groups) {
      labels.push_back(g.second > 1 ? g.first + " x" + std::to_string(g.second) : g.first);
      total += 5 + 6 + text_w(labels.back(), 1) + 12;
    }
    int x = w / 2 - (total - 12) * a / 2;
    int y = 8 * a;
    for (size_t i = 0; i < groups.size(); ++i) {
      c.bitmap(bmp("phone"), x, y + 2 * a, s, p.fg);
      // the signal: one pixel blinking beside the phone, each phone on its own beat
      if (reduce_motion() || std::fmod(now + i * 0.4, 1.4) < 0.9) c.fill_pixels(x + 7 * a, y + 3 * a, a, a, p.pixel);
      text(c, labels[i], x + 11 * a, y, s, p.fg);
      x += (5 + 6 + text_w(labels[i], 1) + 12) * a;
    }
  }

  if (waiting) {
    // the code on its light tile; the eyes above it
    if (qr_.size > 0 && problem.empty()) {
      double t0 = now - open_t0_;
      auto at = [&](double delay, double dur) { return reduce_motion() ? 1.0 : ease_out((t0 - delay) / dur); };
      // the tile opens from its middle, in whole art pixels
      double kt = at(0.02, 0.3);
      int tw = (int)std::lround(tile_.w * (0.55 + 0.45 * kt) / a) * a, th = (int)std::lround(tile_.h * (0.55 + 0.45 * kt) / a) * a;
      Rect t = {tile_.cx() - tw / 2, tile_.cy() - th / 2, tw, th};
      c.cornered(t, a, p.fg, kt);
      // then the code prints itself from the centre outwards, dithered
      double q = reduce_motion() ? 2.0 : (t0 - 0.16) / 0.5;
      int ox = tile_.x + quiet_ * module_, oy = tile_.y + quiet_ * module_;
      if (q >= 1.2) {
        for (int y = 0; y < qr_.size; ++y)
          for (int x = 0; x < qr_.size;) {
            if (!qr_.dark(x, y)) { ++x; continue; }
            int x0 = x;
            while (x < qr_.size && qr_.dark(x, y)) ++x;
            c.fill_pixels(ox + x0 * module_, oy + y * module_, (x - x0) * module_, module_, p.bg);
          }
      } else if (q > 0 && kt > 0.98) {
        double mid = (qr_.size - 1) / 2.0, far = mid * 1.4143;
        for (int y = 0; y < qr_.size; ++y)
          for (int x = 0; x < qr_.size; ++x) {
            if (!qr_.dark(x, y)) continue;
            double d = std::sqrt((x - mid) * (x - mid) + (y - mid) * (y - mid)) / far;
            if (d * 0.82 + bayer(x, y) * 0.18 <= q) c.fill_pixels(ox + x * module_, oy + y * module_, module_, module_, p.bg);
          }
      }
      // the eyes drop in from above and land on the code (tick() gives them the hop)
      double ke = at(0.2, 0.42);
      Face f = face_;
      f.cy -= std::lround((1 - ke) * (1 - ke) * 22) * a;
      f.now_hint = now;
      f.render(c, clamp01(ke * 1.6));
      // the address types itself out; the code line follows
      int s1 = (int)std::lround(s * 1.5);
      int ty = tile_.b() + (addr_gap_ + 2) * a;
      double ka = reduce_motion() ? 1.0 : clamp01((t0 - 0.5) / 0.4);
      size_t shown = (size_t)std::ceil(address.size() * ka);
      int ax = w / 2 - text_w(address, s1) / 2;
      text(c, address.substr(0, shown), ax, ty, s1, p.fg);
      if (shown > 0 && shown < address.size() && std::fmod(t0, 0.16) < 0.1)
        c.fill_pixels(ax + text_w(address.substr(0, shown), s1) + a, ty + 2 * a, s1, 9 * s1, p.pixel);  // the cursor
      std::string line = i18n::format(str::remote_code_line, {{"code", code}});
      text(c, line, w / 2 - text_w(line, s) / 2, ty + 12 * s1 + 4 * a, s, p.dim, at(0.82, 0.3));
    } else {
      face_.now_hint = now;
      face_.render(c, 1.0);
      std::string msg = problem.empty() ? tr(str::remote_no_network) : problem;
      text(c, msg, w / 2 - text_w(msg, s) / 2, tile_.y + tile_.h / 2 - 6 * a, s, p.dim);
    }
  } else {
    // a phone came in. The code lets go of its pixels, the tile turns into a phone, the eyes jump
    // into its screen and land happy; the phone buzzes, pixels pop around it, and it says so.
    double bt = now - conn_t0_;
    bool rm = reduce_motion();
    auto kk = [&](double d, double dur) { return rm ? 1.0 : ease_out((bt - d) / dur); };
    auto snap = [&](double v) { return (int)std::lround(v / a) * a; };
    double km = kk(0.16, 0.34);
    int pw = snap(std::max(tile_.w * 0.5, 44.0 * a)), ph = snap(std::max(tile_.h * 0.84, 74.0 * a));
    int rw = snap(lerp(tile_.w, pw, km)), rh = snap(lerp(tile_.h, ph, km));
    int wig = (!rm && bt > 0.95 && bt < 1.55) ? (((int)(bt * 28) & 1) ? a : -a) : 0;  // buzz
    Rect body = {tile_.cx() - rw / 2 + wig, tile_.cy() - rh / 2, rw, rh};
    c.cornered(body, 2 * a, p.fg);
    // the code's last pixels fall out from the edges in, while the tile still is one
    if (!rm && bt < 0.32 && qr_.size > 0) {
      double q = bt / 0.3, mid = (qr_.size - 1) / 2.0, far = mid * 1.4143;
      int ox = tile_.x + quiet_ * module_, oy = tile_.y + quiet_ * module_;
      for (int y = 0; y < qr_.size; ++y)
        for (int x = 0; x < qr_.size; ++x) {
          if (!qr_.dark(x, y)) continue;
          double d = 1 - std::sqrt((x - mid) * (x - mid) + (y - mid) * (y - mid)) / far;
          if (d * 0.82 + bayer(x, y) * 0.18 < q) continue;
          int fall = (int)std::lround(q * q * 30 * (0.4 + bayer(y, x))) * a;  // they drop as they go
          c.fill_pixels(ox + x * module_, oy + y * module_ + fall, module_, module_, p.bg, 1 - q);
        }
    }
    // the phone's face: a screen, a speaker slot, a button
    Rect scr = {body.x + 4 * a, body.y + 9 * a, body.w - 8 * a, body.h - 18 * a};
    double ks = rm ? 1.0 : clamp01((km - 0.55) / 0.45);
    if (ks > 0) {
      c.fill(scr, p.bg, ks);
      c.fill(body.cx() - 5 * a, body.y + 4 * a, 10 * a, a, p.bg, ks);
      c.fill(body.cx() - a, body.b() - 6 * a, 2 * a, 2 * a, p.bg, ks);
      // the screen lights up with the film's colour, dithered, once the eyes are home
      double kl = kk(0.7, 0.5);
      if (kl > 0)
        for (int y = scr.y; y < scr.b(); y += 2 * a)
          for (int x = scr.x; x < scr.r(); x += 2 * a) {
            double v = 1 - std::fabs((y - scr.cy()) / (double)scr.h) * 2;
            if (v * 0.6 > bayer(x / (2 * a), y / (2 * a)) + (1 - kl)) c.fill_pixels(x, y, 2 * a, 2 * a, glow, 0.5);
          }
    }
    // signal, both sides, lighting up in turn while it buzzes
    if (bt > 0.85) {
      double sc = rm ? 0.5 : std::fmod(bt - 0.85, 0.6) / 0.6;
      for (int side = -1; side <= 1; side += 2)
        for (int k = 0; k < 3; ++k) {
          bool lit = rm || (sc * 4 >= k && sc * 4 < k + 2.2);
          int ax = side < 0 ? body.x - (5 + 4 * k) * a : body.r() + (4 + 4 * k) * a;
          int hh = (6 + 4 * k) * a, ay = body.cy() - hh / 2;
          int bend = side < 0 ? a : -a;
          c.fill_pixels(ax + bend, ay, a, a, p.fg, lit ? 0.9 : 0.18);
          c.fill_pixels(ax, ay + a, a, hh - 2 * a, p.fg, lit ? 0.9 : 0.18);
          c.fill_pixels(ax + bend, ay + hh - a, a, a, p.fg, lit ? 0.9 : 0.18);
        }
    }
    // the eyes: a jump in an arc, from over the code into the screen, shrinking to fit
    double kj = rm ? 1.0 : clamp01((bt - 0.34) / 0.38);
    Face f = face_;
    double sc0 = 2.5, sc1 = std::min(1.6, scr.w / (double)a / 34.0);
    f.scale = lerp(sc0, sc1, ease_in_out(kj));
    double y0 = face_.cy, y1 = scr.cy() + 3 * f.scale * s - 2 * a;
    f.cy = snap(lerp(y0, y1, ease_in_out(kj)) - std::sin(3.14159265 * kj) * 26 * a);
    f.cx = body.cx();
    f.max_look = 1.5;
    f.now_hint = now;
    f.render(c, 1.0);
    // pixels pop out of the phone and fall, in the film's colour and white
    if (!rm && bt > 0.72 && bt < 2.2) {
      double t = bt - 0.72;
      for (int i = 0; i < 16; ++i) {
        double ang = -3.14159265 * (0.08 + 0.84 * (i / 15.0)), sp = 70 + (i * 37 % 50);
        double x = body.cx() + std::cos(ang) * sp * t * a, y = body.y + 4 * a + std::sin(ang) * sp * t * a + 120 * t * t * a;
        double fade = 1 - clamp01((t - 0.9) / 0.5);
        int sz = (i % 3 == 0 ? 3 : 2) * a;
        c.fill_pixels((int)x / a * a, (int)y / a * a, sz, sz, i % 2 ? glow : p.fg, fade);
      }
    }
    // "celular conectado", and whose
    int ty = body.b() + 12 * a;
    double kt = kk(0.62, 0.3);
    std::string head = tr(str::remote_phone_connected);
    int s2 = (int)std::lround(s * 2);
    int lift = (int)std::lround((1 - kt) * 6) * a;
    text(c, head, w / 2 - text_w(head, s2) / 2, ty + lift, s2, p.fg, kt);
    std::string who = device_.empty() ? std::string(tr(str::remote_phone)) : device_;
    double kw = kk(0.8, 0.3);
    text(c, who, w / 2 - text_w(who, s) / 2, ty + 12 * s2 + 4 * a + (int)std::lround((1 - kw) * 6) * a, s, p.dim, kw);
  }

  // the folders the phone can open
  int idx = 0;
  for (auto& hh : hots_) {
    if (hh.id < 0 || hh.id >= (int)chips.size()) continue;
    const RemoteChip& ch = chips[hh.id];
    double ha = hov(hh.id);
    if (ha > 0) c.cornered(hh.r, a, p.hover, ha);
    // a 1 px edge on the art grid
    double kc = reduce_motion() ? 1.0 : ease_out((now - open_t0_ - 0.55 - 0.06 * idx) / 0.3);
    Rect r = hh.r;
    r.y += (int)std::lround((1 - kc) * 6) * a;
    Rgb edge = ch.on ? p.dim : p.edge;
    c.fill(r.x + a, r.y, r.w - 2 * a, a, edge, kc);
    c.fill(r.x + a, r.b() - a, r.w - 2 * a, a, edge, kc);
    c.fill(r.x, r.y + a, a, r.h - 2 * a, edge, kc);
    c.fill(r.r() - a, r.y + a, a, r.h - 2 * a, edge, kc);
    Rgb ink = ch.on ? p.fg : mix(p.dim, p.fg, ha);
    c.bitmap(bmp("folder"), r.x + 6 * a, r.y + 6 * a, s, ink, kc);
    std::string label = fit_end(ch.label, chip_label_ * a, s);
    text(c, label, r.x + 20 * a, r.y + 3 * a, s, ink, kc);
    ++idx;
  }
  for (auto& hh : hots_) {
    double ha = hov(hh.id);
    if (hh.id == kAdd) {
      if (ha > 0) c.cornered(hh.r, a, p.hover, ha);
      c.bitmap(bmp("plus"), hh.r.x + 6 * a + a / 2, hh.r.y + 6 * a + a / 2, s, mix(p.dim, p.fg, ha));
    } else if (hh.id == kSound) {
      if (ha > 0) c.cornered(hh.r, a, p.hover, ha);
      c.bitmap(bmp(sound ? "sound-on" : "sound-off"), hh.r.x + 4 * a + a / 2, hh.r.y + 5 * a + a / 2, s, sound ? mix(p.dim, p.fg, std::max(0.4, ha)) : mix(p.dim, p.fg, ha));
    } else if (hh.id == kOff) {
      if (ha > 0) c.cornered(hh.r, a, p.hover, ha);
      text(c, tr(str::remote_turn_off), hh.r.x + 6 * a, hh.r.y + 3 * a, s, mix(p.dim, p.fg, ha));
    }
  }
  (void)idx;
  if (!reduce_motion()) {
    if (close_t0_ > 0) iris(c, ease_in_out(clamp01((now - close_t0_ - 0.06) / (kLeave - 0.06))), false);
    else if (now - open_t0_ < kEnter) iris(c, ease_out((now - open_t0_) / kEnter) * 1.02, true);
  }
  double alpha = enter * leave;
  if (alpha < 0.999) c.scale_alpha(alpha);
}

RemoteAction RemoteView::mouse_move(int x, int y, double now) {
  (void)now;
  kb_ = false;
  hover_ = hit(x, y);
  return {};
}

RemoteAction RemoteView::mouse_down(int x, int y, double now) {
  int id = hit(x, y);
  if (id == kClose) return {RemoteAct::Close, -1};
  if (id == kAdd) return {RemoteAct::AddFolder, -1};
  if (id == kSound) return {RemoteAct::Sound, -1};
  if (id == kOff) return {RemoteAct::TurnOff, -1};
  if (id >= 0 && id < (int)chips.size()) return {RemoteAct::ToggleFolder, id};
  if (conn_t0_ < 0 && tile_.contains(x, y)) face_.notice(now);  // poking the code: the eyes notice
  return {};
}

RemoteAction RemoteView::key(const std::string& k, bool shift, double now) {
  (void)now;
  if (k == "esc") return {RemoteAct::Close, -1};
  std::vector<int> order;
  for (auto& hh : hots_) order.push_back(hh.id);
  if (order.empty()) return {};
  if (k == "tab") {
    kb_ = true;
    auto it = std::find(order.begin(), order.end(), focus_);
    int i = it == order.end() ? -1 : (int)(it - order.begin()), n = (int)order.size();
    i = i < 0 ? (shift ? n - 1 : 0) : (i + (shift ? n - 1 : 1)) % n;
    focus_ = order[i];
    return {};
  }
  if ((k == "enter" || k == "space") && kb_ && focus_ != -1) {
    for (auto& hh : hots_)
      if (hh.id == focus_) return mouse_down(hh.r.cx(), hh.r.cy(), now);
  }
  return {};
}

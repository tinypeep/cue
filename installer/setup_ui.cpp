#include "setup_ui.h"

#include <algorithm>
#include <cmath>

namespace {
// Two zones: a quiet panel on the left where the eyes live, the content on the right
// with generous margins. Art px; rows are spaced on a 4 px rhythm.
constexpr int kPanel = 168;              // left panel width
constexpr int kX0 = kPanel + 28;         // content column
constexpr int kCW = SetupView::kW - kX0 - 28;
constexpr int kTitleY = 40, kVersionY = 70, kBodyY = 102, kButtonsY = SetupView::kH - 28 - 18;
}

void SetupView::set_size(int w_, int h_, double s_) {
  w = w_;
  h = h_;
  s = std::max(1.0, s_);
  layout();
}

void SetupView::layout() {
  auto R = [&](double x, double y, double ww, double hh) { return Rect(x * s, y * s, ww * s, hh * s); };
  r_[kMin] = R(kW - 44, 8, 16, 16);
  r_[kClose] = R(kW - 24, 8, 16, 16);
  int pw = text_w(primary_label(), 1) + 9 + 28;
  r_[kPrimary] = R(kX0 + kCW - pw, kButtonsY, pw, 18);
  std::string sl = secondary_label();
  int sw = sl.empty() ? 0 : text_w(sl, 1) + 24;
  r_[kSecondary] = R(kX0 + kCW - pw - 8 - sw, kButtonsY, sw, 18);
  r_[kDesktop] = R(kX0 - 5, kBodyY, kCW + 5, 15);
  r_[kLaunch] = R(kX0 - 5, kBodyY + 20, kCW + 5, 15);
  r_[kKeep] = R(kX0 - 5, kBodyY + 22, kCW + 5, 15);
  r_[kDir] = R(kX0 - 5, kBodyY + 46, kCW + 5, 28);
  face.layout(kPanel / 2.0 * s, (kH / 2.0 + 4) * s, s, 2.67);
  face.max_look = 3.0 * face.scale / 1.5;
}

std::string SetupView::primary_label() const {
  switch (stage) {
    case SetupStage::Ask:
      if (mode == SetupMode::Upgrade && old_version.empty()) return "reinstalar";
      return mode == SetupMode::Uninstall ? "desinstalar" : mode == SetupMode::Install ? "instalar" : "atualizar";
    case SetupStage::Working: return "";
    case SetupStage::Done: return mode == SetupMode::Uninstall ? "fechar" : "abrir o Cue";
    case SetupStage::Failed: return "tentar de novo";
  }
  return "";
}

std::string SetupView::secondary_label() const {
  switch (stage) {
    case SetupStage::Ask: return "cancelar";
    case SetupStage::Done: return mode == SetupMode::Uninstall ? "" : "fechar";
    case SetupStage::Failed: return "fechar";
    default: return "";
  }
}

void SetupView::start(double now) {
  t0_ = now;
  stage_t0_ = now;
  face.set_mode(Face::Mode::Follow, now);
  face.sad = mode == SetupMode::Uninstall;
  content_.snap(0);
  bar_.snap(0);
}

void SetupView::set_stage(SetupStage st, double now) {
  if (st == stage) return;
  stage = st;
  stage_t0_ = now;
  hover_ = kNone;
  if (kb_) focus_ = kPrimary;
  content_.snap(6);  // the new content rises into place
  content_.target = 0;
  layout();
  if (st == SetupStage::Done) {
    face.sad = false;
    face.happy = mode != SetupMode::Uninstall;
    face.closed = mode == SetupMode::Uninstall;  // eyes closed: it has gone
    face.notice(now);
    bar_.target = 1;
  }
  if (st == SetupStage::Working) { bar_.snap(0); face.sad = false; }
  if (st == SetupStage::Failed) { face.happy = false; face.thinking = true; }
}

int SetupView::hit(int x, int y) const {
  if (r_[kClose].contains(x, y)) return kClose;
  if (r_[kMin].contains(x, y)) return kMin;
  if (stage == SetupStage::Working) return kNone;
  if (r_[kPrimary].contains(x, y)) return kPrimary;
  if (!secondary_label().empty() && r_[kSecondary].contains(x, y)) return kSecondary;
  if (stage == SetupStage::Ask) {
    if (mode == SetupMode::Uninstall) { if (r_[kKeep].contains(x, y)) return kKeep; }
    else if (mode != SetupMode::Update) {
      if (r_[kDesktop].contains(x, y)) return kDesktop;
      if (r_[kLaunch].contains(x, y)) return kLaunch;
      if (mode == SetupMode::Install && r_[kDir].contains(x, y)) return kDir;
    }
  }
  return kNone;
}

bool SetupView::is_background(int x, int y) const { return hit(x, y) == kNone && !face.bounds().contains(x, y); }

std::vector<int> SetupView::focus_order() const {
  std::vector<int> o;
  if (stage == SetupStage::Ask) {
    if (mode == SetupMode::Uninstall) o.push_back(kKeep);
    else if (mode != SetupMode::Update) {
      o.push_back(kDesktop);
      o.push_back(kLaunch);
      if (mode == SetupMode::Install) o.push_back(kDir);
    }
  }
  if (stage != SetupStage::Working) {
    if (!secondary_label().empty()) o.push_back(kSecondary);
    o.push_back(kPrimary);
  }
  return o;
}

static SetupAct act_of(int id) {
  switch (id) {
    case 0: return SetupAct::Primary;
    case 1: return SetupAct::Secondary;
    case 2: return SetupAct::ToggleDesktop;
    case 3: return SetupAct::ToggleLaunch;
    case 4: return SetupAct::ToggleKeep;
    case 5: return SetupAct::ChangeDir;
    case 6: return SetupAct::Min;
    case 7: return SetupAct::Close;
    default: return SetupAct::None;
  }
}

SetupAct SetupView::mouse_move(int x, int y, double) {
  mx_ = x; my_ = y;
  kb_ = false;
  hover_ = hit(x, y);
  return SetupAct::None;
}

void SetupView::mouse_leave(double) { hover_ = kNone; mx_ = my_ = -1; }

SetupAct SetupView::mouse_down(int x, int y, double now) {
  int id = hit(x, y);
  if (id == kNone && face.bounds().contains(x, y)) { face.hop.v = -36; face.notice(now); }
  return act_of(id);
}

SetupAct SetupView::key(const std::string& k, bool shift, double) {
  if (k == "esc") return stage == SetupStage::Working ? SetupAct::None : SetupAct::Close;
  if (k == "tab") {
    kb_ = true;
    auto o = focus_order();
    if (o.empty()) return SetupAct::None;
    auto it = std::find(o.begin(), o.end(), focus_);
    int i = it == o.end() ? (shift ? (int)o.size() - 1 : 0)
                          : ((int)(it - o.begin()) + (shift ? (int)o.size() - 1 : 1)) % (int)o.size();
    focus_ = o[i];
    return SetupAct::None;
  }
  if (k == "enter" || k == "space") {
    if (stage == SetupStage::Working) return SetupAct::None;
    if (kb_ && focus_ != kNone) return act_of(focus_);
    return k == "enter" ? SetupAct::Primary : SetupAct::None;
  }
  return SetupAct::None;
}

bool SetupView::tick(double now, double dt) {
  bool moving = false;
  if (stage == SetupStage::Working) bar_.target = std::clamp(progress, 0.0, 1.0);
  moving |= bar_.step(dt);
  moving |= content_.step(dt);
  // the eyes: on the button while asking, on the bar while working, on the cursor when it comes near
  bool near = mx_ >= 0;
  if (stage == SetupStage::Working) {
    double bx = (kX0 + kCW * bar_.x) * s, by = (kBodyY + 24) * s;
    face.look_at(bx, by, 0.9);
    moving = true;
  } else if (near) face.look_at(mx_, my_, 0.6);
  else if (stage == SetupStage::Ask) face.look_at(r_[kPrimary].cx(), r_[kPrimary].cy(), 0.5);
  else face.look(0, 0);
  if (stage == SetupStage::Done && now - stage_t0_ > 2.6 && mode != SetupMode::Uninstall) face.happy = false;
  moving |= face.tick(now, dt);
  int act = kb_ ? focus_ : hover_;
  for (int i = 0; i < kCount; ++i) {
    double v = hover_step(hov_[i], act == i, dt);
    if (v != hov_[i]) { hov_[i] = v; moving = true; }
  }
  if (now - t0_ < 0.8 || now - stage_t0_ < 1.4) moving = true;
  return moving;
}

void SetupView::draw_check(Canvas& c, const Rect& r, bool on, const std::string& label, double hv, double a) const {
  const Palette& p = P();
  if (hv > 0) c.cornered(r, (int)s, p.hover, a * hv);
  int bx = r.x + (int)std::lround(5 * s), by = r.y + (int)std::lround(4 * s);
  Rect box{bx, by, (int)std::lround(7 * s), (int)std::lround(7 * s)};
  if (on) {
    c.cornered(box, (int)s, mix(p.fg, p.pixel, hv), a);
    c.cells(".....#" "....#." "#..#.." ".##..." "......", 6, 5, bx + (int)std::lround(0.5 * s), by + (int)std::lround(1 * s),
            s, p.bg, a);
  } else {
    c.cornered(box, (int)s, mix(p.faint, p.dim, hv), a);
    c.cornered(box.inset((int)s), 0, p.bg, a);
  }
  text(c, label, bx + (int)std::lround(13 * s), r.y + (int)std::lround(2 * s), s, on ? mix(p.fg, p.pixel, hv) : mix(p.dim, p.fg, hv), a);
}

void SetupView::draw_button(Canvas& c, const Rect& r, const std::string& label, bool primary, double hv, double a) const {
  const Palette& p = P();
  if (primary) c.cornered(r, (int)s, mix(p.hover, p.press, hv), a);
  else if (hv > 0) c.cornered(r, (int)s, p.hover, a * hv);
  int tw = text_w(label, s) + (primary ? (int)std::lround(9 * s) : 0);
  int x = r.x + (r.w - tw) / 2, y = r.y + (int)std::lround(4 * s);
  Rgb col = primary ? mix(p.fg, p.pixel, hv) : mix(p.dim, p.fg, hv);
  text(c, label, x, y, s, col, a);
  if (primary)
    c.bitmap(bmp("chev-right5"), x + tw - (int)std::lround(3 * s) + (int)std::lround(hv * s), y + (int)std::lround(2 * s), s, col, a);
}

void SetupView::render(Canvas& c, double now) {
  const Palette& p = P();
  if (c.w != w || c.h != h) c.resize(w, h);
  c.clear(p.bg);
  double t = now - t0_;
  double intro = ease_out(clamp01((t - 0.15) / 0.35));
  int lift = (int)std::lround(content_.x * s);
  double ca = intro * ease_out(clamp01((now - stage_t0_) / 0.22 + (stage == SetupStage::Ask ? 1 : 0)));
  auto X = [&](double ax) { return (int)std::lround(ax * s); };

  // the eyes' panel: one step lighter than the page, full height
  c.fill(0, 0, X(kPanel), h, p.band);
  // window buttons
  for (int i : {kMin, kClose}) {
    double hv = ease_out(hov_[i]);
    if (hv > 0) c.cornered(r_[i], (int)s, p.hover, hv);
    static const BitmapDef close_icon{"window-close", 5, 5, "#...#" ".#.#." "..#.." ".#.#." "#...#"};
    const BitmapDef* b = i == kClose ? &close_icon : bmp("win-min");
    double cell = 1.25 * s;
    int bx = (int)std::lround(r_[i].x + (r_[i].w - b->w * cell) / 2), by = (int)std::lround(r_[i].y + (r_[i].h - b->h * cell) / 2);
    c.bitmap(b, bx, by, cell, mix(p.dim, i == kClose ? p.pixel : p.fg, hv));
  }

  // the brand, always there
  text(c, "Cue", X(kX0), X(kTitleY) + (int)std::lround((1 - intro) * 4 * s), 2 * s, p.pixel, intro);
  if ((mode == SetupMode::Upgrade || mode == SetupMode::Update) && !old_version.empty()) {
    // "v1.0 → v1.1", with a small pixel arrow (the font has none)
    int x = X(kX0), y = X(kVersionY);
    x += text(c, "v" + old_version, x, y, s, p.faint, intro) + X(4);
    c.fill(x, y + X(5), X(5), (int)s, p.dim, intro);
    c.cells("#." "##" "#.", 2, 3, x + X(4), y + X(4), s, p.dim, intro);
    x += X(10);
    text(c, "v" + version, x, y, s, p.fg, intro);
  } else {
    text(c, "v" + version + " · .tinypeep", X(kX0), X(kVersionY), s, p.dim, intro);
  }

  auto line = [&](const std::string& txt, double ay, Rgb col) {
    text(c, fit_end(txt, X(kCW), s), X(kX0), X(ay) + lift, s, col, ca);
  };
  switch (stage) {
    case SetupStage::Ask:
      if (mode == SetupMode::Uninstall) {
        line("remover o Cue do computador?", kBodyY, p.fg);
        Rect r = r_[kKeep];
        r.y += lift;
        draw_check(c, r, keep, "manter preferências e recentes", ease_out(hov_[kKeep]), ca);
        line("frames salvos não são apagados", kBodyY + 46, p.faint);
      } else if (mode == SetupMode::Update) {
        line("pronto para atualizar", kBodyY, p.fg);
      } else {
        Rect r1 = r_[kDesktop], r2 = r_[kLaunch];
        r1.y += lift; r2.y += lift;
        draw_check(c, r1, desktop, "atalho na área de trabalho", ease_out(hov_[kDesktop]), ca);
        draw_check(c, r2, launch, "abrir o Cue ao terminar", ease_out(hov_[kLaunch]), ca);
        double hv = mode == SetupMode::Install ? ease_out(hov_[kDir]) : 0.0;
        Rect rd = r_[kDir];
        rd.y += lift;
        if (hv > 0) c.cornered(rd, (int)s, p.hover, hv * ca);
        text(c, "pasta", rd.x + X(5), rd.y + X(2), s, p.faint, ca);
        if (mode == SetupMode::Install) {
          std::string alt = "alterar";
          text(c, alt, rd.r() - X(5) - text_w(alt, s), rd.y + X(2), s, mix(p.faint, p.fg, hv), ca);
        }
        text(c, fit_middle(dir, rd.w - X(10), s), rd.x + X(5), rd.y + X(15), s, mix(p.dim, p.fg, hv), ca);
      }
      break;
    case SetupStage::Working: {
      std::string head = mode == SetupMode::Uninstall ? "desinstalando" : mode == SetupMode::Install ? "instalando" : "atualizando";
      int pct = (int)std::lround(std::clamp(bar_.x, 0.0, 1.0) * 100);
      line(head + " · " + std::to_string(pct) + "%", kBodyY, p.fg);
      // the bar: blocks of 4 art px with a 1 px gap; the leading block breathes
      int bx = X(kX0), by = X(kBodyY + 20) + lift, bw = X(kCW), bh = X(8);
      int blocks = kCW / 5;
      double f = std::clamp(bar_.x, 0.0, 1.0) * blocks;
      for (int i = 0; i < blocks; ++i) {
        int x = bx + X(i * 5);
        double fill = std::clamp(f - i, 0.0, 1.0);
        c.fill(x, by, X(4), bh, p.edge, ca);
        if (fill > 0) {
          double pulse = fill < 1 ? 0.55 + 0.45 * std::sin(now * 9) * 0.5 + 0.2 : 1.0;
          c.fill(x, by, X(4), bh, p.fg, ca * fill * std::min(1.0, pulse));
        }
      }
      (void)bw;
      line(status, kBodyY + 38, p.faint);
      break;
    }
    case SetupStage::Done:
      if (mode == SetupMode::Uninstall) {
        line("o Cue foi removido", kBodyY, p.fg);
        line(keep ? "preferências guardadas" : "até a próxima", kBodyY + 18, p.faint);
      } else {
        line(mode == SetupMode::Install ? "pronto, o Cue está instalado" : "pronto, o Cue está atualizado", kBodyY, p.fg);
        line("ele está no menu iniciar", kBodyY + 18, p.faint);
      }
      break;
    case SetupStage::Failed: {
      line("não deu certo", kBodyY, p.fg);
      // the reason, wrapped on two lines at most
      std::string rest = error, l1, l2;
      std::string word;
      for (size_t i = 0; i <= rest.size(); ++i) {
        char ch = i < rest.size() ? rest[i] : ' ';
        if (ch != ' ') { word += ch; continue; }
        std::string& target = l2.empty() && text_w(l1 + " " + word, 1) <= kCW ? l1 : l2;
        target = target.empty() ? word : target + " " + word;
        word.clear();
      }
      line(l1, kBodyY + 18, p.dim);
      if (!l2.empty()) line(l2, kBodyY + 32, p.dim);
      break;
    }
  }
  if (stage != SetupStage::Working) {
    double a = ca;
    std::string sl = secondary_label();
    if (!sl.empty()) draw_button(c, r_[kSecondary], sl, false, ease_out(hov_[kSecondary]), a);
    draw_button(c, r_[kPrimary], primary_label(), true, ease_out(hov_[kPrimary]), a);
  }
  face.now_hint = now;
  face.render(c, ease_out(clamp01((t - 0.05) / 0.3)));
}

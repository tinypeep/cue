// Layout in every interface language: no label cut or spilling out of its row, no menu outside
// the window, no buttons on top of each other. Every panel of the player, the home and its
// menu, the four welcome screens and the installer, at the four interface sizes, in landscape
// windows, full screen, a portrait (vertical video) window and several DPI scales.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define private public  // the test reads the computed layout (rows, rects) directly
#include "../installer/setup_ui.h"
#include "../src/ui/home.h"
#include "../src/ui/onboarding.h"
#include "../src/ui/play.h"
#undef private
#include "../src/core/i18n.h"
#include "../src/core/media_info.h"
#include "../src/core/ui_scale.h"

static int failures = 0, checks = 0;
static std::string where;
static void expect(bool ok, const std::string& what) {
  ++checks;
  if (!ok && failures++ < 40) std::printf("FAIL [%s] %s\n", where.c_str(), what.c_str());
}
static bool inside(const Rect& in, const Rect& out) { return in.x >= out.x && in.y >= out.y && in.r() <= out.r() && in.b() <= out.b(); }
static std::string R(const Rect& r) { return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "x" + std::to_string(r.h); }

static void settle(PlayView& p, double& t) { for (int i = 0; i < 40; ++i) { t += 1 / 60.0; p.tick(t, 1 / 60.0); } p.take_actions(); }

// Every word of a panel row fits the space the layout gave it.
static void check_rows(PlayView& p) {
  const double s = p.s_;
  const int text_col = p.icon_grid() ? 23 : 6;
  for (auto& r : p.rows_) {
    const std::string tag = "row '" + r.label + "' " + R(r.r);
    auto tw = [&](const std::string& t) { return text_w(t, s); };
    switch (r.k) {
      case PlayView::RK::Option: case PlayView::RK::Action: case PlayView::RK::Submenu: case PlayView::RK::Header: {
        int right = r.right.empty() ? 0 : tw(r.right) + (int)(6 * s);
        int chev = r.k == PlayView::RK::Submenu ? (int)(10 * s) : 0;
        expect((int)(23 * s) + tw(r.label) + right + chev <= r.r.w, tag);
        break;
      }
      case PlayView::RK::Hint:
        expect((int)(text_col * s) + tw(r.label) + (r.right.empty() ? 0 : tw(r.right) + (int)(8 * s)) <= r.r.w, tag);
        break;
      case PlayView::RK::Toggle: expect((int)(text_col * s) + tw(r.label) + (int)(20 * s) <= r.r.w, tag); break;
      case PlayView::RK::Slider:
        expect((int)(text_col * s) + tw(r.label) <= r.parts[0].x + (int)(6 * s), tag);
        expect(r.parts[0].x + r.parts[0].w + tw(r.right) <= r.r.r() + (int)(32 * s), tag + " value");
        break;
      case PlayView::RK::Stepper: expect(r.r.x + (int)((r.icon ? 23 : 6) * s) + tw(r.label) <= r.parts[0].x, tag); break;
      case PlayView::RK::Segmented:
        for (size_t k = 0; k < r.segs.size(); ++k) expect(tw(r.segs[k]) <= r.parts[k].w, tag + " seg '" + r.segs[k] + "'");
        expect(r.parts.back().r() <= r.r.r(), tag + " segs");
        break;
      case PlayView::RK::Links:
        for (size_t k = 0; k < r.segs.size(); ++k) expect(r.parts[k].r() <= r.r.r(), tag + " link '" + r.segs[k] + "'");
        break;
      case PlayView::RK::Info:
        if (!r.checked && !r.right.empty()) expect(tw(r.label) + (int)(4 * s) <= p.info_label_w_ * s, tag);
        break;
      case PlayView::RK::Profile: expect(tw(r.label) <= r.r.w, tag); break;
      case PlayView::RK::CloseHeader: expect((int)(6 * s) + tw(r.label) <= r.r.w - (int)(14 * s), tag); break;
      default: break;
    }
  }
}

static void check_player(int W, int H, double s) {
  PlayView p;
  p.in.dur = 5000; p.in.time = 1869;
  Track a{1, "sub", "por", "", "subrip"}, b{2, "sub", "eng", "English SDH", "subrip"}, k{3, "sub", "jpn", "", "ass"};
  k.forced = true;
  p.in.subs = {a, b, k}; p.in.sid = 1; p.in.kind = SubKind::Text;
  Track x{1, "audio", "eng", "", "aac"}, y{2, "audio", "fre", "", "ac3"}, z{3, "audio", "", "", "aac"};
  p.in.audios = {x, y, z}; p.in.aid = 1;
  p.in.update_ready = true;
  p.in.update_label = i18n::format(str::update_downloading, {{"version", "1.10"}, {"percent", "100"}});
  MediaDetails md;
  md.name = "a very long file name of a film that goes on and on (final cut).mkv"; md.bytes = 1834567890;
  md.duration = 5000; md.container = "matroska,webm"; md.width = 3840; md.height = 2160; md.fps = 23.976;
  md.video_codec = "hevc"; md.audio_codec = "eac3"; md.channels = 6; md.sample_rate = 48000;
  p.in.file_info = media_info_fields(md);
  p.resize(W, H, s);
  double t = 1;
  p.reset(t);
  p.changed(t);
  settle(p, t);
  const Rect win{0, 0, W, H};
  const char* names[] = {"cc", "style", "audio", "more", "speed", "about", "fileinfo", "language", "context"};
  for (int which = 0; which <= 8; ++which) {
    p.open_panel_for_preview(which, t);
    settle(p, t);
    expect(inside(p.panel_rect(), win), std::string(names[which]) + " panel " + R(p.panel_rect()) + " in " + R(win));
    check_rows(p);
    p.key("esc", false, false, t);
    p.key("esc", false, false, t);
    settle(p, t);
  }
  // every tooltip of the dock stays on screen
  for (auto& h : p.hots_) {
    if (h.id == PlayView::B_TIMELINE || h.id == PlayView::B_TIME) continue;
    Rect d = p.dock_rect();
    p.mouse_move(d.x + h.r.cx(), d.y + h.r.cy(), t);
    for (int i = 0; i < 60; ++i) { t += 1 / 60.0; p.tick(t, 1 / 60.0); }
    auto& tip = p.layers[PlayView::L_TIP];
    if (tip.visible) expect(inside({tip.x, tip.y, tip.c.w, tip.c.h}, win), "tooltip of button " + std::to_string(h.id));
  }
  // the longest toasts fit the window
  for (str::Id id : {str::update_no_installer, str::update_rate_limited, str::toast_frame_failed, str::update_none_published}) {
    p.toast(tr(id), nullptr, t);
    for (int i = 0; i < 12; ++i) { t += 1 / 60.0; p.tick(t, 1 / 60.0); }
    auto& L = p.layers[PlayView::L_TOAST];
    expect(L.visible && inside({L.x, L.y, L.c.w, L.c.h}, win), std::string("toast ") + i18n::key_name(id));
  }
}

static void check_home(int W, int H, double s) {
  HomeView h;
  h.set_size(W, H, s);
  h.set_rows({{"a.mkv", "1:24:12", 0.5}, {"b.mkv", "31:09", 0.6}});
  h.update_version = "1.10";
  h.layout();
  const Rect win{0, 0, W, H};
  // the line under the eyes: its words inside the button and the window
  int iw = text_w(tr(str::home_open_file), s) + (int)(17 * s);
  expect(iw <= h.r_open_.w && inside(h.r_open_, win), "open button " + R(h.r_open_));
  expect(text_w(tr(str::home_drop_here), s) <= W, "drop label");
  expect(text_w(tr(str::home_recents), s) < h.r_title_.w - (int)(12 * s), "Recentes title");
  for (bool lang : {false, true}) {
    h.open_menu(lang);
    expect(inside(h.r_menu_, win), std::string("menu ") + (lang ? "languages " : "") + R(h.r_menu_));
    for (size_t i = 0; i < h.menu_ids_.size(); ++i)
      expect(text_w(h.menu_label(h.menu_ids_[i]), s) + (int)((lang ? 14 : 16) * s) <= h.r_menu_items_[i].w,
             "menu item '" + h.menu_label(h.menu_ids_[i]) + "'");
  }
  h.layout_update();
  int uw = (int)(17 * s) + text_w(i18n::format(str::home_update_downloading, {{"version", "1.10"}, {"percent", "100"}}), s);
  expect(h.r_update_.x + (int)(5 * s) + uw <= W, "update line");
}

static void check_welcome(int W, int H, double s) {
  OnboardingView ob;
  ob.set_size(W, H, s);
  const Rect win{0, 0, W, H};
  for (int pg = 0; pg < OnboardingView::kPages; ++pg) {
    auto& L = ob.L_[pg];
    std::string tag = "welcome " + std::to_string(pg + 1);
    expect(inside(L.next, win) && inside(L.back, win), tag + " footer");
    expect(L.back.r() < L.dots.front().x && L.dots.back().r() < L.next.x, tag + " buttons apart " + R(L.back) + " " + R(L.next));
    str::Id heads[4] = {str::ob_intro, str::ob_ambient_title, str::ob_size_title, str::ob_next_title};
    expect(text_w(tr(heads[pg]), 2 * ob.s) <= W, tag + " heading");
  }
  auto& L1 = ob.L_[1];
  int lab = std::max(text_w(tr(str::ob_on), ob.s), text_w(tr(str::ob_off), ob.s));
  expect(L1.toggle.r() + (int)(8 * ob.s) + lab <= W, "welcome 2 toggle label");
  expect(L1.toggle.b() <= L1.next.y, "welcome 2 toggle above the footer");
  auto& L2 = ob.L_[2];
  for (size_t i = 0; i < L2.segs.size(); ++i) expect(inside(L2.segs[i], win), "welcome 3 size " + std::to_string(i));
  str::Id sizes[kUiSizes] = {str::size_small, str::size_medium, str::size_large, str::size_larger};
  for (int i = 0; i < kUiSizes; ++i) expect(text_w(tr(sizes[i]), ob.s) <= L2.segs[i].w, "welcome 3 size word");
  auto& L3 = ob.L_[3];
  for (auto& r : L3.items) { expect(inside(r, win), "welcome 4 row " + R(r)); expect(r.b() <= L3.next.y, "welcome 4 row above footer"); }
}

static void check_setup() {
  int W = SetupView::art_w();
  expect(W >= SetupView::kBaseW && W <= 600, "installer width " + std::to_string(W));
  int cw = W - (168 + 28) - 28;
  for (str::Id id : {str::setup_keep_prefs, str::setup_desktop, str::setup_launch}) expect(text_w(tr(id), 1) + 13 <= cw + 5, "check label");
  for (str::Id id : {str::setup_remove_question, str::setup_frames_kept, str::setup_installed, str::setup_updated,
                     str::setup_start_menu, str::setup_removed, str::setup_ready_update})
    expect(text_w(tr(id), 1) <= cw, std::string("line ") + i18n::key_name(id));
  for (SetupMode m : {SetupMode::Install, SetupMode::Upgrade, SetupMode::Update, SetupMode::Uninstall})
    for (SetupStage st : {SetupStage::Ask, SetupStage::Done, SetupStage::Failed}) {
      SetupView v; v.mode = m; v.stage = st; v.old_version = "0.9";
      v.set_size(W * 2, SetupView::kH * 2, 2);
      if (!v.secondary_label().empty()) expect(v.r_[1].r() <= v.r_[0].x, "installer buttons apart");
      expect(v.r_[1].x >= (168 + 20) * 2, "installer buttons in the column");
      expect(text_w(v.primary_label(), 2) + 9 * 2 <= v.r_[0].w, "installer primary words");
    }
}

int main() {
  struct Win { int w, h; unsigned dpi; const char* name; };
  const Win wins[] = {{1280, 720, 96, "1280x720 100%"},  {960, 540, 96, "960x540 minimum"},   {1600, 900, 120, "1600x900 125%"},
                      {1920, 1080, 144, "1920x1080 150%"}, {1920, 1080, 96, "full screen 1080p"}, {2560, 1440, 192, "2560x1440 200%"},
                      {3840, 2160, 96, "full screen 4K 100%"}, {3840, 2160, 240, "full screen 4K 250%"},
                      {608, 1080, 96, "portrait video"},   {900, 1600, 192, "portrait 200%"}};
  for (int li = 0; li < i18n::kLangCount; ++li) {
    i18n::set_language((i18n::Lang)li);
    for (int choice : {-1, 2, 0, 1}) {
      for (auto& w : wins) {
        double s = ui_scale(w.dpi, w.w, w.h, choice);
        where = std::string(i18n::tag((i18n::Lang)li)) + " · size " + std::to_string(choice) + " · " + w.name + " · s=" + std::to_string(s).substr(0, 4);
        check_player(w.w, w.h, portrait_scale(s, w.w, w.h, choice));
        check_home(w.w, w.h, s);
        check_welcome(w.w, w.h, s);
      }
    }
    where = std::string(i18n::tag((i18n::Lang)li)) + " · installer";
    check_setup();
  }
  if (failures) { std::printf("%d of %d layout checks failed\n", failures, checks); return 1; }
  std::printf("Layout: %d checks in 5 languages x 4 sizes x 10 windows (DPI 100-250%%, full screen, portrait) for every "
              "player panel, tooltips, toasts, the home and its menu, the welcome and the installer passed\n", checks);
}

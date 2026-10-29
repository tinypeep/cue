#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

static void settle(PlayView& p, double& t) {
  for (int i = 0; i < 40; ++i) { t += 1.0 / 60; p.tick(t, 1.0 / 60); }
}

int main() {
  for (bool audio : {false, true}) for (double scale : {1., 1.5, 2., 2.5, 3., 4.}) {
    PlayView p;
    const int w = (int)(400 * scale), h = (int)(220 * scale);
    p.in.audio_only = audio; p.in.dur = 120;
    p.resize(w, h, scale); double t = 1; p.reset(t);
    p.open_panel_for_preview(3, t);
    // The last item in More opens About with either Enter or the right arrow.
    for (int i = 0; i < 20; ++i) p.key("down", false, false, t);
    p.key(audio ? "enter" : "right", false, false, t);
    settle(p, t);
    const Layer& panel = p.layers[PlayView::L_PANEL];
    assert(panel.visible && panel.x >= 0 && panel.y >= 0);
    assert(panel.x + panel.c.w <= w && panel.y + panel.c.h <= h);
    assert(std::abs(panel.x * 2 + panel.c.w - w) <= 1);
    assert(std::abs(panel.y * 2 + panel.c.h - h) <= 1);
    p.take_actions();
    // Static credits are skipped by keyboard navigation. The name opens LinkedIn.
    for (PA expected : {PA::OpenLinkedIn, PA::OpenLinkedIn, PA::OpenGitHub}) {
      p.key("down", false, false, t); p.key("enter", false, false, t);
      auto actions = p.take_actions();
      assert(actions.size() == 1 && actions[0].a == expected);
    }
    for (int i = 0; i < 3; ++i) {
      int x = panel.x + panel.c.w / 2;
      int y = panel.y + (int)std::lround((i == 0 ? 89 : i == 1 ? 114 : 134) * scale);
      p.mouse_move(x, y, t);
      assert(p.cursor_kind() == 1);
      p.mouse_down(x, y, t); p.mouse_up(x, y, t);
      auto actions = p.take_actions();
      assert(actions.size() == 1 && actions[0].a == (i == 2 ? PA::OpenGitHub : PA::OpenLinkedIn));
    }
    assert(!p.in.paused);
    for (int i = 0; i < 8; ++i) p.key("down", false, false, t);
    p.key("enter", false, false, t);
    auto license_actions = p.take_actions();
    assert(license_actions.size() == 1 && license_actions[0].a == PA::OpenLicenses);
    p.key("esc", false, false, t); settle(p, t);
    assert(p.panel_open());
    p.key("esc", false, false, t); settle(p, t);
    assert(!p.panel_open());
    // An outside click dismisses About without pausing the content underneath.
    p.open_panel_for_preview(5, t); settle(p, t); p.take_actions();
    p.mouse_down(1, h / 2, t); p.mouse_up(1, h / 2, t);
    settle(p, t);
    assert(!p.panel_open() && p.take_actions().empty());
  }
  std::puts("About: menu navigation, centered layout, keyboard links, back and outside dismissal passed for audio/video at six scales");
}

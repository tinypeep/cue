#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

static void expect(PlayView& v, int pause, int fullscreen, int maximize = 0) {
  int p = 0, f = 0, m = 0;
  for (auto a : v.take_actions()) {
    p += a.a == PA::TogglePause;
    f += a.a == PA::Fullscreen;
    m += a.a == PA::WinMax;
  }
  assert(p == pause && f == fullscreen && m == maximize);
}

int main() {
  for (bool paused : {false, true}) for (bool audio : {false, true}) {
    PlayView v;
    v.in.paused = paused;
    v.in.audio_only = audio;
    v.in.dur = 240;
    v.resize(1280, 720, 2);
    v.reset(1);
    v.set_double_click_time(0.5);
    v.tick(2, 0.05); v.take_actions();
    // A real DOWN/UP/DBLCLK-as-DOWN/UP sequence, with an engine update in between.
    v.mouse_down(640, 360, 3); v.mouse_up(640, 360, 3.02);
    v.tick(3.1, 0.05); expect(v, 1, 0);
    assert(v.in.paused != paused);
    v.changed(3.12);
    v.mouse_down(642, 361, 3.2);
    v.mouse_up(642, 361, 3.22);
    expect(v, 1, 1);
    assert(v.in.paused == paused);
    v.in.fullscreen = true; v.resize(1920, 1080, 3);
    v.tick(4, 0.05); expect(v, 0, 0); // no late pause or second fullscreen
    v.mouse_down(960, 500, 5); v.mouse_up(960, 500, 5.02);
    v.mouse_down(960, 500, 5.2); v.mouse_up(960, 500, 5.22);
    expect(v, 2, 1); // leaving fullscreen also preserves playing/paused
    assert(v.in.paused == paused);
    v.in.fullscreen = false; v.resize(1280, 720, 2);
    v.mouse_down(640, 360, 7); v.mouse_up(640, 360, 7.02);
    assert(v.tick(7.4, 0.05)); expect(v, 1, 0); // first release is immediate
    v.tick(7.53, 0.05); expect(v, 0, 0);
    v.tick(8, 0.05); expect(v, 0, 0);
    // Keyboard and toolbar stay immediate; reset must discard a pending click.
    v.mouse_down(640, 360, 9); v.mouse_up(640, 360, 9.02);
    v.key("space", false, false, 9.1); expect(v, 2, 0);
    v.tick(10, 0.05); expect(v, 0, 0);
    v.mouse_down(640, 360, 11); v.mouse_up(640, 360, 11.02);
    expect(v,1,0); v.reset(11.1); v.tick(12, 0.05); expect(v, 0, 0);
    // The top strip maximizes exactly once, without pause commands.
    v.mouse_down(400, 20, 13); v.mouse_up(400, 20, 13.02);
    v.mouse_down(400, 20, 13.2); v.mouse_up(400, 20, 13.22);
    expect(v, 0, 0, 1);
    // Releasing a button twice never creates another click.
    v.mouse_up(400, 20, 13.25); v.tick(14, 0.05); expect(v, 0, 0);
  }
  std::puts("double-click checks passed: playing/paused audio/video, enter/exit fullscreen, explicit state restoration, immediate single click exactly once, reset and keyboard cancellation, maximize, duplicate release");
}

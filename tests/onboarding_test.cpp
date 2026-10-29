// First-run welcome and the motion primitives behind it.
#include "../src/ui/onboarding.h"
#include "../src/core/ui_scale.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static void run(OnboardingView& ob, double& now, double secs) {
  for (double t = 0; t < secs; t += 1 / 60.0) { now += 1 / 60.0; ob.tick(now, 1 / 60.0); }
}

int main() {
  // ── analytic spring: same landing at any frame rate, continuous on retarget ──
  for (double z : {0.6, 0.86, 1.0, 1.3}) {
    Spring a(17, z, 1e-4), b(17, z, 1e-4);
    a.snap(0); b.snap(0); a.target = b.target = 100;
    for (int i = 0; i < 60; ++i) a.step(1 / 60.0);    // 60 Hz
    for (int i = 0; i < 144; ++i) b.step(1 / 144.0);  // 144 Hz
    assert(std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.v - b.v) < 1e-5);
    Spring c(17, z, 1e-4);
    c.snap(0); c.target = 100;
    c.step(0.1);  // one long frame (a hitch) lands where many short ones would
    Spring d(17, z, 1e-4);
    d.snap(0); d.target = 100;
    for (int i = 0; i < 10; ++i) d.step(0.01);
    assert(std::fabs(c.x - d.x) < 1e-6);
  }
  {
    Spring s = Spring::rd(0.4, 0.8, 0.001);
    s.snap(0); s.target = 1;
    double peak = 0;
    int frames = 0;
    while (s.step(1 / 60.0) && frames < 600) { peak = std::max(peak, s.x); ++frames; }
    assert(peak > 1.005 && peak < 1.05);  // a soft overshoot, Apple-like
    assert(frames > 20 && frames < 90);
    s.target = 0; s.step(1 / 60.0); double x = s.x, v = s.v;
    s.target = 1; assert(s.x == x && s.v == v);  // retarget keeps position and velocity
  }
  // ── exits now ease out: the change is visible on the first frame ──
  {
    Fade f{1, 0, 0.14, 0.3};
    f.step(1 / 60.0);
    double shown = ease_out(f.v);
    assert(shown < 0.95 && shown > 0.6);
    int n = 1;
    while (f.step(1 / 60.0)) ++n;
    assert(f.v == 0 && n >= 14 && n <= 20);
    double h = 1;
    h = hover_step(h, false, 1 / 60.0);
    assert(ease_out(h) < 0.95);
  }
  // ── the welcome ──
  OnboardingView ob;
  ob.ambient = true;
  ob.ui_scale = -1;
  ob.set_size(1280, 720, 1);
  assert(ob.s >= 1.5 && ob.ui_s == 1);  // comfortable to read even at the smallest size
  double now = 5;
  ob.start(now);
  run(ob, now, 3.5);
  assert(ob.page() == 0);
  // enter moves on; esc finishes from anywhere
  assert(ob.key("enter", false, now).a == ObAct::None && ob.page() == 1);
  run(ob, now, 0.8);
  // space flips the light and reports it
  ObAction a = ob.key("space", false, now);
  assert(a.a == ObAct::Ambient && a.v == 0 && !ob.ambient);
  a = ob.key("space", false, now);
  assert(a.a == ObAct::Ambient && a.v == 1 && ob.ambient);
  ob.key("right", false, now);
  run(ob, now, 0.8);
  assert(ob.page() == 2);
  // arrows pick a size, in the interface's own order
  a = ob.key("right", false, now);
  assert(a.a == ObAct::UiScale && a.v == ui_size_choice(1));
  a = ob.key("right", false, now);
  assert(a.a == ObAct::UiScale && a.v == ui_size_choice(2));
  ob.set_size(1280, 720, 2);  // the App applies it
  assert(ob.ui_s == 2);
  // enter on the size moves on to what is coming; the last button finishes
  a = ob.key("enter", false, now);
  assert(a.a == ObAct::None && ob.page() == 3);
  run(ob, now, 0.8);
  {
    Canvas cc;
    ob.render(cc, now);  // the roadmap rows lay out and draw
  }
  a = ob.key("enter", false, now);
  assert(a.a == ObAct::Finish);
  ob.finish(now, 400, 300, 2.0);
  assert(!ob.finished(now));
  run(ob, now, 0.6);
  assert(ob.finished(now));
  assert(std::fabs(ob.face.cx - 400) < 1.5 && std::fabs(ob.face.cy - 300) < 1.5 && std::fabs(ob.face.scale - 2.0) < 0.01);
  // mouse: every page's primary button is where it is drawn
  OnboardingView m;
  m.set_size(960, 540, 2);
  double t = 1;
  m.start(t);
  run(m, t, 3.5);
  ObAction none = m.mouse_down(m.w - 60, m.h - 44, t);  // "continuar"
  assert(none.a == ObAct::None && m.page() == 1);
  run(m, t, 0.8);
  ObAction skip = m.key("esc", false, t);
  assert(skip.a == ObAct::Finish);
  // tiny windows still lay out without overlapping the footer
  OnboardingView tiny;
  tiny.set_size(480, 360, 1);
  double tt = 1;
  tiny.start(tt);
  run(tiny, tt, 0.5);
  Canvas c;
  tiny.render(c, tt);
  assert(c.w == 480 && c.h == 360);
  std::puts("Welcome: analytic springs at 60/144 Hz and through hitches, Apple-like overshoot, eased exits, "
            "four screens by keyboard and mouse, live light/size choices and the hand-off to the home passed");
}

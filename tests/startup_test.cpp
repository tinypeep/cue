#include "../src/core/playback_warmup.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

int main() {
  PlaybackWarmup gate;
  assert(!gate.ready(100, false)); // file loading alone cannot start background work
  assert(!gate.ready(100, false, PlaybackWarmup::kAtmosphereDelay));
  gate.first_frame(100);
  gate.first_frame(101); // subsequent restarts/seeks do not keep postponing it
  assert(!gate.ready(101.499, false, PlaybackWarmup::kAtmosphereDelay));
  assert(gate.ready(101.5, false, PlaybackWarmup::kAtmosphereDelay));
  assert(!gate.ready(101.5, false)); // colour sampling cannot release timeline work early
  assert(!gate.ready(102, true, PlaybackWarmup::kAtmosphereDelay));
  assert(!gate.ready(102.999, false));
  assert(gate.ready(103, false));
  assert(!gate.ready(104, true)); // opening or window animation still has priority
  assert(gate.ready(104, false));
  gate.reset(); // replacing a file also resets a previously ready gate
  assert(!gate.ready(200, false));
  assert(!gate.ready(200, false, PlaybackWarmup::kAtmosphereDelay));
  gate.first_frame(205);
  assert(!gate.ready(206.499, false, PlaybackWarmup::kAtmosphereDelay));
  assert(gate.ready(206.5, false, PlaybackWarmup::kAtmosphereDelay));
  assert(!gate.ready(206.5, false));
  assert(!gate.ready(207.999, false));
  assert(gate.ready(208, false));

  for (int w : {1280, 3840}) {
    PlayView v;
    v.resize(w, w * 9 / 16, 2);
    v.reset(0);
    v.begin_cover();
    v.prepare(0);
    auto& iris = v.layers[PlayView::L_IRIS];
    assert(iris.visible && iris.c.w == w);
    for (auto px : iris.c.px) assert(px == 0xff000000u);
    v.start_iris(1);
    int updates = 0;
    // Timer + property events can arrive much faster than the display.
    for (int i = 0; i < 100; ++i) {
      double t = 1.0 + i / 240.0;
      iris.dirty = false;
      v.tick(t, 1.0 / 240.0);
      if (iris.visible && iris.dirty) ++updates;
      iris.dirty = false;
      v.prepare(t);
      assert(!iris.dirty); // composing again in the same display slot is free
    }
    assert(updates >= 15 && updates <= 23);
    assert(!v.iris_active() && !iris.visible);
    assert(iris.c.px.empty()); // do not retain a full-window mask after opening
    // A resize must repaint even within the current display slot.
    v.begin_cover(); v.start_iris(2); v.prepare(2.1);
    v.resize(w + 16, w * 9 / 16 + 16, 2); v.prepare(2.1);
    assert(iris.c.w == w + 16 && iris.c.h == w * 9 / 16 + 16);
  }
  std::puts("startup checks passed: 1.5-second atmosphere / 3-second timeline, replacement reset, reveal capped at 60 Hz, 4K resize");
}

#include "../src/core/preview_session.h"
#include "../src/core/playback_warmup.h"
#include "../src/ui/ambient_raster.h"
#include "../src/ui/atmosphere.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
  using E = PreviewWait::Event;
  using R = PreviewWait::Result;
  PreviewWait load(true), seek(false);
  assert(load.consume(E::Stop) == R::Pending); // stop old -> load new: reported replacement regression
  assert(load.consume(E::Other) == R::Pending);
  assert(load.consume(E::Loaded) == R::Ready);
  assert(load.consume(E::End) == R::Failed); // real error/EOF of the requested file
  assert(load.consume(E::Shutdown) == R::Failed);
  assert(seek.consume(E::Loaded) == R::Pending);
  assert(seek.consume(E::Stop) == R::Failed);
  assert(seek.consume(E::Restart) == R::Ready);
  PreviewSession session;
  PlaybackWarmup warmup;
  session.reset();
  const auto old_media = session.generation;
  const auto old_frame = ++session.request;
  session.loaded = true;
  session.busy = true;
  session.failed = true;
  session.misses = 9;
  warmup.first_frame(0);
  assert(warmup.ready(3, false));
  session.reset(); // same path used by Explorer/IPC, Ctrl+O and drag/drop
  warmup.reset();
  assert(!session.loaded && !session.busy && !session.failed && session.misses == 0);
  assert(!session.accepts(old_media)); // old Ready, Failed, NoFrame and Gone are all stale
  assert(!session.accepts_frame(old_media, old_frame));
  assert(!warmup.ready(10, false));
  warmup.first_frame(11);
  assert(!warmup.ready(13.999, false));
  assert(warmup.ready(14, false));
  const auto second = session.generation;
  assert(session.accepts(second));
  assert(session.accepts_frame(second, session.request));
  assert(!session.accepts_frame(second, old_frame));
  session.reset(); // rapid third launch, even before second helper is ready
  assert(!session.accepts(second) && !session.accepts(old_media));
  assert(session.accepts(session.generation) && !session.failed);

  uint32_t ramp[256];
  uint32_t rng = 1;
  for (int sample = 0; sample < 512; ++sample) {
    rng = rng * 1664525u + 1013904223u;
    uint8_t r = rng >> 24, g = rng >> 16, b = rng >> 8, l = rng;
    ambient_ramp(r, g, b, l, ramp);
    for (unsigned mask = 0; mask < 256; ++mask) {
      unsigned a = mask * l / 255;
      uint32_t reference = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
      assert(ramp[mask] == reference);
      assert(((ramp[mask] >> 16) & 255) <= a && ((ramp[mask] >> 8) & 255) <= a && (ramp[mask] & 255) <= a);
    }
  }
  ColorSmoother sm;
  sm.step({255, 0, 0}, 1, 0, 0.9);
  int changed = 0, largest = 0;
  Rgb previous = sm.rgb();
  for (int i = 0; i < 60; ++i) {
    sm.step({0, 0, 255}, 1, 1.0 / 30, 0.9);
    auto current = sm.rgb();
    assert(current.r <= previous.r && current.b >= previous.b);
    if (current.r != previous.r || current.b != previous.b) ++changed;
    largest = std::max(largest, std::abs((int)current.b - previous.b));
    previous = current;
  }
  assert(changed >= 50 && largest <= 10);
  std::puts("ambient checks passed: replacement recovery, stale replies rejected, exact premultiplied raster, smooth 30 Hz transition");
}

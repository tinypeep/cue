#include "../src/core/overlay_buffers.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

int main() {
  OverlayBuffers b;
  constexpr size_t pixels = 3840u * 2160u;
  b.buf[0].resize(pixels); b.buf[1].resize(pixels);
  b.shown = b.inflight = true; b.sending = 1;
  const auto before = b.retained_bytes();
  b.added(true);
  assert(b.on_screen == 1 && !b.inflight && b.retained_bytes() == before);
  b.removing = true;
  b.removed(false);
  assert(b.shown && b.retained_bytes() == before); // no release on a failed remove
  b.removing = true;
  b.removed(true);
  assert(!b.shown && !b.removing && b.on_screen == -1);
  assert(b.retained_bytes() == 0); // 4K opening buffers are actually returned to the allocator

  b.buf[0].resize(pixels);
  b.inflight = true; b.shown = b.removing = true;
  b.removed(true);
  assert(b.retained_bytes() >= pixels * 4); // in-flight upload still owns its pixels
  b.added(false);
  b.removed(true);
  assert(b.retained_bytes() == 0);
  b.buf[0].resize(1000); b.shown = b.removing = true;
  b.removed(true);
  assert(b.retained_bytes() >= 4000); // keep small reusable UI buffers to avoid churn

  PlayView p;
  p.in.dur = 7200; p.in.time = 100;
  p.resize(1280, 720, 2); p.reset(0); p.changed(0);
  auto stable = p.dock_signature();
  p.in.time += 0.001; p.changed(0.001);
  assert(p.dock_signature() == stable); // displayed second and timeline pixel are unchanged
  p.in.time += 1; p.changed(1);
  assert(p.dock_signature() != stable);
  stable = p.dock_signature();
  p.in.muted = true; p.changed(2);
  assert(p.dock_signature() != stable);
  p.in.audios = {Track{1, "audio"}, Track{2, "audio"}};
  p.changed(3); p.prepare(3); // track-count changes still update toolbar hitboxes/layout
  p.in.dur = 120; p.changed(4); p.prepare(4); // hours -> minutes changes timeline width
  std::printf("performance checks passed: %.2f MiB of 4K overlay buffers released after acknowledgement; stable UI avoids invalidation\n",
              before / (1024.0 * 1024.0));
}

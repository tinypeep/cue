#include "../src/core/audio_mode.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

int main(int argc, char**) {
  if (argc > 1) { std::puts(audio_waveform_graph(1).c_str()); return 0; }
  Track a; a.id = 3; a.type = "audio"; a.lang = "en";
  Track b = a; b.id = 7; b.lang = "pt"; b.is_default = true;
  Track video; video.id = 1; video.type = "video";
  assert(audio_visual_track({}) == 0);
  assert(audio_visual_track({a}) == 3); // MP4-only audio and OGG take the same path
  assert(audio_visual_track({a, b}) == 7);
  assert(audio_visual_track({a, b}, 3, "en") == 3);
  assert(audio_visual_track({a, b}, 3, "pt") == 7); // stale ID/language cannot select the wrong stream
  assert(audio_visual_track({a, b}, -1, "en") == 3);
  assert(audio_visual_track({a, video}) == 0); // never replace a film with visualization
  video.albumart = true;
  assert(audio_visual_track({a, video}) == 3);
  assert(audio_visual_track({video}) == 0); // image without audio isn't a song
  assert(audio_waveform_graph(0).empty());

  for (bool audio : {false, true}) {
    PlayView scrub;
    scrub.in.audio_only = audio;
    scrub.in.dur = 240;
    scrub.resize(1280, 720, 2);
    scrub.reset(0);
    for (int i = 0; i < 60; ++i) scrub.tick(i / 60.0, 1.0 / 60);
    const auto& dock = scrub.layers[PlayView::L_DOCK];
    int left = dock.x + 130, right = dock.x + dock.c.w - 130, y = dock.y + 56;
    scrub.take_actions();
    scrub.mouse_move(left, y, 1);
    scrub.mouse_down(left, y, 1);
    int previews = 0, commits = 0;
    auto count = [&]() {
      for (auto action : scrub.take_actions()) {
        previews += action.a == PA::SeekAbs;
        commits += action.a == PA::SeekAbsExact;
      }
    };
    count();
    for (int i = 1; i <= 24; ++i) {
      scrub.mouse_move(left + (right - left) * i / 24, y, 1 + i / 60.0);
      scrub.tick(1 + i / 60.0, 1.0 / 60);
      count();
      assert(!scrub.iris_active()); // seek must never replay the opening animation
    }
    assert(audio ? previews == 0 : previews > 1);
    assert(commits == 0);
    scrub.mouse_up(right, y, 1.5);
    count();
    assert(commits == 1); // exactly one accurate seek at release, including audio
    assert(scrub.dock_visible() && !scrub.iris_active());
  }

  PlayView view;
  view.in.audio_only = true;
  view.in.dur = 240;
  view.resize(1280, 720, 2);
  view.reset(0);
  for (int i = 0; i < 600; ++i) view.tick(i / 60.0, 1.0 / 60);
  assert(view.pinned() && view.dock_visible()); // controls never time out during audio
  assert(view.layers[PlayView::L_DOCK].visible);
  view.key("space", false, false, 10);
  auto actions = view.take_actions();
  assert(!actions.empty() && actions.back().a == PA::TogglePause);
  view.key("right", false, false, 10);
  actions = view.take_actions();
  assert(!actions.empty() && actions.back().a == PA::SeekRel);
  view.in.audio_only = false;
  view.reset(11);
  for (int i = 0; i < 600; ++i) view.tick(11 + i / 60.0, 1.0 / 60);
  assert(!view.pinned() && !view.dock_visible()); // normal video auto-hide returns
  std::puts("audio checks passed: stream detection, visible controls, continuous audio scrub, single release seek, video live scrub unchanged");
}

// Windows integration check against the actual shipped DLL. Compile with mpv.cpp.
// Usage: audio_engine_test.exe DLL_DIRECTORY AUDIO_MP4 AUDIO_OGG VIDEO_MP4
#ifdef _WIN32
#include "../src/win/mpv.h"
#include "../src/core/audio_mode.h"
#include <cassert>
#include <cstdio>
#include <cmath>

int wmain(int argc, wchar_t** argv) {
  assert(argc == 5);
  auto utf8 = [](const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back(); return s;
  };
  Mpv mpv; std::string error;
  assert(mpv.load(argv[1], &error));
  assert(mpv.create(nullptr, 0, 0, &error));
  mpv.option("config", "no"); mpv.option("terminal", "yes");
  mpv.option("vo", "null"); mpv.option("ao", "null");
  mpv.option("idle", "yes"); mpv.option("keep-open", "yes");
  mpv.option("video-sync", "display-resample");
  mpv.option("load-scripts", "no"); mpv.option("msg-level", "all=error");
  assert(mpv.api().initialize(mpv.handle()) >= 0);
  mpv.hook_add("on_preloaded", 201);
  int stage = 0;
  auto events = [&]() {
    while (auto* e = mpv.wait()) {
      if (e->event_id == MPV_EVENT_NONE) break;
      if (e->event_id == MPV_EVENT_HOOK) {
        auto* h = static_cast<mpv_event_hook*>(e->data);
        mpv_node node{};
        assert(mpv.api().get_property(mpv.handle(), "track-list", MPV_FORMAT_NODE, &node) >= 0);
        auto tracks = Mpv::parse_tracks(&node);
        mpv.api().free_node_contents(&node);
        auto id = audio_visual_track(tracks);
        assert((id > 0) == (stage != 2));
        if (id) {
          assert(mpv.set("file-local-options/force-window", "yes"));
          assert(mpv.set("file-local-options/video-sync", "audio"));
          assert(mpv.set("file-local-options/keep-open", "always"));
          assert(mpv.set("file-local-options/scale", "nearest"));
          assert(mpv.set("file-local-options/dscale", "nearest"));
          assert(mpv.set("file-local-options/lavfi-complex", audio_waveform_graph(id)));
        }
        mpv.hook_continue(h->id);
      }
      if (e->event_id == MPV_EVENT_END_FILE) {
        auto* end = static_cast<mpv_event_end_file*>(e->data);
        assert(end->reason != MPV_END_FILE_REASON_ERROR);
      }
    }
  };
  auto pump = [&](DWORD ms) {
    DWORD end = GetTickCount() + ms;
    do { events(); Sleep(5); } while (GetTickCount() < end);
  };
  for (stage = 0; stage < 4; ++stage) {
    auto file = utf8(argv[stage == 2 ? 4 : stage == 1 ? 3 : 2]);
    assert(mpv.cmd({"loadfile", file, "replace"}) >= 0);
    DWORD deadline = GetTickCount() + 8000;
    do { pump(20); } while (mpv.get_double("time-pos", -1) < 0.6 && GetTickCount() < deadline);
    assert(mpv.get_double("time-pos", -1) >= 0.6);
    assert(mpv.get_double("dwidth") == (stage == 2 ? 320 : 1024));
    assert(mpv.get_string("video-sync") == (stage == 2 ? "display-resample" : "audio"));
    if (stage == 2) assert(mpv.get_string("lavfi-complex").empty());
    assert(mpv.set("pause", "yes")); pump(100);
    double before = mpv.get_double("time-pos"); pump(200);
    assert(std::abs(mpv.get_double("time-pos") - before) < 0.05);
    assert(mpv.cmd({"seek", "2", "absolute+exact"}) >= 0); pump(300);
    assert(mpv.get_double("time-pos") >= 1.9);
    assert(mpv.set("pause", "no")); pump(200);
    std::printf("stage %d passed: decode, dimensions, clock, pause, seek, replacement\n", stage);
    std::fflush(stdout);
  }
  mpv.destroy();
  std::puts("bundled Windows libmpv audio integration passed");
  return 0;
}
#endif

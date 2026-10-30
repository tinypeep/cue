// Windows integration: audio_switch_engine_test.exe DLL_DIR MULTITRACK_VIDEO
// Fixture: real video, >=20 seconds, two audio tracks, long keyframe interval.
#ifdef _WIN32
#include "../src/win/mpv.h"
#include "../src/core/audio_mode.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
  assert(argc == 3);
  auto utf8 = [](const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back(); return s;
  };
  Mpv mpv; std::string error;
  assert(mpv.load(argv[1], &error));
  assert(mpv.create(nullptr, 0, 0, &error));
  mpv.option("config", "no"); mpv.option("vo", "null"); mpv.option("ao", "null");
  mpv.option("idle", "yes"); mpv.option("keep-open", "yes");
  mpv.option("load-scripts", "no"); mpv.option("msg-level", "all=error");
  assert(mpv.api().initialize(mpv.handle()) >= 0);
  bool restarted = false;
  auto drain = [&] {
    while (auto* e = mpv.wait()) {
      if (e->event_id == MPV_EVENT_NONE) break;
      if (e->event_id == MPV_EVENT_PLAYBACK_RESTART) restarted = true;
      if (e->event_id == MPV_EVENT_END_FILE)
        assert(static_cast<mpv_event_end_file*>(e->data)->reason != MPV_END_FILE_REASON_ERROR);
    }
  };
  auto pump = [&](DWORD ms) {
    auto end = GetTickCount64() + ms;
    do { drain(); Sleep(5); } while (GetTickCount64() < end);
  };
  assert(mpv.cmd({"loadfile", utf8(argv[2]), "replace"}) >= 0);
  auto deadline = GetTickCount64() + 8000;
  do { pump(20); } while (mpv.get_double("time-pos", -1) < .3 && GetTickCount64() < deadline);
  assert(mpv.get_double("time-pos", -1) >= .3);
  assert(mpv.set("pause", "yes"));
  assert(mpv.cmd({"seek", "11.25", "absolute+exact"}) >= 0);
  pump(300);
  for (bool paused : {true, false}) {
    for (double speed : {1., 1.5}) {
      assert(mpv.set("speed", std::to_string(speed)));
      assert(mpv.set("pause", paused ? "yes" : "no"));
      for (int i = 0; i < 8; ++i) {
        const double before = mpv.get_double("time-pos");
        const bool mixing = i % 2 == 0;
        auto graph = mixing ? audio_mix_graph({1, 2}, false) : std::string{};
        assert(mpv.set("pause", "yes"));
        drain(); restarted = false;
        assert(mpv.cmd_batch(audio_switch_commands(graph, mixing ? 0 : 1 + i % 2)));
        auto wait_restart = [&] {
          auto limit = GetTickCount64() + 4000;
          while (!restarted && GetTickCount64() < limit) pump(5);
          assert(restarted); drain(); restarted = false;
        };
        wait_restart();
        assert(mpv.cmd({"seek", std::to_string(before), "absolute+exact"}) >= 0);
        wait_restart();
        assert(std::abs(mpv.get_double("time-pos") - before) < .075);
        assert(mpv.set("pause", paused ? "yes" : "no"));
        const auto resumed_at = GetTickCount64();
        do {
          pump(5);
          const double elapsed = (GetTickCount64() - resumed_at) / 1000.;
          const double want = before + (paused ? 0 : elapsed * speed);
          const double actual = mpv.get_double("time-pos", want);
          if (std::abs(actual - want) > .45) {
            std::fprintf(stderr, "switch %d pause=%d speed=%.1f: %.3f vs %.3f\n", i, paused, speed, actual, want);
            assert(false);
          }
        } while (GetTickCount64() - resumed_at < 250);
        assert(mpv.get_string("pause") == (paused ? "yes" : "no"));
        assert(std::abs(mpv.get_double("speed") - speed) < .001);
        assert(mpv.get_double("dwidth") > 0);
      }
    }
  }
  mpv.destroy();
  std::puts("bundled engine: 32 single/mix transitions, paused/playing, 1x/1.5x and subsequent seek passed");
  return 0;
}
#endif

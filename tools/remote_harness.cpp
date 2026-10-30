// Runs the remote's real server without the player, for testing it from a browser.
//   x86_64-w64-mingw32-g++ -std=c++20 -O1 -municode -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00
//     -DNOMINMAX tools/remote_harness.cpp src/win/remote_server.cpp src/win/platform.cpp src/core/*.cpp
//     -o build/remote_harness.exe -static -lws2_32 -liphlpapi -lbcrypt -lole32 -lshell32 -luuid -lshcore -lcomdlg32 -ldwmapi
//   remote_harness.exe <font.ttf> <shared folder> [language]
// Prints every command it receives; pairing code 4812; a pretend episode plays.
#include <windows.h>

#include <cstdio>
#include <string>

#include "../src/core/i18n.h"
#include "../src/core/json.h"
#include "../src/core/remote_proto.h"
#include "../src/win/platform.h"
#include "../src/win/remote_server.h"

static RemoteServer g_server;
static remote::State g_st;
static DWORD g_t0 = GetTickCount();

static void publish() { g_server.set_state(remote::state_json(g_st)); }

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_APP + 12) {
    for (auto& e : g_server.take_events()) {
      if (e.kind == RemoteServer::Event::Command) {
        std::printf("command %s v=%g path=%s\n", e.cmd.c.c_str(), e.cmd.v, e.cmd.path.c_str());
        auto& c = e.cmd;
        if (c.c == "toggle") g_st.paused = !g_st.paused;
        if (c.c == "skip") g_st.pos += c.v;
        if (c.c == "seek") g_st.pos = c.v;
        if (c.c == "vol") g_st.volume = c.v;
        if (c.c == "sub") g_st.subs_on = c.v > 0;
        if (c.c == "subpos") g_st.sub_pos = c.v;
        if (c.c == "subsize") g_st.sub_size = (int)c.v;
        if (c.c == "subdelay") g_st.sub_delay = c.v;
        if (c.c == "timer") { g_st.timer = (int)c.v; g_st.timer_left = c.v > 0 ? c.v * 60 : 0; }
        if (c.c == "open") { g_st.file = c.path.substr(c.path.find_last_of('\\') + 1); g_st.pos = 0; g_st.playing_view = true; }
        publish();
      } else {
        std::printf("event %d device=%s token=%s clients=%d\n", (int)e.kind, e.device.c_str(), e.token.c_str(), g_server.clients());
      }
      std::fflush(stdout);
    }
    return 0;
  }
  if (m == WM_TIMER) {
    if (!g_st.paused) g_st.pos += 0.5;
    publish();
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int wmain(int argc, wchar_t** argv) {
  if (argc < 3) { std::printf("usage: remote_harness <font> <folder> [lang]\n"); return 2; }
  i18n::Lang lang = i18n::Lang::PtBR;
  if (argc > 3) i18n::parse_tag(narrow(argv[3]), lang);
  i18n::set_language(lang);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  WNDCLASSW wc{};
  wc.lpfnWndProc = proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"remote-harness";
  RegisterClassW(&wc);
  HWND hwnd = CreateWindowW(wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
  std::string err;
  if (!g_server.start(hwnd, WM_APP + 12, argv[1], &err)) { std::printf("start failed: %s\n", err.c_str()); return 1; }
  std::string boot = "window.CUE={\"lang\":" + json_quote(i18n::tag(lang)) + ",\"strings\":{";
  bool first = true;
  for (int i = 0; i < str::kCount; ++i) {
    std::string key = i18n::key_name((str::Id)i);
    if (key.rfind("remote.", 0) != 0 && key != "number.decimal") continue;
    boot += (first ? "" : ",") + json_quote(key) + ":" + json_quote(tr((str::Id)i));
    first = false;
  }
  boot += "}};";
  g_server.set_boot(boot);
  std::string folder = narrow(argv[2]);
  g_server.set_roots({{folder_display_name(folder), folder}});
  g_server.set_progress({{folder + "\\Show.S01E01.mkv", {0, 2520, true}}, {folder + "\\Show.S01E02.mkv", {1200, 2520, false}}});
  g_server.set_pairing(true, "4812");
  g_st.playing_view = true;
  g_st.file = "Show.S01E02.mkv";
  g_st.pos = 1200;
  g_st.dur = 2520;
  g_st.paused = false;
  g_st.has_subs = true;
  g_st.audio_tracks = 2;
  g_st.next = "Show.S01E03.mkv";
  publish();
  SetTimer(hwnd, 1, 500, nullptr);
  std::printf("listening on %d, lan %s\n", g_server.port(), lan_address().c_str());
  std::fflush(stdout);
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
  (void)g_t0;
  return 0;
}

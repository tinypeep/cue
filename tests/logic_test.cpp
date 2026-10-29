// Behaviour checks for the portable core (run on any OS).
#include <cassert>
#include <cstdio>
#include <filesystem>
#include "../src/ui/play.h"
#include "../src/core/store.h"
#include "../src/core/util.h"
#include "../src/core/sub_style.h"
#include "../src/ui/atmosphere.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void run(PlayView& pv, double& t, double secs) { for (double k = 0; k < secs; k += 1/60.0) { t += 1/60.0; pv.tick(t, 1/60.0); } }

int main() {
  PlayView pv; int s = 2, W = 1280, H = 720;
  pv.in.dur = 3000; pv.in.time = 100;
  pv.resize(W, H, s); double t = 1; pv.reset(t);
  run(pv, t, 0.5);
  CHECK(pv.dock_visible());
  run(pv, t, 2.6);
  CHECK(!pv.dock_visible());                       // mouse still for 2 s: only video
  pv.mouse_move(10, 10, t); run(pv, t, 0.3);
  CHECK(pv.dock_visible());                        // movement brings it back
  // drag the dock by its empty background toward the top: it snaps to top center
  auto& D = pv.layers[PlayView::L_DOCK];
  int gx = D.x + 70 * s, gy = D.y + 3 * s;          // empty strip on the dock background
  pv.mouse_down(gx, gy, t); pv.take_actions();
  for (int i = 1; i <= 30; ++i) { pv.mouse_move(gx, gy - i * 20, t); run(pv, t, 0.016); }
  pv.mouse_move(gx, 30, t); pv.mouse_up(gx, 30, t);
  auto acts = pv.take_actions();
  bool placed = false, paused = false;
  for (auto& a : acts) { if (a.a == PA::Placement) placed = true; if (a.a == PA::TogglePause) paused = true; }
  CHECK(placed);
  CHECK(!paused);                                  // dragging never toggles playback
  CHECK(pv.place.anchor == DockAnchor::TopCenter);
  run(pv, t, 1.0);
  CHECK(D.y < H / 4);
  // A double click changes fullscreen without interrupting playback.
  pv.mouse_down(W / 2, H / 2, t); pv.mouse_up(W / 2, H / 2, t); t += 0.2;
  pv.mouse_down(W / 2, H / 2, t); pv.mouse_up(W / 2, H / 2, t);
  int toggles = 0, fs = 0; for (auto& a : pv.take_actions()) { toggles += a.a == PA::TogglePause; fs += a.a == PA::Fullscreen; }
  CHECK(toggles == 2 && fs == 1); // immediate first click, explicit restore on double click
  // keyboard
  pv.key("right", true, false, t); pv.key("z", false, false, t); pv.key("s", false, false, t);
  acts = pv.take_actions();
  CHECK(acts.size() == 3 && acts[0].a == PA::SeekRel && acts[0].v == 30 && acts[1].a == PA::SubDelay && acts[2].a == PA::SubVisible);
  // style caps
  SubStyle st; st.ass_mode = 0;
  CHECK(!style_caps(st, SubKind::Ass).font && style_caps(st, SubKind::Ass).ass_toggle);
  CHECK(!style_caps(st, SubKind::Bitmap).position);
  bool box = false; for (auto& p : sub_style_props(st, SubKind::Text)) if (p.names[0] == "sub-border-style" && p.value == "background-box") box = true;
  CHECK(box);
  st.bg_op = 0; bool outline = false; for (auto& p : sub_style_props(st, SubKind::Text)) if (p.value == "outline-and-shadow") outline = true;
  CHECK(outline);
  // store round trip with an accented path
  Store a; a.touch("C:\\Vídeos\\Pôr do sol.mkv", 3000, 5); a.set_position("C:\\Vídeos\\Pôr do sol.mkv", 1234.5, 3000);
  SubStyle my; my.font = 2; my.bg_op = 80; a.set_style_for("C:\\Vídeos\\Pôr do sol.mkv", my); a.eggs_seen = 6;
  auto p = std::filesystem::temp_directory_path() / "player_state_test.json";
  CHECK(a.save(p));
  Store b; CHECK(b.load(p));
  CHECK(b.recents.size() == 1 && b.recents[0].path == "C:\\Vídeos\\Pôr do sol.mkv");
  CHECK(std::abs(b.resume_point("C:\\Vídeos\\Pôr do sol.mkv") - 1234.5) < 0.01);
  CHECK(b.style_for("C:\\Vídeos\\Pôr do sol.mkv").font == 2 && b.eggs_seen == 6);
  // resume skips the very end
  b.set_position("C:\\Vídeos\\Pôr do sol.mkv", 2990, 3000); CHECK(b.resume_point("C:\\Vídeos\\Pôr do sol.mkv") < 0);
  // labels
  std::vector<Track> tr = {{1, "audio", "por", "", "aac"}, {2, "audio", "eng", "Commentary", "aac"}, {3, "audio", "eng", "", "ac3"}, {4, "audio", "", "", "aac"}};
  CHECK(track_label(tr[0], tr) == "Português");
  CHECK(track_label(tr[1], tr) == "English · commentary");
  CHECK(track_label(tr[2], tr) == "English 2");
  CHECK(track_label(tr[3], tr) == "faixa 4");
  CHECK(fmt_time(5052) == "1:24:12" && fmt_time(522) == "08:42" && fmt_delay(0.3) == "+0,3 s");
  // subtitle: drag it straight from the video, no panel needed; a click on it belongs to it
  {
    PlayView v; v.in.dur = 100; v.in.sid = 1; v.in.sub_shown = true; v.in.kind = SubKind::Text;
    v.resize(W, H, s); double tt = 1; v.reset(tt); run(v, tt, 0.3); v.take_actions();
    int sy = (int)(v.lifted_pos() / 100.0 * H) - 15;  // inside the subtitle strip, where it is drawn
    v.mouse_down(W / 2, sy, tt); v.mouse_up(W / 2, sy, tt);
    auto a1 = v.take_actions();
    CHECK(a1.empty());                               // never pauses through the subtitle
    tt += 1;
    v.mouse_down(40, sy, tt); v.mouse_up(40, sy, tt);  // far beside it: just the video, a normal click
    run(v, tt, 0.4); // disambiguate from a double click
    auto a0 = v.take_actions();
    std::erase_if(a0,[](const PlayAction& a){return a.a==PA::SubLift;});
    CHECK(a0.size() == 1 && a0[0].a == PA::TogglePause);
    tt += 1;
    double before = v.in.style.pos;
    v.mouse_down(W / 2, sy, tt);
    for (int i = 1; i <= 10; ++i) v.mouse_move(W / 2, sy - i * 20, tt);
    v.mouse_up(W / 2, sy - 200, tt);
    run(v, tt, 1.0);
    bool live = false, paused2 = false;
    for (auto& a : v.take_actions()) { live |= a.a == PA::StyleLive || a.a == PA::Style; paused2 |= a.a == PA::TogglePause; }
    CHECK(live && !paused2);
    CHECK(v.in.style.pos < before - 10);
    // the top strip moves the window
    v.mouse_down(400, 10, tt); v.mouse_move(460, 30, tt);
    bool drag = false; for (auto& a : v.take_actions()) drag |= a.a == PA::WinDragBegin;
    CHECK(drag && v.win_dragging());
    v.mouse_up(460, 30, tt);
    CHECK(!v.win_dragging());
  }
  // the hand only over the subtitle's real lines (laid out like libass), not beside a short one
  {
    PlayView v; v.in.dur = 100; v.in.sid = 1; v.in.sub_shown = true; v.in.kind = SubKind::Text;
    v.in.sub_text = {"to bind us", "to whomever we meet."};
    v.place.anchor = DockAnchor::TopCenter;  // the dock away from the subtitle: no lift in this check
    v.resize(W, H, s); double tt = 1; v.reset(tt); run(v, tt, 3.0);  // dock gone: subtitle at its place
    double k = H / 720.0, fs = 44 * k, top = (H - 22 * k - 2 * fs) * v.in.style.pos / 100.0;
    int y1 = (int)(top + fs * 0.5), y2 = (int)(top + fs * 1.5);
    v.mouse_move(W / 2, y1, tt);       CHECK(v.cursor_kind() == 2);   // on the first line
    v.mouse_move(W / 2 + 150, y1, tt); CHECK(v.cursor_kind() == 0);   // beside the short line
    v.mouse_move(W / 2 + 150, y2, tt); CHECK(v.cursor_kind() == 2);   // the long line reaches there
    v.mouse_move(W / 2 + 400, y2, tt); CHECK(v.cursor_kind() == 0);   // past its end
  }
  // popovers: the first outside click only dismisses
  {
    PlayView v; v.in.dur = 100; v.in.sid = 1; v.in.subs = {Track{1, "sub", "por", "", "subrip"}}; v.in.kind = SubKind::Text;
    v.resize(W, H, s); double tt = 1; v.reset(tt); run(v, tt, 0.3);
    v.open_panel_for_preview(0, tt); run(v, tt, 0.3); v.take_actions();
    CHECK(v.panel_open());
    v.mouse_down(W / 3, H / 3, tt); v.mouse_up(W / 3, H / 3, tt);
    CHECK(!v.panel_open() && v.take_actions().empty());   // closed, nothing else happened
    v.mouse_down(W / 3, H / 3, tt); v.mouse_up(W / 3, H / 3, tt);
    run(v, tt, 0.4);
    auto a2 = v.take_actions();
    std::erase_if(a2,[](const PlayAction& a){return a.a==PA::SubLift;});
    CHECK(a2.size() == 1 && a2[0].a == PA::TogglePause);  // the second click is normal
    // keyboard: playback without revealing the dock
    run(v, tt, 3.0);
    CHECK(!v.dock_visible());
    v.key("space", false, false, tt); v.key("right", false, false, tt); v.key("]", false, false, tt);
    run(v, tt, 0.3);
    CHECK(!v.dock_visible());
    std::vector<PlayAction> a3;
    for (auto& a : v.take_actions()) if (a.a != PA::SubLift) a3.push_back(a);
    CHECK(a3.size() == 3 && a3[0].a == PA::TogglePause && a3[1].a == PA::SeekRel && a3[2].a == PA::Speed && a3[2].v == 1.25);
    // a twitch of the mouse does not bring it back; a real move does
    v.mouse_move(W / 3 + 1, H / 3, tt); run(v, tt, 0.2);
    CHECK(!v.dock_visible());
    v.mouse_move(W / 3 + 40, H / 3, tt); run(v, tt, 0.3);
    CHECK(v.dock_visible());
  }
  // settings: v1 files migrate, a corrupt file falls back to the backup
  {
    auto dir = std::filesystem::temp_directory_path();
    auto p1 = dir / "player_v1.json";
    { FILE* f = std::fopen(p1.string().c_str(), "wb");
      std::fputs("{\"v\":1,\"volume\":40,\"dock\":{\"anchor\":1},\"recents\":[{\"path\":\"C:\\\\a.mkv\",\"pos\":50,\"dur\":100}]}", f);
      std::fclose(f); }
    SettingsStore m; CHECK(m.load(p1));
    CHECK(m.load_status == "migrated" && m.volume == 40 && m.dock.anchor == DockAnchor::TopCenter && m.recents.size() == 1);
    m.ui_scale = 1; m.ambient = true; m.recents[0].sid = 2; m.recents[0].sid_lang = "por"; m.recents[0].sub_delay = 0.4;
    CHECK(m.save(p1));
    SettingsStore m2; CHECK(m2.load(p1) && m2.load_status == "ok");
    CHECK(m2.ui_scale == 1 && m2.ambient && m2.recents[0].sid == 2 && std::abs(m2.recents[0].sub_delay - 0.4) < 1e-6);
    CHECK(m2.save(p1));  // now a .bak exists
    { FILE* f = std::fopen(p1.string().c_str(), "wb"); std::fputs("{garbage", f); std::fclose(f); }
    SettingsStore m3; CHECK(m3.load(p1) && m3.load_status == "recovered" && m3.ui_scale == 1);
  }
  // atmosphere: the representative colour, not the average; greys stay neutral
  {
    std::vector<uint32_t> f(160 * 90, 0xFF000000u);
    for (int y = 20; y < 70; ++y) for (int x = 30; x < 130; ++x) f[y * 160 + x] = 0xFF1E7A80u;  // teal scene on black
    FrameColors fc = analyze_frame(f.data(), 160, 90);
    CHECK(fc.valid && fc.strength > 0.5 && fc.accent.g > fc.accent.r && fc.accent.b > fc.accent.r);
    std::vector<uint32_t> g(160 * 90, 0xFF808080u);
    for (int i = 0; i < 40; ++i) g[i * 7] = 0xFFFF0000u;  // a few neon specks on grey
    FrameColors gc = analyze_frame(g.data(), 160, 90);
    CHECK(gc.valid && gc.strength < 0.3);
    std::vector<uint32_t> n(160 * 90, 0xFFFF00FFu);       // pure neon magenta
    FrameColors nc = analyze_frame(n.data(), 160, 90);
    int mx = std::max({(int)nc.accent.r, (int)nc.accent.g, (int)nc.accent.b}), mn = std::min({(int)nc.accent.r, (int)nc.accent.g, (int)nc.accent.b});
    CHECK(mx - mn < 160);                                  // tamed, never neon
    ColorSmoother sm; sm.step({0, 0, 0}, 0, 0.016, 1.2); sm.step({255, 255, 255}, 1, 0.1, 1.2);
    CHECK(sm.rgb().r < 40);                                // a cut does not jump
  }
  {  // the volume line reaches 125% (booster) and sticks to 100%
    PlayView v; v.in.dur = 100; int S = 2;
    v.resize(1280, 720, S); double tt = 1; v.reset(tt); v.mouse_move(640, 400, tt); run(v, tt, 0.4); v.take_actions();
    auto& DL = v.layers[PlayView::L_DOCK]; Rect d{DL.x, DL.y, DL.c.w, DL.c.h};
    auto vol_at = [&](int ax) { v.mouse_down(d.x + ax * S, d.y + 13 * S, tt); v.mouse_up(d.x + ax * S, d.y + 13 * S, tt);
      double r = -1; for (auto& a : v.take_actions()) if (a.a == PA::Volume) r = a.v; return r; };
    CHECK(vol_at(27 + 46) == 125);
    CHECK(vol_at(27 + 37) == 100);                        // a hair past the notch still lands on 100
    double low = vol_at(27 + 18); CHECK(low > 45 && low < 55);
  }
  std::printf(fails ? "%d failures\n" : "all checks passed\n", fails);
  return fails ? 1 : 0;
}

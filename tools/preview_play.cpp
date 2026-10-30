// Composes PlayView layers over a frame rendered by preview_frame.py.
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/ui/play.h"
#include "../src/core/sub_style.h"
#include "../src/core/i18n.h"

static Canvas load_frame(int W, int H, const SubStyle& st) {
  std::string cmd = "python3 tools/preview_frame.py " + std::to_string(W) + " " + std::to_string(H) +
                    " /tmp/frame.png assets/fonts/" + std::string(st.font == 0 ? "CueOmni" : st.font == 1 ? "CueClean" : "CueReadable") +
                    ".ttf " + std::to_string(st.size) + " " + std::to_string(st.bg_op) + " " + std::to_string(st.pos) +
                    " && convert /tmp/frame.png -depth 8 rgba:/tmp/frame.rgba";
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "frame failed\n");
  Canvas c; c.resize(W, H);
  FILE* f = std::fopen("/tmp/frame.rgba", "rb");
  std::vector<unsigned char> b((size_t)W * H * 4);
  if (f) { size_t n = std::fread(b.data(), 1, b.size(), f); (void)n; std::fclose(f); }
  for (size_t i = 0; i < c.px.size(); ++i)
    c.px[i] = 0xFF000000u | (b[i*4] << 16) | (b[i*4+1] << 8) | b[i*4+2];
  return c;
}

void save_canvas(const Canvas& c, const std::string& name);

static void shot(PlayView& pv, double& now, const std::string& name) {
  for (int i = 0; i < 40; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); pv.take_actions(); }
  Canvas fr = load_frame(pv.in.w, pv.in.h, pv.in.style);
  for (auto& L : pv.layers) if (L.visible) fr.blit(L.c, L.x, L.y);
  save_canvas(fr, name);
}

void preview_play(const std::string& out, int s) {
  int W = 640 * s, H = 360 * s;
  PlayView pv;
  pv.in.dur = 5 * 3600 / 3.6; pv.in.time = 1869; pv.in.volume = 70;
  Track a{1, "sub", "por", "", "subrip"}, b{2, "sub", "eng", "English SDH", "subrip"};
  a.selected = true;
  pv.in.subs = {a, b}; pv.in.sid = 1; pv.in.kind = SubKind::Text;
  Track x{1, "audio", "eng", "", "aac"}, y{2, "audio", "por", "", "aac"};
  pv.in.audios = {x, y}; pv.in.aid = 1;
  pv.resize(W, H, s);
  double now = 1;
  pv.reset(now);
  pv.changed(now);
  pv.set_hover_for_preview(W / 2, H - 30 * s, now);
  shot(pv, now, out + "/play-dock.png");
  pv.set_hover_for_preview(W - 12 * s, 12 * s, now);  // close button
  shot(pv, now, out + "/play-chrome.png");
  pv.set_hover_for_preview(W / 2 - 18 * s, H - 39 * s, now);  // rest on a dock button for its tooltip
  for (int i = 0; i < 50; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); }
  shot(pv, now, out + "/play-tooltip.png");
  pv.set_hover_for_preview(W / 2 + 60 * s, H - 24 * s, now);
  shot(pv, now, out + "/play-timeline.png");
  pv.open_panel_for_preview(0, now);
  pv.set_hover_for_preview(W / 2 + 70 * s, H - 120 * s, now);
  shot(pv, now, out + "/play-cc.png");
  pv.open_panel_for_preview(1, now);
  pv.set_hover_for_preview(10, 10, now);
  pv.begin_sub_drag_for_preview(now);
  shot(pv, now, out + "/play-style.png");
  pv.mouse_up(10, 10, now);
  pv.open_panel_for_preview(3, now);
  shot(pv, now, out + "/play-more.png");
  pv.key("esc", false, false, now);
  pv.toast("CC", "check", now, true);
  for (int i = 0; i < 20; ++i) { now += 1/60.0; pv.tick(now, 1/60.0); }
  shot(pv, now, out + "/play-toast.png");
  for (int i = 0; i < 300; ++i) { now += 1/60.0; pv.tick(now, 1/60.0); }
  shot(pv, now, out + "/play-clean.png");
  // speed panel (value, slider between - and +, stops), hovering the 1.5 stop
  pv.in.speed = 1.25;
  pv.open_panel_for_preview(4, now);
  shot(pv, now, out + "/play-speed.png");
  pv.key("esc", false, false, now);
  // frame saved, with the folder button hovered; the back arrow in the corner
  pv.toast_folder(tr(str::toast_frame_saved), now);
  for (int i = 0; i < 20; ++i) { now += 1/60.0; pv.tick(now, 1/60.0); }
  pv.set_hover_for_preview(W / 2, H / 2, now);
  pv.set_hover_for_preview(W / 2 + 37 * s, 22 * s, now);
  shot(pv, now, out + "/play-frame-toast.png");
  pv.set_hover_for_preview(10 * s, 9 * s, now);
  shot(pv, now, out + "/play-back.png");
  // repeat on, on the right next to speed; then the narrowest landscape dock (audio tracks too)
  pv.in.speed = 1.0; pv.in.loop = true; pv.in.ended = true; pv.in.paused = true; pv.in.time = pv.in.dur;
  pv.changed(now);
  pv.set_hover_for_preview(W / 2, H - 30 * s, now);
  for (int i = 0; i < 50; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); }
  shot(pv, now, out + "/play-loop.png");
  PlayView pn;
  pn.in = pv.in; pn.in.ended = false;
  pn.resize(312 * s, 200 * s, s);
  pn.reset(now); pn.changed(now);
  pn.set_hover_for_preview(156 * s, 170 * s, now);
  shot(pn, now, out + "/play-narrow.png");
  // portrait: the compact dock keeps repeat on the right of the transport
  PlayView pp;
  pp.in = pv.in; pp.in.ended = false; pp.in.paused = false; pp.in.time = 61;
  pp.resize(270 * s, 480 * s, s);
  pp.reset(now); pp.changed(now);
  pp.set_hover_for_preview(135 * s, 440 * s, now);
  shot(pp, now, out + "/play-portrait.png");
}

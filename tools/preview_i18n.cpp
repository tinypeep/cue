// The main screens in each interface language, as PNG, for reviewing translations and layout
// without Windows (needs ImageMagick `convert`):
//   g++ -std=c++20 -O2 -I. -o preview_i18n tools/preview_i18n.cpp installer/setup_ui.cpp src/ui/*.cpp src/core/*.cpp
//   ./preview_i18n out 2            (art scale; files are out/<tag>-<screen>.png)
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../installer/setup_ui.h"
#include "../src/core/i18n.h"
#include "../src/core/media_info.h"
#include "../src/core/util.h"
#include "../src/ui/home.h"
#include "../src/ui/onboarding.h"
#include "../src/ui/play.h"

static void save(const Canvas& c, const std::string& name) {
  std::string raw = name + ".rgba";
  FILE* f = std::fopen(raw.c_str(), "wb");
  if (!f) return;
  std::vector<unsigned char> buf((size_t)c.w * c.h * 4);
  for (size_t i = 0; i < c.px.size(); ++i) {
    uint32_t v = c.px[i];
    unsigned a = v >> 24;
    auto un = [&](unsigned x) { return a ? std::min(255u, (x * 255 + a / 2) / a) : 0u; };
    buf[i * 4] = (unsigned char)un((v >> 16) & 255);
    buf[i * 4 + 1] = (unsigned char)un((v >> 8) & 255);
    buf[i * 4 + 2] = (unsigned char)un(v & 255);
    buf[i * 4 + 3] = (unsigned char)a;
  }
  std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  std::string cmd = "convert -size " + std::to_string(c.w) + "x" + std::to_string(c.h) + " -depth 8 rgba:" + raw + " " + name +
                    " && rm " + raw;
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed\n");
}

// a calm synthetic film frame under the playback layers
static Canvas frame(int W, int H) {
  Canvas c;
  c.resize(W, H);
  for (int y = 0; y < H; ++y) {
    double t = (double)y / H;
    Rgb col{(uint8_t)(30 + 120 * t), (uint8_t)(48 + 50 * t), (uint8_t)(86 - 30 * t)};
    c.fill_pixels(0, y, W, 1, col);
  }
  c.fill(W * 0.6, H * 0.35, W * 0.1, W * 0.1, {250, 210, 150});
  c.fill(0, H * 0.78, W, H * 0.22, {14, 12, 18});
  return c;
}

static void shot(PlayView& pv, double& now, const std::string& name) {
  for (int i = 0; i < 40; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); pv.take_actions(); }
  Canvas fr = frame(pv.in.w, pv.in.h);
  for (auto& L : pv.layers) if (L.visible) fr.blit(L.c, L.x, L.y);
  save(fr, name);
}

int main(int argc, char** argv) {
  std::string out = argc > 1 ? argv[1] : "out";
  double s = argc > 2 ? std::atof(argv[2]) : 2;
  for (int li = 0; li < i18n::kLangCount; ++li) {
    auto lang = (i18n::Lang)li;
    i18n::set_language(lang);
    std::string p = out + "/" + i18n::tag(lang) + "-";

    // home, with the ··· menu showing the language list
    {
      int W = (int)(480 * s), H = (int)(270 * s);
      HomeView hv;
      hv.set_size(W, H, s);
      hv.set_rows({{"Cena de exemplo.mkv", "1:24:12", 0.51}, {"夜のドライブ 第3話.mkv", "31:09", 0.62},
                   {"entrevista.mp4", "08:42", 0.3}, {"Été à Montréal (version finale).mov", "52:17", 0.44}});
      hv.update_version = "1.1";
      double now = 10;
      Canvas c;
      for (int i = 0; i < 6; ++i) { now += 1 / 60.0; hv.tick(now, 1 / 60.0); }
      hv.render(c, now); save(c, p + "home.png");
      HomeView hm;
      hm.set_size(W, H, s);
      hm.set_rows(hv.rows);
      hm.language = li == 0 ? -1 : li;
      // open the menu, then its language list (keyboard, like a person would)
      for (int i = 0; i < 1 + (int)hv.rows.size() + 1; ++i) hm.key("tab", false, now);
      hm.key("enter", false, now);
      hm.key("enter", false, now);
      for (int i = 0; i < 30; ++i) { now += 1 / 60.0; hm.tick(now, 1 / 60.0); }
      hm.render(c, now); save(c, p + "home-language.png");
      HomeView he;
      he.set_size(W, H, s);
      he.drag_over(W * 0.8, H * 0.2, now);
      for (int i = 0; i < 30; ++i) { now += 1 / 60.0; he.tick(now, 1 / 60.0); }
      he.render(c, now); save(c, p + "home-empty-drag.png");
    }

    // welcome, the four screens
    {
      OnboardingView ob;
      ob.set_size((int)(480 * s), (int)(270 * s), s);
      double now = 10, dt = 1 / 60.0;
      ob.start(now);
      Canvas c;
      auto run = [&](double secs) { for (int i = 0; i < (int)(secs / dt); ++i) { now += dt; ob.tick(now, dt); } };
      run(4.0); ob.render(c, now); save(c, p + "welcome-1.png");
      ob.go(1, now); run(1.4); ob.render(c, now); save(c, p + "welcome-2.png");
      ob.go(2, now); run(1.2); ob.render(c, now); save(c, p + "welcome-3.png");
      ob.go(3, now); run(1.6); ob.render(c, now); save(c, p + "welcome-4.png");
    }

    // playback: dock, tooltip, menus, file info, toasts
    {
      int W = (int)(640 * s), H = (int)(360 * s);
      PlayView pv;
      pv.in.dur = 5000; pv.in.time = 1869; pv.in.volume = 70;
      Track a{1, "sub", "por", "", "subrip"}, b{2, "sub", "eng", "English SDH", "subrip"}, k{3, "sub", "jpn", "", "ass"};
      k.forced = true;
      pv.in.subs = {a, b, k}; pv.in.sid = 1; pv.in.kind = SubKind::Text;
      Track x{1, "audio", "eng", "", "aac"}, y{2, "audio", "fre", "", "ac3"}, z{3, "audio", "", "", "aac"};
      pv.in.audios = {x, y, z}; pv.in.aid = 1;
      pv.in.language = li == 1 ? -1 : li;
      pv.in.update_ready = true;
      pv.in.update_label = i18n::format(str::update_to, {{"version", "1.1"}});
      MediaDetails md;
      md.name = "Landscape at sunset (final version).mkv"; md.bytes = 1834567890; md.duration = 5000;
      md.container = "matroska,webm"; md.width = 3840; md.height = 2160; md.fps = 23.976; md.video_codec = "hevc";
      md.audio_codec = "eac3"; md.channels = 6; md.sample_rate = 48000;
      pv.in.file_info = media_info_fields(md);
      pv.resize(W, H, s);
      double now = 1;
      pv.reset(now);
      pv.changed(now);
      pv.set_hover_for_preview(W / 2, H - 30 * (int)s, now);
      shot(pv, now, p + "play-dock.png");
      pv.set_hover_for_preview(W / 2 - 18 * (int)s, H - 39 * (int)s, now);
      for (int i = 0; i < 50; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); }
      shot(pv, now, p + "play-tooltip.png");
      const char* names[] = {"cc", "style", "audio", "more", "speed", "about", "fileinfo", "language", "context"};
      for (int which : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
        pv.open_panel_for_preview(which, now);
        pv.set_hover_for_preview(4, 4, now);
        shot(pv, now, p + "play-" + names[which] + ".png");
        pv.key("esc", false, false, now);
        pv.key("esc", false, false, now);
      }
      pv.toast(i18n::format(str::toast_sub_delay, {{"delay", fmt_delay(0.3)}}), nullptr, now);
      for (int i = 0; i < 10; ++i) { now += 1 / 60.0; pv.tick(now, 1 / 60.0); }
      shot(pv, now, p + "play-toast.png");
      // portrait video: the compact dock and the More menu
      PlayView pp;
      pp.in = pv.in;
      pp.resize((int)(270 * s), (int)(480 * s), s);
      pp.reset(now); pp.changed(now);
      pp.open_panel_for_preview(3, now);
      shot(pp, now, p + "play-portrait-more.png");
    }

    // installer
    {
      Canvas c;
      auto setup = [&](SetupMode m, SetupStage st, double prog, const std::string& name) {
        SetupView v; v.mode = m; v.version = "1.0"; v.old_version = m == SetupMode::Install ? "" : "0.9";
        v.dir = "C:\\Cue";
        v.status = i18n::format(str::setup_copying, {{"file", "libmpv-2.dll"}});
        v.error = tr(str::setup_needs_admin);
        v.set_size((int)(SetupView::art_w() * s), (int)(SetupView::kH * s), s);
        double now = 1; v.start(now);
        for (int i = 0; i < 60; ++i) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
        if (st != SetupStage::Ask) v.set_stage(st, now);
        v.progress = prog;
        for (int i = 0; i < 90; ++i) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
        v.render(c, now); save(c, p + name);
      };
      setup(SetupMode::Install, SetupStage::Ask, 0, "setup-install.png");
      setup(SetupMode::Install, SetupStage::Working, 0.42, "setup-working.png");
      setup(SetupMode::Install, SetupStage::Done, 1, "setup-done.png");
      setup(SetupMode::Update, SetupStage::Ask, 0, "setup-update.png");
      setup(SetupMode::Uninstall, SetupStage::Ask, 0, "setup-uninstall.png");
      setup(SetupMode::Uninstall, SetupStage::Done, 1, "setup-uninstalled.png");
      setup(SetupMode::Install, SetupStage::Failed, 0, "setup-failed.png");
    }
    std::printf("%s done\n", i18n::tag(lang));
  }
}

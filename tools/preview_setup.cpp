// PNG previews of the installer window: g++ -std=c++20 -O2 -o preview_setup tools/preview_setup.cpp installer/setup_ui.cpp src/ui/*.cpp src/core/*.cpp
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/core/i18n.h"
#include "../installer/setup_ui.h"
void save_canvas(const Canvas& c, const std::string& name) {
  std::string raw = name + ".rgba";
  FILE* f = std::fopen(raw.c_str(), "wb");
  std::vector<unsigned char> buf((size_t)c.w * c.h * 4);
  for (size_t i = 0; i < c.px.size(); ++i) {
    uint32_t v = c.px[i]; unsigned a = v >> 24;
    auto un = [&](unsigned x) { return a ? std::min(255u, (x * 255 + a / 2) / a) : 0u; };
    buf[i*4]=(unsigned char)un((v>>16)&255); buf[i*4+1]=(unsigned char)un((v>>8)&255); buf[i*4+2]=(unsigned char)un(v&255); buf[i*4+3]=(unsigned char)a;
  }
  std::fwrite(buf.data(), 1, buf.size(), f); std::fclose(f);
  std::string cmd = "convert -size " + std::to_string(c.w) + "x" + std::to_string(c.h) + " -depth 8 rgba:" + raw + " " + name + " && rm " + raw;
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed\n");
}
int main(int argc, char** argv) {
  // CUE_LANG=en|es|fr|zh-Hans picks the interface language (Portuguese by default)
  { i18n::Lang l = i18n::Lang::PtBR; if (const char* e = std::getenv("CUE_LANG")) i18n::parse_tag(e, l); i18n::set_language(l); }
  std::string out = argc > 1 ? argv[1] : "out";
  double s = 2;
  Canvas c;
  auto shot = [&](SetupMode m, SetupStage st, double prog, const std::string& name, bool hover = false) {
    SetupView v; v.mode = m; v.version = "1.0"; v.old_version = m == SetupMode::Install ? "" : "0.9";
    v.dir = "C:\\Cue";
    v.status = i18n::format(str::setup_copying, {{"file", "libmpv-2.dll"}}); v.error = tr(str::setup_needs_admin);
    v.set_size(SetupView::art_w() * s, SetupView::kH * s, s);
    double now = 1; v.start(now);
    if (hover) v.mouse_move(v.w - 60, v.h - 24, now);
    for (int i = 0; i < 60; ++i) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
    if (st != SetupStage::Ask) v.set_stage(st, now);
    v.progress = prog;
    for (int i = 0; i < 90; ++i) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
    v.render(c, now); save_canvas(c, out + "/" + name);
  };
  shot(SetupMode::Install, SetupStage::Ask, 0, "setup-install.png", true);
  shot(SetupMode::Install, SetupStage::Working, 0.42, "setup-working.png");
  shot(SetupMode::Install, SetupStage::Done, 1, "setup-done.png");
  shot(SetupMode::Upgrade, SetupStage::Ask, 0, "setup-upgrade.png");
  shot(SetupMode::Update, SetupStage::Working, 0.7, "setup-update.png");
  shot(SetupMode::Uninstall, SetupStage::Ask, 0, "setup-uninstall.png");
  shot(SetupMode::Uninstall, SetupStage::Done, 1, "setup-uninstalled.png");
  shot(SetupMode::Install, SetupStage::Failed, 0, "setup-failed.png");
  std::puts("ok");
}

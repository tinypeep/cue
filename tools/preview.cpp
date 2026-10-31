// Renders Cue screens to PNG on any OS (uses ImageMagick `convert` to encode).
// Build: see README.md (Desenvolvimento).
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/core/i18n.h"
#include <vector>

#include "../src/ui/gfx.h"
#include "../src/ui/home.h"
#ifdef WITH_PLAY
#include "../src/ui/play.h"
#endif

void save_canvas(const Canvas& c, const std::string& name);
static void save(const Canvas& c, const std::string& name) { save_canvas(c, name); }
void save_canvas(const Canvas& c, const std::string& name) {
  std::string raw = "/tmp/preview.rgba";
  FILE* f = std::fopen(raw.c_str(), "wb");
  std::vector<unsigned char> buf((size_t)c.w * c.h * 4);
  for (size_t i = 0; i < c.px.size(); ++i) {
    uint32_t v = c.px[i];
    unsigned a = v >> 24;
    auto un = [&](unsigned x) { return a ? std::min(255u, (x * 255 + a / 2) / a) : 0u; };
    buf[i * 4 + 0] = (unsigned char)un((v >> 16) & 255);
    buf[i * 4 + 1] = (unsigned char)un((v >> 8) & 255);
    buf[i * 4 + 2] = (unsigned char)un(v & 255);
    buf[i * 4 + 3] = (unsigned char)a;
  }
  std::fwrite(buf.data(), 1, buf.size(), f);
  std::fclose(f);
  std::string cmd = "convert -size " + std::to_string(c.w) + "x" + std::to_string(c.h) + " -depth 8 rgba:" + raw +
                    " " + name;
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed\n");
  std::printf("%s\n", name.c_str());
}

static void run(HomeView& hv, double& now, double secs) {
  for (double t = 0; t < secs; t += 1 / 60.0) { now += 1 / 60.0; hv.tick(now, 1 / 60.0); }
}

int main(int argc, char** argv) {
  // CUE_LANG=en|es|fr|zh-Hans|ru picks the interface language (Portuguese by default)
  { i18n::Lang l = i18n::Lang::PtBR; if (const char* e = std::getenv("CUE_LANG")) i18n::parse_tag(e, l); i18n::set_language(l); }
  std::string out = argc > 1 ? argv[1] : "out";
  int s = argc > 2 ? std::atoi(argv[2]) : 2;
  int W = 480 * s, H = 270 * s;
  HomeView hv;
  hv.set_size(W, H, s);
  hv.set_rows({{"Cena de exemplo.mkv", "1:24:12", 0.51},
               {"Episódio de exemplo.mkv", "31:09", 0.62},
               {"entrevista.mp4", "08:42", 0.3},
               {"filme.mov", "52:17", 0.44},
               {"Landscape at sunset (final revision).mp4", "12:03", -1}});
  Canvas c;
  double now = 10;
  run(hv, now, 0.1);
  hv.render(c, now); save(c, out + "/home.png");

  hv.mouse_move(W * 0.7, hv.face.cy - 0, now);
  hv.set_hover_row(0, now);
  run(hv, now, 0.6);
  hv.render(c, now); save(c, out + "/home-hover.png");
  hv.mouse_leave(now);
  run(hv, now, 0.6);

  hv.drag_over(W * 0.8, H * 0.2, now);
  run(hv, now, 0.6);
  hv.render(c, now); save(c, out + "/home-drag.png");
  hv.drag_leave(now);
  run(hv, now, 1);

  for (int v = 1; v <= 3; ++v) {
    hv.force_egg(v, now);
    double mid[4] = {0, 3.0, 1.5, 2.6};
    run(hv, now, mid[v]);
    hv.render(c, now); save(c, out + "/egg" + std::to_string(v) + ".png");
    run(hv, now, 5);
  }

  HomeView empty;
  empty.set_size(W, H, s);
  empty.render(c, 1); save(c, out + "/home-empty.png");

#ifdef WITH_PLAY
  extern void preview_play(const std::string&, int);
  preview_play(out, s);
#endif
  return 0;
}

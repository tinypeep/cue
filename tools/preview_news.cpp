// Renders the "saiu do forno" splash to PNG at a few window sizes.
//   g++ -std=c++20 -O1 -o preview_news tools/preview_news.cpp src/ui/*.cpp src/core/*.cpp && ./preview_news out
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/core/i18n.h"
#include "../src/ui/news_view.h"

static void save(const Canvas& c, const std::string& name) {
  std::string raw = "/tmp/preview_news.rgba";
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
                    " -background '#202020' -flatten " + name;
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed\n");
  std::printf("%s\n", name.c_str());
}

int main(int argc, char** argv) {
  { i18n::Lang l = i18n::Lang::PtBR; if (const char* e = std::getenv("CUE_LANG")) i18n::parse_tag(e, l); i18n::set_language(l); }
  std::string out = argc > 1 ? argv[1] : "out";
  struct Sz { int w, h; double s; const char* name; } sizes[] = {
      {1280, 720, 2, "news-1280"}, {1920, 1080, 2, "news-1920"}, {800, 520, 2, "news-small"}, {560, 640, 1, "news-narrow"}};
  for (auto& z : sizes) {
    NewsView v;
    v.version = "1.1";
    v.set_size(z.w, z.h, z.s);
    Canvas c;
    double now = 100;
    v.open(now);
    for (double t = 0; t < 0.9; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
    v.pointer_move(v.w / 2 + 60, v.h - 40, now);
    for (double t = 0; t < 0.3; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
    v.render(c, now);
    save(c, out + "/" + z.name + ".png");
  }
}

// Renders the remote's pairing scene to PNG (waiting, connected, no network).
//   g++ -std=c++20 -O1 -o preview_remote tools/preview_remote.cpp src/ui/*.cpp src/core/*.cpp && ./preview_remote out 2
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/core/i18n.h"
#include "../src/ui/remote_view.h"

static void save(const Canvas& c, const std::string& name) {
  std::string raw = "/tmp/preview_remote.rgba";
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
  // over black, like the film behind it
  std::string cmd = "convert -size " + std::to_string(c.w) + "x" + std::to_string(c.h) + " -depth 8 rgba:" + raw +
                    " -background black -flatten " + name;
  if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "convert failed\n");
  std::printf("%s\n", name.c_str());
}

int main(int argc, char** argv) {
  { i18n::Lang l = i18n::Lang::PtBR; if (const char* e = std::getenv("CUE_LANG")) i18n::parse_tag(e, l); i18n::set_language(l); }
  std::string out = argc > 1 ? argv[1] : "out";
  int s = argc > 2 ? std::atoi(argv[2]) : 2;
  RemoteView v;
  v.chips = {{"Vídeos", true}, {"Downloads", true}, {"Área de Trabalho", false}};
  v.address = "192.168.0.12:7878";
  v.code = "481234";
  v.set_size(480 * s, 270 * s, s);
  v.set_link("http://192.168.0.12:7878/#c=481234");
  Canvas c;
  double now = 100;
  v.open(now);
  // the opening, a few frames in
  for (double at : {0.12, 0.3, 0.5, 0.75}) {
    RemoteView o = v;
    double t = now;
    while (t < now + at) { t += 1 / 60.0; o.tick(t, 1 / 60.0); }
    o.render(c, t);
    char name[64];
    std::snprintf(name, sizeof name, "/remote-open-%03d.png", (int)(at * 100));
    save(c, out + name);
  }
  v.phones = {"iPhone", "Android", "iPhone"};
  for (double t = 0; t < 1.2; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
  v.render(c, now); save(c, out + "/remote-waiting.png");
  for (double t = 0; t < 1.3; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
  v.mouse_move(v.w / 2, v.h - 22 * s, now);
  for (double t = 0; t < 0.3; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
  v.render(c, now); save(c, out + "/remote-waiting-2.png");
  v.connected("iPhone", now);
  for (double t = 0; t < 0.35; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
  v.render(c, now); save(c, out + "/remote-connected.png");
  RemoteView n = v;
  n.problem = "";
  n.set_link("");
  n.open(now);
  for (double t = 0; t < 1; t += 1 / 60.0) { now += 1 / 60.0; n.tick(now, 1 / 60.0); }
  n.render(c, now); save(c, out + "/remote-nonet.png");
  // a small window
  v.set_size(320 * s, 200 * s, s);
  v.open(now);
  for (double t = 0; t < 1; t += 1 / 60.0) { now += 1 / 60.0; v.tick(now, 1 / 60.0); }
  v.render(c, now); save(c, out + "/remote-small.png");
  // a phone comes in, frame by frame, then the scene leaves
  {
    RemoteView q;
    q.chips = {{"Vídeos", true}, {"Downloads", true}};
    q.address = "192.168.0.12:7878";
    q.code = "481234";
    q.set_size(480 * s, 270 * s, s);
    q.set_link("http://192.168.0.12:7878/#c=481234");
    double t = 500;
    q.open(t);
    for (int i = 0; i < 120; ++i) { t += 1 / 60.0; q.tick(t, 1 / 60.0); }
    q.connected("iPhone", t);
    double t0 = t;
    for (double at : {0.1, 0.25, 0.45, 0.62, 0.9, 1.2, 1.6, 2.4}) {
      while (t < t0 + at) { t += 1 / 60.0; q.tick(t, 1 / 60.0); }
      q.render(c, t);
      char name[64];
      std::snprintf(name, sizeof name, "/conn-%03d.png", (int)(at * 100));
      save(c, out + name);
    }
    q.close(t);
    double t1 = t;
    for (double at : {0.1, 0.2, 0.3}) {
      while (t < t1 + at) { t += 1 / 60.0; q.tick(t, 1 / 60.0); }
      q.render(c, t);
      char name[64];
      std::snprintf(name, sizeof name, "/leave-%03d.png", (int)(at * 100));
      save(c, out + name);
    }
  }
  for (auto size : {std::pair{500, 890}, {320,568}, {320,200}}) {
    v.set_size(size.first * s, size.second * s, s);
    v.open(now);
    v.render(c, now + 2);
    save(c, out + "/remote-" + std::to_string(size.first) + "x" + std::to_string(size.second) + ".png");
  }

}

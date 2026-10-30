// GitHub cover (social preview), README banner and icon, drawn with Cue's own pixel renderer.
//   g++ -std=c++20 -I. -o brand tools/brand_images.cpp src/ui/*.cpp src/core/*.cpp && ./brand
// Every block is drawn on its own transparent canvas, trimmed to its ink, and only then placed:
// the composition is centred on what is actually visible, not on guessed offsets.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/ui/face.h"
#include "../src/ui/gfx.h"

static void save(const Canvas& c, const std::string& name) {
  FILE* f = std::fopen((name + ".rgba").c_str(), "wb");
  for (uint32_t v : c.px) {
    unsigned a = v >> 24;
    auto un = [&](unsigned x) { return a ? std::min(255u, (x * 255 + a / 2) / a) : 0u; };
    unsigned char b[4] = {(unsigned char)un((v >> 16) & 255), (unsigned char)un((v >> 8) & 255), (unsigned char)un(v & 255), (unsigned char)a};
    std::fwrite(b, 1, 4, f);
  }
  std::fclose(f);
  std::string cmd = "convert -size " + std::to_string(c.w) + "x" + std::to_string(c.h) + " -depth 8 rgba:" + name +
                    ".rgba " + name + ".png && rm " + name + ".rgba";
  if (std::system(cmd.c_str())) std::fprintf(stderr, "convert failed\n");
}

// The visible part of a canvas, as its own canvas.
static Canvas trim(const Canvas& c) {
  int x0 = c.w, y0 = c.h, x1 = -1, y1 = -1;
  for (int y = 0; y < c.h; ++y)
    for (int x = 0; x < c.w; ++x)
      if (c.px[(size_t)y * c.w + x] >> 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
  Canvas o;
  if (x1 < 0) return o;
  o.resize(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = 0; y < o.h; ++y)
    for (int x = 0; x < o.w; ++x) o.px[(size_t)y * o.w + x] = c.px[(size_t)(y + y0) * c.w + x + x0];
  return o;
}
static Canvas blank(int w, int h) { Canvas c; c.resize(w, h); c.clear_transparent(); return c; }

const char* kBucket[19] = {
    "......##...#.....", "...##.###.###....", "..#####+##+####..", ".###+######+####.", "#####+##########+",
    "RRRRRRRRRRRRRRRRR", ".aaabbbaaabbbaaa.", ".aaabbbaaabbbaaa.", ".aaabbbaaabbbaaa.", "..aabbbaaabbbaa..",
    "..aabbbaaabbbaa..", "..aabbbaaabbbaa..", "..aabbbaaabbbaa..", "...abbbaaabbba...", "...abbbaaabbba...",
    "...abbbaaabbba...", "...abbbaaabbba...", "....bbbaaabbb....", "....bbbaaabbb...."};

// The character in three parts (eyes, bucket, kernels), cropped to the same box so they line up.
struct Character { Canvas eyes, bucket, kernels; int w = 0, h = 0; };
static Canvas crop(const Canvas& c, int x0, int y0, int w, int h) {
  Canvas o = blank(w, h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) o.px[(size_t)y * w + x] = c.px[(size_t)(y + y0) * c.w + x + x0];
  return o;
}
static Character character(int u) {
  const Palette& p = P();
  Canvas e = blank(60 * u, 60 * u), b = blank(60 * u, 60 * u), k = blank(60 * u, 60 * u);
  int cx = 30 * u, top = 6 * u;
  Face face;
  face.layout(cx, top + 9 * u, u / 2.0, 2.5);
  face.max_look = 3;
  face.gx.x = face.gx.target = 1.2;
  face.gy.x = face.gy.target = -1.0;  // looking up at the popcorn
  face.started = true;
  face.render(e, 1.0);
  int bx = cx - 17 * u / 2, by = top + 20 * u;
  for (int j = 0; j < 19; ++j)
    for (int i = 0; i < 17; ++i) {
      char ch = kBucket[j][i];
      if (ch == '.') continue;
      Rgb col = ch == '#' ? p.pixel : ch == '+' ? p.dim : (ch == 'R' || ch == 'a') ? p.fg : p.faint;
      b.fill(bx + i * u, by + j * u, u, u, col);
    }
  const char* kk[2] = {".#.###.#.", "##.###.##"};
  k.cells(kk[0], 3, 3, bx + 2 * u, by - 7 * u, u, p.pixel);
  k.cells(kk[1], 3, 3, bx + 13 * u, by - 10 * u, u, p.pixel);
  k.cells(kk[0], 3, 3, bx + 17 * u, by - 4 * u, u, p.pixel);
  // the box around all three
  int x0 = e.w, y0 = e.h, x1 = -1, y1 = -1;
  for (const Canvas* c : {&e, &b, &k})
    for (int y = 0; y < c->h; ++y)
      for (int x = 0; x < c->w; ++x)
        if (c->px[(size_t)y * c->w + x] >> 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
  Character out;
  out.w = x1 - x0 + 1; out.h = y1 - y0 + 1;
  out.eyes = crop(e, x0, y0, out.w, out.h);
  out.bucket = crop(b, x0, y0, out.w, out.h);
  out.kernels = crop(k, x0, y0, out.w, out.h);
  return out;
}

// A picture made of named layers: saved flat (PNG) and layer by layer (for the PSD).
struct Doc {
  int w, h;
  std::vector<std::pair<std::string, Canvas>> layers;
  Doc(int w_, int h_) : w(w_), h(h_) {}
  Canvas& add(const std::string& name) { layers.push_back({name, blank(w, h)}); return layers.back().second; }
  void place(const std::string& name, const Canvas& src, int x, int y) { add(name).blit(src, x, y); }
  void save_all(const std::string& base) {
    Canvas flat = blank(w, h);
    for (auto& l : layers) flat.blit(l.second, 0, 0);
    save(flat, base);
    std::string dir = base + "-layers";
    if (std::system(("mkdir -p " + dir).c_str())) {}
    // files numbered bottom to top; the layer names (with spaces and accents) go in layers.txt
    FILE* names = std::fopen((dir + "/layers.txt").c_str(), "wb");
    for (size_t i = 0; i < layers.size(); ++i) {
      char n[32]; std::snprintf(n, sizeof n, "%02d", (int)(i + 1));
      save(layers[i].second, dir + "/" + n);
      if (names) std::fprintf(names, "%s\t%s\n", n, layers[i].first.c_str());
    }
    if (names) std::fclose(names);
  }
};

// The ambient light of the first screen: stepped, dithered rings of film colour around a point.
static void glow(Canvas& c, int ox, int oy, int R, int gc, double strength) {
  static const int bayer[4] = {0, 2, 3, 1};
  const Rgb top{120, 60, 210}, bot{244, 140, 76};
  int sx = ox - R - ((ox - R) % gc + gc) % gc, sy = oy - R - ((oy - R) % gc + gc) % gc;
  for (int yy = sy; yy < oy + R; yy += gc)
    for (int xx = sx; xx < ox + R; xx += gc) {
      double d = std::hypot(xx + gc / 2.0 - ox, yy + gc / 2.0 - oy) / R;
      if (d >= 1) continue;
      double fall = (1 - d) * (1 - d);
      int th = bayer[(((yy / gc) & 1) * 2) + ((xx / gc) & 1)];
      double q = std::floor(fall * 4 + th / 4.0) / 4.0;
      if (q <= 0) continue;
      double ny = std::clamp((yy + gc / 2.0 - (oy - R)) / (2.0 * R), 0.0, 1.0);
      c.fill(xx, yy, gc, gc, mix(top, bot, ny), strength * std::min(1.0, q));
    }
}

// A column of lines, left-aligned on their ink, with the given gaps between them.
struct Line { Canvas ink; int gap_before = 0; };
static Line words(const std::string& t, double s, Rgb col, int gap) {
  Canvas c = blank(text_w(t, s) + (int)(8 * s), (int)(16 * s));
  text(c, t, (int)(2 * s), (int)(2 * s), s, col);
  return {trim(c), gap};
}
static Line icons(double s, int spacing) {
  const Palette& p = P();
  Canvas c = blank(1200, (int)(12 * s));
  int x = 0;
  for (const char* n : {"rewind", "play", "forward", "loop", "cc", "fs-enter"}) {
    const BitmapDef* b = bmp(n);
    if (!b) continue;
    c.bitmap(b, x, (int)std::lround((7 - b->h) * s / 2), s, std::string(n) == "play" ? p.pixel : p.dim);
    x += (int)(b->w * s) + spacing;
  }
  return {trim(c), 0};
}
struct Named { std::string name; Line line; };
static int column_w(const std::vector<Named>& v) { int w = 0; for (auto& n : v) w = std::max(w, n.line.ink.w); return w; }
static int column_h(const std::vector<Named>& v) { int h = 0; for (auto& n : v) h += n.line.gap_before + n.line.ink.h; return h; }
static void place_column(Doc& d, const std::vector<Named>& v, int x, int y) {
  for (auto& n : v) { y += n.line.gap_before; d.place(n.name, n.line.ink, x, y); y += n.line.ink.h; }
}

// The character and the words side by side, both centred on the middle line, the pair centred
// across; the light sits behind the character.
static void pair(Doc& d, const Character& who, const std::vector<Named>& col, int gap, int glow_r, int glow_cell) {
  int cw = column_w(col), ch = column_h(col);
  int total = who.w + gap + cw;
  int lx = (d.w - total) / 2, ly = (d.h - who.h) / 2;
  int rx = lx + who.w + gap, ry = (d.h - ch) / 2;
  glow(d.add("ambient light"), lx + who.w / 2, ly + who.h * 11 / 20, glow_r, glow_cell, 0.85);
  d.place("eyes", who.eyes, lx, ly);
  d.place("popcorn bucket", who.bucket, lx, ly);
  d.place("popcorn", who.kernels, lx, ly);
  place_column(d, col, rx, ry);
}

int main() {
  const Palette& p = P();
  // 1 · cover / social preview, 1280 x 640 (GitHub's 2:1)
  {
    Doc d(1280, 640);
    d.add("background").clear(p.bg);
    std::vector<Named> col = {{"name", words("Cue", 22, p.pixel, 0)},
                              {"tagline", words("video player for Windows", 4, p.fg, 34)},
                              {"subtitle", words("beautiful, lightweight, subtitle-first", 3, p.dim, 16)},
                              {"icons", Line{icons(4, 26).ink, 44}},
                              {"signature", words(".tinypeep  ·  v1.0", 3, p.faint, 40)}};
    pair(d, character(8), col, 96, 270, 16);
    d.save_all("cue-cover");
  }
  // 2 · README banner, 1280 x 320
  {
    Doc d(1280, 320);
    d.add("background").clear(p.bg);
    std::vector<Named> col = {{"name", words("Cue", 15, p.pixel, 0)},
                              {"tagline", words("video player for Windows", 3, p.fg, 24)},
                              {"signature", words(".tinypeep  ·  v1.0", 2, p.dim, 14)}};
    pair(d, character(5), col, 72, 160, 10);
    d.save_all("cue-banner");
  }
  // 3 · icon, 512 x 512: the eyes over the name, centred in a black square with stepped corners
  {
    Doc d(512, 512);
    auto stepped = [](Canvas& c, int x, int y, int w, int h, int st, Rgb col) {
      for (int k = 0; k < 3; ++k) {
        int in = (3 - k) * st;
        c.fill(x + in, y + k * st, w - 2 * in, st, col);
        c.fill(x + in, y + h - (k + 1) * st, w - 2 * in, st, col);
      }
      c.fill(x, y + 3 * st, w, h - 6 * st, col);
    };
    stepped(d.add("border"), 16, 16, 480, 480, 12, Rgb{34, 34, 34});
    stepped(d.add("background"), 28, 28, 456, 456, 12, Rgb{4, 4, 4});
    Canvas eyes = blank(512, 300);
    Face face; face.layout(256, 200, 5, 2.5); face.started = true; face.render(eyes, 1.0);
    Canvas e = trim(eyes);
    Line w = words("Cue", 12, p.pixel, 0);
    int gap = 40, h = e.h + gap + w.ink.h, y = (512 - h) / 2;
    glow(d.add("ambient light"), 256, y + h / 2, 230, 16, 0.75);
    d.place("eyes", e, 256 - e.w / 2, y);   // centred on the word under them
    d.place("name", w.ink, 256 - w.ink.w / 2, y + e.h + gap);
    d.save_all("cue-icon");
  }
  std::puts("ok");
}

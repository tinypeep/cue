#pragma once
#include "gfx.h"

// The video shader runs before OSD composition; glyphs never enter the blur.
struct BlurCloud {
  Rect controls;
  double alpha = 0;
};

// Pure-white ink with the native equivalent of drop-shadow(0 1px 2px #0009).
// Only the shadow is filtered. Pixel glyphs are copied without resampling.
inline void white_control_shadow(Canvas& c, double strength) {
  const int w = c.w, h = c.h;
  std::vector<double> tmp(c.px.size());
  constexpr double weights[] = {0.070159, 0.131075, 0.190713, 0.216106,
                                0.190713, 0.131075, 0.070159};
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    double a = 0;
    for (int k = -3; k <= 3; ++k)
      if (x + k >= 0 && x + k < w) a += (c.px[y*w+x+k] >> 24) * weights[k+3];
    tmp[y*w+x] = a;
  }
  Canvas shadow; shadow.resize(w, h);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
    double a = 0;
    for (int k = -3; k <= 3; ++k)
      if (y-1+k >= 0 && y-1+k < h) a += tmp[(y-1+k)*w+x] * weights[k+3];
    shadow.px[y*w+x] = (uint32_t)std::lround(a * 0.6 * strength) << 24;
  }
  shadow.blit(c, 0, 0);
  c = std::move(shadow);
}

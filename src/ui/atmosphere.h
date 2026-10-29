// The film's atmosphere, read from a small frame: not the RGB average, but the colour that
// is actually present and representative, with deep blacks ignored and neon tamed.
#pragma once
#include <cstdint>

#include "gfx.h"

struct FrameColors {
  bool valid = false;
  Rgb accent{255, 255, 255};  // for the timeline, handle and active states (readable on black)
  double strength = 0;        // 0 = a grey scene (stay neutral) .. 1 = a clearly coloured one
  Rgb edge[4]{};              // top, right, bottom, left: soft light for the ambient glow
  double edge_light[4]{};     // how much light each side gives (dark sides glow less)
  // Ambilight-style zones around the picture, clockwise from the top-left corner:
  // top 8 (left to right), right 5 (top to bottom), bottom 8 (right to left), left 5 (bottom to top)
  static constexpr int kZones = 26;
  Rgb zone[kZones]{};         // vivid hue of that stretch of the border (full value)
  double zone_light[kZones]{};  // how bright that stretch is (0 = dark, no glow)
};

// px: BGRA (0xAARRGGBB), w x h.
FrameColors analyze_frame(const uint32_t* px, int w, int h);

// Moves `cur` toward `target` with a time constant `tau` (seconds): never a jump at a cut.
struct ColorSmoother {
  double r = 255, g = 255, b = 255, k = 0;
  bool primed = false;
  bool step(Rgb target, double target_k, double dt, double tau);
  Rgb rgb() const;
};

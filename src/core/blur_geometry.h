#pragma once
#include "../ui/blur_cloud.h"
#include <utility>

struct CloudUniforms { double x=.5, y=.5, rx=.5, ry=.1, sx=.01, sy=.01; };

// A point of the window (client px) in OUTPUT texture coordinates (0..1 over the picture itself).
inline std::pair<double, double> output_point(double px, double py, int viewport_w, int viewport_h,
                                              int video_w, int video_h, int rotation) {
  rotation = (rotation % 360 + 360) % 360;
  bool portrait_rotation = rotation == 90 || rotation == 270;
  double vw = portrait_rotation ? video_h : video_w;
  double vh = portrait_rotation ? video_w : video_h;
  double fit = std::min(viewport_w / std::max(1., vw), viewport_h / std::max(1., vh));
  vw = std::max(1., vw * fit); vh = std::max(1., vh * fit);
  double x = (px - (viewport_w - vw) / 2) / vw, y = (py - (viewport_h - vh) / 2) / vh;
  if (rotation == 90) x = 1 - x;
  if (rotation == 180) { x = 1 - x; y = 1 - y; }
  if (rotation == 270) y = 1 - y;
  return {x, y};
}

// Convert the viewport ellipse into the OUTPUT texture (already transposed for 90/270 degrees). This keeps
// letterboxing, portrait clips and metadata rotation aligned with the controls.
inline CloudUniforms cloud_uniforms(BlurCloud cloud, int viewport_w, int viewport_h,
                                   int video_w, int video_h, int rotation, int dpi) {
  rotation = (rotation % 360 + 360) % 360;
  bool portrait_rotation = rotation == 90 || rotation == 270;
  double vw = portrait_rotation ? video_h : video_w;
  double vh = portrait_rotation ? video_w : video_h;
  double fit = std::min(viewport_w / std::max(1.,vw), viewport_h / std::max(1.,vh));
  vw = std::max(1., vw * fit); vh = std::max(1., vh * fit);
  double padding = 16. * dpi / 96;
  auto d = cloud.controls;
  double x = (d.cx() - (viewport_w-vw)/2) / vw;
  double y = (d.y-padding + (d.h+2*padding)*.52 - (viewport_h-vh)/2) / vh;
  CloudUniforms u{x,y,(d.w+2*padding)*.64/vw,(d.h+2*padding)*.60/vh,
                   6.*dpi/96/vw,6.*dpi/96/vh};
  if (rotation==90) { u.x=1-x; u.y=y; }
  if (rotation==180) { u.x=1-x; u.y=1-y; }
  if (rotation==270) { u.x=x; u.y=1-y; }
  // OUTPUT has transposed axes already; only final destination flips remain.
  return u;
}

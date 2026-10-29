// Ambient light, in the spirit of an Ambilight TV: a soft halo just outside the window, each
// stretch lit by the colour of the picture's border next to it. Built to cost almost nothing:
// the halo's shape is computed once per window size; an update only recolours a prepared list
// of pixels through a cached colour ramp, at up to 30 Hz, and moving the window moves it as
// is. A separate click-through layered window kept right below the player in z-order.
#pragma once
#include <windows.h>

#include <cstdint>
#include <vector>

#include "../ui/atmosphere.h"
#include "../ui/gfx.h"

class Ambient {
 public:
  static constexpr int kZones = FrameColors::kZones;
  bool create(HINSTANCE inst);
  void destroy();
  void reset();  // hide immediately between videos, reusing allocated surfaces
  // zones clockwise from the top-left corner (see FrameColors); light 0..1 per zone
  // strength 0..1 scales the whole halo (no repaint: only the strips' opacity changes)
  bool update(HWND owner, const Rgb* zones, const double* light, bool visible, double strength, double dt, bool defer_geometry = false);
  void follow(HWND owner, bool defer_geometry = false);  // move cached pixels during interaction
  bool active() const { return alpha_ > 0.001 || want_; }

 private:
  // four thin strips around the player (top and bottom include the corners): only pixels that
  // can glow are ever stored or sent, never the big transparent middle
  struct Px { uint32_t idx; uint16_t lut; uint8_t a; };
  struct Strip {
    HWND wnd = nullptr;
    HDC mem = nullptr;
    HBITMAP bmp = nullptr;
    uint32_t* bits = nullptr;
    int x = 0, y = 0, w = 0, h = 0;  // relative to the halo's top-left
    std::vector<Px> shape;
  };
  Strip st_[4];
  bool want_ = false, failed_ = false;
  double alpha_ = 0;     // global fade
  double strength_ = 0.15;
  BYTE blend() const;
  int last_a_ = -1;
  double since_paint_ = 1;
  bool colors_dirty_ = true;
  Rgb zone_[kZones]{};
  double light_[kZones]{};
  RECT rect_{};          // the player's visible edge the shape was built for
  int M_ = 0;
  bool built_ = false;
  static constexpr int kLut = 512;
  uint8_t lr_[kLut]{}, lg_[kLut]{}, lb_[kLut]{}, la_[kLut]{};
  std::vector<uint32_t> tint_;
  void build_shape(const RECT& r, int dpi);
  // the strips are cut to the monitor: light that would fall off screen is never made or sent
  void layout_strips(const RECT& r, int M, RECT out[4]) const;
  bool rebuild_pending_ = false;
  double since_build_ = 1;
  int dpi_ = 96;
  void build_lut();
  void paint(HWND owner);
  void place(HWND owner, bool show);
  void free_surfaces();
  void hide();
};

#pragma once
#include "gfx.h"
#include <array>
struct SceneGrid {
  static constexpr int cols=24, rows=14;
  std::array<Rgb,cols*rows> pixels{};
  int width=0,height=0;
  bool valid=false;
};
SceneGrid sample_scene_grid(const uint32_t* pixels,int width,int height);
Rgb scene_background(const SceneGrid& scene,double x,double y,int viewport_w,int viewport_h);
double scene_contrast(Rgb a,Rgb b);
Rgb region_ink(const SceneGrid& scene,const Rect& region,int viewport_w,int viewport_h,Rgb fallback,bool& dark);

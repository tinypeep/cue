#pragma once
#include <algorithm>
#include <cmath>
struct VideoWindowSize { int w, h; };
inline VideoWindowSize video_window_size(double ar, int work_w, int work_h, int dpi, double ui) {
  ar = std::clamp(ar, .1, 10.0);
  double max_w=work_w*.90, max_h=work_h*.84;
  double w=ar<.95 ? max_h*ar : std::min(work_w*.67,1600.*dpi/96);
  double h=w/ar;
  double shrink=std::min({1.,max_w/w,max_h/h}); w*=shrink; h*=shrink;
  double grow=std::max({1.,240.*ui/w,180.*ui/h});
  grow=std::min(grow,std::min(max_w/w,max_h/h)); w*=grow;h*=grow;
  return {std::max(1,(int)std::lround(w)),std::max(1,(int)std::lround(h))};
}

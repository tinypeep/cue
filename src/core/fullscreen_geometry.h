#pragma once
#include <algorithm>
#include <cmath>

namespace fullscreen_motion {
constexpr double lights_down = .055, zoom = .32, lights_up = .09;
// Underdamped response with a restrained overshoot and a smooth finite tail.
// Computed from elapsed time, so the spring is identical at 60/120/144 Hz.
inline double spring_curve(double k) {
  k=std::clamp(k,0.,1.);
  constexpr double w=9.,z=.80;
  double wd=w*std::sqrt(1-z*z);
  double value=1-std::exp(-z*w*k)*(std::cos(wd*k)+z*w/wd*std::sin(wd*k));
  double tail=std::clamp((k-.80)/.20,0.,1.);
  tail=tail*tail*(3-2*tail);
  return value+(1-value)*tail;
}
struct Rect { double left, top, right, bottom; };
inline double width(Rect r) { return r.right-r.left; }
inline double height(Rect r) { return r.bottom-r.top; }
inline Rect fit(Rect box, double ar) {
  if (!(ar>0)) ar=16./9;
  double w=width(box), h=w/ar;
  if(h>height(box)) { h=height(box); w=h*ar; }
  double x=box.left+(width(box)-w)/2, y=box.top+(height(box)-h)/2;
  return {x,y,x+w,y+h};
}
// A source viewport may contain letterbox bars. Scale its video, not its outer
// bounds, to the destination video. This keeps the picture identical when the
// source changes from a fullscreen buffer to a window of a different aspect.
inline Rect map_viewport(Rect source, Rect destination, double video_ar) {
  if (!(video_ar>0)) video_ar=width(source)/std::max(1.,height(source));
  Rect a=fit(source,video_ar), b=fit(destination,video_ar);
  double scale=width(b)/std::max(1.,width(a));
  double x=b.left-(a.left-source.left)*scale, y=b.top-(a.top-source.top)*scale;
  return {x,y,x+width(source)*scale,y+height(source)*scale};
}
}  // namespace fullscreen_motion

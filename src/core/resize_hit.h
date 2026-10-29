#pragma once
#include <algorithm>

// Win32 HT codes, kept portable so parent and video-child policies share tests.
inline int player_resize_hit(int x, int y, int w, int h, int dpi, double ui = 0) {
  if (x < 0 || y < 0 || x >= w || y >= h) return 1; // HTCLIENT
  // Reserve the chrome button strip and the back button before testing edges.
  if (ui > 0 && y >= 2*ui && y < 20*ui &&
      ((x >= w-56*ui && x < w-4*ui) || (x >= 4*ui && x < 22*ui))) return 1;
  int edge = std::max(4, (6 * dpi + 48) / 96);
  int corner = std::max(edge, (14 * dpi + 48) / 96);
  // Window buttons and the back arrow always win inside their visible targets.
  // Their first art row starts at four scaled pixels, outside this narrow top rim.
  bool left=x<edge, right=x>=w-edge, top=y<edge, bottom=y>=h-edge;
  // Wide corner targets join two edges without stealing the centre of the title strip.
  if ((top && x<corner) || (left && y<corner)) return 13;
  if ((top && x>=w-corner) || (right && y<corner)) return 14;
  if ((bottom && x<corner) || (left && y>=h-corner)) return 16;
  if ((bottom && x>=w-corner) || (right && y>=h-corner)) return 17;
  return top?12:bottom?15:left?10:right?11:1;
}

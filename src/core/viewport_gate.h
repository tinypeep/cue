#pragma once

// A fade-in must not expose overlays prepared for a viewport the renderer has
// not adopted yet. The fallback supports engines without dimension observers,
// but still requires the native child window to have reached the target size.
inline bool viewport_ready_for_controls(int width, int height, int observed_w,
                                       int observed_h, bool native_ready, double elapsed,
                                       double minimum_delay=.04) {
  if(width<=0 || height<=0 || !native_ready || elapsed<minimum_delay) return false;
  return (width==observed_w && height==observed_h) || elapsed>1.0;
}

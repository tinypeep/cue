#pragma once
// Only a normal 16:9 home shape can be restored. A landscape film may still
// be ultrawide (or 4:3); its saved viewport must not become the next home.
inline bool restore_saved_window(bool opening_media, int width, int height) {
  if(opening_media || width<=0 || height<=0) return false;
  long long delta=9LL*width-16LL*height;
  return delta>=-16 && delta<=16; // one physical pixel of rounding
}

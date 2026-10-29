// Output sizing for timeline thumbnails. A zero limit preserves colour-sampling quality.
#pragma once
#include <algorithm>
#include <cstdint>
struct PreviewSize { int w, h; };
inline PreviewSize preview_size(int w, int h, int limit) {
  if (w <= 0 || h <= 0) return {0, 0};
  if (limit <= 0 || w <= limit) return {w, h};
  return {limit, std::max(1, (int)((int64_t)h * limit / w))};
}

#pragma once
#include <cstdint>

// A cached ramp replaces four multiplies/divides at every halo pixel on every
// frame. Rebuild only the ring entries whose colour/brightness actually changed.
inline void ambient_ramp(uint8_t r, uint8_t g, uint8_t b, uint8_t light, uint32_t* out) {
  for (unsigned mask = 0; mask < 256; ++mask) {
    unsigned a = mask * light / 255;
    out[mask] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
  }
}

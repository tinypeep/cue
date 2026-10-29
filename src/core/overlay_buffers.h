#pragma once
#include <cstdint>
#include <vector>
#include "frame_cadence.h"

// Pixels leased to the asynchronous renderer. Large allocations are returned
// only after overlay-remove is acknowledged, never while mpv may read them.
struct OverlayBuffers {
  std::vector<uint32_t> buf[2];
  int on_screen = -1, sending = -1;
  bool inflight = false, shown = false, removing = false;
  FrameCadence cadence;

  void added(bool success) {
    inflight = false;
    if (success && sending >= 0) on_screen = sending;
    sending = -1;
  }
  size_t retained_bytes() const { return (buf[0].capacity() + buf[1].capacity()) * sizeof(uint32_t); }
  void removed(bool success) {
    removing = false;
    if (!success) return; // failed removal does not end the renderer's lease
    shown = false;
    on_screen = -1;
    if (!inflight && retained_bytes() >= 1024 * 1024) {
      for (auto& pixels : buf) std::vector<uint32_t>().swap(pixels);
    }
  }
};

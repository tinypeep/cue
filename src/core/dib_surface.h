#pragma once
#include <cstdint>

// Owns a DIB and its memory DC as one resource. Ops is the tiny GDI adapter in
// win/dib_surface.h; tests use the same owner with counted handles/failure injection.
template<class Ops> class OwnedDibSurface {
 public:
  using DC = typename Ops::DC;
  using Bitmap = typename Ops::Bitmap;
  using Object = typename Ops::Object;
  OwnedDibSurface() = default;
  ~OwnedDibSurface() { reset(); }
  OwnedDibSurface(const OwnedDibSurface&) = delete;
  OwnedDibSurface& operator=(const OwnedDibSurface&) = delete;

  bool resize(int width, int height) {
    if (width <= 0 || height <= 0) return false;
    if (dc_ && width == w_ && height == h_) return true;
    DC next_dc = Ops::create_dc();
    if (!next_dc) return false;
    void* next_bits = nullptr;
    Bitmap next_bitmap = Ops::create_bitmap(width, height, &next_bits);
    if (!next_bitmap || !next_bits) {
      Ops::delete_dc(next_dc);
      if (next_bitmap) Ops::delete_bitmap(next_bitmap);
      return false;
    }
    Object previous = Ops::select(next_dc, next_bitmap);
    if (!Ops::valid(previous)) {
      Ops::delete_dc(next_dc);
      Ops::delete_bitmap(next_bitmap);
      return false;
    }
    reset(); // allocate first: an OOM must not discard the current valid surface
    dc_ = next_dc; bitmap_ = next_bitmap; previous_ = previous;
    bits_ = static_cast<uint32_t*>(next_bits); w_ = width; h_ = height;
    return true;
  }
  void reset() {
    if (dc_) {
      if (Ops::valid(previous_)) Ops::select(dc_, previous_);
      // Delete the DC before the bitmap even if restoring the old object failed.
      Ops::delete_dc(dc_);
    }
    if (bitmap_) Ops::delete_bitmap(bitmap_);
    dc_ = {}; bitmap_ = {}; previous_ = {}; bits_ = nullptr; w_ = h_ = 0;
  }
  DC dc() const { return dc_; }
  uint32_t* bits() const { return bits_; }
  int width() const { return w_; }
  int height() const { return h_; }
 private:
  DC dc_{};
  Bitmap bitmap_{};
  Object previous_{};
  uint32_t* bits_ = nullptr;
  int w_ = 0, h_ = 0;
};

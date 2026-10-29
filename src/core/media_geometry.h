#pragma once
#include <cstdint>
#include <utility>

struct VideoShape {
  int64_t width=0,height=0,rotation=0;
  bool valid() const { return width>0 && height>0; }
  double aspect() const {
    if(!valid()) return 0;
    double w=(double)width,h=(double)height;
    if((rotation%180+180)%180==90) std::swap(w,h);
    return w/h;
  }
};

// A single reply owns the complete width/height/rotation tuple for this load.
// Unchanged dimensions need not generate observer events between two files.
class MediaGeometry {
 public:
  void reset() { ++request_; shape_={}; }
  uint64_t request() { return ++request_; }
  uint64_t current_request() const { return request_; }
  bool accept(uint64_t request,VideoShape shape) {
    if(request!=request_ || !shape.valid()) return false;
    shape_=shape; return true;
  }
  VideoShape shape() const { return shape_; }
 private:
  uint64_t request_=1ull<<52;
  VideoShape shape_{};
};

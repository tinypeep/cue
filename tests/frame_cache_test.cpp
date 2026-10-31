#include "../src/core/frame_cache.h"
#include <cassert>
#include <cstdio>
#include <map>

static FrameCache::Image frame(int w,int h,size_t reserve=0) {
  FrameCache::Image image;image.w=w;image.h=h;
  if(reserve)image.px.reserve(reserve);
  image.px.resize((size_t)w*h);return image;
}
int main() {
  FrameCache small(16,3);
  assert(small.put(1,frame(1,1)) && small.put(2,frame(1,1)) && small.put(3,frame(1,1)));
  small.touch(1);assert(small.put(4,frame(1,1)));
  assert(small.count(1) && !small.count(2) && small.size()==3);
  assert(small.put(1,frame(2,1)) && small.retained_bytes()==16);
  assert(!small.put(1,frame(5,1)) && small.find(1)->second.w==2);
  // A vector with spare capacity must be charged for it, not just its visible pixels.
  assert(!small.put(9,frame(1,1,8)) && !small.count(9));
  FrameCache many;
  for(int i=0;i<10000;++i) { assert(many.put(i,frame(1,1)));assert(many.size()<=FrameCache::kMaxEntries); }
  assert(many.size()==256);many.clear();assert(many.empty() && !many.retained_bytes());
  std::map<int,FrameCache::Image> old;
  for(int i=0;i<400;++i)old.emplace(i,frame(320,180));
  size_t old_bytes=0;for(const auto& [k,image]:old)old_bytes+=image.bytes();
  FrameCache bounded;size_t peak=0;
  for(int i=0;i<1000;++i) {
    assert(bounded.put(i,frame(320,180)));
    peak=std::max(peak,bounded.retained_bytes());
    assert(peak<=FrameCache::kMaxBytes && bounded.size()<=FrameCache::kMaxEntries);
  }
  assert(bounded.count(999) && !bounded.count(0));
  bounded.clear();assert(bounded.retained_bytes()==0);
  std::printf("Frame cache: 1000 high-resolution preview inserts bounded to %.2f MiB; 400-image count-only fixture uses %.2f MiB; capacity accounting, LRU, replacements, rejection and clear passed\n",peak/1048576.0,old_bytes/1048576.0);
}

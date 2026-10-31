#include "../src/core/dib_surface.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <set>

struct CountedGdi {
  using DC = int; using Bitmap = int; using Object = int;
  static inline int next = 2, fail = 0, failed_deletes = 0;
  static inline std::map<int,int> selections;
  static inline std::set<int> bitmaps;
  static inline uint32_t pixels[4];
  static DC create_dc() { if(fail==1) return 0; int h=next++; selections[h]=1; return h; }
  static Bitmap create_bitmap(int,int,void** bits) {
    if(fail==2) return 0;
    *bits=fail==3 ? nullptr : pixels;
    int h=next++; bitmaps.insert(h); return h;
  }
  static Object select(DC dc,Object obj) {
    if(fail==4 || (fail==5 && obj==1)) return 0;
    int old=selections.at(dc); selections[dc]=obj; return old;
  }
  static bool valid(Object o) { return o>0; }
  static void delete_dc(DC dc) { assert(selections.erase(dc)==1); }
  static void delete_bitmap(Bitmap b) {
    for(auto [dc,selected]:selections) if(selected==b) { ++failed_deletes; return; }
    assert(bitmaps.erase(b)==1);
  }
};
int main() {
  using Surface=OwnedDibSurface<CountedGdi>;
  {
    Surface surface;
    for(int i=0;i<5000;++i) {
      assert(surface.resize(320+i%200,180+i%123));
      assert(CountedGdi::bitmaps.size()==1 && CountedGdi::selections.size()==1);
      assert(surface.bits());
      if(i%17==0) { surface.reset(); surface.reset(); assert(CountedGdi::bitmaps.empty()); }
    }
    for(int fail=1;fail<=4;++fail) {
      CountedGdi::fail=0; assert(surface.resize(640,360));
      auto* bits=surface.bits(); int w=surface.width();
      CountedGdi::fail=fail;
      assert(!surface.resize(1920,1080));
      assert(surface.bits()==bits && surface.width()==w);
      assert(CountedGdi::bitmaps.size()==1 && CountedGdi::selections.size()==1);
    }
    CountedGdi::fail=5; surface.reset(); // failed deselection still frees the bitmap safely
    assert(CountedGdi::bitmaps.empty() && CountedGdi::selections.empty());
    CountedGdi::fail=0; assert(surface.resize(3840,2160));
  }
  assert(CountedGdi::bitmaps.empty() && CountedGdi::selections.empty() && CountedGdi::failed_deletes==0);
  // Reproduce the previous sequence against the documented GDI deletion contract.
  for(int i=0;i<50;++i) {
    auto dc=CountedGdi::create_dc(); void* bits=nullptr;
    auto bitmap=CountedGdi::create_bitmap(100,100,&bits);
    CountedGdi::select(dc,bitmap); CountedGdi::delete_bitmap(bitmap);
    CountedGdi::delete_dc(dc);
  }
  assert(CountedGdi::bitmaps.size()==50 && CountedGdi::failed_deletes==50);
  auto leaked=CountedGdi::bitmaps;
  for(int b:leaked) CountedGdi::delete_bitmap(b);
  std::puts("DIB ownership: old sequence leaves 50/50 bitmaps unfreed under documented API contract; fixed owner remains at one surface across 5000 resizes, rolls back allocation failures and releases all handles");
}

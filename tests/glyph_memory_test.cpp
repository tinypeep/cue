#include "../src/ui/gfx.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

static std::atomic<long long> live{0};
void* operator new(std::size_t n) {
  if(void* p=std::malloc(n ? n : 1)) { ++live; return p; }
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { if(p) { --live; std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }

int main() {
  has_glyph('A'); has_glyph(0x100000); // initialize static tables before the measurement
  auto before=live.load();
  for(uint32_t cp=0x100001;cp<=0x10FFFD;++cp) assert(!has_glyph(cp));
  auto after=live.load();
  std::printf("Unsupported glyph probes: 65533; retained allocation growth: %lld\n",after-before);
  std::fflush(stdout);
  if(after!=before) return 1;
  // Existing CJK glyphs remain valid and are decoded only once.
  assert(has_glyph(0x4E2D)); auto warm=live.load();
  for(int i=0;i<10000;++i) assert(has_glyph(0x4E2D));
  assert(live==warm);
}

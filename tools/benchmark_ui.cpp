// Local portable benchmark; compile separately from the app (see docs/PERFORMANCE.md).
#include "../src/ui/play.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>

static size_t allocations = 0;
void* operator new(size_t n) {
  ++allocations;
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

int main() {
  PlayView p;
  p.in.dur = 7200;
  p.resize(1280, 720, 2);
  p.reset(0);
  for (int i = 0; i < 60; ++i) p.tick(i / 60.0, 1.0 / 60);
  for (int pass = 0; pass < 3; ++pass) {
    const size_t before = allocations;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 200000; ++i) {
      p.in.time = 100 + i / 120.0;
      p.changed(i / 120.0);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("changed: %.3f ms, %zu heap allocations, time %.3f\n", ms, allocations - before, p.in.time);
  }
}

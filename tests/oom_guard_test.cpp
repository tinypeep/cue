// Out of memory in one message: the message is given up, a note is made (not one per message),
// and the program goes on; other exceptions are not swallowed.
#include "../src/core/oom_guard.h"
#include <cassert>
#include <cstdio>
#include <new>
#include <stdexcept>

// (under AddressSanitizer, tools/sanitize.sh, a huge allocation stops the test with ASan's own
// report instead of throwing: there the failure is thrown by hand; tools/test.sh asks for the memory)
#if defined(__SANITIZE_ADDRESS__)
static long huge() { throw std::bad_alloc(); }
#else
// The pointer escapes to a volatile sink so an optimiser cannot elide the allocation: -O2
// otherwise removes an unused vector's allocation and the test never reaches the allocator.
static char* volatile g_sink;
static long huge() {
  char* p = new char[(size_t)1 << 62];
  g_sink = p;
  delete[] p;
  return -1;
}
#endif

int main() {
  int notes = 0;
  // the usual case: the value comes through, no note
  assert(oom_guard::run([] { return 7; }, [&] { ++notes; }, [] { return -1; }) == 7 && notes == 0);
  // a huge allocation fails: the fallback answers, one note
  long r = oom_guard::run(huge, [&] { ++notes; }, [] { return -1L; });
  assert(r == -1 && notes == 1);
  // a note that itself throws does not take the program down
  r = oom_guard::run([]() -> long { throw std::bad_alloc(); }, [] { throw std::bad_alloc(); }, [] { return -2L; });
  assert(r == -2);
  // any other exception is a bug: it goes through
  bool through = false;
  try { oom_guard::run([]() -> int { throw std::runtime_error("x"); }, [] {}, [] { return 0; }); } catch (const std::runtime_error&) { through = true; }
  assert(through);
  // notes: the first at once, then one every 5 s however many messages fail
  oom_guard::Throttle t;
  int written = 0;
  for (int i = 0; i < 1000; ++i) written += t.due(i * 0.01);
  assert(written == 2 && t.skipped == 998);
  std::puts("oom guard: out of memory in a message gives it up and goes on, one note per 5 s, other exceptions pass passed");
}

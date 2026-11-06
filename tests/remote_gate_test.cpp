// g++ -std=c++20 tests/remote_gate_test.cpp -o remote_gate_test
#include "../src/win/remote_gate.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace std::chrono;

static bool enter_gate(Gate& g, std::shared_ptr<std::atomic<uint64_t>>& gen, uint64_t epoch, bool* out) {
  *out = g.enter(3, gen, epoch);
  return *out;
}

int main() {
  auto gen = std::make_shared<std::atomic<uint64_t>>(0);
  // Basic accounting: three slots, leave releases, leave never underflows.
  {
    Gate g;
    assert(g.enter(3, gen, 0) && g.enter(3, gen, 0) && g.enter(3, gen, 0));
    g.leave(); g.leave(); g.leave();
    g.leave();  // extra leave must not drive busy negative
    assert(g.enter(3, gen, 0));
    g.leave();
  }
  // Generation change cancels a waiter promptly (not at the 8s timeout).
  {
    Gate g;
    assert(g.enter(3, gen, 0) && g.enter(3, gen, 0) && g.enter(3, gen, 0));
    bool out = true;
    auto t0 = steady_clock::now();
    std::thread waiter([&] { enter_gate(g, gen, 0, &out); });
    std::this_thread::sleep_for(milliseconds(100));
    ++*gen;  // epoch 0 -> 1
    g.wake_all();
    waiter.join();
    auto dt = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    assert(!out);
    assert(dt < 4000);  // woken by generation change, not the deadline
    g.leave(); g.leave(); g.leave();
    *gen = 0;  // reset for the next scenario (fresh epoch values below)
  }
  // A wake_all() that ran before enter() must not be lost: the predicate is
  // checked before the first wait, so this returns at once.
  {
    Gate g;
    assert(g.enter(3, gen, 0) && g.enter(3, gen, 0) && g.enter(3, gen, 0));
    ++*gen;
    g.wake_all();  // no waiters yet
    auto t0 = steady_clock::now();
    bool out = true;
    enter_gate(g, gen, 0, &out);  // epoch 0 is stale now
    auto dt = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    assert(!out);
    assert(dt < 4000);
    g.leave(); g.leave(); g.leave();
    *gen = 0;
  }
  // leave() hands a freed slot to a queued waiter.
  {
    Gate g;
    assert(g.enter(3, gen, 0) && g.enter(3, gen, 0) && g.enter(3, gen, 0));
    bool out = false;
    std::thread waiter([&] { enter_gate(g, gen, 0, &out); });
    std::this_thread::sleep_for(milliseconds(100));
    g.leave();
    waiter.join();
    assert(out);
    g.leave(); g.leave(); g.leave();
  }
  // A waiter with no wakeup and no generation change times out (8s) instead
  // of blocking forever, so a dead share cannot wedge the gate.
  {
    Gate g;
    assert(g.enter(3, gen, 0) && g.enter(3, gen, 0) && g.enter(3, gen, 0));
    bool out = true;
    auto t0 = steady_clock::now();
    enter_gate(g, gen, 0, &out);
    auto dt = duration_cast<milliseconds>(steady_clock::now() - t0).count();
    assert(!out);
    assert(dt >= 5000 && dt < 12000);
    g.leave(); g.leave(); g.leave();
  }
  std::puts("Remote gate: slot accounting, generation cancel, lost-wakeup guard, handoff and 8s timeout passed");
}

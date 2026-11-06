#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

// Bounds how many thumbnail/cover renders run at once. Waiters never queue
// forever: a generation change (stop() / share or token change) cancels
// immediately, and an 8s timeout matches the request socket timeout so one
// dead share cannot hold every other worker.
struct Gate {
  std::mutex mu;
  std::condition_variable cv;
  int busy = 0;
  bool enter(int limit, const std::shared_ptr<std::atomic<uint64_t>>& generation, uint64_t epoch) {
    std::unique_lock<std::mutex> l(mu);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    // Predicate form: the generation check is re-evaluated before the first
    // wait, so a wake_all() that ran before this call cannot be lost and
    // leave the waiter sleeping until the deadline.
    bool ready = cv.wait_until(l, deadline, [&] { return busy < limit || *generation != epoch; });
    if (!ready && busy >= limit) return false;  // timed out still queued
    if (*generation != epoch) return false;
    if (busy >= limit) return false;
    ++busy;
    return true;
  }
  void leave() { { std::lock_guard<std::mutex> l(mu); if (busy > 0) --busy; } cv.notify_one(); }
  // Take mu before notifying: a waiter that has checked its predicate but not
  // yet blocked must not miss the wakeup (notify_all without the lock can be
  // lost between the predicate check and entering the wait).
  void wake_all() { std::lock_guard<std::mutex> l(mu); cv.notify_all(); }
};

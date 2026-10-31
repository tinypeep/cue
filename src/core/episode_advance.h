#pragma once
#include <string>
#include <utility>

// Own the destination across EOF/idle/timer events. Duplicate EOF notifications
// must not enqueue the same transition twice or borrow mutable discovery state.
class EpisodeAdvance {
 public:
  bool arm(const std::string& path, bool allowed) {
    if (!allowed || path.empty()) return false;
    if (target_.empty()) target_ = path;
    return true;
  }
  bool return_home(bool idle, bool loaded) const { return idle && !loaded && target_.empty(); }
  bool pending() const { return !target_.empty(); }
  std::string take() { return std::exchange(target_, {}); }
  void clear() { target_.clear(); }
 private:
  std::string target_;
};

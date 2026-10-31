#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

// Timeline pictures are UI assets, not decoder/cache packets. Account actual vector
// capacity, not just entry count, so large UI scales and portrait media stay bounded.
class FrameCache {
 public:
  static constexpr size_t kMaxBytes = 12u * 1024 * 1024;
  static constexpr size_t kMaxEntries = 256;
  struct Image {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    uint64_t used = 0;
    size_t bytes() const { return px.capacity() * sizeof(uint32_t); }
  };
  using Map = std::map<int, Image>;
  explicit FrameCache(size_t bytes = kMaxBytes, size_t entries = kMaxEntries) : max_bytes_(bytes), max_entries_(entries) {}
  bool put(int key, Image image) {
    const size_t cost = image.bytes();
    if (!max_entries_ || image.w <= 0 || image.h <= 0 || image.px.size() < (size_t)image.w * image.h || cost > max_bytes_) return false;
    auto old = items_.find(key);
    if (old != items_.end()) { bytes_ -= old->second.bytes(); items_.erase(old); }
    while (!items_.empty() && (items_.size() >= max_entries_ || bytes_ > max_bytes_ - cost)) {
      auto victim = std::min_element(items_.begin(), items_.end(), [](const auto& a,const auto& b) { return a.second.used < b.second.used; });
      bytes_ -= victim->second.bytes(); items_.erase(victim);
    }
    image.used = ++clock_;
    items_.emplace(key, std::move(image)); bytes_ += cost;
    return true;
  }
  void touch(int key) { auto at = items_.find(key); if (at != items_.end()) at->second.used = ++clock_; }
  void clear() { items_.clear(); bytes_ = 0; clock_ = 0; }
  size_t retained_bytes() const { return bytes_; }
  bool empty() const { return items_.empty(); }
  size_t size() const { return items_.size(); }
  size_t count(int key) const { return items_.count(key); }
  auto begin() { return items_.begin(); }
  auto end() { return items_.end(); }
  auto find(int key) { return items_.find(key); }
  auto lower_bound(int key) { return items_.lower_bound(key); }
 private:
  Map items_;
  size_t bytes_ = 0, max_bytes_, max_entries_;
  uint64_t clock_ = 0;
};

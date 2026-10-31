#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

// The UI never writes a pipe. A slow decoder gets a bounded mailbox instead.
class PreviewCommands {
 public:
  static constexpr size_t kMaxLine = 16 * 1024, kMaxBytes = 64 * 1024, kMaxItems = 16;
  bool push(std::string line, bool replace = false) {
    std::lock_guard<std::mutex> lock(mu_);
    if (stopped_ || line.empty() || line.size() > kMaxLine ||
        line.find_first_of("\r\n") != std::string::npos || line.find('\0') != std::string::npos) return false;
    if (replace) { queue_.clear(); bytes_ = 0; }
    if (queue_.size() >= kMaxItems || bytes_ + line.size() > kMaxBytes) return false;
    bytes_ += line.size();
    queue_.push_back(std::move(line));
    cv_.notify_one();
    return true;
  }
  bool take(std::string& line) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&] { return stopped_ || !queue_.empty(); });
    if (stopped_) return false;
    line = std::move(queue_.front());
    queue_.pop_front();
    bytes_ -= line.size();
    return true;
  }
  void stop() {
    std::lock_guard<std::mutex> lock(mu_);
    stopped_ = true;
    queue_.clear(); bytes_ = 0;
    cv_.notify_all();
  }
 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  size_t bytes_ = 0;
  bool stopped_ = false;
};

#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Bounded UTF-8 filename list, shared by the launcher and the existing window.
// A final empty entry terminates the list; two zero bytes mean activation only.
namespace launch_request {
constexpr size_t kMaxBytes = 256 * 1024;

inline std::string encode(const std::vector<std::string>& files) {
  std::string payload;
  for (const auto& file : files) {
    if (file.empty() || file.find('\0') != std::string::npos || file.size() >= kMaxBytes ||
        payload.size() + file.size() + 2 > kMaxBytes) return {};
    payload.append(file);
    payload.push_back('\0');
  }
  payload.push_back('\0');
  if (payload.size() == 1) payload.push_back('\0');
  return payload;
}

inline bool decode(const void* data, size_t bytes, std::vector<std::string>& files) {
  files.clear();
  if (!data || bytes < 2 || bytes > kMaxBytes) return false;
  const auto* chars = static_cast<const char*>(data);
  if (chars[bytes - 1] || chars[bytes - 2]) return false;
  const std::string payload(chars, bytes);
  std::vector<std::string> decoded;
  size_t start = 0;
  while (start < bytes - 1) {
    size_t end = payload.find('\0', start);
    if (end == start) return decoded.empty() && bytes == 2;
    decoded.push_back(payload.substr(start, end - start));
    start = end + 1;
  }
  files = std::move(decoded);
  return true;
}
}  // namespace launch_request

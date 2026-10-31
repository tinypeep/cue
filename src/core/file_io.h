#pragma once
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>

// Compare with bounded scratch storage. Never read an arbitrarily large existing
// file just to decide whether a small generated file needs replacing.
inline bool file_contents_equal(const std::filesystem::path& path, std::string_view desired) {
  std::ifstream current(path,std::ios::binary|std::ios::ate);
  if (!current || current.tellg()!=std::streampos(desired.size())) return false;
  current.seekg(0);
  std::array<char,4096> buffer{};
  for (size_t at=0;at<desired.size();) {
    size_t count=std::min(buffer.size(),desired.size()-at);
    if (!current.read(buffer.data(),(std::streamsize)count) || std::string_view(buffer.data(),count)!=desired.substr(at,count)) return false;
    at+=count;
  }
  return true;
}

inline constexpr size_t kMaxStateBytes = 4 * 1024 * 1024;
inline bool read_state_file(const std::filesystem::path& path, std::string& out) {
  out.clear();
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f || f.tellg() < 0 || f.tellg() > std::streampos(kMaxStateBytes)) return false;
  f.seekg(0);
  std::array<char,4096> buffer{};
  while (f) {
    f.read(buffer.data(), buffer.size());
    size_t count = static_cast<size_t>(f.gcount());
    if (count > kMaxStateBytes - out.size()) { out.clear(); return false; }
    out.append(buffer.data(), count);
  }
  return f.eof() && !f.bad();
}

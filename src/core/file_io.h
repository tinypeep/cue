#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
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

// Durable writes.
//
// A file that is renamed into place while its bytes are still in the operating
// system's page cache can be lost in a power cut: the name is on the disk, the
// content is not. The store writes its state to a temporary file and swaps it
// over the good one, so the temporary file has to reach the storage device
// before the swap, and the directory entry has to reach it after.

// Pushes the bytes of an existing file to the storage device. Returns false
// when the file cannot be opened or the platform refuses the flush: the caller
// must then treat the file as not durable and must not swap it into place.
bool flush_file_to_disk(const std::filesystem::path& path);

// Encrypts a credential for the state file. On Windows it is DPAPI, so only
// this user on this machine can read it back; elsewhere, base64, so the state
// file still does not hold the credential in the clear. The memory copy is
// always the plain credential, and the server never sees the encrypted form.
std::string protect_blob(const std::string& plain);
std::string unprotect_blob(const std::string& blob);

// Writes a file that must not exist yet. The file is created, never opened: a
// name that someone else put there (a link to another file) is not written to
// and not followed. Returns false when the file could not be created or
// written.
bool write_new_file(const std::filesystem::path& path, const std::string& content);

// Best effort: makes a rename inside the directory durable. The file is in
// place either way; only the directory entry can still be lost, so a failure
// here is not an error for the caller. Returns whether the platform accepted
// the request (Windows does not flush directory handles in general).
bool flush_directory(const std::filesystem::path& dir);

// How many durable flushes have been performed. The store must flush the
// temporary file before every swap; a test watches these counters to prove it.
std::uint64_t durable_file_flushes();
std::uint64_t durable_directory_flushes();
void reset_durable_flushes();

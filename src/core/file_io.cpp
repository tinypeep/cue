#include "file_io.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <atomic>

namespace {
std::atomic<std::uint64_t> g_file_flushes{0};
std::atomic<std::uint64_t> g_directory_flushes{0};
}  // namespace

bool flush_file_to_disk(const std::filesystem::path& path) {
#ifdef _WIN32
  HANDLE file = CreateFileW(path.native().c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const BOOL ok = FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok) return false;
#else
  // O_RDWR: fsync on a read-only descriptor is EBADF on some platforms.
  const int fd = ::open(path.native().c_str(), O_RDWR);
  if (fd < 0) return false;
  const int rc = ::fsync(fd);
  ::close(fd);
  if (rc != 0) return false;
#endif
  g_file_flushes.fetch_add(1, std::memory_order_relaxed);
  return true;
}

namespace {
std::string base64_encode(const std::string& bytes) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string r;
  size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    uint32_t v = (uint32_t)(unsigned char)bytes[i] << 16 | (uint32_t)(unsigned char)bytes[i + 1] << 8 |
                 (unsigned char)bytes[i + 2];
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += t[v & 63];
  }
  if (i + 1 == bytes.size()) {
    uint32_t v = (uint32_t)(unsigned char)bytes[i] << 16;
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += "==";
  } else if (i + 2 == bytes.size()) {
    uint32_t v = (uint32_t)(unsigned char)bytes[i] << 16 | (uint32_t)(unsigned char)bytes[i + 1] << 8;
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += '=';
  }
  return r;
}

// Its inverse, or "" when the text is not valid base64.
std::string base64_decode(const std::string& text) {
  int d[256];
  for (int i = 0; i < 256; ++i) d[i] = -1;
  for (int i = 0; i < 64; ++i) {
    const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    d[(unsigned char)t[i]] = i;
  }
  std::string out;
  int val = 0, bits = -8;
  for (char c : text) {
    if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
    const int v = d[(unsigned char)c];
    if (v < 0) return {};
    val = val << 6 | v;
    bits += 6;
    if (bits >= 0) { out += (char)((val >> bits) & 0xFF); bits -= 8; }
  }
  return out;
}
}

std::string protect_blob(const std::string& plain) {
#ifdef _WIN32
  // DPAPI, with no interaction: only this user on this machine can read it
  // back. The description names what the blob is, for a debugger.
  DATA_BLOB in{};
  in.pbData = (BYTE*)plain.data();
  in.cbData = (DWORD)plain.size();
  DATA_BLOB out{};
  if (CryptProtectData(&in, L"Cue remote credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) != 1)
    return {};  // not encryptable: the file gets nothing
  std::string encoded = base64_encode(std::string((char*)out.pbData, out.cbData));
  LocalFree(out.pbData);
  return encoded;
#else
  // no DPAPI here: base64, so the state file still does not hold the
  // credential in the clear
  return base64_encode(plain);
#endif
}

std::string unprotect_blob(const std::string& blob) {
  const std::string bytes = base64_decode(blob);
  if (bytes.empty()) return {};
#ifdef _WIN32
  DATA_BLOB in{};
  in.pbData = (BYTE*)bytes.data();
  in.cbData = (DWORD)bytes.size();
  DATA_BLOB out{};
  if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) != 1)
    return {};
  std::string plain((char*)out.pbData, out.cbData);
  LocalFree(out.pbData);
  return plain;
#else
  return bytes;
#endif
}

bool write_new_file(const std::filesystem::path& path, const std::string& content) {
#ifdef _WIN32
  // CREATE_NEW: the file must not exist. A link that is already there is not
  // opened, so nothing is written through it.
  HANDLE file = CreateFileW(path.native().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  size_t written = 0;
  while (ok && written < content.size()) {
    DWORD chunk = 0;
    const size_t left = content.size() - written;
    chunk = (DWORD)(left > 0x10000 ? 0x10000 : left);
    if (!WriteFile(file, content.data() + written, chunk, &chunk, nullptr) || chunk == 0) { ok = false; break; }
    written += chunk;
  }
  CloseHandle(file);
  return ok && written == content.size();
#else
  // O_EXCL: the file must not exist either.
  const int fd = ::open(path.native().c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return false;
  bool ok = true;
  size_t written = 0;
  while (ok && written < content.size()) {
    const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
    if (n <= 0) { ok = false; break; }
    written += (size_t)n;
  }
  ::close(fd);
  return ok && written == content.size();
#endif
}

bool flush_directory(const std::filesystem::path& dir) {
#ifdef _WIN32
  // Windows has no directory handle that FlushFileBuffers accepts in general
  // (NTFS ignores it); the rename is still ordered by the file system. Count
  // the attempt so a test can see that the store tried.
  if (dir.empty()) return false;
  g_directory_flushes.fetch_add(1, std::memory_order_relaxed);
  HANDLE handle = CreateFileW(dir.native().c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  const BOOL ok = FlushFileBuffers(handle);
  CloseHandle(handle);
  return ok != FALSE;
#else
  std::filesystem::path target = dir.empty() ? std::filesystem::path(".") : dir;
  g_directory_flushes.fetch_add(1, std::memory_order_relaxed);
  const int fd = ::open(target.native().c_str(), O_RDONLY);
  if (fd < 0) return false;
  const int rc = ::fsync(fd);
  ::close(fd);
  return rc == 0;
#endif
}

std::uint64_t durable_file_flushes() { return g_file_flushes.load(std::memory_order_relaxed); }
std::uint64_t durable_directory_flushes() { return g_directory_flushes.load(std::memory_order_relaxed); }
void reset_durable_flushes() {
  g_file_flushes.store(0, std::memory_order_relaxed);
  g_directory_flushes.store(0, std::memory_order_relaxed);
}

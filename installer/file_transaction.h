#pragma once
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <utility>
#include <chrono>
#include <thread>
#include <functional>

namespace detail {
inline bool rename_retryable(const std::error_code& ec) {
  if (!ec) return false;
#ifdef _WIN32
  switch (ec.value()) {  // Win32: access denied, sharing violation, lock violation
    case 5:
    case 32:
    case 33:
      return true;
    default: break;
  }
#endif
  return ec == std::errc::permission_denied || ec == std::errc::resource_unavailable_try_again ||
         ec == std::errc::device_or_resource_busy;
}
inline bool rename_with_retry(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error) {
  for (int attempt = 0; ; ++attempt) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) { error.clear(); return true; }
    if (attempt >= 39 || !rename_retryable(ec)) { error = ec; return false; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
}  // namespace detail

// Staged files and backups live on the destination volume. Existing files are never opened
// for writing: a failed commit restores them by rename. Keep backups if recovery itself fails.
class FileTransaction {
 public:
  FileTransaction(std::filesystem::path root, std::filesystem::path work)
      : root_(std::move(root)), work_(std::move(work)) {}
  ~FileTransaction() {
    try {
      std::error_code ec;
      if (!committed_ && !rollback(ec)) return;
      if (remove_tree) remove_tree(work_);
      else std::filesystem::remove_all(work_, ec);
    } catch (...) { /* Preserve recovery files if cleanup cannot finish. */ }
  }
  // How the staging folder is removed at the end. The installer sets one that never follows a
  // link (a junction or symbolic link inside is removed as a link): it may run as administrator.
  std::function<void(const std::filesystem::path&)> remove_tree;
  FileTransaction(const FileTransaction&) = delete;
  FileTransaction& operator=(const FileTransaction&) = delete;
  std::filesystem::path staged(const std::filesystem::path& rel) const { return work_ / "new" / rel; }
  void add(const std::filesystem::path& rel, bool remove = false) { entries_.push_back({rel, remove}); }
  const std::filesystem::path& recovery_path() const { return work_; }
  bool recovered() const {
    for (const auto& e : entries_) if (e.backed_up || e.installed) return false;
    return true;
  }
  bool commit(std::error_code& error) {
    // Pre-validate every destination up front (and every staged source) before any
    // rename, so a bad entry (a directory or reparse point where a file should go)
    // fails with nothing changed instead of leaving a mixed new/old install.
    for (auto& e : entries_) {
      auto dst = root_ / e.rel;
      bool dst_exists = std::filesystem::exists(dst, error);
      if (error) return fail(error);
      if (dst_exists && !std::filesystem::is_regular_file(dst, error)) {
        if (!error) error = std::make_error_code(std::errc::invalid_argument);
        return fail(error);
      }
      if (!e.remove) {
        auto src = staged(e.rel);
        if (!std::filesystem::is_regular_file(src, error)) {
          if (!error) error = std::make_error_code(std::errc::no_such_file_or_directory);
          return fail(error);
        }
      }
    }
    for (auto& e : entries_) {
      auto dst = root_ / e.rel, backup = work_ / "old" / e.rel;
      bool exists = std::filesystem::exists(dst, error);
      if (error) return fail(error);
      if (exists) {
        if (!std::filesystem::is_regular_file(dst, error)) {
          if (!error) error = std::make_error_code(std::errc::invalid_argument);
          return fail(error);
        }
        std::filesystem::create_directories(backup.parent_path(), error);
        if (error) return fail(error);
        if (!detail::rename_with_retry(dst, backup, error)) return fail(error);
        e.backed_up = true;
      }
      if (!e.remove) {
        std::filesystem::create_directories(dst.parent_path(), error);
        if (error) return fail(error);
        if (!detail::rename_with_retry(staged(e.rel), dst, error)) return fail(error);
        e.installed = true;
      }
    }
    committed_ = true;
    return true;
  }
 private:
  struct Entry { std::filesystem::path rel; bool remove = false, backed_up = false, installed = false; };
  bool fail(std::error_code& error) {
    std::error_code recovery;
    if (!rollback(recovery)) error = recovery;
    return false;
  }
  bool rollback(std::error_code& error) {
    bool ok = true;
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
      std::error_code ec;
      auto dst = root_ / it->rel;
      if (it->installed) {
        std::filesystem::remove(dst, ec);
        if (ec) { error = ec; ok = false; continue; }
        it->installed = false;
      }
      if (it->backed_up) {
        std::error_code re;
        if (!detail::rename_with_retry(work_ / "old" / it->rel, dst, re)) { error = re; ok = false; continue; }
        it->backed_up = false;
      }
    }
    return ok;
  }
  std::filesystem::path root_, work_;
  std::vector<Entry> entries_;
  bool committed_ = false;
};

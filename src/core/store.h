// SettingsStore: the one place where everything the person decided is kept.
// Local only (a JSON file in AppData), no account, no cloud. Three parts:
//   prefs    global preferences (subtitle look, language, interface size, ambient light...)
//   session  how the app was left (window, dock, eggs seen)
//   media    per file: position, duration, last opened, tracks, delay, its own style
// Versioned schema, defaults for anything missing, migration from older files, a backup
// copy, and a corrupt file never takes the app down (it is set aside and defaults are used).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "model.h"

struct SettingsStore {
  static constexpr int kSchema = 5;

  // ── prefs (global) ─────────────────────────────────────────────
  SubStyle default_style;
  double volume = 100;
  bool muted = false;
  int ui_scale = 2;           // -1 small, 2 medium (default), 0 large, 1 larger
  bool controls_free = false;  // optional controls without a card
  bool loop = false;           // repeat the file (the dock's repeat button)
  bool ambient = true;      // ambient light behind the window
  double ambient_strength = 0.15;  // 0.05..1: starts soft, the person turns it up
  bool resume = true;        // continue where a file was left
  std::string slang, alang;  // last subtitle / audio language picked by hand
  bool onboarded = false;     // the first-run welcome was seen (or skipped)
  // interface language: "auto" follows Windows' display languages at every start; a tag
  // ("pt-BR", "en", "es", "fr", "zh-Hans") is the person's own choice and stays until changed.
  // Independent of slang/alang (subtitle and audio tracks).
  std::string language = "auto";
  // updates (GitHub releases): when the last check ran (unix seconds), what it found,
  // and a version the person chose to skip
  int64_t update_checked = 0;
  std::string update_available, update_skipped;

  // ── session ────────────────────────────────────────────────────
  DockPlace dock;
  // last normal window rect (physical px at `dpi`), and whether it was maximized
  struct WinPlace { bool has = false; int x = 0, y = 0, w = 0, h = 0, dpi = 96; bool max = false; } win;
  int eggs_seen = 0;  // bitmask of horror variants already seen

  // ── media ──────────────────────────────────────────────────────
  std::vector<RecentItem> recents;  // newest first
  static constexpr size_t kMaxRecents = 24;  // kept on disk; home shows 6

  // what load() found: "ok", "missing", "migrated", "recovered" (from backup), "reset" (corrupt)
  std::string load_status;

  bool load(const std::filesystem::path& path);
  bool save(const std::filesystem::path& path) const;

  RecentItem* find(const std::string& media_path);
  RecentItem& touch(const std::string& media_path, double dur, int64_t now);
  void set_position(const std::string& media_path, double pos, double dur);
  void clear_recents() { recents.clear(); }
  // Where to resume, or -1. Skips the very start and the very end.
  double resume_point(const std::string& media_path) const;
  SubStyle style_for(const std::string& media_path) const;
  void set_style_for(const std::string& media_path, const SubStyle& st);

 private:
  bool parse(const std::string& text);
};
using Store = SettingsStore;

std::string normalize_path(const std::string& p);

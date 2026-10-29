// Updates from GitHub Releases: no server of our own.
// The check reads https://api.github.com/repos/<owner>/<repo>/releases/latest at most every
// 15 days (or when asked), compares versions and picks the installer asset. Portable: the
// network and file parts live in src/win/updater.cpp.
#pragma once
#include <cstdint>
#include <string>

struct ReleaseInfo {
  std::string version;      // "1.2.0" (tag without the leading v)
  std::string tag;          // "v1.2.0"
  std::string notes;        // release body, first lines
  std::string asset_name;   // "Cue-Setup-1.2.0.exe"
  std::string asset_url;    // browser_download_url
  std::string sha256;       // from the asset's "digest" (GitHub adds it to every upload), lowercase hex
  int64_t size = 0;
};

constexpr int64_t kUpdateEvery = 15 * 24 * 3600;  // seconds between automatic checks

// -1, 0, 1. Numeric parts compare as numbers ("1.10" > "1.9"); a leading v is ignored;
// a pre-release suffix ("1.2.0-beta") sorts before the release itself.
int compare_versions(const std::string& a, const std::string& b);
bool update_due(int64_t now, int64_t last_checked);
// Reads a /releases/latest response. False when it is not a usable release
// (draft, prerelease, no installer asset).
bool parse_release(const std::string& json, ReleaseInfo& out);
// "1.2" for "1.2.0", "1.2.1" stays
std::string short_version(const std::string& v);

#include "release.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "json.h"

namespace {
std::string strip_v(std::string v) {
  while (!v.empty() && std::isspace((unsigned char)v.front())) v.erase(v.begin());
  if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(v.begin());
  return v;
}
void split(const std::string& v, std::vector<long>& nums, std::string& pre) {
  std::string core = v;
  size_t dash = v.find_first_of("-+ ");
  if (dash != std::string::npos) { core = v.substr(0, dash); pre = v.substr(dash + 1); }
  size_t i = 0;
  while (i <= core.size()) {
    size_t j = core.find('.', i);
    if (j == std::string::npos) j = core.size();
    std::string part = core.substr(i, j - i);
    long n = 0;
    for (char ch : part) if (std::isdigit((unsigned char)ch)) n = n * 10 + (ch - '0'); else break;
    nums.push_back(n);
    i = j + 1;
  }
  while (nums.size() > 1 && nums.back() == 0) nums.pop_back();
}
std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
bool ends_with(const std::string& s, const std::string& e) {
  return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}
}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
  std::vector<long> na, nb;
  std::string pa, pb;
  split(strip_v(a), na, pa);
  split(strip_v(b), nb, pb);
  size_t n = std::max(na.size(), nb.size());
  for (size_t i = 0; i < n; ++i) {
    long x = i < na.size() ? na[i] : 0, y = i < nb.size() ? nb[i] : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  if (pa.empty() != pb.empty()) return pa.empty() ? 1 : -1;  // 1.2.0 > 1.2.0-beta
  if (pa != pb) return pa < pb ? -1 : 1;
  return 0;
}

bool update_due(int64_t now, int64_t last_checked) {
  if (last_checked <= 0 || last_checked > now + 24 * 3600) return true;  // never, or a clock that went back
  return now - last_checked >= kUpdateEvery;
}

std::string short_version(const std::string& v) {
  std::string s = strip_v(v);
  if (ends_with(s, ".0") && std::count(s.begin(), s.end(), '.') >= 2) s.resize(s.size() - 2);
  return s;
}

bool parse_release(const std::string& json, ReleaseInfo& out) {
  JVal j;
  if (!json_parse(json, j) || j.t != JVal::Obj) return false;
  if (j["draft"].boolean(false) || j["prerelease"].boolean(false)) return false;
  out = ReleaseInfo{};
  out.tag = j["tag_name"].str();
  out.version = strip_v(out.tag);
  if (out.version.empty() || !std::isdigit((unsigned char)out.version[0])) return false;
  std::string body = j["body"].str();
  // a few lines are enough for a toast or a tooltip
  size_t cut = 0;
  for (int lines = 0; cut < body.size() && lines < 6; ++lines) {
    size_t nl = body.find('\n', cut);
    cut = nl == std::string::npos ? body.size() : nl + 1;
  }
  out.notes = body.substr(0, cut);
  // the installer: "Cue-Setup-1.2.0.exe" (any name with "setup" ending in .exe is accepted)
  const JVal* best = nullptr;
  for (auto& a : j["assets"].a) {
    std::string name = lower(a["name"].str());
    if (!ends_with(name, ".exe") || name.find("setup") == std::string::npos) continue;
    if (!best || name.find("cue") != std::string::npos) best = &a;
  }
  if (!best) return false;
  out.asset_name = (*best)["name"].str();
  out.asset_url = (*best)["browser_download_url"].str();
  out.size = (int64_t)(*best)["size"].num(0);
  std::string digest = lower((*best)["digest"].str());  // "sha256:…"
  if (digest.rfind("sha256:", 0) == 0) {
    std::string hex = digest.substr(7);
    bool ok = hex.size() == 64 && std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit((unsigned char)c); });
    if (ok) out.sha256 = hex;
  }
  if (out.asset_url.rfind("https://", 0) != 0) return false;
  return true;
}

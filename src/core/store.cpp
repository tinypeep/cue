#include <chrono>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
#include "store.h"
#include "remote_proto.h"
#include "file_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "json.h"
#include "i18n.h"

// A shared folder read from the state file is not trusted: it must be a plain
// drive path ("C:\Videos"), and not the root of a drive, which would share
// everything on it. The name of a folder the person chose is not ours to
// widen.
static bool shared_folder_ok(const std::string& path) {
  if (path.size() < 4 || path.size() > 400) return false;
  if (path[0] < 'A' || path[0] > 'Z' || path[1] != ':') return false;  // a drive, not a UNC or a device
  if (path.find(':') != 1) return false;                              // no second colon: no stream
  if (path.find("..") != std::string::npos) return false;            // no way up
  if (path.find('\\') == std::string::npos) return false;             // a real folder, not a drive root
  if (path.back() == '\\') return false;                               // a drive root shares everything
  for (char c : path) if ((unsigned char)c < 32) return false;
  return true;
}

std::string normalize_path(const std::string& p) {
  std::string o = p;
#ifdef _WIN32
  for (auto& c : o) {
    if (c == '/') c = '\\';
    else c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
  }
#endif
  return o;
}

static SubStyle style_from(const JVal& j, const SubStyle& def) {
  SubStyle s = def;
  if (j.t != JVal::Obj) return s;
  s.font = std::clamp(j["font"].integer(def.font), 0, 2);
  s.size = std::clamp(j["size"].integer(def.size), 24, 80);
  s.text_op = std::clamp(j["text"].integer(def.text_op), 10, 100);
  s.bg_op = std::clamp(j["bg"].integer(def.bg_op), 0, 100);
  s.pos = std::clamp(j["pos"].num(def.pos), kSubPosMin, kSubPosMax);
  s.ass_mode = std::clamp(j["ass"].integer(def.ass_mode), 0, 1);
  s.outline = std::clamp(j["outline"].integer(def.outline), 0, 6);
  s.line_gap = std::clamp(j["lgap"].integer(def.line_gap), kLineGapMin, kLineGapMax);
  s.tracking = std::clamp(j["track"].integer(def.tracking), -3, 10);
  return s;
}

static std::string style_json(const SubStyle& s) {
  char b[220];
  std::snprintf(b, sizeof b, "{\"font\":%d,\"size\":%d,\"text\":%d,\"bg\":%d,\"pos\":%.1f,\"ass\":%d,\"outline\":%d,\"lgap\":%d,\"track\":%d}",
                s.font, s.size, s.text_op, s.bg_op, s.pos, s.ass_mode, s.outline, s.line_gap, s.tracking);
  return b;
}

static void read_media(const JVal& r, RecentItem& it, const SubStyle& def) {
  it.path = r["path"].str();
  it.pos = std::clamp(r["pos"].num(0), 0.0, 1e9);
  it.dur = std::clamp(r["dur"].num(0), 0.0, 1e9);
  it.last = r["last"].integer<int64_t>(0);
  if (r["style"].t == JVal::Obj) { it.has_style = true; it.style = style_from(r["style"], def); }
  it.sid = r["sid"].integer<int64_t>(-1);
  it.aid = r["aid"].integer<int64_t>(-1);
  it.sid_lang = r["slang"].str();
  it.aid_lang = r["alang"].str();
  it.sub_visible = r["subvis"].boolean(true);
  it.sub_delay = std::clamp(r["delay"].num(0), -600.0, 600.0);
}

// Reads any schema this app has written. Anything missing keeps its default.
bool SettingsStore::parse(const std::string& text) {
  JVal j;
  if (!json_parse(text, j) || j.t != JVal::Obj) return false;
  int schema = j["schema"].integer(1);
  // v1 kept everything at the top level; v2 groups it. Both read through the same names.
  const JVal& pf = schema >= 2 ? j["prefs"] : j;
  const JVal& ss = schema >= 2 ? j["session"] : j;
  default_style = style_from(pf["style"], SubStyle{});
  volume = std::clamp(pf["volume"].num(100), 0.0, kVolMax);
  muted = pf["muted"].boolean(false);
  ui_scale = std::clamp(pf["ui_scale"].integer(2), -1, 3);
  if (ui_scale == 1) ui_scale = 3;  // the old 1.5x "larger" (and 1.1's "huge") became the 1.25x larger
  if (schema < 4 && ui_scale == 0) ui_scale = -1; // migrate the former default once
  if (schema < 5 && ui_scale == -1) ui_scale = 2; // 1.0: médio became the default, small is a choice
  controls_free = pf["controls_free"].boolean(false);
  music_look = std::clamp(pf["music_look"].integer(0), 0, 2);
  music_mono = pf["music_mono"].boolean(false);
  cover_tilt = pf["cover_tilt"].boolean(true);
  // (on by default: each setting shows itself at work; saved under a new name, so the earlier
  // default of off, kept by the 1.3 test builds, does not carry over)
  settings_examples = pf["settings_hints"].boolean(true);
  mini_sub_size = std::clamp(pf["mini_sub_size"].integer(0), 0, 2);
  smooth_motion = pf["smooth_motion"].boolean(false);
  mini_pinned = pf["mini_pinned"].boolean(false);
  loop = pf["loop"].boolean(false);
  loop_all = pf["loop_all"].boolean(false) && !loop;
  shuffle = pf["shuffle"].boolean(false);
  ambient = pf["ambient"].boolean(true);
  // v3 made the light softer by default: older files start again from the new default
  ambient_strength = schema >= 3 ? std::clamp(pf["ambient_strength"].num(0.15), 0.05, 1.0) : 0.15;
  resume = pf["resume"].boolean(true);
  slang = pf["slang"].str();
  alang = pf["alang"].str();
  onboarded = pf["onboarded"].boolean(false);
  news_seen = pf["news_seen"].str();
  news_closed = pf["news_closed"].str();
  installed = pf["installed"].integer<int64_t>(0);
  survey = pf["survey"].str();
  language = i18n::normalize_pref(pf["language"].str(i18n::kAuto));  // missing or unknown: follow Windows
  const JVal& rm = pf["remote"];
  remote_on = rm["on"].boolean(false);
  remote_sound = rm["sound"].boolean(true);
  remote_folders.clear();
  remote_folders_set = rm["folders"].t == JVal::Arr;
  for (auto& f : rm["folders"].a) {
    // A shared folder from the state file is not trusted: it must be a plain
    // drive path, and not the root of a drive (that would share everything).
    // A folder without "on" is not shared: sharing is a choice, not a default.
    const std::string path = f["path"].str();
    if (shared_folder_ok(path) && remote_folders.size() < 16) remote_folders.push_back({path, f["on"].boolean(false)});
  }
  remote_credentials.clear();
  // The credentials of the phones that may pair. On disk they are encrypted
  // (DPAPI on Windows), in memory they are the plain credential: a state file
  // that someone reads is not a set of keys. A file from an older Cue held
  // hashes of tokens; those phones must pair again.
  for (auto& t : rm["credentials"].a) {
    const std::string plain = unprotect_blob(t.str());
    if (remote::valid_token(plain) && remote_credentials.size() < 8) remote_credentials.push_back(plain);
  }
  const JVal& up = ss["update"];
  update_checked = up["checked"].integer<int64_t>(0);
  update_available = up["available"].str();
  update_skipped = up["skipped"].str();
  eggs_seen = ss["eggs"].integer(0);
  const JVal& d = ss["dock"];
  dock.anchor = (DockAnchor)std::clamp(d["anchor"].integer(0), 0, 4);
  dock.fx = std::clamp(d["fx"].num(0.5), 0.0, 1.0);
  dock.fy = std::clamp(d["fy"].num(1.0), 0.0, 1.0);
  const JVal& wn = ss["window"];
  win = WinPlace{};
  if (wn.t == JVal::Obj) {
    win.has = true;
    win.x = wn["x"].integer(0); win.y = wn["y"].integer(0);
    win.w = wn["w"].integer(0); win.h = wn["h"].integer(0);
    win.dpi = std::clamp(wn["dpi"].integer(96), 48, 960);
    win.max = wn["max"].boolean(false);
    if (win.w < 200 || win.h < 120 || win.w > 16384 || win.h > 16384 ||
        win.x < -1000000 || win.x > 1000000 || win.y < -1000000 || win.y > 1000000) win.has = false;
  }
  recents.clear();
  for (auto& r : (schema >= 2 ? j["media"] : j["recents"]).a) {
    RecentItem it;
    read_media(r, it, default_style);
    if (it.path.empty()) continue;
    recents.push_back(it);
    if (recents.size() >= kMaxRecents) break;
  }
  load_status = schema < kSchema ? "migrated" : "ok";
  return true;
}

bool SettingsStore::load(const std::filesystem::path& path) {
  std::string text;
  std::filesystem::path bak = path;
  bak += ".bak";
  if (!read_state_file(path, text)) {
    // no file yet (first run), or only the backup survived an interrupted write
    if (read_state_file(bak, text) && parse(text)) { load_status = "recovered"; return true; }
    load_status = "missing";
    return false;
  }
  if (parse(text)) return true;
  // corrupt: set it aside (never delete what the person had), then try the backup
  std::error_code ec;
  std::filesystem::path bad = path;
  bad += ".corrupt";
  std::filesystem::rename(path, bad, ec);
  *this = SettingsStore{};
  if (read_state_file(bak, text) && parse(text)) { load_status = "recovered"; return true; }
  *this = SettingsStore{};
  load_status = "reset";
  return false;
}

bool SettingsStore::save(const std::filesystem::path& path) const {
  char b[320];
  std::string o = "{\n  \"schema\": " + std::to_string(kSchema) + ",\n";
  // prefs
  o += "  \"prefs\": {\n    \"style\": " + style_json(default_style) + ",\n";
  std::snprintf(b, sizeof b,
                "    \"volume\": %.0f, \"muted\": %s, \"ui_scale\": %d, \"ambient\": %s, \"resume\": %s,\n",
                volume, muted ? "true" : "false", ui_scale, ambient ? "true" : "false", resume ? "true" : "false");
  o += b;
  std::snprintf(b, sizeof b, "    \"ambient_strength\": %.2f,\n", ambient_strength);
  o += b;
  o += std::string("    \"controls_free\": ") + (controls_free ? "true" : "false") + ",\n";
  o += "    \"music_look\": " + std::to_string(music_look) + ",\n";
  o += std::string("    \"music_mono\": ") + (music_mono ? "true" : "false") + ",\n";
  o += std::string("    \"cover_tilt\": ") + (cover_tilt ? "true" : "false") + ",\n";
  o += std::string("    \"settings_hints\": ") + (settings_examples ? "true" : "false") + ",\n";
  o += "    \"mini_sub_size\": " + std::to_string(mini_sub_size) + ",\n";
  o += std::string("    \"smooth_motion\": ") + (smooth_motion ? "true" : "false") + ",\n";
  o += std::string("    \"mini_pinned\": ") + (mini_pinned ? "true" : "false") + ",\n";
  o += std::string("    \"loop\": ") + (loop ? "true" : "false") + ",\n";
  o += std::string("    \"loop_all\": ") + (loop_all ? "true" : "false") + ",\n";
  o += std::string("    \"shuffle\": ") + (shuffle ? "true" : "false") + ",\n";
  o += std::string("    \"onboarded\": ") + (onboarded ? "true" : "false") + ",\n";
  o += "    \"news_seen\": " + json_quote(news_seen) + ", \"news_closed\": " + json_quote(news_closed) + ",\n";
  o += "    \"installed\": " + std::to_string((long long)installed) + ", \"survey\": " + json_quote(survey) + ",\n";
  o += "    \"language\": " + json_quote(i18n::normalize_pref(language)) + ",\n";
  o += std::string("    \"remote\": {\"on\": ") + (remote_on ? "true" : "false") + ", \"sound\": " + (remote_sound ? "true" : "false");
  if (remote_folders_set) {
    o += ", \"folders\": [";
    for (size_t i = 0; i < remote_folders.size(); ++i)
      o += (i ? ", " : "") + std::string("{\"path\": ") + json_quote(remote_folders[i].path) + ", \"on\": " + (remote_folders[i].on ? "true" : "false") + "}";
    o += "]";
  }
  o += ", \"credentials\": [";
  for (size_t i = 0; i < remote_credentials.size(); ++i) o += (i ? ", " : "") + json_quote(protect_blob(remote_credentials[i]));
  o += "]},\n";
  o += "    \"slang\": " + json_quote(slang) + ", \"alang\": " + json_quote(alang) + "\n  },\n";
  // session
  o += "  \"session\": {\n";
  std::snprintf(b, sizeof b, "    \"eggs\": %d,\n    \"dock\": {\"anchor\": %d, \"fx\": %.4f, \"fy\": %.4f}", eggs_seen,
                (int)dock.anchor, dock.fx, dock.fy);
  o += b;
  if (win.has) {
    std::snprintf(b, sizeof b, ",\n    \"window\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"dpi\": %d, \"max\": %s}",
                  win.x, win.y, win.w, win.h, win.dpi, win.max ? "true" : "false");
    o += b;
  }
  o += ",\n    \"update\": {\"checked\": " + std::to_string((long long)update_checked) +
       ", \"available\": " + json_quote(update_available) + ", \"skipped\": " + json_quote(update_skipped) + "}";
  o += "\n  },\n";
  // media
  o += "  \"media\": [";
  for (size_t i = 0; i < recents.size(); ++i) {
    auto& r = recents[i];
    std::snprintf(b, sizeof b, "{\"pos\": %.2f, \"dur\": %.2f, \"last\": %lld", r.pos, r.dur, (long long)r.last);
    o += (i ? ",\n    " : "\n    ") + std::string(b) + ", \"path\": " + json_quote(r.path);
    if (r.sid >= 0) o += ", \"sid\": " + std::to_string(r.sid) + ", \"slang\": " + json_quote(r.sid_lang);
    if (r.aid >= 0 || r.aid == -2) o += ", \"aid\": " + std::to_string(r.aid) + ", \"alang\": " + json_quote(r.aid_lang);
    if (!r.sub_visible) o += ", \"subvis\": false";
    if (std::fabs(r.sub_delay) > 0.001) { std::snprintf(b, sizeof b, ", \"delay\": %.3f", r.sub_delay); o += b; }
    if (r.has_style) o += ", \"style\": " + style_json(r.style);
    o += "}";
  }
  o += recents.empty() ? "]\n}\n" : "\n  ]\n}\n";
  std::filesystem::path tmp = path, bak = path;
  tmp += ".tmp";
  bak += ".bak";
  // Unchanged state needs no main-file write. Seed/refresh a missing or old
  // recovery copy once, then leave both files untouched on repeated saves.
  if (file_contents_equal(path,o)) {
    if (!file_contents_equal(bak,o)) {
      std::error_code backup_error;
      std::filesystem::copy_file(path,bak,std::filesystem::copy_options::overwrite_existing,backup_error);
      if (!backup_error) flush_directory(path.parent_path());  // best effort: the copy is on the disk
      return !backup_error;
    }
    return true;
  }
  // safe write: a temporary file, the previous version kept as .bak, then an atomic swap
  // (a failed save leaves no stale .tmp behind: thanks to omnizs38, #17)
  auto abandon_tmp = [&]() {
    std::error_code cleanup_ec;
    std::filesystem::remove(tmp, cleanup_ec);
    return false;
  };
  {
    // The temporary file is created, never opened: a name that something else
    // put there (a link to another file) must not be written through. A file
    // left over from a crash is removed first; if something puts a name back
    // in between, the save gives up rather than write through it.
    std::error_code leftover_ec;
    std::filesystem::remove(tmp, leftover_ec);
    if (!write_new_file(tmp, o)) return abandon_tmp();
    // Push the temporary file to the storage device before it replaces the good
    // state. Without this the bytes are still in the page cache when the swap
    // happens, so a power cut can leave a torn file behind a backup that has
    // already been replaced. Fail closed: a flush that cannot be performed
    // must not be silently skipped.
    if (!flush_file_to_disk(tmp)) return abandon_tmp();
  }
  std::error_code ec;
#ifdef _WIN32
  // Windows: one call swaps the new file in and keeps the previous one as the backup (a rename,
  // not a copy: one write less every time). Something reading the file for a moment (an
  // antivirus checking the file just written, the search indexer) can refuse it: tries for about
  // two seconds (this runs on a worker), then gives up (the next save writes it again).
  for (int attempt = 0; attempt < 12; ++attempt) {
    BOOL ok = std::filesystem::exists(path, ec)
                  ? ReplaceFileW(path.c_str(), tmp.c_str(), bak.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr)
                  : MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (ok) {
      // Best effort: the rename itself must survive a power cut too. The file is
      // already in place, so a refusal here is not a failed save.
      flush_directory(path.parent_path());
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30 + 30 * std::min(attempt, 5)));
  }
  return abandon_tmp();
#else
  const bool had_path = std::filesystem::exists(path, ec);
  if (ec) return abandon_tmp();
  if (had_path) {
    // the previous good version becomes the backup (remove first: overwrite is unreliable on some
    // runtimes); without a backup the good file is never replaced
    std::error_code remove_ec, copy_ec;
    std::filesystem::remove(bak, remove_ec);
    std::filesystem::copy_file(path, bak, copy_ec);
    if (copy_ec) return abandon_tmp();
  }
  // atomic replace on the same volume; something reading the file for a moment can refuse it: a
  // few short tries, then give up
  for (int attempt = 0; attempt < 6; ++attempt) {
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (!ec) {
      // Best effort: the rename itself must survive a power cut too. The file is
      // already in place, so a refusal here is not a failed save.
      flush_directory(path.parent_path());
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(15 + 15 * attempt));
  }
  return abandon_tmp();
#endif
}

RecentItem* SettingsStore::find(const std::string& p) {
  std::string n = normalize_path(p);
  for (auto& r : recents)
    if (normalize_path(r.path) == n) return &r;
  return nullptr;
}

RecentItem& SettingsStore::touch(const std::string& p, double dur, int64_t now) {
  std::string n = normalize_path(p);
  auto it = std::find_if(recents.begin(), recents.end(), [&](const RecentItem& r) { return normalize_path(r.path) == n; });
  RecentItem item;
  if (it != recents.end()) { item = *it; recents.erase(it); }
  item.path = p;
  if (dur > 0) item.dur = dur;
  item.last = now;
  recents.insert(recents.begin(), item);
  if (recents.size() > kMaxRecents) recents.resize(kMaxRecents);
  return recents.front();
}

bool SettingsStore::set_position(const std::string& p, double pos, double dur) {
  RecentItem* r = find(p);
  if (!r) return false;
  double next_duration = dur > 0 ? dur : r->dur;
  if (r->pos == pos && r->dur == next_duration) return false;
  r->dur = next_duration;
  r->pos = pos;
  return true;
}

double SettingsStore::resume_point(const std::string& p) const {
  std::string n = normalize_path(p);
  for (auto& r : recents) {
    if (normalize_path(r.path) != n) continue;
    if (r.pos < 10) return -1;
    if (r.dur > 0 && (r.pos > r.dur - 30 || r.pos > r.dur * 0.97)) return -1;
    return r.pos;
  }
  return -1;
}

SubStyle SettingsStore::style_for(const std::string& p) const {
  std::string n = normalize_path(p);
  for (auto& r : recents)
    if (normalize_path(r.path) == n && r.has_style) return r.style;
  return default_style;
}

void SettingsStore::set_style_for(const std::string& p, const SubStyle& st) {
  RecentItem* r = find(p);
  if (!r) return;
  r->has_style = true;
  r->style = st;
}

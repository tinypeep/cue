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
  loop = pf["loop"].boolean(false);
  ambient = pf["ambient"].boolean(true);
  // v3 made the light softer by default: older files start again from the new default
  ambient_strength = schema >= 3 ? std::clamp(pf["ambient_strength"].num(0.15), 0.05, 1.0) : 0.15;
  resume = pf["resume"].boolean(true);
  slang = pf["slang"].str();
  alang = pf["alang"].str();
  onboarded = pf["onboarded"].boolean(false);
  news_seen = pf["news_seen"].str();
  news_closed = pf["news_closed"].str();
  language = i18n::normalize_pref(pf["language"].str(i18n::kAuto));  // missing or unknown: follow Windows
  const JVal& rm = pf["remote"];
  remote_on = rm["on"].boolean(false);
  remote_sound = rm["sound"].boolean(true);
  remote_folders.clear();
  remote_folders_set = rm["folders"].t == JVal::Arr;
  for (auto& f : rm["folders"].a)
    if (!f["path"].str().empty() && remote_folders.size() < 16) remote_folders.push_back({f["path"].str(), f["on"].boolean(true)});
  remote_tokens.clear();
  for (auto& t : rm["tokens"].a)
    if (remote::valid_token(t.str()) && remote_tokens.size() < 8) remote_tokens.push_back(t.str());
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
  o += std::string("    \"loop\": ") + (loop ? "true" : "false") + ",\n";
  o += std::string("    \"onboarded\": ") + (onboarded ? "true" : "false") + ",\n";
  o += "    \"news_seen\": " + json_quote(news_seen) + ", \"news_closed\": " + json_quote(news_closed) + ",\n";
  o += "    \"language\": " + json_quote(i18n::normalize_pref(language)) + ",\n";
  o += std::string("    \"remote\": {\"on\": ") + (remote_on ? "true" : "false") + ", \"sound\": " + (remote_sound ? "true" : "false");
  if (remote_folders_set) {
    o += ", \"folders\": [";
    for (size_t i = 0; i < remote_folders.size(); ++i)
      o += (i ? ", " : "") + std::string("{\"path\": ") + json_quote(remote_folders[i].path) + ", \"on\": " + (remote_folders[i].on ? "true" : "false") + "}";
    o += "]";
  }
  o += ", \"tokens\": [";
  for (size_t i = 0; i < remote_tokens.size(); ++i) o += (i ? ", " : "") + json_quote(remote_tokens[i]);
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
    if (r.aid >= 0) o += ", \"aid\": " + std::to_string(r.aid) + ", \"alang\": " + json_quote(r.aid_lang);
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
      return !backup_error;
    }
    return true;
  }
  // safe write: a temporary file, the previous version kept as .bak, then an atomic swap
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << o;
    f.flush();
    if (!f) return false;
  }
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    // the previous good version becomes the backup (remove first: overwrite is unreliable on some runtimes)
    std::filesystem::remove(bak, ec);
    std::filesystem::copy_file(path, bak, ec);
  }
  ec.clear();
  std::filesystem::rename(tmp, path, ec);  // atomic replace on the same volume
  return !ec;
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

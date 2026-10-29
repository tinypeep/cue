#include <cctype>
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

std::string fmt_time(double sec, bool force_hours) {
  if (!std::isfinite(sec) || sec < 0) sec = 0;
  long t = (long)sec;
  long h = t / 3600, m = (t / 60) % 60, s = t % 60;
  char buf[32];
  if (h > 0 || force_hours) std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", h, m, s);
  else std::snprintf(buf, sizeof buf, "%02ld:%02ld", m, s);
  return buf;
}

std::string fmt_delay(double sec) {
  double r = std::round(sec * 10) / 10;
  char buf[32];
  if (std::fabs(r) < 0.05) return "0,0 s";
  std::snprintf(buf, sizeof buf, "%s%.1f s", r > 0 ? "+" : "-", std::fabs(r));
  std::string out = buf;
  for (auto& ch : out) if (ch == '.') ch = ',';
  return out;
}

std::string file_name(std::string_view p) {
  size_t k = p.find_last_of("/\\");
  return std::string(k == std::string_view::npos ? p : p.substr(k + 1));
}

std::string ext_lower(std::string_view p) {
  std::string n = file_name(p);
  size_t d = n.rfind('.');
  if (d == std::string::npos) return "";
  std::string e = n.substr(d + 1);
  for (auto& c : e) c = (char)std::tolower((unsigned char)c);
  return e;
}

bool is_subtitle_path(std::string_view p) {
  static const char* exts[] = {"srt", "vtt", "webvtt", "ass", "ssa", "sub", "sup", "idx", "smi", "lrc"};
  std::string e = ext_lower(p);
  for (auto x : exts) if (e == x) return true;
  return false;
}

bool is_media_path(std::string_view p) {
  // Anything that is not a subtitle is offered to the engine; it decides.
  return !p.empty() && !is_subtitle_path(p);
}

SubKind sub_kind(const std::string& c) {
  if (c.empty()) return SubKind::None;
  if (c == "ass" || c == "ssa") return SubKind::Ass;
  if (c.find("pgs") != std::string::npos || c.find("dvd") != std::string::npos ||
      c.find("dvb") != std::string::npos || c == "xsub" || c == "hdmv_pgs_subtitle")
    return SubKind::Bitmap;
  return SubKind::Text;
}

namespace {
struct Lang { const char* codes; const char* name; };
// Native names when the Omni Mono can draw them, Portuguese names otherwise.
const Lang kLangs[] = {
    {"pt,por,pob,pt-br,pt-pt,ptbr", "Português"}, {"en,eng", "English"},
    {"es,spa,es-419,es-es", "Español"},           {"fr,fre,fra", "Français"},
    {"de,ger,deu", "Deutsch"},                     {"it,ita", "Italiano"},
    {"nl,dut,nld", "Nederlands"},                  {"pl,pol", "Polski"},
    {"sv,swe", "Svenska"},                         {"no,nor,nob,nno", "Norsk"},
    {"da,dan", "Dansk"},                           {"fi,fin", "Suomi"},
    {"tr,tur", "Türkçe"},                          {"ca,cat", "Català"},
    {"ro,rum,ron", "romeno"},                      {"hu,hun", "Magyar"},
    {"cs,cze,ces", "tcheco"},                      {"ja,jpn", "japonês"},
    {"zh,chi,zho,chs,cht", "chinês"},              {"ko,kor", "coreano"},
    {"ru,rus", "russo"},                           {"uk,ukr", "ucraniano"},
    {"ar,ara", "árabe"},                           {"he,heb", "hebraico"},
    {"hi,hin", "hindi"},                           {"el,gre,ell", "grego"},
    {"th,tha", "tailandês"},                       {"vi,vie", "vietnamita"},
    {"id,ind", "indonésio"},                       {"ms,may,msa", "malaio"},
};

std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string lang_name(const std::string& code) {
  std::string c = lower(code);
  for (auto& l : kLangs) {
    std::string list = l.codes;
    size_t st = 0;
    while (st <= list.size()) {
      size_t e = list.find(',', st);
      if (e == std::string::npos) e = list.size();
      if (list.compare(st, e - st, c) == 0) return l.name;
      st = e + 1;
    }
  }
  return "";
}

bool technical(const std::string& t) {
  static const char* bad[] = {"ac3", "eac3", "dts", "aac", "flac", "opus", "truehd", "atmos", "kbps", "5.1",
                              "7.1", "2.0", "srt", "pgs", "ass", "ssa", "sup", "vobsub", "subrip", "track", "stereo"};
  std::string l = lower(t);
  for (auto b : bad) if (l.find(b) != std::string::npos) return true;
  // a site or release group stamped in the title ("moviesmod.farm", "www..."): not a name
  if (l.find("www") != std::string::npos || l.find("http") != std::string::npos) return true;
  size_t dot = l.rfind('.');
  if (dot != std::string::npos && l.find(' ') == std::string::npos && dot + 2 < l.size() && std::isalpha((unsigned char)l[dot + 1])) return true;
  return t.size() > 24;
}
}  // namespace

std::string track_label(const Track& t, const std::vector<Track>& all) {
  std::string name = lang_name(t.lang);
  bool title_ok = !t.title.empty() && !technical(t.title);
  if (name.empty()) name = title_ok ? t.title : "";
  if (name.empty()) {
    int n = 1;
    for (auto& o : all) { if (o.id == t.id) break; ++n; }
    name = "faixa " + std::to_string(n);
  } else {
    // same language twice: tell them apart without jargon
    int same = 0, idx = 0;
    for (auto& o : all) {
      if (lang_name(o.lang) == lang_name(t.lang) && !t.lang.empty()) { ++same; if (o.id == t.id) idx = same; }
    }
    if (same > 1) {
      if (title_ok && lower(t.title) != lower(name)) name += " · " + lower(t.title);
      else name += " " + std::to_string(idx);
    }
  }
  if (t.forced && t.type == "sub") name += " · forçada";
  return name;
}

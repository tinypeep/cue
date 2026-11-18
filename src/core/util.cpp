#include <cctype>
#include "util.h"
#include "i18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

std::string fmt_time(double sec, bool force_hours) {
  if (!std::isfinite(sec) || sec < 0) sec = 0;
  // (a broken file can report any length; long is 32 bits on Windows, so the cast is bounded)
  long long t = (long long)std::min(sec, 1e15);
  long long h = t / 3600, m = (t / 60) % 60, s = t % 60;
  char buf[48];
  if (h > 0 || force_hours) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, s);
  else std::snprintf(buf, sizeof buf, "%02lld:%02lld", m, s);
  return buf;
}

std::string fmt_delay(double sec) {
  if (!std::isfinite(sec)) sec = 0;  // a broken property must not print "nan s"
  double r = std::round(sec * 10) / 10;
  char buf[32];
  if (std::fabs(r) < 0.05) r = 0;
  std::snprintf(buf, sizeof buf, "%s%.1f s", r > 0 ? "+" : (r < 0 ? "-" : ""), std::fabs(r));
  std::string out = buf;
  char sep = tr(str::number_decimal)[0];  // "0,3 s" in Portuguese, "0.3 s" in English
  for (auto& ch : out) if (ch == '.') ch = sep ? sep : '.';
  return out;
}

std::string file_name(std::string_view p) {
  size_t k = p.find_last_of("/\\");
  return std::string(k == std::string_view::npos ? p : p.substr(k + 1));
}

std::string file_stem(std::string_view p) {
  std::string n = file_name(p);
  size_t d = n.rfind('.');
  return d == std::string::npos || d == 0 ? n : n.substr(0, d);
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

SubKind sub_kind(const std::string& c) {
  if (c.empty()) return SubKind::None;
  if (c == "ass" || c == "ssa") return SubKind::Ass;
  if (c.find("pgs") != std::string::npos || c.find("dvd") != std::string::npos ||
      c.find("dvb") != std::string::npos || c == "xsub" || c == "hdmv_pgs_subtitle")
    return SubKind::Bitmap;
  return SubKind::Text;
}

namespace {
// Endonyms when the name is a word of the Omni's own Latin letters (they read the same in every
// interface language); otherwise the name in the interface language (str::lang_*).
struct LangName { const char* codes; const char* native; str::Id key; };
const LangName kLangs[] = {
    {"pt,por,pob,pt-br,pt-pt,ptbr", "Português", str::kCount}, {"en,eng", "English", str::kCount},
    {"es,spa,es-419,es-es", "Español", str::kCount},           {"fr,fre,fra", "Français", str::kCount},
    {"de,ger,deu", "Deutsch", str::kCount},                     {"it,ita", "Italiano", str::kCount},
    {"nl,dut,nld", "Nederlands", str::kCount},                  {"pl,pol", "Polski", str::kCount},
    {"sv,swe", "Svenska", str::kCount},                         {"no,nor,nob,nno", "Norsk", str::kCount},
    {"da,dan", "Dansk", str::kCount},                           {"fi,fin", "Suomi", str::kCount},
    {"tr,tur", "Türkçe", str::kCount},                          {"ca,cat", "Català", str::kCount},
    {"ro,rum,ron", nullptr, str::lang_ro},                      {"hu,hun", "Magyar", str::kCount},
    {"cs,cze,ces", nullptr, str::lang_cs},                      {"ja,jpn", nullptr, str::lang_ja},
    {"zh,chi,zho,chs,cht", nullptr, str::lang_zh},              {"ko,kor", nullptr, str::lang_ko},
    {"ru,rus", nullptr, str::lang_ru},                          {"uk,ukr", nullptr, str::lang_uk},
    {"ar,ara", nullptr, str::lang_ar},                          {"he,heb", nullptr, str::lang_he},
    {"hi,hin", nullptr, str::lang_hi},                          {"el,gre,ell", nullptr, str::lang_el},
    {"th,tha", nullptr, str::lang_th},                          {"vi,vie", nullptr, str::lang_vi},
    {"bg,bul", "Български", str::kCount}, {"hr,hrv", "Hrvatski", str::kCount},
    {"et,est", "Eesti", str::kCount}, {"is,ice,isl", "Íslenska", str::kCount},
    {"lt,lit", "Lietuvių", str::kCount}, {"lv,lav", "Latviešu", str::kCount},
    {"sk,slo,slk", "Slovenčina", str::kCount}, {"sl,slv", "Slovenščina", str::kCount},
    {"id,ind", nullptr, str::lang_id},                          {"ms,may,msa", nullptr, str::lang_ms},
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
      if (list.compare(st, e - st, c) == 0) return l.native ? l.native : tr(l.key);
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

namespace {
std::string subtitle_label(const Track& t,size_t ordinal) {
  std::string code=lower(t.lang);std::replace(code.begin(),code.end(),'_','-');
  std::string name=lang_name(code);
  size_t dash=code.find('-');
  if(name.empty()&&dash!=std::string::npos)name=lang_name(code.substr(0,dash));
  if(!name.empty()&&dash!=std::string::npos) {
    std::string region=code.substr(dash+1);
    for(auto& c:region)c=(char)std::toupper((unsigned char)c);
    name+=" ("+region+")";
  }
  std::string title=t.title, normalized=lower(title);
  bool useful=!title.empty()&&normalized.find("http")==std::string::npos&&normalized.find("www.")==std::string::npos;
  for(const char* generic:{"srt","ass","ssa","pgs","sup","subrip","vobsub","hdmv_pgs_subtitle"})if(normalized==generic||normalized=="["+std::string(generic)+"]")useful=false;
  if(normalized.starts_with("track ")||normalized.starts_with("faixa "))useful=false;
  if(name.empty()) {
    if(useful)name=title;
    else if(!code.empty()&&code!="und")name=t.lang;
    else name=i18n::format(str::track_n,{{"n",std::to_string(ordinal)}});
  } else {
    // Qualifiers precede long authored titles, so ellipsis cannot hide accessibility flags.
    if(t.forced)name+=" · "+std::string(tr(str::track_forced));
    if(t.hearing_impaired)name+=" · "+std::string(tr(str::track_sdh));
    if(useful&&normalized!=lower(name)&&!(normalized=="sdh"&&t.hearing_impaired)&&!(normalized=="forced"&&t.forced))name+=" · "+title;
    return name;
  }
  if(t.forced)name+=" · "+std::string(tr(str::track_forced));
  if(t.hearing_impaired&&lower(name)!="sdh")name+=" · "+std::string(tr(str::track_sdh));
  return name;
}
}
std::vector<std::string> track_labels(const std::vector<Track>& tracks) {
  std::vector<std::string> labels;labels.reserve(tracks.size());
  std::unordered_map<std::string,size_t> counts,seen;
  for(size_t i=0;i<tracks.size();++i) {
    labels.push_back(tracks[i].type=="sub"?subtitle_label(tracks[i],i+1):track_label(tracks[i],tracks));
    ++counts[labels.back()];
  }
  for(auto& label:labels)if(counts[label]>1) {auto original=label;label+=" "+std::to_string(++seen[original]);}
  return labels;
}

std::string track_label(const Track& t, const std::vector<Track>& all) {
  if(t.type=="sub") {
    auto labels=track_labels(all);
    for(size_t i=0;i<all.size();++i)if(all[i].id==t.id)return labels[i];
    return subtitle_label(t,1);
  }
  std::string name = lang_name(t.lang);
  bool title_ok = !t.title.empty() && !technical(t.title);
  if (name.empty()) name = title_ok ? t.title : "";
  if (name.empty()) {
    int n = 1;
    for (auto& o : all) { if (o.id == t.id) break; ++n; }
    name = i18n::format(str::track_n, {{"n", std::to_string(n)}});
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
  if (t.forced && t.type == "sub") name += std::string(" · ") + tr(str::track_forced);
  return name;
}

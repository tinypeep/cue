#include "i18n.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "i18n_catalog.inc"

namespace i18n {
namespace {
std::atomic<int> g_lang{(int)kFallback};
std::atomic<uint32_t> g_revision{1};

const char* const kTags[kLangCount] = {"pt-BR", "en", "es", "fr", "zh-Hans"};
const char* const kNative[kLangCount] = {"Português (Brasil)", "English", "Español", "Français",
                                         "\xe7\xae\x80\xe4\xbd\x93\xe4\xb8\xad\xe6\x96\x87"};  // 简体中文

std::string lower(std::string_view s) {
  std::string o(s);
  for (auto& c : o) c = (char)std::tolower((unsigned char)c);
  return o;
}

std::vector<std::string> subtags(std::string_view bcp47) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : bcp47) {
    if (c == '-' || c == '_') { out.push_back(lower(cur)); cur.clear(); }
    else cur += c;
  }
  out.push_back(lower(cur));
  return out;
}

// CLDR cardinal rules, integers only (that is all the interface counts).
bool is_one(Lang l, long long n) {
  switch (l) {
    case Lang::PtBR: case Lang::Fr: return n == 0 || n == 1;
    case Lang::En: case Lang::Es: return n == 1;
    case Lang::ZhHans: return false;
  }
  return n == 1;
}

const char* lookup(Lang l, str::Id id, bool one) {
  if (id >= str::kCount) return "";
  if (one) {
    const char* t = pick(kOne[(int)l][id], kOne[(int)kFallback][id], nullptr);
    return t ? t : lookup(l, id, false);
  }
  return pick(kOther[(int)l][id], kOther[(int)kFallback][id], kKeyNames[id]);
}

std::string fill(std::string_view text, Args args) {
  std::string out;
  out.reserve(text.size() + 16);
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '{') {
      size_t e = text.find('}', i);
      if (e != std::string_view::npos) {
        std::string_view name = text.substr(i + 1, e - i - 1);
        bool done = false;
        for (auto& a : args)
          if (a.first == name) { out += a.second; done = true; break; }
        if (done) { i = e + 1; continue; }
      }
    }
    out += text[i++];
  }
  return out;
}

std::set<std::string> placeholders(const char* t) {
  std::set<std::string> s;
  if (!t) return s;
  std::string_view v(t);
  for (size_t i = 0; (i = v.find('{', i)) != std::string_view::npos;) {
    size_t e = v.find('}', i);
    if (e == std::string_view::npos) break;
    s.insert(std::string(v.substr(i + 1, e - i - 1)));
    i = e + 1;
  }
  return s;
}
}  // namespace

const char* tag(Lang l) { return kTags[(int)l]; }
const char* native_name(Lang l) { return kNative[(int)l]; }

bool parse_tag(std::string_view saved, Lang& out) {
  std::string s = lower(saved);
  for (int i = 0; i < kLangCount; ++i)
    if (s == lower(kTags[i])) { out = (Lang)i; return true; }
  return false;
}

std::string normalize_pref(std::string_view saved) {
  Lang l;
  return parse_tag(saved, l) ? std::string(tag(l)) : std::string(kAuto);
}

bool match_ui_language(std::string_view bcp47, Lang& out) {
  auto t = subtags(bcp47);
  if (t.empty() || t[0].empty()) return false;
  const std::string& p = t[0];
  if (p == "pt") { out = Lang::PtBR; return true; }   // pt-PT and others: the Brazilian text
  if (p == "en") { out = Lang::En; return true; }
  if (p == "es") { out = Lang::Es; return true; }
  if (p == "fr") { out = Lang::Fr; return true; }
  if (p == "zh") {
    // the script decides when it is given; otherwise the region
    for (size_t i = 1; i < t.size(); ++i) {
      if (t[i] == "hans") { out = Lang::ZhHans; return true; }
      if (t[i] == "hant") return false;
    }
    if (t.size() == 1) { out = Lang::ZhHans; return true; }  // plain "zh": Simplified by default
    const std::string& region = t[1];
    if (region == "cn" || region == "sg") { out = Lang::ZhHans; return true; }
    return false;  // zh-TW, zh-HK, zh-MO and anything else: not Simplified
  }
  return false;
}

Lang resolve(std::string_view saved_pref, const std::vector<std::string>& ui_languages) {
  Lang l;
  if (parse_tag(saved_pref, l)) return l;
  for (auto& u : ui_languages)
    if (match_ui_language(u, l)) return l;
  return kFallback;
}

void set_language(Lang l) {
  if ((int)l >= kLangCount) l = kFallback;
  if (g_lang.exchange((int)l) != (int)l) g_revision.fetch_add(1);
}
Lang language() { return (Lang)g_lang.load(std::memory_order_relaxed); }
uint32_t revision() { return g_revision.load(std::memory_order_relaxed); }

const char* tr(str::Id id) { return lookup(language(), id, false); }

std::string format(str::Id id, Args args) { return fill(tr(id), args); }

std::string plural(str::Id id, long long n, Args args) {
  Lang l = language();
  std::string num = std::to_string(n);
  std::string t = fill(lookup(l, id, kPluralKey[id] && is_one(l, n)), args);
  return fill(t, {{"n", num}});
}

std::string decimal(double v, int places) {
  char b[64];
  std::snprintf(b, sizeof b, "%.*f", places, std::isfinite(v) ? v : 0.0);
  std::string s = b;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  const char* sep = tr(str::number_decimal);
  if (sep[0] && sep[0] != '.')
    for (auto& c : s) if (c == '.') c = sep[0];
  return s;
}

const char* key_name(str::Id id) { return id < str::kCount ? kKeyNames[id] : ""; }
const char* raw(Lang l, str::Id id, bool one_form) {
  if (id >= str::kCount) return nullptr;
  return one_form ? kOne[(int)l][id] : kOther[(int)l][id];
}

std::vector<Issue> audit() {
  std::vector<Issue> out;
  for (int li = 0; li < kLangCount; ++li) {
    Lang l = (Lang)li;
    for (int i = 0; i < str::kCount; ++i) {
      auto id = (str::Id)i;
      const char* t = kOther[li][i];
      if (!t || !*t) { out.push_back({l, id, "missing"}); continue; }
      auto want = placeholders(kOther[(int)kFallback][i]);
      if (placeholders(t) != want) out.push_back({l, id, "placeholders differ from English"});
      if (kPluralKey[i]) {
        bool needs_one = l != Lang::ZhHans;
        if (needs_one && !kOne[li][i]) out.push_back({l, id, "missing plural form 'one'"});
        if (kOne[li][i] && placeholders(kOne[li][i]) != want) out.push_back({l, id, "plural 'one' placeholders differ"});
      }
    }
  }
  return out;
}

}  // namespace i18n

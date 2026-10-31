// Interface language: resolution from Windows' display languages, the manual choice, fallback,
// persistence, the six catalogs (complete, same placeholders, plural forms) and that every
// character the interface can show has a pixel glyph (nothing turns into a box or vanishes).
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "../src/core/i18n.h"
#include "../src/core/media_info.h"
#include "../src/core/store.h"
#include "../src/core/util.h"
#include "../src/ui/gfx.h"

using i18n::Lang;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static bool maps(const char* tag, Lang want) { Lang l; return i18n::match_ui_language(tag, l) && l == want; }
static bool unmapped(const char* tag) { Lang l; return !i18n::match_ui_language(tag, l); }

int main() {
  // ── one Windows display language -> a catalog ───────────────────
  for (auto t : {"en", "en-US", "en-GB", "en-AU", "en-IN", "EN-us", "en_CA"}) CHECK(maps(t, Lang::En));
  for (auto t : {"es", "es-ES", "es-MX", "es-419", "es-AR", "es-US"}) CHECK(maps(t, Lang::Es));
  for (auto t : {"fr", "fr-FR", "fr-CA", "fr-BE", "fr-CH", "fr-LU"}) CHECK(maps(t, Lang::Fr));
  for (auto t : {"ru", "ru-RU", "ru-BY", "ru-KZ", "RU-ru", "ru_RU"}) CHECK(maps(t, Lang::Ru));
  for (auto t : {"pt-BR", "pt-PT", "pt", "pt-AO", "pt_br"}) CHECK(maps(t, Lang::PtBR));  // pt-PT uses the Brazilian text
  for (auto t : {"zh-Hans", "zh-CN", "zh-SG", "zh-Hans-CN", "zh-Hans-SG", "zh-Hans-HK", "ZH-cn", "zh"}) CHECK(maps(t, Lang::ZhHans));
  // Traditional Chinese is never shown Simplified
  for (auto t : {"zh-Hant", "zh-TW", "zh-HK", "zh-MO", "zh-Hant-TW", "zh-Hant-HK", "zh-Hant-CN"}) CHECK(unmapped(t));
  for (auto t : {"de-DE", "ja-JP", "ko-KR", "it-IT", "qps-ploc", "", "-"}) CHECK(unmapped(t));

  // ── priority: manual > first supported Windows language (in order) > English ──
  CHECK(i18n::resolve("auto", {"de-DE", "zh-TW", "es-AR", "en-US"}) == Lang::Es);
  CHECK(i18n::resolve("auto", {"fr-CA", "pt-BR"}) == Lang::Fr);
  CHECK(i18n::resolve("auto", {"pt-BR", "fr-CA"}) == Lang::PtBR);
  CHECK(i18n::resolve("auto", {"de-DE", "ru-RU"}) == Lang::Ru);
  CHECK(i18n::resolve("auto", {"zh-TW", "zh-HK"}) == Lang::En);           // nothing supported: English
  CHECK(i18n::resolve("auto", {"zh-HK", "zh-CN"}) == Lang::ZhHans);       // keeps looking past Traditional
  CHECK(i18n::resolve("auto", {"de-DE", "ja-JP"}) == Lang::En);
  CHECK(i18n::resolve("auto", {}) == Lang::En);
  CHECK(i18n::resolve("fr", {"pt-BR", "en-US"}) == Lang::Fr);             // a manual choice wins
  CHECK(i18n::resolve("zh-Hans", {"zh-TW"}) == Lang::ZhHans);
  CHECK(i18n::resolve("pt-BR", {}) == Lang::PtBR);
  CHECK(i18n::resolve("", {"es-MX"}) == Lang::Es);                        // empty = automatic
  CHECK(i18n::resolve("xx-YY", {"es-MX"}) == Lang::Es);                   // unknown = automatic
  CHECK(i18n::normalize_pref("PT-br") == "pt-BR" && i18n::normalize_pref("ZH-HANS") == "zh-Hans");
  CHECK(i18n::normalize_pref("RU-ru") == "ru");
  CHECK(i18n::normalize_pref("auto") == "auto" && i18n::normalize_pref("") == "auto" && i18n::normalize_pref("en-US") == "auto");

  // ── the choice is saved, survives a restart and an update ──────
  auto path = std::filesystem::temp_directory_path() / "cue-i18n-test.json";
  for (std::string pref : {"auto", "pt-BR", "en", "es", "fr", "zh-Hans", "ru"}) {
    Store a; a.language = pref; a.ui_scale = 3; CHECK(a.save(path));
    Store b; CHECK(b.load(path)); CHECK(b.language == pref); CHECK(b.ui_scale == 3);
    // the next start resolves from it before Windows is consulted
    Lang want; bool manual = i18n::parse_tag(pref, want);
    CHECK(i18n::resolve(b.language, {"de-DE", "zh-TW"}) == (manual ? want : Lang::En));
  }
  {
    // a 1.0 state file (no "language"): automatic, everything else intact
    std::ofstream f(path, std::ios::trunc);
    f << R"({"schema": 5, "prefs": {"ui_scale": 0, "ambient": false, "slang": "por", "alang": "eng"}, "session": {}, "media": []})";
    f.close();
    Store b; CHECK(b.load(path)); CHECK(b.language == "auto"); CHECK(b.ui_scale == 0 && !b.ambient);
    CHECK(b.slang == "por" && b.alang == "eng");  // subtitle/audio track languages stay their own thing
    std::ofstream g(path, std::ios::trunc);
    g << R"({"schema": 5, "prefs": {"language": "klingon"}})";
    g.close();
    Store c; CHECK(c.load(path)); CHECK(c.language == "auto");
  }
  std::filesystem::remove(path);
  std::filesystem::remove(path.string() + ".bak");

  // ── the catalogs ───────────────────────────────────────────────
  auto issues = i18n::audit();
  for (auto& is : issues) std::printf("  %s %s: %s\n", i18n::tag(is.lang), i18n::key_name(is.id), is.what.c_str());
  CHECK(issues.empty());
  for (int li = 0; li < i18n::kLangCount; ++li)
    for (int i = 0; i < str::kCount; ++i) {
      const char* t = i18n::raw((Lang)li, (str::Id)i);
      CHECK(t && *t);
    }
  // a key a catalog lacks falls back to English, then to its own name (never an empty label)
  CHECK(std::string(i18n::pick(nullptr, "open a file", "home.open_file")) == "open a file");
  CHECK(std::string(i18n::pick("", "open a file", "home.open_file")) == "open a file");
  CHECK(std::string(i18n::pick(nullptr, nullptr, "home.open_file")) == "home.open_file");
  CHECK(std::string(i18n::pick("abra", "open", "k")) == "abra");
  // the active language changes at once, and its revision tells views to re-measure
  i18n::set_language(Lang::PtBR);
  uint32_t r0 = i18n::revision();
  CHECK(std::string(tr(str::home_open_file)) == "abra um arquivo");
  i18n::set_language(Lang::Fr);
  CHECK(i18n::revision() != r0);
  CHECK(std::string(tr(str::home_open_file)) == "ouvrir un fichier");
  uint32_t r1 = i18n::revision();
  i18n::set_language(Lang::Fr);
  CHECK(i18n::revision() == r1);  // same language: nothing to redo
  // placeholders by name, never sentence pieces glued together
  i18n::set_language(Lang::En);
  CHECK(i18n::format(str::update_to, {{"version", "1.2"}}) == "update to 1.2");
  CHECK(i18n::format(str::home_update_downloading, {{"version", "1.2"}, {"percent", "42"}}) == "downloading 1.2 · 42%");
  i18n::set_language(Lang::Fr);
  CHECK(i18n::format(str::setup_installing, {{"percent", "7"}}) == "installation · 7\xc2\xa0%");
  i18n::set_language(Lang::ZhHans);
  CHECK(i18n::format(str::update_to, {{"version", "1.2"}}) == "\xe6\x9b\xb4\xe6\x96\xb0\xe5\x88\xb0 1.2");  // 更新到 1.2
  // plurals per language
  i18n::set_language(Lang::PtBR);
  CHECK(i18n::plural(str::info_channels_n, 1) == "1 canal" && i18n::plural(str::info_channels_n, 6) == "6 canais");
  i18n::set_language(Lang::En);
  CHECK(i18n::plural(str::info_channels_n, 1) == "1 channel" && i18n::plural(str::info_channels_n, 0) == "0 channels");
  i18n::set_language(Lang::Fr);
  CHECK(i18n::plural(str::info_channels_n, 0) == "0 canal" && i18n::plural(str::info_channels_n, 8) == "8 canaux");
  i18n::set_language(Lang::Es);
  CHECK(i18n::plural(str::info_channels_n, 1) == "1 canal" && i18n::plural(str::info_channels_n, 3) == "3 canales");
  i18n::set_language(Lang::ZhHans);
  CHECK(i18n::plural(str::info_channels_n, 1) == "1 \xe5\xa3\xb0\xe9\x81\x93");  // 1 声道
  i18n::set_language(Lang::Ru);
  CHECK(i18n::plural(str::info_channels_n, 1) == "1 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb");      // 1 канал
  CHECK(i18n::plural(str::info_channels_n, 2) == "2 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb\xd0\xb0");  // 2 канала
  CHECK(i18n::plural(str::info_channels_n, 5) == "5 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb\xd0\xbe\xd0\xb2");  // 5 каналов
  CHECK(i18n::plural(str::info_channels_n, 11) == "11 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb\xd0\xbe\xd0\xb2");  // 11 каналов
  CHECK(i18n::plural(str::info_channels_n, 21) == "21 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb");  // 21 канал
  CHECK(i18n::plural(str::info_channels_n, 22) == "22 \xd0\xba\xd0\xb0\xd0\xbd\xd0\xb0\xd0\xbb\xd0\xb0");  // 22 канала
  // numbers follow the language's decimal mark
  i18n::set_language(Lang::PtBR); CHECK(fmt_delay(0.3) == "+0,3 s" && i18n::decimal(23.976, 2) == "23,98");
  i18n::set_language(Lang::En);   CHECK(fmt_delay(-1.2) == "-1.2 s" && fmt_delay(0) == "0.0 s" && i18n::decimal(1.5, 2) == "1.5");
  i18n::set_language(Lang::Fr);   CHECK(fmt_delay(0.3) == "+0,3 s");
  i18n::set_language(Lang::ZhHans); CHECK(fmt_delay(0.3) == "+0.3 s");
  // track names: endonyms stay, other languages are named in the interface language
  {
    std::vector<Track> t(4);
    t[0].id = 1; t[0].type = "sub"; t[0].lang = "por";
    t[1].id = 2; t[1].type = "sub"; t[1].lang = "jpn"; t[1].forced = true;
    t[2].id = 3; t[2].type = "sub";
    t[3].id = 4; t[3].type = "sub"; t[3].lang = "eng";
    i18n::set_language(Lang::Fr);
    CHECK(track_label(t[0], t) == "Português" && track_label(t[1], t) == "japonais · forcés" && track_label(t[2], t) == "piste 3");
    i18n::set_language(Lang::En);
    CHECK(track_label(t[1], t) == "Japanese · forced" && track_label(t[3], t) == "English");
  }
  // the file panel: labels and units in the language
  {
    MediaDetails m; m.name = "a.mkv"; m.bytes = 1500000000; m.channels = 6;
    i18n::set_language(Lang::Fr);
    auto f = media_info_fields(m);
    CHECK(f[1].first == "taille" && f[1].second == "1,5 Go");
    i18n::set_language(Lang::En);
    f = media_info_fields(m);
    CHECK(f[1].first == "size" && f[1].second == "1.5 GB");
    bool six = false;
    for (auto& kv : f) if (kv.second == "6 channels") six = true;
    CHECK(six);
  }

  // ── every character has a pixel glyph ──────────────────────────
  int fallback_cps = 0;
  std::set<uint32_t> missing;
  auto scan = [&](std::string_view t) {
    for (size_t i = 0; i < t.size();) {
      uint32_t cp = next_cp(t, i);
      if (cp == '\n') continue;
      if (!has_glyph(cp)) missing.insert(cp);
      if (cp >= 0x2E80) ++fallback_cps;
    }
  };
  for (int li = 0; li < i18n::kLangCount; ++li) {
    scan(i18n::native_name((Lang)li));
    for (int i = 0; i < str::kCount; ++i)
      for (int f = 0; f < i18n::kPluralFormCount; ++f)
        if (auto t = i18n::raw((Lang)li, (str::Id)i, (i18n::PluralForm)f)) scan(t);
  }
  for (uint32_t cp : missing) std::printf("  no glyph for U+%04X\n", cp);
  CHECK(missing.empty());
  CHECK(fallback_cps > 500);  // the Chinese catalog is drawn by the fallback, not skipped
  // the fallback sits on the Omni grid: an ideograph is wider than a Latin letter, and drawing
  // it really paints pixels (a 10 px pixel glyph on the 12-row line box)
  CHECK(text_w("\xe6\x92\xad", 1) == 10 && text_w("H", 1) == 6);  // 播
  Canvas c; c.resize(40, 24);
  int drawn = text(c, "\xe6\x92\xad\xe6\x94\xbe", 0, 0, 2, {255, 255, 255});  // 播放
  int lit = 0;
  for (uint32_t v : c.px) if (v) ++lit;
  CHECK(drawn == 40 && lit > 80);
  // the Omni keeps its own characters (identity), and gains Spanish opening marks
  CHECK(has_glyph(0x00E9) && has_glyph(0x00BF) && has_glyph(0x00A1) && has_glyph(0x00A0));
  CHECK(text_w("\xc2\xbf", 1) == 5);  // the Omni-drawn ¿, not the fallback's
  // wrapping: words at spaces; Chinese between ideographs, never before a closing mark
  CHECK(wrap_lines("one two three", text_w("one two", 1), 1).size() == 2);
  {
    auto l = wrap_lines("\xe9\x80\x9a\xe8\xbf\x87\xe7\xbd\x91\xe7\xbb\x9c\xef\xbc\x8c\xe6\xb2\x99\xe5\x8f\x91", 40, 1);  // 通过网络，沙发
    CHECK(l.size() == 2);
    CHECK(l.size() == 2 && l[1].rfind("\xef\xbc\x8c", 0) != 0);  // "，" never starts a line
  }

  std::printf(failures ? "%d failures\n" : "i18n: Windows UI language order, Simplified/Traditional Chinese, manual priority, "
              "English fallback, persistence across restart/update, 6 complete catalogs (%d keys), placeholders, plurals, "
              "decimals and full glyph coverage passed\n", failures ? failures : (int)str::kCount);
  return failures ? 1 : 0;
}

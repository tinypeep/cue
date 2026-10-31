// Interface language: six complete catalogs compiled into the program, one stable key per text.
//
//   tr(str::home_open_file)                         "open a file" (localized)
//   format(str::update_to, {{"version", "1.2"}})     placeholders by name, never concatenated pieces
//   plural(str::info_channels_n, 6)                  plural forms per language ({n} is filled in)
//
// The language is resolved once (at start, or when the person picks one) and kept in an atomic
// index; a lookup is two array reads, so drawing code may call tr() every frame. Nothing here
// touches Windows or the disk: src/win gives resolve() the Windows UI languages it read once.
// A key missing from a catalog falls back to English (tests/i18n_test.cpp makes sure none is).
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "strings.h"

namespace i18n {

enum class Lang : uint8_t { PtBR = 0, En, Es, Fr, ZhHans, Ru };
constexpr int kLangCount = 6;
constexpr Lang kFallback = Lang::En;
constexpr const char* kAuto = "auto";  // the saved preference "follow Windows"

// CLDR cardinal forms. Only languages that need more than one/other use Few and Many.
enum class PluralForm : uint8_t { Other = 0, One, Few, Many };
constexpr int kPluralFormCount = 4;

const char* tag(Lang l);          // "pt-BR", "en", "es", "fr", "zh-Hans", "ru" (also what is saved)
const char* native_name(Lang l);  // in the language itself, so a wrong pick is easy to undo
bool parse_tag(std::string_view saved, Lang& out);   // one of the tags above (any case)
std::string normalize_pref(std::string_view saved);  // "auto" or a tag; anything else -> "auto"

// One Windows UI language ("fr-CA", "es-419", "zh-Hans-SG", "zh-TW"...) -> a catalog, or false.
// Regional English, Spanish, French and Russian use their catalog; any Portuguese uses pt-BR;
// Chinese is matched only when it is Simplified (zh-Hans, zh-CN, zh-SG, plain zh). Traditional
// Chinese (zh-Hant, zh-TW, zh-HK, zh-MO) is never mapped to Simplified.
bool match_ui_language(std::string_view bcp47, Lang& out);
// 1. a manual choice saved by the person, 2. the first supported language in the Windows UI
// preference order, 3. English.
Lang resolve(std::string_view saved_pref, const std::vector<std::string>& ui_languages);

void set_language(Lang l);
Lang language();
uint32_t revision();  // changes with every set_language(); views re-measure their words then

using Args = std::initializer_list<std::pair<std::string_view, std::string_view>>;
const char* tr(str::Id id);
std::string format(str::Id id, Args args);
std::string plural(str::Id id, long long n, Args args = {});
// Numbers with the language's decimal separator ("1,5" / "1.5"); trailing zeros trimmed.
std::string decimal(double v, int places);

// For tests and tools.
const char* key_name(str::Id id);
const char* raw(Lang l, str::Id id, PluralForm form = PluralForm::Other);  // nullptr when not translated
// The fallback rule itself: the language's own text, else English, else the key's name.
inline const char* pick(const char* own, const char* english, const char* key) {
  return own && *own ? own : (english && *english ? english : key);
}
struct Issue { Lang lang; str::Id id; std::string what; };
std::vector<Issue> audit();  // missing texts, placeholder mismatches, missing plural forms

}  // namespace i18n

using i18n::tr;

// Windows-only check of the language detection with the real API (also runs under Wine):
//   x86_64-w64-mingw32-g++ -std=c++20 -O2 -municode -static -o ui_language_win_test.exe \
//     tests/ui_language_win_test.cpp src/win/ui_language.cpp src/core/i18n.cpp src/core/json.cpp
//   ui_language_win_test.exe [state.json]   prints Windows' display languages and the result
#include <windows.h>
#include <cstdio>
#include <string>
#include "../src/win/ui_language.h"

int wmain(int argc, wchar_t** argv) {
  auto langs = windows_ui_languages();
  std::printf("windows ui languages:");
  for (auto& l : langs) std::printf(" %s", l.c_str());
  std::printf("\n");
  std::string saved = argc > 1 ? saved_language_pref(argv[1]) : std::string(i18n::kAuto);
  i18n::Lang l = apply_ui_language(saved, langs);
  std::printf("saved: %s -> interface: %s\n", saved.c_str(), i18n::tag(l));
  // a second call is served from the start-up read (no new query)
  std::printf("cached: %s\n", windows_ui_languages().size() == langs.size() ? "yes" : "no");
  return 0;
}

#include "ui_language.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "../core/json.h"
#include "../core/file_io.h"

static std::vector<std::string> read_ui_languages() {
  std::vector<std::string> out;
  ULONG count = 0, chars = 0;
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &chars) || chars == 0) return out;
  std::wstring buf(chars, L'\0');
  if (!GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buf.data(), &chars)) return out;
  // a double-NUL-terminated list: "fr-CA\0en-US\0\0"
  for (const wchar_t* p = buf.c_str(); *p; p += wcslen(p) + 1) {
    int n = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, p, -1, s.data(), n, nullptr, nullptr);
    if (!s.empty()) out.push_back(s);
  }
  return out;
}

std::vector<std::string> windows_ui_languages() {
  static const std::vector<std::string> once = read_ui_languages();  // one query per process start
  return once;
}

static bool read_pref(const std::wstring& path, std::string& pref) {
  std::string text;
  if (!read_state_file(path, text)) return false;
  JVal j;
  if (!json_parse(text, j) || j.t != JVal::Obj) return false;
  pref = i18n::normalize_pref(j["prefs"]["language"].str(i18n::kAuto));
  return true;
}

std::string saved_language_pref(const std::wstring& state_json) {
  std::string pref;
  if (read_pref(state_json, pref) || read_pref(state_json + L".bak", pref)) return pref;
  return i18n::kAuto;
}

i18n::Lang apply_ui_language(const std::string& saved_pref, const std::vector<std::string>& ui_languages) {
  i18n::Lang l = i18n::resolve(saved_pref, ui_languages);
  i18n::set_language(l);
  return l;
}

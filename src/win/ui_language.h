// Windows side of the interface language: what Windows says about the person's display
// languages, read once at start (never per frame), and the language saved in state.json.
// Shared by Cue.exe and the installer.
#pragma once
#include <string>
#include <vector>

#include "../core/i18n.h"

// GetUserPreferredUILanguages(MUI_LANGUAGE_NAME), in the person's order: {"fr-CA", "en-US"}.
// Only display languages: never the region, time zone, keyboard or number format.
std::vector<std::string> windows_ui_languages();
// prefs.language from a state.json (or its .bak); "auto" when absent or unreadable.
std::string saved_language_pref(const std::wstring& state_json);
// Resolves (saved choice > Windows UI order > English), makes it current and returns it.
i18n::Lang apply_ui_language(const std::string& saved_pref, const std::vector<std::string>& ui_languages);

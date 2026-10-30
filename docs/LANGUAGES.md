# Languages

Cue's interface, installer, and uninstaller support five languages:

| Tag | Language | Catalog |
|---|---|---|
| `pt-BR` | Português (Brasil), reference text | `i18n/pt-BR.json` |
| `en` | English, per-key fallback | `i18n/en.json` |
| `es` | Español, neutral | `i18n/es.json` |
| `fr` | Français | `i18n/fr.json` |
| `zh-Hans` | 简体中文 | `i18n/zh-Hans.json` |

## Selection

Priority: **saved manual choice → first supported Windows display language → English**. Display languages are read once per launch with `GetUserPreferredUILanguages`; keyboard, timezone, country, and regional format do not determine the UI language.

`en-*`, `es-*` (including `es-419`), and `fr-*` map to their base languages. All `pt-*`, including `pt-PT`, use Brazilian Portuguese. Simplified Chinese accepts `zh-Hans`, its subtags, `zh-CN`, `zh-SG`, and bare `zh`. Traditional Chinese (`zh-Hant`, `zh-TW`, `zh-HK`, `zh-MO`) is not mapped to Simplified Chinese; matching continues, then falls back to English.

Choose **… → language** during playback or beside Recent on the home screen. Labels use native language names. Changes apply immediately, including open license-reader labels, and persist in `prefs.language`. The installer/uninstaller use the same resolution rules and saved preference. Subtitle/audio track selection still uses its separate `slang`/`alang` preferences and Windows regional language.

## Translation boundaries

Cue translates its screens, controls, menus, tips, notifications, file-information panels, dialog titles/filters, errors, and installer. Native Windows dialog buttons, system menus, and permission prompts follow Windows.

Brand names, file names, codecs, attribution, and legal document contents are not translated dynamically. Recognized track-language names use their native spelling where Omni supports it.

## Glyphs

Omni remains the primary 12-row bitmap font. Missing characters use a generated table from **Fusion Pixel Font 10px (zh_hans)** by TakWolf, SIL OFL 1.1. The table aligns to Omni's baseline without resampling and contains 11,303 glyphs, including roughly 10,400 ideographs, kana, Greek, Cyrillic, and additional Latin letters. Hangul is not included; unsupported characters are omitted rather than drawn as boxes.

`¿`, `¡`, and nonbreaking spaces are native project glyphs. Video subtitles still use libass and the selected subtitle font.

## Editing catalogs

1. Edit `i18n/*.json` with identical stable keys. Keep named placeholders such as `{version}`, `{file}`, and `{path}` identical across languages. Plurals use `one`/`other`; Chinese uses `other`. Do not build sentences from translated fragments.
2. Run `make i18n` or `python3 tools/gen_i18n.py`. This validates completeness, placeholders, and plurals, then generates `src/core/strings.h` and `src/core/i18n_catalog.inc`.
3. For new characters outside Omni, run `python3 tools/gen_fallback_glyphs.py` (development dependencies: freetype-py and fontTools).
4. Run `bash tools/test.sh`. `i18n_test.cpp` checks selection, fallback, persistence, catalogs, and glyphs; `i18n_layout_test.cpp` checks layouts across languages, sizes, DPI, fullscreen, and vertical video.
5. Use `tools/preview_i18n.cpp` for visual checks. Other previews accept `CUE_LANG=en|es|fr|zh-Hans`.

Tone: short lowercase phrases without final periods or exclamation marks. French actions use infinitives and U+00A0 before `?`, `:`, `%`; Spanish uses neutral `tú`; Chinese uses full-width punctuation.

To add a language, update `LANGS` in the generator, `i18n::Lang`, `kTags`, `kNative`, plural rules, and `match_ui_language`, then regenerate and test.

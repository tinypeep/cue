# Release checks · Cue 1.1

This is a condensed record of the supplied release review, not a new certification or a record of checks run during documentation editing.

## Previously recorded

- Version 1.1 centralized in `src/core/version.h`; expected release tag `v1.1`.
- Windows x64 cross-build without warnings and 25 portable tests passing.
- Installer, silent install, uninstall, onboarding, playback, subtitle controls, fullscreen, live language switching, saved preferences, and the distributed audio engine exercised in Wine 9.
- Language tests cover resolution, Traditional/Simplified Chinese distinction, fallback, catalogs, placeholders, plurals, persistence, and glyph coverage. Layout tests recorded roughly 27,000 checks over five languages, four sizes, DPI/window combinations, fullscreen, and vertical video.
- The earlier engine review checked API exports, DLL imports, hashes, codecs/filters, source archives, build recipes, and notices. The author confirmed engine build 1.1.4 working on Windows.
- The supplied code review reported no credentials, tokens, personal paths, or logs in the release source. `.gitignore` excludes build output, logs, and local state.
- Asset origin declarations and licenses are recorded in [ASSETS.md](ASSETS.md); engine source details are in [SOURCES.md](../third_party/libmpv/SOURCES.md).

## Before publishing

1. Keep `tinypeep/cue` public so unauthenticated update checks work.
2. Attach `Cue-1.1-Setup.exe` and the matching `Cue-1.1-Engine-Source.zip` (the 1.0 engine package, renamed) to `v1.1`; preserve notices and clear source links. GitHub provides the application source archive for the tag.
3. Verify installed `sources/README.txt` points to that release. The current installer links to source ZIPs rather than bundling them.
4. Test on native Windows: fullscreen while playing/paused, single/double-click input, resize/DPI, multi-entry display-language preferences, Narrator, the native license reader, “Open with”, updates, and uninstall.

## Limits

Wine is not native Windows. Cross-builds and portable tests do not measure actual startup latency, GPU cost, compositor behavior, or accessibility. Earlier beta notes sometimes describe superseded behavior; use current source and release instructions.

The executable is unsigned and may trigger Windows SmartScreen or local policy warnings. The review does not establish antivirus clearance, trademark availability, codec patent coverage, or complete asset originality.

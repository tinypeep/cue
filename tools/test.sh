#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests/obj
# the translation catalogs: complete, same placeholders, plural forms (also checked again in C++)
if command -v python3 >/dev/null; then python3 tools/gen_i18n.py --check; python3 tools/gen_remote_page.py --check; fi
# Under MSYS2/MinGW the portable sources take their native Windows branches, which use the
# system's CNG and DPAPI. Elsewhere (the CI's Linux) these libraries do not exist.
libs=()
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) libs=(-lbcrypt -lcrypt32) ;;
esac
objects=()
for source in src/core/*.cpp src/ui/*.cpp installer/setup_ui.cpp; do
  object="build/tests/obj/$(basename "${source%.cpp}").o"
  g++ -std=c++20 -O2 -Wall -Wextra -c "$source" -o "$object"
  objects+=("$object")
done
for source in tests/*_test.cpp; do
  [[ "$source" == tests/*_engine_test.cpp ]] && continue    # Windows only (real DLL)
  [[ "$source" == tests/ui_language_win_test.cpp ]] && continue  # Windows only (real language API)
  [[ "$source" == tests/recent_thumbs_win_test.cpp ]] && continue  # Windows only (WIC, the home's pictures on disk)
  executable="build/tests/$(basename "${source%.cpp}")"
  g++ -std=c++20 -O2 "$source" "${objects[@]}" ${libs[@]+"${libs[@]}"} -o "$executable"
  "$executable"
done

# Headless transport methods with a fake engine/timer; no Win32 or renderer required.
if command -v python3 >/dev/null; then python3 tests/hidden_playback_test.py; fi

# Deterministic production JavaScript transport regressions (no browser dependency).
if command -v node >/dev/null; then node tests/remote_lifecycle_test.cjs; node tests/remote_fetch_test.cjs; node tests/remote_auth_page_test.cjs; fi

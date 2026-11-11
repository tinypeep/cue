#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests/obj
# the translation catalogs: complete, same placeholders, plural forms (also checked again in C++)
if command -v python3 >/dev/null; then python3 tools/gen_i18n.py --check; python3 tools/gen_remote_page.py --check; fi
objects=()
for source in src/core/*.cpp src/ui/*.cpp installer/setup_ui.cpp; do
  object="build/tests/obj/$(basename "${source%.cpp}").o"
  g++ -std=c++20 -O2 -Wall -Wextra -c "$source" -o "$object"
  objects+=("$object")
done
# An object whose source is gone would still be linked in, and the program
# would fail with the same symbol twice. Remove what no longer has a source.
for object in build/tests/obj/*.o; do
  name="$(basename "${object%.o}")"
  [[ -f "src/core/$name.cpp" || -f "src/ui/$name.cpp" || -f "installer/$name.cpp" ]] || rm -f "$object"
done
# the credential is encrypted with DPAPI on Windows (Crypt32); the Linux build
# does not need the library at all
CRYPT32=
if [[ "$OSTYPE" == msys* || "$OSTYPE" == cygwin* || "$OSTYPE" == win* ]]; then CRYPT32=-lcrypt32; fi

for source in tests/*_test.cpp; do
  [[ "$source" == tests/*_engine_test.cpp ]] && continue    # Windows only (real DLL)
  [[ "$source" == tests/ui_language_win_test.cpp ]] && continue  # Windows only (real language API)
  executable="build/tests/$(basename "${source%.cpp}")"
  g++ -std=c++20 -O2 "$source" "${objects[@]}" $CRYPT32 -o "$executable"
  "$executable"
done

# Headless transport methods with a fake engine/timer; no Win32 or renderer required.
if command -v python3 >/dev/null; then python3 tests/hidden_playback_test.py; fi

# Deterministic production JavaScript transport regressions (no browser dependency).
if command -v node >/dev/null; then node tests/remote_lifecycle_test.cjs; node tests/remote_fetch_test.cjs; fi

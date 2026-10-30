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
for source in tests/*_test.cpp; do
  [[ "$source" == tests/*_engine_test.cpp ]] && continue    # Windows only (real DLL)
  [[ "$source" == tests/ui_language_win_test.cpp ]] && continue  # Windows only (real language API)
  executable="build/tests/$(basename "${source%.cpp}")"
  g++ -std=c++20 -O2 "$source" "${objects[@]}" -o "$executable"
  "$executable"
done

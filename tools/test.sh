#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/tests/obj
objects=()
for source in src/core/*.cpp src/ui/*.cpp; do
  object="build/tests/obj/$(basename "${source%.cpp}").o"
  g++ -std=c++20 -O2 -Wall -Wextra -c "$source" -o "$object"
  objects+=("$object")
done
for source in tests/*_test.cpp; do
  [[ "$source" == tests/audio_engine_test.cpp ]] && continue
  executable="build/tests/$(basename "${source%.cpp}")"
  g++ -std=c++20 -O2 "$source" "${objects[@]}" -o "$executable"
  "$executable"
done

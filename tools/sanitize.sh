#!/usr/bin/env bash
# Every portable test under AddressSanitizer + LeakSanitizer + UBSan (Linux, g++). Slow (about an
# hour on two cores): run before a release. Prints FAILED lines and a summary; logs in build/asan/.
set -u
cd "$(dirname "$0")/.."
out=build/asan; mkdir -p $out/obj $out/bin
F="-std=c++20 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined"
objs=()
for src in src/core/*.cpp src/ui/*.cpp installer/setup_ui.cpp; do
  o=$out/obj/$(basename "${src%.cpp}").o
  g++ $F -c "$src" -o "$o" || echo "FAILED compile $src"
  objs+=("$o")
done
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=1} UBSAN_OPTIONS=${UBSAN_OPTIONS:-print_stacktrace=1}
bad=0
CRYPT32=
if [[ "$OSTYPE" == msys* || "$OSTYPE" == cygwin* || "$OSTYPE" == win* ]]; then CRYPT32=-lcrypt32; fi
for t in tests/*_test.cpp; do
  [[ $t == tests/*_engine_test.cpp || $t == tests/ui_language_win_test.cpp ]] && continue
  n=$(basename "${t%.cpp}")
  g++ $F "$t" "${objs[@]}" $CRYPT32 -o "$out/bin/$n" || { echo "FAILED compile $n"; bad=1; continue; }
  if ! timeout 1800 "$out/bin/$n" > "$out/$n.log" 2>&1 || grep -q "Sanitizer\|runtime error" "$out/$n.log"; then
    echo "FAILED $n: $(grep -m2 -E 'ERROR|runtime error|Assertion' "$out/$n.log" | tr '\n' ' ')"; bad=1
  fi
done
[[ $bad == 0 ]] && echo "sanitizers: every test clean" || exit 1

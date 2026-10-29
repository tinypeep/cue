#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PREFIX="$ROOT/prefix"
JOBS="${JOBS:-6}"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
if [[ ! -f "$ROOT/build/mpv/build.ninja" ]]; then
 meson setup "$ROOT/build/mpv" "$ROOT/src/mpv" --cross-file "$ROOT/cross.ini" --prefix "$PREFIX" --libdir lib --buildtype release --default-library shared --auto-features disabled -Dcplayer=false -Dlibmpv=true -Dgpl=false -Dbuild-date=false -Dlua=disabled -Dwasapi=enabled -Dd3d11=enabled -Dshaderc=enabled -Dspirv-cross=enabled -Dd3d-hwaccel=enabled -Dwin32-threads=enabled -Dvector=enabled -Dzlib=enabled -Dgl=disabled -Dprefer_static=true
fi
meson compile -C "$ROOT/build/mpv" -j "$JOBS"
meson install -C "$ROOT/build/mpv"
mkdir -p "$ROOT/output"
cp "$ROOT/build/mpv/libmpv-2.dll" "$ROOT/output/"
x86_64-w64-mingw32-strip "$ROOT/output/libmpv-2.dll"
sha256sum "$ROOT/output/libmpv-2.dll"

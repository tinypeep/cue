#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PREFIX="$ROOT/prefix"
JOBS="${JOBS:-6}"
mkdir -p "$PREFIX" "$ROOT/build" "$ROOT/logs"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
export CFLAGS="-O2 -DNDEBUG"
export CXXFLAGS="-O2 -DNDEBUG"
cat > "$ROOT/cross.ini" <<EOF
[binaries]
c = 'x86_64-w64-mingw32-gcc'
cpp = 'x86_64-w64-mingw32-g++'
ar = 'x86_64-w64-mingw32-gcc-ar'
strip = 'x86_64-w64-mingw32-strip'
windres = 'x86_64-w64-mingw32-windres'
dlltool = 'x86_64-w64-mingw32-dlltool'
pkg-config = 'pkg-config'
[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
[properties]
needs_exe_wrapper = true
[built-in options]
c_args = ['-O2', '-DNDEBUG']
cpp_args = ['-O2', '-DNDEBUG']
c_link_args = ['-static']
cpp_link_args = ['-static']
EOF
cat > "$ROOT/cross.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH "$PREFIX")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
mbuild() {
 local name="$1"; shift
 if [[ ! -f "$ROOT/build/$name/build.ninja" ]]; then
  meson setup "$ROOT/build/$name" "$ROOT/src/$name" --cross-file "$ROOT/cross.ini" --prefix "$PREFIX" --libdir lib --buildtype release --default-library static --auto-features disabled "$@"
 fi
 meson compile -C "$ROOT/build/$name" -j "$JOBS"
 meson install -C "$ROOT/build/$name"
}
step() { echo "START $1"; "$@"; echo "DONE $1"; }
if [[ ! -f "$PREFIX/lib/libz.a" ]]; then
 cmake -S "$ROOT/src/zlib" -B "$ROOT/build/zlib" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cross.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF
 cmake --build "$ROOT/build/zlib" -j "$JOBS"
 cmake --install "$ROOT/build/zlib"
fi
if [[ -f "$PREFIX/lib/libzs.a" && ! -f "$PREFIX/lib/libz.a" ]]; then cp "$PREFIX/lib/libzs.a" "$PREFIX/lib/libz.a"; fi
if [[ ! -f "$PREFIX/lib/libfribidi.a" ]]; then step mbuild fribidi -Ddocs=false -Dbin=false -Dtests=false; fi
if [[ ! -f "$PREFIX/lib/libfreetype.a" ]]; then
 cmake -S "$ROOT/src/freetype" -B "$ROOT/build/freetype" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cross.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
 cmake --build "$ROOT/build/freetype" -j "$JOBS"
 cmake --install "$ROOT/build/freetype"
fi
if [[ ! -f "$PREFIX/lib/libharfbuzz.a" ]]; then step mbuild harfbuzz -Dtests=disabled -Dutilities=disabled -Ddocs=disabled -Dsubset=disabled -Dintrospection=disabled; fi
if [[ ! -f "$PREFIX/lib/libass.a" ]]; then step mbuild libass -Ddirectwrite=enabled -Dasm=enabled; fi
if [[ ! -f "$PREFIX/lib/libdav1d.a" ]]; then step mbuild dav1d -Denable_tools=false -Denable_tests=false; fi
if [[ ! -f "$PREFIX/lib/libavcodec.a" ]]; then
 mkdir -p "$ROOT/build/ffmpeg"
 cd "$ROOT/build/ffmpeg"
 "$ROOT/src/ffmpeg/configure" --prefix="$PREFIX" --pkg-config=pkg-config --pkg-config-flags=--static --cross-prefix=x86_64-w64-mingw32- --arch=x86_64 --target-os=mingw32 --enable-cross-compile --disable-autodetect --enable-static --disable-shared --disable-programs --disable-doc --disable-debug --disable-encoders --enable-encoder=png --enable-encoder=rawvideo --disable-muxers --enable-muxer=image2 --enable-muxer=image2pipe --disable-avdevice --enable-libdav1d --enable-zlib --enable-d3d11va --enable-dxva2 --enable-schannel --disable-pthreads --enable-w32threads --extra-cflags="-O2 -I$PREFIX/include" --extra-ldflags="-L$PREFIX/lib" --extra-libs="-lws2_32 -lbcrypt -lsecur32 -lcrypt32" 
 make -j "$JOBS"
 make install
fi
# Rendering libraries and final libmpv are built by build-renderer.sh.

#!/usr/bin/env bash
set -euo pipefail
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PREFIX="$ROOT/prefix"
JOBS="${JOBS:-3}"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
for dep in glslang spirv-tools spirv-headers; do
 ln -sfn "$ROOT/src/$dep" "$ROOT/src/shaderc/third_party/$dep"
done
ln -sfn "$ROOT/src/spirv-headers" "$ROOT/src/spirv-tools/external/spirv-headers"
if [[ ! -f "$PREFIX/lib/libshaderc_combined.a" ]]; then
 cmake -S "$ROOT/src/shaderc" -B "$ROOT/build/shaderc" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cross.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_EXECUTABLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON -DSHADERC_ENABLE_WERROR_COMPILE=OFF -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DENABLE_GLSLANG_BINARIES=OFF -DBUILD_SHARED_LIBS=OFF -DSPIRV_WERROR=OFF
 cmake --build "$ROOT/build/shaderc" --target shaderc_combined -j "$JOBS"
 cp "$ROOT/build/shaderc/libshaderc/libshaderc_combined.a" "$PREFIX/lib/"
 cp -r "$ROOT/src/shaderc/libshaderc/include/shaderc" "$PREFIX/include/"
fi
cat > "$PREFIX/lib/pkgconfig/shaderc.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: shaderc
Description: Shaderc combined static library
Version: 2026.5
Libs: -L\${libdir} -lshaderc_combined
Libs.private: -lstdc++ -lwinpthread
Cflags: -I\${includedir}
EOF
if [[ ! -f "$PREFIX/lib/libspirv-cross-c.a" ]]; then
 cmake -S "$ROOT/src/spirv-cross" -B "$ROOT/build/spirv-cross" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$ROOT/cross.cmake" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_CLI=OFF -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_STATIC=ON -DSPIRV_CROSS_SHARED=OFF
 cmake --build "$ROOT/build/spirv-cross" -j "$JOBS"
 cmake --install "$ROOT/build/spirv-cross"
fi
cat > "$PREFIX/lib/pkgconfig/spirv-cross-c-shared.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include/spirv_cross
Name: spirv-cross-c-shared
Description: SPIRV-Cross C API (static linkage)
Version: 0.68.0
Libs: -L\${libdir} -lspirv-cross-c -lspirv-cross-glsl -lspirv-cross-hlsl -lspirv-cross-msl -lspirv-cross-cpp -lspirv-cross-reflect -lspirv-cross-util -lspirv-cross-core
Libs.private: -lstdc++
Cflags: -I\${includedir}
EOF
cp -r "$ROOT/src/vulkan-headers/include/." "$PREFIX/include/"
ln -sfn "$ROOT/src/fast_float/include" "$ROOT/src/libplacebo/3rdparty/fast_float/include"
if [[ ! -f "$PREFIX/lib/libplacebo.a" ]]; then
 meson setup "$ROOT/build/libplacebo" "$ROOT/src/libplacebo" --cross-file "$ROOT/cross.ini" --prefix "$PREFIX" --libdir lib --buildtype release --default-library static --auto-features disabled -Dd3d11=enabled -Dshaderc=enabled -Dvulkan=disabled -Dopengl=disabled -Ddemos=false -Dtests=false
 meson compile -C "$ROOT/build/libplacebo" -j "$JOBS"
 meson install -C "$ROOT/build/libplacebo"
fi

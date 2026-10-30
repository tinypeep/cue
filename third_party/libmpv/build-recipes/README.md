# Playback engine sources · build 1.1.4

This package contains the sources actually used to build `libmpv-2.dll`, its incorporated dependencies, build recipes, and license notices. No sample media or personal data is included.

## Build

Environment: Linux x86_64, GCC/MinGW-w64 POSIX (GCC 13 was used), Python 3.12, GNU Make, pkgconf, and NASM 2.16 or newer. Python build tools are pinned in `requirements-build.txt`; general-purpose build tools are not bundled.

On Ubuntu/Debian, install `g++-mingw-w64-x86-64`, `mingw-w64-tools`, `make`, `nasm`, `pkgconf`, `python3`, and `python3-venv`. Create/activate a virtual environment, then run:

```sh
python3 -m pip install -r requirements-build.txt
bash build-all.sh
```

Sources in `archives/` are checked by SHA256 before extraction; no library source downloads occur during the build. Output: `output/libmpv-2.dll`. Toolchain differences may change the binary hash; bit-for-bit reproduction with different tools is not promised. Sources/configuration correspond to `binary-manifest.json`.

## Configuration

- mpv in LGPL mode, without cplayer or GPL-only parts.
- FFmpeg without GPL/nonfree: native decoders, demuxers, filters, PNG/rawvideo screenshot encoders, dav1d for AV1, and zlib.
- libass, FreeType, HarfBuzz, and FriBidi for subtitles/fonts.
- D3D11 rendering, Shaderc, SPIRV-Cross, libplacebo, D3D11VA, and WASAPI.
- Libraries statically incorporated into the replaceable DLL.
- No optional DVD/Blu-ray backends, VapourSynth, Vulkan, Lua/JavaScript, or external video encoders.

Recipes create some pkg-config names for static linking; zlib uses a `libz.a` copy of `libzs.a`. No functional playback patches were applied. Shaderc and FastFloat dependency directory links are reconstructed by the recipes.

## Licenses and distribution

The combined engine uses LGPL-3.0-or-later, selecting permitted later LGPL versions for Apache-2.0 compatibility. Individual notices remain in `notices/` and original sources. FreeType uses FTL; FastFloat uses MIT. GCC/libstdc++ and MinGW retain their runtime permissions/exceptions. Windows system libraries are not supplied.

Portions of this software are copyright © The FreeType Project
(www.freetype.org). All rights reserved.

Publish this source ZIP beside the Windows package of the **same version** and Cue's source code. Preserve archives, manifests, notices, and clear source access; do not substitute a moving “latest” source link. Users may replace the DLL with an API-compatible modified build.

This package covers the corresponding engine sources, not the authorship of Cue Omni or project artwork.

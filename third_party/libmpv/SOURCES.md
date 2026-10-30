# Corresponding playback engine sources · build 1.1.4

The engine was built locally from the **16 source archives** identified in `sources.json`. Original archives, notices, licenses, and the actual build recipes are provided in **Cue-1.1-Engine-Source.zip**, named for the Cue release (the same engine build as 1.0). This is an archived source package, not a moving upstream “latest” link.

| Item | Value |
|---|---|
| Binary | `libmpv-2.dll` |
| DLL SHA256 | `26fa28434e30aa319a8ab39fba76dbc4a84757b89349c1fc69b5e55ea6c02bca` |
| Source ZIP SHA256 | `6ce73f8f9b7209e390f45c16f6ec0fbdfab383eddb7a7726656f1f24b4fcb13e` |
| Combined distribution | LGPL-3.0-or-later; individual licenses preserved |

Components: mpv, FFmpeg, libplacebo, libass, HarfBuzz, FriBidi, FreeType, dav1d, zlib, Shaderc, glslang, SPIRV-Tools, SPIRV-Headers, SPIRV-Cross, Vulkan-Headers, and FastFloat. Build tools are documented in the source ZIP; GCC/libstdc++/MinGW notices and runtime exceptions are in `notices/`.

## Distribution

Publish the Windows package, Cue source, and matching engine source ZIP together. Keep a clear source link beside the application download, preserve notices, and allow replacement of the DLL with API-compatible modified versions.

For Cue 1.1, the installer links to these large archives rather than bundling them:
[release v1.1](https://github.com/tinypeep/cue/releases/tag/v1.1) → GitHub's **Source code (zip)** and **Cue-1.1-Engine-Source.zip**. Installed `sources/README.txt` contains that address; the license reader can open the folder.

## Recorded verification

The supplied engine review checked SHA256 hashes, API exports, DLL imports, codec/filter configuration, compilation, and portable tests. Import tables showed no external runtime dependencies beyond Windows DLLs. The author confirmed build 1.1.4 working on Windows. That local engine review was static/portable; Wine was unavailable in that review environment. Later app-level Wine checks are recorded separately in the release notes.

This record covers the engine, not the originality of Omni visual assets.

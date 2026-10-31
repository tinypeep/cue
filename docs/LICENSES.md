# Licenses · Cue 1.1.1

Cue's own code uses **GPL-3.0-or-later**. The full terms are in `LICENSE`; [COPYRIGHT.md](../COPYRIGHT.md) identifies authorship and third-party boundaries. Software is provided without warranty under the applicable terms.

## Playback engine

The distributed `libmpv-2.dll` was built from archived sources, excluding mpv's GPL-only parts and without enabling FFmpeg GPL/nonfree options. The combined engine distribution uses **LGPL-3.0-or-later**, selecting permitted later LGPL versions for Apache-2.0 compatibility. Individual component licenses/notices remain in `third_party/libmpv/notices/`; FreeType uses FTL and FastFloat uses MIT. The old nonfree-marked `mpv.dll` is not included.

Corresponding sources for all 16 components, versions, SHA256 hashes, notices, and build recipes are provided in **Cue-1.1.1-Engine-Source.zip** (engine build 1.1.4, unchanged since Cue 1.0), beside the installer and GitHub-generated Cue source archive on the [v1.1.1 release page](https://github.com/tinypeep/cue/releases/tag/v1.1.1). See [SOURCES.md](../third_party/libmpv/SOURCES.md) and `third_party/libmpv/manifest.json`.

Keep the DLL replaceable with API-compatible modified versions. Preserve notices and source access when redistributing. The installed `sources/README.txt` links to the release; the in-app license reader provides full texts and a source-folder button.

## Fonts and visual assets

| Component | Attribution / license |
|---|---|
| Cue Clean (Inter 500) | The Inter Project Authors; SIL OFL 1.1 |
| Cue Readable (Atkinson Hyperlegible 400) | Braille Institute of America; SIL OFL 1.1 |
| Cue Omni | Created with Claude for the project, as declared by the author; SIL OFL 1.1 |
| Custom Omni SVGs, glyphs, sprites | Same declared origin; GPL-3.0-or-later |
| Fusion Pixel fallback table | Fusion Pixel Font 10px (zh_hans), 2024.05.12; Copyright (c) 2022 TakWolf; SIL OFL 1.1; Reserved Font Name “Fusion Pixel” |

OFL fonts remain under OFL; Cue's GPL does not replace their terms. Notices are included. Fusion Pixel's source license is in `third_party/fusion-pixel-font/OFL.txt` (`licenses/FusionPixel-OFL.txt` in the Windows package); the pixel table is not distributed as a font under the reserved name. See [asset provenance](ASSETS.md) and [language glyphs](LANGUAGES.md).

## Phone remote

- QR encoder (`src/core/qr.cpp`): written for Cue following the structure of Project Nayuki's QR Code generator (Copyright (c) Project Nayuki, MIT License, https://www.nayuki.io/page/qr-code-generator-library), and checked against it; GPL-3.0-or-later as part of Cue. The MIT notice: *Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. The Software is provided "as is", without warranty of any kind.*
- The remote's page, server, sound, and pixel art: Cue's own code, GPL-3.0-or-later.

## Installer

- `installer/lzma/`: Igor Pavlov's LZMA SDK decoder, public domain; used only by the installer.
- Installer and packaging code (`installer/`, `tools/make_setup.py`): GPL-3.0-or-later.

This record does not establish complete asset originality, codec patent coverage, or trademark rights. Preserve all applicable notices and conditions.

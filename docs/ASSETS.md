# Visual assets: origin and licenses

Gabriel Benzoni states that Cue Omni and the custom Omni SVGs, glyphs, and sprites were created with Claude for this project. This records the author's declaration, not an independent originality assessment. AI use alone is not treated as evidence of infringement; licenses cover rights the distributor can grant and do not override pre-existing third-party rights. The supplied review identified no concrete evidence of prohibited copying.

| Asset | License / provenance |
|---|---|
| Custom code, SVGs, glyphs, sprites | GPL-3.0-or-later |
| Cue Omni | SIL OFL 1.1; no Reserved Font Name; `assets/fonts/CueOmni-OFL.txt` |
| Cue Clean / Inter | Original SIL OFL 1.1 and attribution preserved |
| Cue Readable / Atkinson Hyperlegible | Original SIL OFL 1.1 and attribution preserved |
| Fusion Pixel fallback glyphs | Fusion Pixel Font 10px (zh_hans), 2024.05.12, Copyright (c) 2022 TakWolf, SIL OFL 1.1; Reserved Font Name “Fusion Pixel” |

Cue Omni's outlines, family, and appearance were preserved when copyright/license metadata and the license text were added. Font notices are copied to `licenses/` in the Windows package.

## Supplied artwork

- `res/cue.ico`: supplied by the author for 1.1.5; all seven resolutions preserved.
- Six action SVGs in `assets/icons/`: supplied for 1.1.7 (save frame, back, information, open, close video, About). Originals are preserved; `tools/gen_action_icons.py` compiles the pixel grid without runtime SVG dependencies.
- `assets/icons/skip-15s-sheet.svg`: supplied for 1.1.8; retained after compact icons returned in 1.1.9.

These assets follow the custom project asset license. The saved-frame sound is synthesized in code, without a third-party recording.

## Language glyphs

`tools/gen_fallback_glyphs.py` converts a subset of Fusion Pixel Font into a pixel table for characters absent from Omni. Source and OFL text remain in `third_party/fusion-pixel-font/`; the Windows package includes `licenses/FusionPixel-OFL.txt`. The compiled table is not distributed as a font named “Fusion Pixel”. Existing Omni glyphs are unchanged. `¿`, `¡`, nonbreaking spaces, and the 7×7 language globe were drawn in the project's visual grammar and follow the custom asset license.

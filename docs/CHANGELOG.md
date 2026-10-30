# Changelog

## 1.0

First public release. Earlier 1.1.x numbers were internal/beta builds.

- Five languages across the app, installer, and uninstaller: Brazilian Portuguese, English, Spanish, French, and Simplified Chinese. Automatic Windows display-language matching, saved manual selection, live switching, and pixel fallback glyphs.
- Native installer, shortcuts, Windows uninstall integration, and GitHub update checks every 15 days with SHA-256 download verification.
- Four-screen onboarding for Cue, ambient lighting, interface size, and upcoming features.
- Four adaptive interface sizes (small, medium, large, larger), with medium selected for fresh installations; consistent width/DPI scaling and a stacked vertical-video layout.
- Minimal playback controls with volume, speed, ±15-second seek, repeat, subtitles, conditional audio-track selection, and fullscreen.
- Two control styles, scene-driven ambient lighting, timeline previews, saved positions, and audio-only playback with a reactive waveform.
- Draggable subtitles with font, style, opacity, and synchronization settings; playback pauses during repositioning and resumes when appropriate.
- Shader-driven pixel fullscreen transitions, native window corners, and updated menus and input handling.

See the [user guide](GUIDE.md) for controls and the [release process](UPDATES.md) for packaging.

## Beta history

Condensed development history. Entries describe their respective builds; later versions may replace these behaviors or defaults.

| Build | Main changes |
|---|---|
| 1.1.21 beta | Subtler frameless blur cloud; reversible subtitle/control transitions; shared 60 Hz animation/upload deadlines; buffer-in-flight and reduced-motion protections. Native frame rate still requires Windows validation. |
| 1.1.20 beta | Independent subtitle return animation; window aspect ratio follows each new file; small UI temporarily became the default; revised resize targets; two-pass Gaussian blur. |
| 1.1.19 beta | Uniform white frameless controls with local blur cloud; reversible appearance transition; revised resize hit areas; reduced Win32 calls and preview sampling work. |
| 1.1.17 beta | Experimental local scene-contrast colors for all frameless controls, including timeline segments, with smoothing and hysteresis. |
| 1.1.16 beta | Scene-dominant frameless control color; removed extra luminance analysis; native border/corner adjustments. |
| 1.1.15 beta | Added saved control-background preference and frameless layout, using the existing scene analyzer. |
| 1.1.14 beta | Taller volume track/handle and larger window-button targets/icons. |
| 1.1.13 beta | Horizontal home screen without media; corrected vertical/horizontal volume proportions. |
| 1.1.12 beta | Vertical volume, speed, and More returned to the top; timeline moved to the bottom; removed permanent circular play background. |
| 1.1.11 beta | Intermediate vertical hierarchy and compact secondary controls. |
| 1.1.10 beta | Three-row vertical controls; horizontal layout and compact ±15-second icons retained. |
| 1.1.9 beta | “Cue beta” branding and Windows prerelease metadata. |
| 1.1.9 | Restored compact seek icons while retaining 15-second actions and tooltips. |
| 1.1.8 | Fullscreen/window-state fixes, vertical controls, C shortcut, playback feedback, synthesized saved-frame sound, Pictures/Cue/Frames migration, elapsed/remaining time toggle, and ambient lighting enabled for new preferences. |
| 1.1.7 | Six supplied action SVGs compiled into pixel assets; .tinypeep branding; compatibility identifiers retained. |
| 1.1.6 | File-information panel, in-app license reader and source access; no additional metadata decoder. Earlier packages bundled source ZIPs. |
| 1.1.5 | Supplied multi-resolution application icon; engine and corresponding sources remained build 1.1.4. |

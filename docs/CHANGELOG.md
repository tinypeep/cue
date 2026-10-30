# Changelog

## 1.1

The phone remote, and more.

### Highlights

- **Phone remote.** Open **… → remote control** (or **··· → remote control** on the home screen) and scan the QR code: your phone becomes a remote in its browser, nothing to install. A throwback click wheel moves through the film with a preview of where it will land; the center switches it to volume. Subtitles (on/off, timing, size, position), audio track and a sleep timer are in its menu. Browse your Videos and Downloads (or any folder you add) as a grid with thumbnails, and series move on to the next episode on their own. Phones that paired once reconnect by themselves.
- **Multiple audio tracks.** Recordings with separate tracks (like OBS: game, microphone and voice chat) can play every track at once, mixed at their own levels. Cue does it on its own when the tracks are clearly parts of one recording; anywhere else, the audio menu's **multiple tracks** lets you tick the ones you want. Remembered per file.
- **Subtitles back to default.** If a subtitle style or position gets out of hand, **CC → restore default** brings yours back in one click, and a second click undoes it.
- **Always on top.** A pin beside the window buttons (or **Ctrl+T**) keeps Cue above other windows while you work.

### Fixes and improvements

- A short **what's new** screen after updating, and a **what's new** line on the home until you close it.
- Interface sizes reworked: four even steps that always look different from each other, following Windows' scale up to 5K and 8K screens. **Ctrl + mouse wheel** changes the size, like **Ctrl +** / **Ctrl −**.
- The control bar lands exactly where its corner marks show when you drag it (a short drag no longer snaps back to the default place).
- Changing audio tracks keeps you at the same moment of the film, with a black-and-white transition while the sound switches; subtitles always come back afterwards.
- The pairing screen opens with its own animation and a soft sound; the film waits behind it, paused and in black and white.
- Lighter when idle: with the remote on and no phone connected, Cue does almost no extra work.

## 1.0

First public release. The 1.1.x beta numbers below were internal builds before it; they are older than 1.1.

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

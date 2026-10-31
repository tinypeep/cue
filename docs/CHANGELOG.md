# Changelog

## 1.1.1

### Final 1.1 polish

- The next file lives on the card at the end of the video, on the phone and on **N** (or the keyboard's media "next" key). The duplicate "Next episode" / "Up next" entry in the ⋯ menu is gone.
- Phone folders: the arrow beside the title goes one folder up and only appears when there is a folder above; a separate × leaves the folders from anywhere. With several shared folders, the top of one goes back to their list.
- Phone menus: an arrow beside each title goes one step back (a value to the subtitles list, the subtitles to the menu, the menu to the player).
- The phone remote no longer zooms on a quick double tap; fast taps on + and − all count.
- Embedded subtitles stay in sync at every speed: they are timed by the engine on the video's own clock (measured at 1×, 2× and 3× with zero drift).

### Next-file preview and series boundaries

- Show the destination filename and a small asynchronous thumbnail in the player's next card.
- Match normalized series title as well as season/episode; never automatically continue a different or unidentified show.
- Keep unrelated files as manual Up next suggestions, with matching labels in the player and phone.


### Episode handoff fix

- Preserve the next filename when opening it; `open` owns its argument before resetting discovery state.
- Capture automatic destinations across EOF/END_FILE/idle events, consume them once, and clear cancelled/stale requests.
- Reject empty open requests without disturbing playback.

### Remote layout and playback follow-up

- Enlarge portrait pairing independently of the playback dock scale; wrap folder actions.
- Prioritize Escape for the pairing overlay, preserve early cancellation and return keyboard focus on close.
- Add an exit timer fallback and avoid synchronous drag handoff to the player window.
- Keep system keyboard volume/mute separate from Cue's internal volume.
- Add Next episode to the player menu and a compact, cancellable countdown in the last 10 playback seconds; automatically continue numbered episodes at EOF, respecting loop and the end-of-episode sleep timer.
- Make the phone timeline fill follow the dragged position; dragging the active wheel dot seeks continuously while other wheel gestures retain detents.

All remote, memory and disk improvements are consolidated in this release.

### Stability and input handling

- Move preview pipe writes off the UI thread into a bounded, replaceable command queue.
- Give preview workers shared ownership of their process/pipe state; release stale posted frames on shutdown.
- Restrict child handle inheritance and require successful kill-on-close job assignment.
- Bound remote lifecycle events and keep disconnects from delaying remote shutdown.
- Make file-worker cleanup exception safe and acquire Winsock ownership before thread launch.
- Bound settings reads/JSON nodes and validate numeric conversions before casting; reject implausible saved window geometry.
- See [Additional stability review](STABILITY.md) for validation and remaining limits.

### Disk and RAM

- Generate timeline previews on demand instead of scanning distant timestamps in the background after a hover.
- Bound timeline pixel storage to 12 MiB / 256 entries with LRU eviction and capacity-based accounting.
- Keep colour-only samples transient while preserving ambient-light timing and sampling quality.
- Skip unchanged position/settings writes, preserve recovery-copy refresh and reuse identical blur-shader files.
- Exclude temporary build/sync leftovers from the installer.
- Add cache/real-filesystem I/O regressions and an optional read-only Windows resource probe. See [Disk and RAM review](RESOURCES.md) for scope and tradeoffs.

### Memory lifecycle

- Fix DIB/DC ownership when pairing/news windows resize; restore selection before deleting the bitmap.
- Release full-window scene surfaces and canvases after closing; destroy overlay windows and graphics resources explicitly.
- Give the display-pacing worker independent shared state so a late DwmFlush cannot access a destroyed owner or closed event.
- Stop caching unsupported Unicode code points. Known fallback glyphs remain cached and stable.
- Release the phone file browser’s DOM/blob URLs and abort pending requests on close; release timeline preview URLs when scrubbing ends.
- Add counted resource/allocation regressions, a Chromium memory stress test and a native Windows GDI-counter test. See [Memory review](MEMORY.md).

### Phone UI and security

- Reflow remote pairing for portrait desktop windows; wrap shared-folder chips and fit text.
- Adapt the phone remote to available width/height, browser bars and safe-area insets; keep primary targets at least 44 CSS pixels.
- Fix the folder bitmap and center icon clipping.
- Harden pairing: six digits, two-minute validity, one use, CSPRNG-only 256-bit remembered tokens, and revocation when turning the remote off. Existing phones pair again once.
- Validate Host/Origin and WebSocket handshakes; require first-message authentication and HTTP Bearer headers. Add strict input validation, script nonces, CSP and resource limits.
- Restrict file access to enabled local roots; reject special Windows paths and descendant reparse points.
- Limit listing work, worker count and lazy thumbnail requests; avoid redundant menu rebuilding and release sound nodes/object URLs.
- Require SHA-256 verification for automatic installer downloads and use unique temporary folders.
- See [Security](SECURITY.md) for remaining HTTP and platform-validation limitations.

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

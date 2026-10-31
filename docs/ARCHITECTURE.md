# Architecture

Cue uses **C++20, Win32, and dynamically loaded libmpv**. Portable UI/core code supports Linux previews and tests; Windows integration stays in `src/win/`.

## Source map

| Path | Responsibility |
|---|---|
| `src/core/` | State, persistence, subtitle settings, language catalogs, release parsing |
| `src/ui/` | Pixel renderer, Omni assets, animation, home, onboarding, playback panels |
| `src/win/` | Windowing, DPI, input, dialogs, clipboard, libmpv, updater, preview host |
| `installer/` | Native installer, uninstaller, LZMA unpacking |
| `i18n/` | Five translation catalogs |
| `assets/`, `res/` | Fonts, icons, the phone remote's page, executable resources |
| `tools/`, `tests/` | Generators, previews, packaging, portable and engine tests |

## Playback and input

The UI thread owns application state and rendering. An engine thread loads libmpv after the first window paint, allowing initialization to overlap the home exit animation. libmpv handles demuxing, decoding, D3D11 output, audio, and subtitles through libass/FFmpeg. Hardware decoding uses `hwdec=auto-safe`, with software fallback.

The video occupies a child window. Mouse events are forwarded without blocking the engine; keyboard bindings reach the UI through mpv. Engine events wake the Win32 loop through `PostMessage`, with bounded batches to keep it responsive. Commands are asynchronous and seeking allows only one request in flight.

`SingleInstance` holds a per-session mutex. Later launches send UTF-8 paths through `WM_COPYDATA` and exit before loading the engine. Requests received during startup wait in `pending_open_`. Preview-host and registration helper modes bypass this path.

A single click sends pause/resume immediately. A recognized double-click restores the starting playback state before toggling fullscreen; a brief state change between clicks remains possible.

## Rendering and motion

The home screen uses a software BGRA canvas; playback UI uses premultiplied BGRA `overlay-add` buffers. Buffers are reused or freed only after engine acknowledgement. Idle/hidden UI avoids redraws; minimized windows suspend UI animations. Home presentation invalidates only the changed rectangle.

Omni supplies palette tokens, bitmap typography, icons, and motion conventions. Geometry resolves to physical pixels. UI scale depends on width, DPI, and the selected size; subtitle/video scale is independent. Layout waits until a manual resize ends to avoid scale jumps.

`Spring` uses closed-form damped motion. `VsyncPacer` waits on `DwmFlush` only while needed; playback overlays target approximately 60 Hz, independently of video frame rate. Timer fallback and Windows reduced-motion preferences are supported.

The frameless control style uses a masked, separable Gaussian shader before subtitle/OSD composition. Later beta tuning reduced the cloud to a 6 px radius, 35% maximum strength, and 95% brightness. Passes become inactive when hidden. Subtitle return and control-style changes use reversible interpolation.

Fullscreen uses shader parameters `cue_on`, `cue_dark`, `cue_px`, and `cue_gray`: exit, wait for the dark frame and overlay removal, resize, wait for the new renderer geometry, then enter. Native rounded corners are requested through DWM in windowed mode and disabled when fullscreen/maximized.

## Previews and ambient lighting

`Cue.exe --thumb-host` runs a separate low-priority, CPU-only engine for timeline previews and scene-color samples. Two pipes carry one request at a time; generation IDs reject stale responses. A job object ties its lifetime to Cue. Failure removes previews without stopping playback.

Color sampling starts 1.5 seconds after the first frame; thumbnails wait 3 seconds. Both restart on file changes. Hover output is reduced before pipe transfer/cache storage, while color-sampling resolution stays unchanged. As of 1.1.1, timeline requests are on demand with a 12 MiB / 256-entry LRU cache; colour-only probes are transient, and there is no speculative full-timeline scan.

`atmosphere.cpp` chooses representative scene colors and smooths changes. Ambient lighting uses click-through layered windows below the player, cached geometry and color ramps, and approximately 30 Hz updates. Dragging reuses existing images and defers sampling/rebuilds until the gesture ends.

## Audio-only files

`on_preloaded` inspects actual tracks, ignoring album artwork as video. `core/audio_mode.h` builds a `lavfi-complex` graph: `asplit` preserves sound and feeds `showwaves` (160×48, 30 Hz), scaled with nearest-neighbor pixels and centered in 1024×576. No second audio decoder or whole-file analysis is needed.

Controls remain visible; video previews and ambient lighting stay off. Timeline dragging updates the displayed time and sends one exact seek on release. The graph drops two preparation frames to avoid flashing. Failed visualization initialization triggers one retry without it. File-local settings reset when media unloads.

## Phone remote

`core/remote_proto` holds everything portable: HTTP parsing, the WebSocket handshake (SHA-1, Base64) and frames, the JSON state and commands, folder filtering, natural order and next-episode detection, pairing-code throttling, strict command validation, resource limits and lexical path containment. `core/qr` encodes the pairing QR code (byte mode, versions 1-10, ECC M; verified against a reference encoder). `core/remote_sound` synthesizes the pairing sound as WAV bytes.

`win/remote_server` runs one thread with a `select` loop for HTTP and WebSocket clients, accepting only private IPv4 peers, exact local-endpoint `Host` headers and same-origin browser connections. Folder listings and thumbnails (Explorer's `IShellItemImageFactory`, reduced to 84×48 BMP) run on at most six short-lived workers that own their socket and copies of what they read. A same-origin WebSocket upgrades without exposing player state, then requires a credential in its first masked text frame within five seconds. HTTP data endpoints require a Bearer header. PINs have six digits, two-minute validity and one use; fresh tokens have 256 bits from Windows CSPRNG, with no predictable fallback. Turning the remote off revokes remembered tokens. WebSocket connections recheck token validity; frames, outgoing buffers, message rates and pending UI commands are bounded. HTML uses a per-response script nonce and CSP. Filesystem access rejects special Windows path syntax, checks resolved handles and refuses descendant reparse points. HTTP remains unencrypted.

Commands reach the UI thread through `WM_APP + 12`; App applies them as the same `PlayAction`s the dock produces, then publishes a state snapshot (~2 Hz while phones are connected, and after every command).

The page preserves its pixel grid while fitting width, laying out height separately and maintaining at least 44 CSS-pixel touch targets for primary controls. Short screens scroll. Thumbnails load lazily with at most three browser requests, and credentials stay out of URLs.

The page (`assets/remote/index.html`) is embedded by `tools/gen_remote_page.py` into `win/remote_page.inc`; the server injects the language's `remote.*` strings. The pairing scene is `ui/remote_view` (portable, previewable with `tools/preview_remote.cpp`) inside `win/remote_scene`, which hosts it in `win/overlay`: a layered popup laid over the player's client area that runs any `ui/scene_view` (pointer, keys, its own frame timer, background drag moving the window). The once-per-version "what's new" splash (`ui/news_view`, previewable with `tools/preview_news.cpp`) uses the same host; `Store::news_seen` remembers the last version shown. `tools/remote_harness.cpp` runs the real server without the player for browser testing.

## Persistence and lifecycle

`SettingsStore` uses schema 2 (`prefs`, `session`, `media`), migration, defaults, atomic writes, a backup, and a 400 ms debounce. Immutable snapshots are saved by one worker at a time. Per-media state includes position, duration, selected tracks/languages, subtitle delay, visibility, and style.

State remains in `%APPDATA%\tinyoffice\Cue` for compatibility. Font and “Open with” registration run after startup. Compatible compiled shaders are cached. `cue.log` records startup milestones; verbose mpv logging requires `--log`.

On close, visible windows and lighting disappear first; playback pauses and state is saved before engine teardown. Preview work is terminated separately.

## Development

```bash
bash tools/test.sh
g++ -std=c++20 -O1 -DWITH_PLAY -o preview tools/preview.cpp tools/preview_play.cpp src/ui/*.cpp src/core/*.cpp
./preview out 2
```

Asset generators: `gen_assets.py`, `gen_action_icons.py`, `build_fonts.py`, and `gen_fallback_glyphs.py`. `brand_images.cpp` renders the cover, banner, icon, and layers; `brand_psd.py` assembles PSDs. Regenerate branding from the repository root and place outputs in `docs/`.

[Languages](LANGUAGES.md) · [Performance and evidence](PERFORMANCE.md) · [Release process](UPDATES.md)

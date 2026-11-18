# Architecture

Cue uses **C++20, Win32, and dynamically loaded libmpv**. Portable UI/core code supports Linux previews and tests; Windows integration stays in `src/win/`.

## Source map

| Path | Responsibility |
|---|---|
| `src/core/` | State, persistence, subtitle settings, language catalogs, release parsing |
| `src/ui/` | Pixel renderer, Omni assets, animation, home, onboarding, playback panels |
| `src/win/` | Windowing, DPI, input, dialogs, clipboard, libmpv, updater, preview host |
| `installer/` | Native installer, uninstaller, LZMA unpacking |
| `i18n/` | Eight translation catalogs |
| `assets/`, `res/` | Fonts, icons, the phone remote's page, executable resources |
| `tools/`, `tests/` | Generators, previews, packaging, portable and engine tests |

## Playback and input

The UI thread owns application state and rendering. An engine thread loads libmpv after the first window paint, allowing initialization to overlap the home exit animation. libmpv handles demuxing, decoding, D3D11 output, audio, and subtitles through libass/FFmpeg. Hardware decoding uses `hwdec=auto-safe`, with software fallback.

The video occupies a child window. Mouse events are forwarded without blocking the engine; keyboard bindings reach the UI through mpv. Engine events wake the Win32 loop through `PostMessage`, with bounded batches to keep it responsive. Commands are asynchronous and seeking allows only one request in flight.

`SingleInstance` holds a per-session mutex, released by `quit_now` once the state is saved (the window is hidden first, with Windows' own closing animation; the engine ends behind it). Later launches send UTF-8 paths through `WM_COPYDATA` and exit before loading the engine. Requests received during startup wait in `pending_open_`. Preview-host and registration helper modes bypass this path.

Number keys 0–9 jump to 0–90% of a finite-duration video using the existing asynchronous exact-seek path, regardless of length. Mini players, music, open panels, modifiers, active drags/transitions and inactive/loading native windows reject these jumps. Win32 and mpv bindings cover both number rows; mpv uses `nonrepeatable` bindings and Win32 rejects held-key repeats. Ctrl+0 keeps its interface-size reset. The shortcut sheet documents the video-only keys.

A single click sends pause/resume immediately. A recognized double-click restores the starting playback state before toggling fullscreen; a brief state change between clicks remains possible.

## Rendering and motion

The home screen uses a software BGRA canvas; playback UI uses premultiplied BGRA `overlay-add` buffers. Buffers are reused or freed only after engine acknowledgement. Idle/hidden UI avoids redraws; minimized windows suspend UI animations. Home presentation invalidates only the changed rectangle.

Omni supplies palette tokens, bitmap typography, icons, and motion conventions. Geometry resolves to physical pixels. UI scale depends on width, DPI, and the selected size; subtitle/video scale is independent. Layout waits until a manual resize ends to avoid scale jumps.

`Spring` uses closed-form damped motion. `VsyncPacer` waits on `DwmFlush` only while needed; playback overlays target approximately 60 Hz, independently of video frame rate. Timer fallback and Windows reduced-motion preferences are supported.

The frameless control style uses a masked, separable Gaussian shader before subtitle/OSD composition. Later beta tuning reduced the cloud to a 6 px radius, 35% maximum strength, and 95% brightness. Passes become inactive when hidden. Control-style changes use reversible interpolation. The dock uses a critically damped trajectory. Subtitles use a finite 300 ms lift / 360 ms return, preserving position and velocity on reversal. Cached target geometry prevents repeated layout retargeting.

Fullscreen uses shader parameters `cue_on`, `cue_dark`, `cue_px`, and `cue_gray`: exit, wait for the dark frame and overlay removal, resize, wait for the new renderer geometry, then enter. During the resize a black click-through topmost popup (`StageCover`, `win/flight_ghost.*`) covers the monitor (going out, the restored window's client area), so DWM's frames of the window at its new place with its old contents never show; it leaves when the renderer is ready (0.6 s at most). The opening iris is the same pass (`cue_ir`: black outside an ellipse of the picture's proportions, in blocks of `cue_cell`), driven from `PlayView::iris_open` while `PlayInput::iris_engine`; it starts once the window has the file's shape (`App::window_settled`, at most 0.9 s after the first frame). Native rounded corners are requested through DWM in windowed mode and disabled when fullscreen/maximized.

## Previews and ambient lighting

`Cue.exe --thumb-host` runs a separate low-priority, CPU-only engine for timeline previews and scene-color samples. Two pipes carry one active decode and a replaceable latest destination; generation and request IDs reject stale responses. The helper reads commands independently of decoding and checks for superseding work every 10 ms while waiting for engine events. A job object ties its lifetime to Cue. Failure removes previews without stopping playback.

Color sampling starts 1.5 seconds after the first frame. The preview helper warms up after the first frame/reveal and a 250 ms playback delay; an explicit hover can start as soon as the reveal settles. Navigation requests use a stable one-second interval and a fast keyframe seek, with the actual returned time stored alongside each image. A new hover dispatches immediately when navigation is idle; subsequent destinations are coalesced to at most 30 Hz. Active navigation finishes before the latest destination is dispatched, preventing seek starvation; colour work is preemptible. Only a result matching the current destination is displayed. The helper remembers which keyframe each navigation request landed on and keeps the last 64 keyframe pictures: a request it can prove lands on one of them (from earlier answers, or from the file's Cues/stss index once the engine has confirmed it twice) is answered without a seek (`core/keyframe_index.h`). An HEVC file is decoded with slice threading (`on_preloaded` hook). File changes and close still cancel old work. The 12 MiB / 256-entry RAM cache remains bounded, without speculative lookahead or a full-timeline scan. Each hover epoch clears its display and rejects stale replies; a cache miss within a continuous hover retains the current picture and geometry until replacement, with its own timestamp. New hovers clear pixels while keeping the per-file card aspect. Known video geometry reserves the first card; the view retains small source pixels and scales into the tooltip canvas on content changes. Previews fit the mini player, remain below its window buttons and clear of its dock; music remains time-only. The helper returns below normal CPU priority after each navigation operation. Main-player clicks remain precise, and colour probes retain exact seeking. Timing fields in the helper protocol support the separate Windows CPU/GPU diagnostic. See [thumbnail policy](THUMBNAILS-2026-11-08.md).

`atmosphere.cpp` chooses representative scene colors and smooths changes. Ambient lighting uses click-through layered windows below the player, cached geometry and color ramps, and approximately 30 Hz updates. Dragging reuses existing images and defers sampling/rebuilds until the gesture ends. The halo's shape is built for the whole window once per size; its layered surfaces hold only the part on the virtual screen. When a move reveals a part the surfaces lack, they are cut again from the shape at once (the whole strips while the window is held), and trimmed back a moment after it settles.

## Audio-only files

`on_preloaded` inspects actual tracks, ignoring album artwork as video. `core/audio_mode.h` builds a `lavfi-complex` graph: `asplit` preserves sound and feeds a 24 kHz copy to `showfreqs`, which draws a spectrum visualization and applies no equalization (24 bands × 20 rows, 30 Hz, logarithmic frequency, square-root amplitude). The bands are mirrored around the centre (`hflip` + `hstack`, bass in the middle), `lagfun` lets bars fall slowly, and a dim bottom row marks silence. The 48×20 picture is scaled 8× with nearest-neighbour pixels, cut into dots by `drawgrid`, given an 18% reflection, and centred in 1024×576. All filters work on the tiny picture before scaling. No second audio decoder or whole-file analysis is needed.

Controls remain visible; video previews and ambient lighting stay off. Timeline dragging updates the displayed time and sends one exact seek on release. The graph drops two preparation frames to avoid flashing. Failed visualization initialization triggers one retry without it. File-local settings reset when media unloads.

## Phone remote

`core/remote_proto` holds everything portable: HTTP parsing, the WebSocket handshake (SHA-1, Base64) and frames, the JSON state and commands, folder filtering, natural order and next-episode detection, pairing-code throttling, strict command validation, resource limits and lexical path containment. `core/qr` encodes the pairing QR code (byte mode, versions 1-10, ECC M; verified against a reference encoder). `core/remote_sound` synthesizes the pairing sound as WAV bytes.

`win/remote_server` runs one thread with a `select` loop for HTTP and WebSocket clients, accepting only private IPv4 peers, exact local-endpoint `Host` headers and same-origin browser connections. Folder listings and thumbnails (Explorer's `IShellItemImageFactory`, reduced to 84×48 BMP; for songs, first the cover `core/cover_art` finds in the file's tags — ID3v2, FLAC, MP4, Ogg/Opus — or beside it, decoded by WIC at most 512 px a side) run on at most six short-lived workers that own their socket and copies of what they read. A same-origin WebSocket upgrades without exposing player state, then requires a credential in its first masked text frame within five seconds. HTTP data endpoints require a Bearer header. PINs have six digits, two-minute validity and one use; fresh tokens have 256 bits from Windows CSPRNG, with no predictable fallback. Turning the remote off revokes remembered tokens. WebSocket connections recheck token validity; frames, outgoing buffers, message rates and pending UI commands are bounded. HTML uses a per-response script nonce and CSP. Filesystem access rejects special Windows path syntax, checks resolved handles and refuses descendant reparse points. HTTP remains unencrypted.

Commands reach the UI thread through `WM_APP + 12`; App applies them as the same `PlayAction`s the dock produces, then publishes a state snapshot (~2 Hz while phones are connected, and after every command).

The page preserves its pixel grid while fitting width, laying out height separately and maintaining at least 44 CSS-pixel touch targets for primary controls. Short screens scroll. Thumbnails load lazily with at most three browser requests, and credentials stay out of URLs.

The page (`assets/remote/index.html`) is embedded by `tools/gen_remote_page.py` into `win/remote_page.inc`; the server injects the language's `remote.*` and `queue.*` strings. The pairing scene is `ui/remote_view` (portable, previewable with `tools/preview_remote.cpp`) inside `win/remote_scene`, which hosts it in `win/overlay`: a layered popup laid over the player's client area that runs any `ui/scene_view` (pointer, keys, its own frame timer, background drag moving the window). The once-per-version "what's new" splash (`ui/news_view`, previewable with `tools/preview_news.cpp`) uses the same host; `Store::news_seen` remembers the last version shown. `tools/remote_harness.cpp` runs the real server without the player for browser testing.

## Persistence and lifecycle

`SettingsStore` uses schema 2 (`prefs`, `session`, `media`), migration, defaults, atomic writes, a backup, and a 400 ms debounce. Immutable snapshots are saved by one worker at a time. Per-media state includes position, duration, selected tracks/languages, subtitle delay, visibility, and style.

State lives in `%APPDATA%\tinypeep\Cue` (moved once from `tinyoffice\Cue` by the running Cue, `migrate_data_dir`; registry ProgIDs, the mutex and window classes keep their `tinyoffice` names, so associations and the update handoff stay intact). Font and “Open with” registration run after startup. Compatible compiled shaders are cached. `cue.log` records startup milestones; verbose mpv logging requires `--log`.

On close, visible windows and lighting disappear first; playback pauses and state is saved before engine teardown. Preview work is terminated separately.

## Development

```bash
bash tools/test.sh
g++ -std=c++20 -O1 -DWITH_PLAY -o preview tools/preview.cpp tools/preview_play.cpp src/ui/*.cpp src/core/*.cpp
./preview out 2
```

`tools/test.sh` runs on Linux and, from an MSYS2/MinGW shell, natively on Windows (it links the
system's CNG and DPAPI libraries there; `tools/native-tests.bat` starts it from a normal prompt).
`.github/workflows/tests.yml` runs it on every push to `main` and every pull request.

Asset generators: `gen_assets.py`, `gen_action_icons.py`, `build_fonts.py`, and `gen_fallback_glyphs.py`. `brand_images.cpp` renders the cover, banner, icon, and layers; `brand_psd.py` assembles PSDs. Regenerate branding from the repository root and place outputs in `docs/`.

[Languages](LANGUAGES.md) · [Performance and evidence](PERFORMANCE.md) · [Release process](UPDATES.md)

## Mini player

Shuffle in the compact music menu includes its state in the icon highlight and keeps a bright marker beneath it while enabled, including while hovered. Its label changes to the enabled wording. The existing stored preference and ShuffleDeck continue to control playback.

Repeat carries the same persistent marker in the overflow menu as shuffle. The bar and menu
both add a small numeral for repeat-one, including a file without a multi-item queue; repeat-list
uses the plain loop icon. Both presentations use the existing off/list/one cycle and actions.

The mini players are laid out in art px: a film's needs about 288 x 162 of them, a song's card
`kCardMinW x kCardMinH` (250 x 130, its title above the playback). Their windows keep a mini
player's range at every interface size, and `mini_scale` (ui_scale.h, through `App::scale_for`)
gives them the chosen size's base scale, stepped down by half art px until that room is there;
`play_scale` returns the same scale in the mini player, so its bar and its menus match.

Scale: `ui_scale` (DPI, window, chosen size; capped so the 320 x 180 canvas fits) gives `s_`;
`scale_for` gives the mini player `mini_scale`; `play_scale` raises a portrait film's bar and
music framed close. Every change of window size or scale goes through
`App::relayout` (scale held during an edge drag, then `refresh_ui_scale` → `rescale`): the views
laid out, music's frame chosen again (`music_fit_window`, chosen in art px), the overlays sent.
The smallest windows use the medium size's base scale at every size.
Menus: after their own ways of fitting (Settings' stages, "..." folding, About's tighter rhythm,
the lists' and the subtitle list's own scale steps), `layout_rows` steps any menu's pixels down
by half art px (`panel_fit_`, read through `panel_draw_s()`) until it fits `panel_room_h()`: the
window under its buttons, beside the bar. A closed menu frees its canvases.

`App::enter_mini` / `exit_mini` run `step_mini_fx`: the picture breaks into pixels where it is
(the snap pass, 0.12 s), then `FlightGhost` (`win/flight_ghost.*`) shows a DWM thumbnail of the
window in a click-through topmost popup at the same place (for the way in, the part in the mini
player's shape), the window is cloaked and switched to its destination at once (`mini_switch`),
and the thumbnail's popup flies from rect to rect (0.46 s in, ease-out with a little overshoot;
0.40 s out, no overshoot), the source cropped to the popup's shape every frame, the pixels
resolving. On landing the window is uncloaked and the popup goes the frame after. Without a
thumbnail the plain flight remains (cloaked 80 ms, then the window springs into its corner).

Music in a roomy window is `MusicFrame::Centre` (`music_frame_for`; very wide, short windows stay
`Scene`, the split composition; `music_frame_stacked` is Tall and Cover): `music_view` centres the
cover with its wings over its words (`PlayView::music_centre_below`) between the top strip and the
bar, and with the list open (`music_list_beside`) centres the group in the left half while
`centre_list_rect` holds the list in the right. The bar there is `music_full_` (`kMusicDockH`): the
timeline over the buttons, nearly the window's width (`music_full_place`). The card's turning cover
is `CoverTurnWindow` (src/win/cover_turn_window.cpp): a layered, click-through window owned by the
player, drawn on its own thread at each DwmFlush by `cover_turn_draw` (core/cover_turn.h, the
cover pass's camera) with its shadow; the engine gets `cov_hide` > 0 (no cover; its shadow scaled
by 1 - cov_hide, which follows the window's `shadow()`: the two cross-fade at the start and the
end); `request_cover_art` reads covers at the card's size (`card_art_prefetch` keeps the current
one and the songs either side ready). The scene's cover is in its middle across (x 700), so the
picture reaches equally both sides; the room pass adds a faint wash over the whole room and
`music_backdrop` matches its far end. A song's mini player is the card (`PlayView::card_`). Its list is `QueueView` bare and compact:
one 24 art px line per song at the card's scale (`scene_list_scale`), `split_artist_title` and
`plain_title` for the names. A song opened in the card is built with the person's look like any
other: the card masks the spectrum's wings (`wing_on`), so back in the full player the spectrum is
there at once, with nothing reopened (a still graph used to stay on that song until the next). A song change through the queue in the card (`queue_play`) runs
`step_card_flip`: the cover pass's turn (`flip_ry_`, added to `cov_ry`) to 86° in 0.15 s with the
gain fading, `queue_play_now`, edge-on until the new song's `PLAYBACK_RESTART` (2 s at most), then
from -86° to flat with an ease-out-back in 0.42 s; `PlayInput::music_leaving` steps the words out.
The cover pass narrows its shadow with the turn and fills what the cover uncovers from the
light just outside its four sides.

`App::scale_for` gives the mini player its own scale (`mini_scale`) on every path (`WM_SIZE`,
`refresh_ui_scale`, `set_ui_scale`, a new DPI).

A film's mini player opens `Panel::SubMini` where the full player opens `Panel::CC`
(`open_panel` redirects it; leaving the mini player closes it): the switch, an `RK::Picker`
(the track, its arrows round the list) and a three-way `RK::Segmented` for the size
(`PA::MiniSubSize`, `Store::mini_sub_size`), which `App::step_sub_fx` multiplies into the
subtitles' scale only while the mini player is on (1, 1.25, 1.5), after the readable floor.
The panel shows a picked track at once through `sub_pick_` and leaves `in.sid` to the engine:
the app compares a pick with `in.sid`, so changing it early made the pick look current.
`film_mini()` keeps the film's mini player a quick look: `right_click` opens nothing, up next
leaves "..." and `open_panel`, and `next_notice`/`next_wanted` show nothing in either mini player.

The card in the spectrum-alone look (`App::card_drawn`): the engine has no cover, so the view
draws the read-ahead cover (`in.card_art`) in its lowest layer, `L_ART`, over the card's square
with its shadow (`cover_turn_draw`, flat), redrawn only for a new cover or place; hidden while a
turn draws it (`cov_hide_`), shown again when the turn would hand the cover back.

The survey's invitation is a HomeView card (`layout_survey`, `draw_survey`); the app says when
(`survey_wanted`: updated, or a week after `Store::installed`; not over the welcome, the news or a
scene) and records the answer (`Store::survey`). Its scene steps 8 times a second for two pages,
then rests, so `next_event` asks only for its blinks.

Music's bar: `music_full` (the bar along the foot) only without the bar's background
(`style_mix_ >= 0.5`, so a style change swaps them halfway); with it, the film's compact bar.
`layout_scene_` relays the bar when `in.cover_scene` changes, and `configure_audio_mode` keeps a
song's scene until its own is known (its `apply_smooth()` redraws the interface on the way). The
list button's count is a tag (`queue_badge_*`); `layout_badge_` relays when its digits change.

## Settings

The visual-hints toggle retains a 200-art-pixel base width in both states, avoiding a sideways shift when its preview appears. The whole hint block enters over 140 ms with a 3-art-pixel rise and fade at UI cadence, then releases its animation state. Its temporary canvas clips the hint picture, glow and labels to their own area, including the horizontal side column, and is released when hidden/closed. Reduced motion presents it immediately; toggling emits only the settings preference action, without native window movement.

"..." holds the file (open, close) and places to go (phone remote, settings last).
`Panel::Settings` holds the kept choices: music's looks (for music), the ambient light and its
strength, smooth motion (films), the interface size, the controls' background, the language and
About (its back arrow and Esc return to Settings). Music's list always opens in the scene
(`scene_list`): beside the words, in the card's column, or over the cover in the close
compositions as its own composition (`list_layout`: a centred column, the cover framed as the
header's thumbnail by `App::music_view_target` from `list_cover`, the words beside it, the bar under
the list; the wing pass steps the wings out and the room light goes quiet); only a window too short
for that dims the cover under the list (`list_veil` → the shader's `veil` pass); `list_compact`
picks the one-line rows. Its
first row (`RK::Preview`, `render_preview`) shows the setting under the pointer or the keyboard at
work (`settings_demo_track`: the last one pointed at stays), redrawn at 30 frames a second only
while something in it moves (on by default, `settings_hints` in state.json). A window too short
for the whole panel steps through `settings_stage` (`kSettingsStages`: the picture on top, then
beside the rows where `(settings_w_ + kSettingsSide) * s` fits the window, then the rows closing
up, the interface folded into its page and the groups closer, on top or beside; the settings
alone last), from
`settings_restart()` at every open, "i", and change of size or interface scale. Its pictures are
measured in the panel's own rounded px (`px(n) = lround(n * s)`), so half-step scales keep them
inside their frame.

Shuffle (`ShuffleDeck`, core/queue.h): a deck of the list's other songs dealt from an `mt19937`,
synced to the queue each time (added songs join, removed ones leave), with the songs played as
the way back; `App::queue_following` and `PreviousInQueue` ask it when `store_.shuffle` and music.
A dragged bar with Shift held (`PlayView::set_shift`, read from `GetKeyState` on every move and
at the 30 Hz poll while held) keeps `x = (w - dw) / 2` and only the centred anchors.

## Music in a strip

A window at least `kMusicSplitRatio` (2.2) times wider than tall, that no picture of the scene's
shape covers with the cover above the bar, is framed half and half (`music_view`, `MusicView::wx/ww`,
`scene_words_x/w`): the cover and its wings centred in the left half, the words from the middle.
Around any music picture the engine's `background-color` is `music_backdrop` (the room's far light
in the cover's average border colour) and the edge pass fades toward it (`edge_c*`).

## Session queue

`core/queue.h` owns up to 512 ordered audio/video entries with stable IDs. The
application owns the queue, not mpv's playlist: one decoder and the existing
load/EOF path remain in use. Explicit queue order takes priority over episode
discovery and stops at its end. Clearing the queue restores automatic discovery.
Removing the playing entry does not stop playback. Closing the video clears the
session queue; it is not written to preferences.

`win/app_queue.cpp` scans one folder on a low-priority worker, without recursion,
up to 4,000 directory entries / 512 playable files, in natural order. Results are
published through an atomic completion flag and consumed by a UI timer. Only one
scan exists at a time; cancellation discards results without dereferencing App.
OS filesystem calls can remain blocked; cancelled work retains its slot until it
finishes. File changes and shutdown discard results.

`ui/queue_view` renders only visible rows of a complete scrollable list. Stable-ID
row springs preserve positions during a reorder; one absolute move commits on
release. Resize, dismissal and external queue revisions cancel pending gestures.
Scroll and reorder motion respect the Windows reduced-motion preference.
Visible video rows share the existing single low-priority Windows thumbnail
worker with the next-episode card. A 32-image LRU holds 84×48 BGRA previews
(about 504 KiB of pixel data), with no disk cache or second decoder. Missing
previews are remembered until the entry is removed. Results use stable IDs,
so a late completion cannot update a replacement entry. The timer stops when
the panel closes. Only the visible viewport is composed; the queue stays bounded
to 512 entries. Each visible preview keeps a copy scaled to its row (released when
the row leaves the viewport), and the panel background is cached per size, so a
scroll frame copies rows instead of resampling previews pixel by pixel. The panel is a popover
anchored to the up next button like the bar's other menus: centred on its button, stopped
at the bar's edge (centred on the bar when wider than it), then kept inside the window margin. Windows at least 760 art px wide keep the interface
scale and allow about ten rows (320 px wide); smaller windows compact its scale.
Other menus keep their placement above (or below) the bar, but when that space is
too short for them (short, wide windows) they open beside the bar, level with its
outer edge; About rises above the bar instead of covering it.
The dock gives up next a permanent slot beside repeat (left of the
subtitle group in narrow docks); it takes the film's accent while the queue is not empty.
The wide dock has three zones on one 22 art px beat (centre to centre): volume and
speed, playback centred on the dock (previous · −15 · play · +15 · next with a queue),
and one even row of secondary buttons from up next to More. Play leaves the centre only
when the zones would otherwise collide.

Music uses `audio_cover_tail` or `audio_spectrum_tail` (`core/audio_mode.h`), chosen by the
look. The cover look takes the engine's album-art track (`audio_cover_track`) as `[vidN]`, or,
for a song without one, Cue's tile drawn in the graph (`audio_placeholder_cover`: a 26x26
`color` frame with the note in `drawbox` runs, scaled by 20 with nearest-neighbour). The
spectrum look places the 384x200 equalizer, doubled, where the cover stands in the same
1920x1080 scene. `cover_scene_` means the scene is on (either look); `cover_art_` means a
cover is drawn (the shader's pass and the tilt). The cover is scaled once with
Lanczos to 520 px and placed on one 1920x1080 scene (`MusicScene`), the cover on the left
and the words' column on the right. The window frames it (`MusicFrame`, `music_view`):
`Scene` shows all of it, `Tall` (portrait windows) the cover with the words under it,
`Cover` (small windows) the cover and its wings alone above the bar. In a window of another
shape than 16:9, `Scene` grows from fitting toward filling the window (at most by a third), as
far as the cover, the wings and the start of the words' column stay inside margins of 3.5%
(7% of the height above and below) and above the bar, found by a bisection on the scale; the
words end in "..." at the window's edge. Where the picture still stops short of the window
(very wide windows, `Cover` in a wide one), the `edge_*` pass fades it into the window's black
on those sides, over a quarter of the frame's smaller side at most and never over the cover or
wings: the picture and its light can only paint inside the picture's own frame, and would
otherwise meet the black on a straight line. A new interface scale when a resize ends glides
the framing too (`refresh_ui_scale`). The framing is mpv's
`video-zoom` / `video-pan-x/y` (`mpv_framing`), updated on every
`WM_SIZE` including mid-drag and glided for 0.34 s across frames (`App::music_fit_window`,
`step_music_view`); nothing is rebuilt. They are the engine's own options, not file-local ones:
every file sets them in `configure_audio_mode` (a film and a song without a picture back to none,
`frame_plain`; a song to its framing, synchronously, before its first picture), so the last
song's picture, still on screen while the next opens, stays framed. During that gap
(`music_gap_`, from `open` to `configure_audio_mode`) the music passes of the shader options are
sent as they last were (`music_opts_`), and the room's light keeps its colours (`room_kept_`),
crossing to the new cover's in 0.45 s. The shader's `cov_*` coordinates are taken in the
shown part of the picture (`music_visible`). A reopened song, or the next song of an album,
keeps the window's size (no `request_video_geometry`). The 24x20 spectrum (`showfreqs` at 60 fps:
4096-sample window, 0.92 overlap) is turned sideways, mirrored around the bass and drawn
as two dot wings, each row tinted by the cover row beside it; each cell is as opaque as it is
lit (the cells merged with themselves by `alphamerge`, then a `blend` that takes the colour
from the tint and the alpha from the cells), so a fading cell lets the light behind it through
instead of turning into a dark hole over a bright cover; the gaps come from `drawgrid` with
`replace=1`. The music leads both merges, so when the sound ends the last frame keeps its holes
(an `alphamerge` led by the cover would lose its alpha input at end of stream and paint solid
wings). Every two-input blend is led by the music stream,
so the scene runs at the spectrum's rate. The background is the cover
reduced to 32x18, blurred and dimmed, plus the blurred spectrum multiplied by those colours,
so the light swells with the music, then vignetted and scaled up. In this look the engine
scales smoothly (`spline36`/`mitchell`); the spectrum alone keeps nearest-neighbour. The
cover's shadow, tilt (up to 6°, a camera at four half-sides), light band and rim are a pass
of the output shader (`cov_*` uniforms, updated only while something moves); where the
turned cover uncovers the flat one, the light is borrowed from just outside it. The
captions are a UI layer (`L_MUSIC`), placed from the same `MusicScene`; *up next* in a
song's last 12 seconds is another (`L_NEXT`), dropped in at the top.

Look changes reopen the song rather than swapping `lavfi-complex` in place
(`App::reopen_music`): with the cover's attached-picture track in the graph, a runtime swap
could leave the graph waiting for a cover frame the demuxer does not send again (no sound)
or fail and end the file (the queue skipped). The reopen runs inside the audio-switch stage
machine: pixels out (0.12 s, the song still playing), `loadfile` of the same path with `start`
at the position read just then and pause held (`music_reopening_` in the `on_load` hook),
pixels in (0.26 s) and pause restored at `PLAYBACK_RESTART`. An absolute start is already a
precise seek, so the reopen skips the exact seek a track change needs when it lands within
0.2 s. Keeping both looks in one graph to switch without a reopen was measured at about 60%
more CPU for the cover look, and is not used. `on_file_loaded` keeps the interface (no reset) for a reopen. A
song that follows another in play mode starts at stage 5 of the same machine (`music_intro_`).
The wings' dot grid is one `color`+`drawgrid` frame multiplied into their alpha by `blend`
(repeated by framesync), not `drawgrid` per picture.

A song opened without a queue builds one from its folder (`App::music_album_queue`:
playable audio, natural order, at most 512 around the song). Music skips resume points,
timeline previews and the next file's thumbnail. Queue rows for music ask the shell for
a square cover (`make_thumb(path, square)`), and a list of only music uses the compact
album row; beside the cover, `QueueView` runs bare (rows only, no sheet, header or
footer) in the words' column (`PlayView::scene_list_rect`). Repeat is `loop` (this one,
`loop-file=inf`) or `loop_all` (`App::queue_following` wraps to the first item). While another
app's full-screen window covers Cue on its monitor, or Cue is cloaked on another desktop
(`App::covered`, polled twice a second), `hidden_` stops UI frames like a minimised window,
sends `cov_on=0` and sets a film's file-local `video-sync=audio`. The phone's face for a
song is `/cover` (the playing song's shell thumbnail at 72 px, cached), announced by the
`cover` field of the state. The taskbar's thumbnail toolbar (`win/taskbar_buttons.*`, `ITaskbarList3`)
is attached on `TaskbarButtonCreated` and synced from the poll timer; its icons are the
dock's pixel glyphs. File types register `Capabilities\FileAssociations` under
`Software\tinyoffice\Cue` and `RegisteredApplications`, so Default apps maps each
extension to its ProgID. If the cover graph fails, the file is
reopened once with the spectrum alone, and then without a picture.

`ui_scale` measures a squarer window by its area (as a 16:9 window of the same area),
bounded by its width at 1000 DIPs per reference step: 4:3 and square video keep the
controls of a comparable 16:9 window instead of dropping a step.

The home's Recent list is built by `core/recents_view.h`: recents (newest first) are
grouped by `remote::series_id`, so a show is one entry that opens its latest episode and
counts the others; `series_title` and `episode_label` give the readable name and
"S01E03". Rows are two lines (38 art px pitch); the window shows as many as fit.
`recent_kind` tells a film, an episode, a song, long audio (`resumes_audio`: 20 min or more)
and a book (`.m4b`, or long audio in 3+ chapters) apart; `ago` says when in calendar days.
Each row has a picture (`HomeRow::thumb`, `ui/thumb_art`): 48 x 27 art px, 32 x 18 in a
smaller window, none below 400 art px of composition. `win/recent_thumbs` reads them on one
low-priority worker (a film: `thumbs\<fnv1a-64 of the normalized path>.jpg`, 160 x 90 at most,
stale when older than the entry's `last`; audio: `cover_image`, else its saved picture) and
answers by message. Every picture Cue makes comes from the timeline's own helper, `thumbs_`
(`next_home_shot`), with sessions of their own (`kHomeShotSessions`, routed in `on_thumb`):
`shot_background` (keyframe seek, below normal priority), one file at a time, only on the home.
The film just left goes first, at once (`go_home`); the others 1 s after the home came in. Nothing
new starts while the pointer is on an entry (at most 3 s). A file opening ends a shot in flight
(`stop_home_shots` ends the helper; an idle one is kept for the player); a helper started for the home alone goes when done.
`home_shot_reachable` keeps network, disc and cloud-only files (OneDrive placeholders) out, except
the film just left. `film_left` marks a film's
frame out of date when it is closed or replaced; nothing is taken from the playing engine.
The home scales each picture once per size into a cached tile;
stand-ins are drawn on the art grid from a hash of the path. Audio is square (`thumb_art::square`);
a song's record (`make_disc`) is cached per size, turn and light. `HomeView::clear_animated`
snapshots the rows as drawn, then the cleared list lets them go in a stagger (`ghosts_`).
When a queue starts from the playing file, `MediaQueue::without_first` drops that
file once from the added batch (ASCII case-insensitive, either slash), so a folder
containing it does not list it twice. The phone fetches an authenticated `/queue` snapshot only
when opened or its revision changes, not on every playback tick. The snapshot
contains bounded names, IDs and audio/preview flags, never filesystem paths. Remote playback/additions
are revalidated against currently shared roots, including after folder scans.

The remote server keeps a private ID-to-path map for `/queue-thumb?id=…`.
Authentication, current shared-root/reparse checks and existing bounded thumbnail
workers apply to every request. Queue/root changes invalidate the published
snapshot epoch. No preview route accepts a queue filesystem path from the phone.
The phone loads visible previews with at most two requests and 32 Blob URLs;
closing cancels work and revokes URLs after the short exit. No disk cache is added.

The mobile sheet retains keyed rows across clock ticks, batches mutations before
FLIP measurements, and animates only visible changes. Adding from the phone uses
the folder sheet in an adding mode: a per-visit set of sent paths marks tiles and
prevents duplicate sends from repeated taps; the set is discarded on exit. The
return to the queue scrolls to the end and highlights the entries added. Drag release commits one
absolute stable-ID move with optimistic placement until the authoritative update.
At most 12 outgoing row nodes remain briefly for exits. Dismissal, reduced motion
and page hiding cancel pending gestures/animations. Folder requests publish busy,
actual-added, empty and error results; clear cancels outstanding results. The
folder worker remains nonrecursive and retains its slot until cancellation finishes.

## Subtitle reading scale

Styled text uses the existing 16:9 height as its reading-size reference when a
landscape window becomes wider: `clamp(width * 9 / (16 * height), 1, 2)` compensates
mpv's height-based font scaling. 16:9 and taller windows retain their established
size. The continuous cap protects extremely short windows. Saved style values,
UI size settings, authored ASS and bitmap subtitles are unchanged. Font size,
tracking, outline and background padding share the compensation; libass line
spacing remains in physical frame pixels. Drag targets and control clearance
use the same geometry. Resize updates only changed properties asynchronously.

## Bounded subtitle menu and queue motion

`ui/play_tracks.cpp` lays out a clipped subtitle viewport between fixed controls.
The panel width and height are capped to client geometry; unusually small windows
compact the panel scale. Track IDs and full labels remain in rows, while fitted
labels are measured once per layout. Scroll renders only intersecting rows and
updates the custom scrollbar; it never rebuilds the track list. `tracks_revision`
invalidates metadata only when the engine publishes a new list, avoiding string
signatures over every title on playback ticks. Subtitle labels are built in one
batch with duplicate counts rather than rescanning the list for every row.
The native video-window subclass posts fractional wheel deltas to the UI thread;
the engine does not also process the same Win32 event.

Queue removal commits in the session model immediately. At most 12 visible row
rasters survive as noninteractive exit snapshots, including a clear of 512 items.
The stagger is capped at 80 ms, with 150 ms individual exits / 180 ms clear exits;
exits shift 6 art px toward the remove button. Additions reveal over 160 ms with a
700 ms highlight; additions outside the viewport scroll it to them. A drag lifts
the row with a hover-style step pose and a pixel shadow, and a spring-driven dotted
slot marks the destination. Panel height settles over 200 ms after exits finish.
Only container bounds change during this contraction; rows are not rebuilt.
New mutations supersede the displayed state without delayed model callbacks.
Closing, replacing the panel or reducing motion releases exit snapshots and
settles pending motion. No additional handles, threads or disk files are used.

## Motion language

Desktop motion uses the primitives in `ui/anim.h`: poses answer on the first frame
(`hover_step`), positions move on closed-form springs, and exits ease out from the
value on screen, so any motion can reverse mid-way. The phone page follows the same
rules in CSS: presses scale on the first frame (50 ms), then release on one shared
spring (`--spring`, about 3% overshoot) in 240–300 ms. Entrances travel 8–14 CSS px;
staggers stay under about 110 ms in total. Image fades and the click-wheel position
dot change in two or three pixel steps. `prefers-reduced-motion` removes transitions
and animations on both surfaces.

## Music's light show

For music the ambient switch drives a pass inside the picture rather than the halo windows. When a song is opened, a meter joins its file-local audio filter chain: `@cuelvl:lavfi=[astats=metadata=1:reset=1:…]` reports each audio frame's RMS level and zero-crossing rate as frame metadata, read through `af-metadata/cuelvl` at the poll's 30 Hz while the window can be seen. The meter changes no sample; a failing user filter is bypassed by the engine. `ui/music_light` turns the readings into motion: readings wait for the moment they are heard (the engine runs `audio-buffer` plus 40 ms ahead), a slow automatic gain gives the music's body (rising over 0.4 s, falling over 1.1 s: the light follows the body, not each hit), a breathing cycle runs under it (`breath`, a sine of 6.5 s in quiet passages down to 4 s when full, its depth 0.45 to 1 with the body, easing to rest over about 1.5 s at a pause; 8 s and shallow for reduced motion), a fast envelope over a slow one starts a ring (at most three a second), a level a few seconds long against half a minute marks a chorus and turns the colours half a turn, and the zero-crossing rate against its own average makes twinkles. Windows' reduced motion leaves only a slow breath. The `lit_*` hook in `win/blur_shader.h` draws it on OUTPUT, before the OSD, in soft light only: two quarter-size passes first find the lit dots (`LIT_MASK`: the wings or the spectrum alone, the cells that stay lit for any sound left out, and with the cover the graph's soft light between the dots, read in the gap beside the cover and past the wing, subtracted) and gather, for each pixel, the dots found on the way back toward the cover's centre or the spectrum's foot (`LIT_RAYS`, 32 jittered taps over 27% of the way, longer on the in-breath), so the dots glow a short way into the room, in their own colours; then a bloom outside the cover (rounded-square distance) whose reach and strength follow the body a little and the breath (`lit_br`) more, with only a trace of each passing ring, and a Gaussian pool in each corner of the window nudged in turn by the rings (`lit_c*`, decaying over 0.7 s). With the spectrum alone (`lit_cov=0`) the bloom is round, centred and also lit behind the equalizer, so no dark square is left where a cover would be, and its dots take the light's colour where they stand. Colours come from eight samples of the cover's border colours (`lit_p*`, from the decoded cover's zones, from the left, clockwise), interpolated by angle and turned by `lit_t`; the spectrum's dots are tinted with the cover's colour at their height. Screen blending, a 1/255 noise against banding, nothing drawn on the cover. Parameters travel with the other dynamic shader options; nothing is sent while the window is hidden or the show is still.

`core/cover_art` finds a song's picture without the engine (ID3v2 APIC/PIC with unsynchronisation, FLAC PICTURE, MP4 `covr`, Ogg Vorbis/Opus `METADATA_BLOCK_PICTURE`, then `<song>.jpg`, cover, folder, front); `win/cover_image` decodes it with WIC (scaled while decoding) on a background thread, for the phone's thumbnails, the show's colours, the pairing screen's glow and the media card. A film's card shows a still the engine saves with `screenshot-to-file` a few seconds in (checked on a worker that it is not black; up to three tries) and its name from `remote::card_words` (series and episode, or title and year, release tags dropped).

## Black and white music, covers in bars, the shortcut sheet

Black and white is the last pass on OUTPUT before the snap (`//!WHEN mono 0 >`): Rec. 709 luma mixed in by `mono`, everywhere but the cover (with the cover look, the pass projects each pixel through the same camera as the cover's tilt and keeps what the turned cover covers in colour), eased over 0.3 s by `App::step_mono` and set to 1 from a song's first picture when the choice is saved (`SettingsStore::music_mono`). It needs no rebuild of the engine's graph, so switching never reopens the song; films never get it.

A song's cover is read once by `request_cover` (`win/cover_image`): started in `App::open` for a song, one worker reads the encoded picture (`cover_art::find`), decodes it at 192 px with WIC, finds the black bars with `trim_dark_bars` (`core/image_trim.h`), and reads the light's colours from the trimmed picture halved to ~96 px; the encoded bytes serve Windows' media card. The last four results are kept in memory, keyed by the file's path, size and last-write time (a re-tagged file is read again); an in-flight read is shared, never repeated. When the engine asks for the music graph it waits at most 60 ms for it (usually long done) and, only if the picture is the one the engine has (same aspect as the album-art track's `demux-w`/`demux-h`), puts the inner box in `MusicScene::crop_*`; `audio_cover_tail` then crops `[vidN]` before the cover, its wings' tint and its light are taken from it.

The shortcut sheet is a panel (`Panel::Keys`, rows of `RK::Keys`) built from one table in `ui/play.cpp`, the keys' own names (ctrl, shift, esc, space) translated. `build_keys` picks the largest layout that fits whole and clear of the window's buttons: two columns at the interface's scale, rows closed up, then half-step smaller scales, then (the smallest window at the smallest size) two narrow columns with shortened names. `?` arrives as a character (`WM_CHAR`, any keyboard layout) or as the engine's `?` binding; the engine's bindings also cover l, n, p, c and Ctrl+T, which after a click on the picture reach the engine's window first.

A maximized window keeps its state when a file asks for another shape: `adapt_window` writes the new size into the window placement's restore rectangle (workspace coordinates) instead of restoring the window, as full screen keeps `pending_ar_` for its way out.

## The room's light, the look's fade, the window going away

The cover looks' graph draws the cover and its wings on black: the room is padded from a 2x2 crop
of the spectrum (`pad`), so it is led by the music and ends with it, and nothing is scaled to the
full size per picture. The soft light around the cover is the `bg_on` pass of `win/blur_shader.h`,
before the light show's: a glow centred on the cover (`bg_x`, `bg_y`, `bg_hw`, from the same view
as the cover pass), strongest at its edge, in the eight border colours `lit_p*` facing their own
side, faded in over half a second once the colours are read (`bg_k`). The cover itself is never
painted. **Cover only** (`look 2`) is the cover on black at two pictures a second (`showwaves`
r=2 padded), the same pass for its light, no meter and no light show.

A new look reopens the song (see the audio switch): its sound fades out with the picture and in
after the reopen through `volume-gain` (dB, the engine's own gain under the person's volume),
`App::set_switch_gain`. Music starts with `initial-audio-sync=no`, so its first samples are never
trimmed to meet the picture's first frame.

Minimizing changes nothing in the engine: rebuilding the picture's chain (a slower `vf`) or
switching a film's `video-sync` made the sound stall for a moment both ways. Only Cue's poll goes
to 4 Hz; a film under a full-screen window keeps `video-sync=audio` as before.

Narrow portrait and square windows raise the control bar's scale (`portrait_scale`). A window
less than 15% taller than wide keeps the regular dock: Windows Snap must not force a
desktop-width bar into a tiny column. Clear portrait proportions, up to 3:4, keep the
compact composition regardless of DPI or art-canvas width. Between those proportions,
a 400-art-pixel width decides whether the regular controls fit.
The same `compact_video_dock` decision controls scale and layout. The panels take
`PlayView::panel_s()`, matching the approved bar scale when width permits,
in half steps and within about 80% of the width; up next in a portrait window is the bar's width.

## Windows media controls

`win/media_controls` registers the window with System Media Transport Controls through `ISystemMediaTransportControlsInterop::GetForWindow`, with the few Windows Runtime interfaces declared in vtable order and `RoGetActivationFactory`, `RoActivateInstance` and the HSTRING functions loaded from combase.dll. It sends the playback status, the previous/next availability, the music display properties (title and artist; a film's name as the title), the cover's encoded bytes as a thumbnail stream (`CreateRandomAccessStreamOverStream` over a memory stream), and the timeline every few seconds or after a jump. Button presses arrive on a system thread and are posted to the window as `WM_APP_MEDIA_BUTTON`, handled like the taskbar's buttons. Leaving playback disables the controls and clears the card.

## Ambient light and rounded corners

The halo is four click-through layered strips around the player's visible frame.
Windows 11 rounds that frame by 8 px at 96 dpi, which used to leave a dark wedge at
each corner between the square halo and the arc. The top and bottom strips now
reach `C = 8 px × dpi / 96 + 1` under the window and light only the wedges outside
the arc, at full edge strength and with the corner's colour; the left and right
strips start and end `C` px further in, so no pixel is lit twice. With square
corners (Windows 10, maximized) those pixels stay hidden under the player.

## 1.3.2 stability and performance follow-up

Music lighting reads the complete `af-metadata/cuelvl` map asynchronously, at
most one query in flight at the existing poll cadence. File/seek/pause/hidden
transitions invalidate old inputs without resetting the visible crossfade. Music
text rendering compares cached fields rather than allocating a concatenated key
per frame. Two lazy cover workers, each with four waiting slots, replace per-job
threads; obsolete queued work is discarded and still-needed requests are retried.
A hidden card turn is settled before the UI frame loop suspends.

`SeekDispatch` owns one accepted keyframe seek and one latest destination; a
rejected enqueue never occupies the slot. Opening a file, returning home and
exact/relative seeks clear superseded destinations. Media-card screenshots carry
unique request IDs, so old files cannot publish a new card image. See the
[review and evidence](PERFORMANCE-REVIEW.md) for validation limits.

## Remote reconnection and bounded filesystem work (1.3.2)

Phone connections have an authentication deadline and foreground ping/pong
liveness checks. Foreground/network restoration replaces the old socket and
requests authoritative state without replaying commands. Continuous controls
coalesce by kind; media generations prevent delayed playback commands and
optimistic state from crossing an open, including identical filenames.

`core/async_worker.h` owns a bounded FIFO and shared thread state. Folder discovery
uses one active read plus one replaceable destination. Remote path validation
uses a separate lane with at most 128 outstanding jobs; only completed, current
permission/navigation/import generations can reach the UI. Stop and queue clear
invalidate pending work. Shutdown does not join blocked filesystem calls.
See [remote stability review](REMOTE-STABILITY-REVIEW.md) for the findings,
validation evidence and native test limits.

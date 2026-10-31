# User guide

## Playback

Open a file with `Ctrl+O`, drag it onto Cue, or use File Explorer's **Open with** menu. Cue uses one window: opening another file replaces the current media and saves its position. Launching Cue again brings that window forward.

Cue plays common video formats through libmpv, including MKV, MP4, MOV, WebM, and AVI. Audio-only MP4, OGG, Opus, MP3, M4A, AAC, WAV, and FLAC use a reactive pixel waveform. Album artwork does not prevent audio mode. If visualization initialization fails, Cue retries without it.

At the end of a video, Cue pauses on the last frame. Play restarts it. Enable repeat to loop continuously.

## Controls

| Key / action | Behavior |
|---|---|
| Space / click video | Play / pause |
| F / Alt+Enter / double-click | Toggle fullscreen |
| ← / → | Seek 5 seconds |
| Shift+← / Shift+→ | Seek 30 seconds |
| « / » buttons | Seek 15 seconds |
| L | Toggle repeat; preference is saved |
| ↑ / ↓ / mouse wheel | Volume, up to 125%; orange above 100% |
| M | Mute |
| N / media next key | Open the next file when Cue found one |
| [ / ] | Adjust playback speed |
| 1× button | Speed slider (0.25×–3×), 0.05 steps, and presets |
| \ | Reset speed to 1× |
| C | Subtitle panel |
| S | Toggle subtitles |
| Z / X | Subtitles 0.1 seconds earlier / later |
| Ctrl+O | Open a file |
| Ctrl+V | Open video/subtitle files copied in File Explorer |
| Ctrl+T | Always on top (the pin beside the window buttons) |
| Ctrl+plus / Ctrl+minus / Ctrl+0 | Next UI size / previous / default |
| Tab / Shift+Tab | Navigate controls and panels |
| Esc | Close panel, exit fullscreen, then return home |
| Click either time display | Toggle elapsed / remaining time on the left |

The music icon beside **CC** appears only for files with multiple audio tracks. Its menu also offers **multiple tracks**, where any of the tracks can be ticked to play at once (or **all** of them): made for recordings in parts, like an OBS capture with the game, the microphone and voice chat on separate tracks. Cue mixes such files on its own when none of their tracks carries a language or is named like a dub or commentary.
Right-click to view file information, save a frame without subtitles, or close the video. The saved-frame confirmation can open its folder with the image selected.

## Subtitles and appearance

Open **CC → style** to change the font, size, opacity, background, and position. Subtitle formats may limit available styling. Drag subtitles to reposition them; playback pauses during the gesture and resumes if it was playing. If they end up hard to read, **CC → restore default** (it appears, with a round arrow, whenever the look differs from the default) brings back your default look and position in one click; click it again right away to undo.

The **…** menu contains ambient lighting, interface size, control background, language, and About Cue. Four sizes are available: small, medium, large, and larger; medium is the fresh-install default. Each is a quarter step from the next, and every step stays visibly different at any window or screen. **Ctrl +** / **Ctrl −** (or Ctrl and the mouse wheel) step through them from anywhere, and **Ctrl 0** goes back to medium; the size's name shows for a moment. Sizes adapt to window width and DPI, with a stacked layout for vertical video. Video and subtitle size are independent of UI size.

Ambient lighting follows scene colors and fades in after playback starts. It is hidden on the home screen and in fullscreen or maximized mode.

Cue supports Brazilian Portuguese, English, Spanish, French, and Simplified Chinese. Automatic mode follows Windows display-language preferences, then falls back to English. Change the language in **… → language**, or the **…** beside Recent on the home screen. Changes apply immediately and are saved.

## Phone remote

Open **… → remote control** while a film plays, or **··· → remote control** beside Recent on the home screen. Cue shows a QR code with its network address and a six-digit code. Point the phone's camera at the code (or type the address in the phone's browser and enter the code). The phone and the computer must be on the same network. Nothing is installed on the phone.

The first time, Windows may ask whether Cue can use the network: allow it on **private** networks. On a network marked as public, Windows blocks phones from reaching Cue.

| On the phone | Does |
|---|---|
| Turn the wheel | Move through the film; faster turns take bigger steps (5 s, 15 s, 1 min). Cue seeks when the finger lifts |
| Center button | Switch the wheel between time and volume; in menus and folders, confirm |
| « / » | 15 seconds back / forward. Hold « for ten seconds back with subtitles shown for that moment |
| ▶❚❚ or double-tap the eyes | Play / pause; resuming steps back 3 seconds |
| menu | Subtitles (on/off, timing, size, position), audio track, sleep timer |
| Folder button | Browse the shared folders and open a file on the computer |
| The bar | Tap or drag to seek (or to set the volume, in volume mode) |

In menus and folders the « button becomes a back arrow; **menu** closes. Subtitle position is dragged on a small screen on the phone, or nudged with the wheel and the − / + buttons.

**Next episode.** Cue looks for the file after the current one in its folder: by episode number (S01E04, 1x04, Ep 4) or by name. In the last 90 seconds the phone offers it. With a phone connected, it starts on its own a moment after the episode ends, unless the sleep timer is set to the end of the episode.

**Shared folders.** The pairing screen lists them as chips: Videos and Downloads start on, Desktop starts off, and **+** adds any folder. A phone can list and open only what is inside a folder that is on. Thumbnails are the ones Windows shows in Explorer, shrunk to the remote's pixel size; files without one get a pixel landscape.

**Privacy and pairing.** Cue listens on the local network only (port 7878, or the next free one) and answers only addresses of private networks. The code is accepted only while the pairing screen is open; several wrong codes lock pairing for a minute. A paired phone keeps a token in its browser; Cue remembers the last eight. **Turn off** on the pairing screen stops the server and tells connected phones; it stays off until the remote is opened again.

The phone's browser cannot use its volume buttons for the film, and only Android phones vibrate with the wheel (Safari gives web pages no access to the iPhone's motor); every phone gets the soft tick.

## Files and settings

- Preferences, recent files, and positions: `%APPDATA%\tinyoffice\Cue\state.json` (including the remote's shared folders and paired phones).
- Backup: `state.json.bak`; corrupted state is preserved as `.corrupt`.
- Saved frames: the Windows Pictures folder, under `Cue\Frames`.
- Shader cache: `%APPDATA%\tinyoffice\Cue\shader-cache`.

The old `tinyoffice\Player` state and `.tinyoffice Player` frames migrate on first launch. Internal folder names remain for compatibility.

| Command | Purpose |
|---|---|
| `Cue.exe --welcome` | Show onboarding again |
| `Cue.exe --register` | Register “Open with” for the current user; does not change the default app |
| `Cue.exe --log` | Enable `mpv.log` in the state folder |

About Cue includes credits, an in-app license reader, source links, and update checks. Cue checks for updates every 15 days. See [installation and updates](UPDATES.md).

### Remote security

The pairing code works once and expires after two minutes; a waiting pairing screen refreshes it. Six wrong attempts in a minute lock pairing for one minute. Turning the remote off revokes all remembered phones. This update requires existing phones to pair again once.

The remote uses HTTP on private IPv4 networks. Pairing controls access; it does **not** encrypt traffic. Use a trusted home network. Cue does not request router port forwarding. Only explicitly enabled, local-drive folders can be browsed; UNC paths and junctions/symlinks below those folders are refused.

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
| [ / ] | Adjust playback speed |
| 1× button | Speed slider (0.25×–3×), 0.05 steps, and presets |
| \ | Reset speed to 1× |
| C | Subtitle panel |
| S | Toggle subtitles |
| Z / X | Subtitles 0.1 seconds earlier / later |
| Ctrl+O | Open a file |
| Ctrl+V | Open video/subtitle files copied in File Explorer |
| Ctrl+plus / Ctrl+minus / Ctrl+0 | Next UI size / previous / default |
| Tab / Shift+Tab | Navigate controls and panels |
| Esc | Close panel, exit fullscreen, then return home |
| Click either time display | Toggle elapsed / remaining time on the left |

The music icon beside **CC** appears only for files with multiple audio tracks.
Right-click to view file information, save a frame without subtitles, or close the video. The saved-frame confirmation can open its folder with the image selected.

## Subtitles and appearance

Open **CC → style** to change the font, size, opacity, background, and position. Subtitle formats may limit available styling. Drag subtitles to reposition them; playback pauses during the gesture and resumes if it was playing.

The **…** menu contains ambient lighting, interface size, control background, language, and About Cue. Four sizes are available: small, medium, large, and larger; medium is the fresh-install default. Sizes adapt to window width and DPI, with a stacked layout for vertical video. Video and subtitle size are independent of UI size.

Ambient lighting follows scene colors and fades in after playback starts. It is hidden on the home screen and in fullscreen or maximized mode.

Cue supports Brazilian Portuguese, English, Spanish, French, and Simplified Chinese. Automatic mode follows Windows display-language preferences, then falls back to English. Change the language in **… → language**, or the **…** beside Recent on the home screen. Changes apply immediately and are saved.

## Files and settings

- Preferences, recent files, and positions: `%APPDATA%\tinyoffice\Cue\state.json`.
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

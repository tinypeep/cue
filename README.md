![Cue](docs/cue-banner.png)

# Cue · .tinypeep

**The video is the interface.**

A free, open-source video and audio player for Windows. Lightweight, local, and built around your content.

**[Download Cue](https://github.com/tinypeep/cue/releases/latest)** · [Português](README.pt-BR.md) · [Changelog](docs/CHANGELOG.md) · [Manifesto](MANIFESTO.md)

## Made for watching

- Minimal pixel interface with two control styles and four sizes that follow the window and the screen's DPI, up to 8K.
- Subtitles with font, size, position, and timing controls.
- Dynamic ambient lighting that follows the scene.
- Audio-only playback with a pixel spectrum visualization that follows the music.
- One window, saved playback positions, and on-demand timeline previews.
- Up next: a temporary file/folder queue one click away in the control bar, shared with the phone remote.
- Eight languages, selected automatically from Windows or changed in the app.
- A phone remote with a throwback click wheel: pair with a QR code, no app to install. Time, volume and up next modes.

## Get started

Download `Cue-1.3.1-Setup.exe` from the [latest release](https://github.com/tinypeep/cue/releases/latest) and run it. **Windows x64.** No administrator privileges required.

Open a file, drag it onto Cue, or use **Open with** in File Explorer.

| Shortcut | Action |
|---|---|
| Space | Play / pause |
| F or double-click | Fullscreen |
| ← / → | Seek 5 seconds |
| C | Subtitle controls |
| N / P | Next file, when Cue found one / previous file in the queue |
| Ctrl+O | Open a file |
| Q | Up next queue |
| Ctrl+Shift+O | Open a folder |
| Esc | Close a panel, leave fullscreen, or return home |

More controls and settings: [User guide](docs/GUIDE.md).

## Build

Use `build.bat` in an MSVC x64 developer prompt, `make CROSS=` with MinGW-w64, or `make` on Linux with `g++-mingw-w64-x86-64`. Builds are parallel by default (`make JOBS=8` to limit); `make dev` is a faster `-O1` no-LTO iteration build.
Place the release's `libmpv-2.dll` in `build/Cue/`. Run `make setup` to build the installer.

Portable tests: `bash tools/test.sh`. Playback and native interactions require Windows validation.

[Architecture](docs/ARCHITECTURE.md) · [Languages](docs/LANGUAGES.md) · [Releases](docs/UPDATES.md) · [Performance](docs/PERFORMANCE.md) · [Release checks](docs/RELEASE-CHECKLIST.md)

## Thanks

The Russian translation was reviewed with help from [omni38](https://github.com/omnizs38). See [Acknowledgments](docs/THANKS.md).

## License

Cue's code is **GPL-3.0-or-later**. Fonts and third-party components retain their own licenses. See [LICENSE](LICENSE), [copyright](COPYRIGHT.md), and [component licenses](docs/LICENSES.md).

Corresponding playback engine sources are provided as `Cue-1.3.1-Engine-Source.zip` on the [release page](https://github.com/tinypeep/cue/releases/tag/v1.3.1).

Forks and modified builds: [NOTICE](NOTICE.md) · [Branding and build identity](docs/BRANDING.md).

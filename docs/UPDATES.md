# Installation and updates

## Update behavior

Cue reads the latest public GitHub release from `CUE_UPDATE_REPO` in `src/core/version.h` (currently `tinypeep/cue`):

```text
GET https://api.github.com/repos/tinypeep/cue/releases/latest
```

Checks run every 15 days after launch on a low-priority maintenance thread, or manually from About Cue. Failed offline checks retry on a later launch. The tag must be newer than `CUE_VERSION`; drafts and prereleases are excluded by this API endpoint.

The updater selects an `.exe` asset containing `setup` in its name, downloads over HTTPS, and checks size, GitHub's SHA-256 `digest`, and executable signature bytes. The installer checks payload CRC-32 before writing files. These checks do not imply a digitally signed executable.

An update appears on the home screen, in More, and in About. It can be skipped for that version. Updating saves playback position, closes Cue, runs the installer with `--update`, and reopens the app.

## Publish a release

1. Keep `tinypeep/cue` public, or update `CUE_UPDATE_REPO`.
2. Update `CUE_VERSION_*` and `CUE_VERSION` in `src/core/version.h`; add a matching `## <version>` section to `docs/CHANGELOG.md`.
3. Put the matching `libmpv-2.dll` in `build/Cue/`.
4. With `gh` authenticated, run `bash tools/release.sh`. It tests, builds, packages, and publishes `v<version>`. The engine source defaults to `sources/Cue-Engine-Source.zip`; override with `ENGINE_SRC=...` if needed.

For manual publication, run `make setup`, create the matching GitHub tag/release, and attach `Cue-<version>-Setup.exe` plus `Cue-<version>-Engine-Source.zip`. GitHub supplies Cue's source archives for the tag. For the current release, use `v1.0`.

Keep the matching engine source ZIP and license notices available beside the download. The installer contains `sources/README.txt` pointing to that release rather than bundling the large source archives.

## Installer

`installer/setup.cpp` and `setup_ui.cpp` use Cue's pixel renderer. An LZMA payload is appended to the executable by `tools/make_setup.py`.

| Command | Action |
|---|---|
| `Cue-Setup.exe` | Install or update |
| `Cue-Setup.exe --silent` | Install without a window; exit 0 means success |
| `Cue-Setup.exe --update --wait <pid> --dir <folder>` | Wait for Cue, install, relaunch |
| `uninstall.exe --uninstall [--silent]` | Uninstall |

Default location: `C:\Cue`, with no administrator prompt in the documented installation flow. The installer creates Start menu/optional desktop shortcuts, current-user Uninstall and App Paths entries, and an `install.txt` file inventory. Updates remove files no longer shipped. Cue registers “Open with” without changing the Windows default app.

Uninstall removes these entries and registered subtitle fonts. Preferences/recent files can be kept; saved frames in Pictures/Cue/Frames are never deleted. The uninstaller runs a temporary copy to remove its installation files.

Preview without Windows:
```bash
g++ -std=c++20 -O2 -o preview_setup tools/preview_setup.cpp installer/setup_ui.cpp src/ui/*.cpp src/core/*.cpp
./preview_setup out
```

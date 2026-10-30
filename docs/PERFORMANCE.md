# Performance

Cue keeps playback independent from interface work. Hidden/idle UI avoids redraws, minimized windows suspend UI animation, and large temporary buffers are released after engine acknowledgement. Previews run in a low-priority helper; the main decoder and codec support are unchanged.

## Current approach

- Paint the window before loading the engine DLL; overlap engine startup with the home exit animation.
- Cache compiled shaders; register fonts and file associations after launch; enable verbose mpv logging only with `--log`.
- Coalesce engine events and save immutable state snapshots on a worker.
- Redraw only changed home regions and avoid formatting/layout work when visible state is unchanged.
- Pace animation through compositor vblank where available, with timer fallback. Playback UI targets approximately 60 Hz independently of video cadence.
- Delay color sampling by 1.5 seconds and thumbnails by 3 seconds after the first frame. During window dragging, reuse lighting images and defer preview work.
- Reduce hover-thumbnail transfer/cache pixel count without reducing scene-color sampling resolution.
- Use two separable Gaussian passes for the frameless control cloud, with half-resolution intermediate textures; disable them when hidden. Visible blur still has a GPU cost.

## Recorded measurements

These are **historical local measurements**, retained from the supplied development notes. They were not rerun for this documentation edit and are not Windows startup, playback FPS, or total-process memory measurements.

| Measurement | Before | After | Context |
|---|---:|---:|---|
| 1,000 dock redraw updates | 232.95 ms | 52.62 ms | GCC `-O2`, Linux, 1280×720, median of 3 |
| 200,000 dock-state updates | 140.3 ms | 3.7 ms | Portable logic, median of 3 |
| Temporary allocations in those state updates | 400,000 | 0 | Same benchmark |
| Two 3840×2160 opening buffers retained after removal acknowledgement | 63.28 MiB | 0 MiB | Returned to allocator; not measured physical RAM |
| Windows x64 executable size | 1,888,256 bytes | 1,753,088 bytes | Earlier optimization revision |
| Opaque 900×140 layer on 1280×720 | 1.02 ms | 0.71 ms | GCC `-O2`, Linux, best of 15 |
| Same layer while fading | 1.62 ms | 1.36 ms | Same setup |

An earlier visual comparison found 36 matching UI states. OpenGL/Linux checks covered shader alignment at 0/90/180/270-degree rotation; they do not establish D3D11 performance on Windows.

## Reproduce and validate

```bash
g++ -std=c++20 -O2 tools/benchmark_render.cpp src/ui/*.cpp src/core/*.cpp -o render-benchmark
./render-benchmark
bash tools/test.sh
```

Use the same machine, compiler, benchmark, and media to compare revisions. Additional logic benchmarks are in `tools/benchmark_ui.cpp`; buffer/invalidation checks are in `tests/performance_test.cpp`.

`cue.log` timestamps first paint, DLL mapping, engine initialization, open request, file load, first video frame, and iris start. Measure native Windows launch, pause/resume, fullscreen, resize, DPI changes, and playback under representative GPU/media load. Portable tests, Wine runs, and timing simulations cannot establish actual 60 fps or end-user startup time.

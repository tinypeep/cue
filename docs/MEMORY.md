# Memory and resource lifecycle review: 1.1.1

Scope: supplied Cue 1.1.1 first-party code, particularly repeated scene open/close/resize, font fallback lookups, phone browsing, and worker shutdown. This does not certify all native playback, GPU, OS shell or codec allocations.

## Findings and fixes

| Finding | Classification | Change |
|---|---|---|
| Overlay resize calls DeleteObject while the bitmap is selected into its DC, ignores the return and loses the old handle | Incorrect GDI resource ownership; deletion is not guaranteed by the documented API contract | DIB and DC owned together; restore selection, destroy DC and delete bitmap; safe rollback on allocation failure |
| Overlay destruction stops its pacer but does not explicitly destroy its HWND, DC or bitmap | Missing cleanup | Idempotent destroy/destructor; explicitly invoked on app shutdown |
| Closing a scene keeps its DIB and full-window Canvas allocated | Retained buffers, not unbounded leakage by itself | Release both after the close animation finishes; recreate on next open |
| Font fallback caches every unsupported code point as an empty entry | Growing process-lifetime cache | Cache positive entries from the finite font table only; misses allocate nothing |
| Vsync stop times out after 200ms but its worker still references the owner and event | Potential use-after-free/invalid-handle access during slow compositor shutdown | Worker owns shared state/event independently until it actually exits; start/stop are idempotent |
| Phone browser keeps hidden file DOM, blob URLs and pending fetch closures after closing | Resource/cache retention | Clear file DOM/data, revoke blobs, disconnect observer, cancel listing/thumbnail requests; reject late completions |
| Timeline preview blob remains until replaced | One retained image, not an unbounded leak | Revoke on scrub end, next scrub start and media changes; abort obsolete preview requests |

A 3840×2160 BGRA buffer is 31.64 MiB. The scene has both a DIB and a CPU canvas: approximately 63.28 MiB of pixel storage at that size. This is a size calculation, not a measured reduction in total process RSS. Windows/allocators may retain freed pages for reuse.

## Evidence

- **Unsupported glyph reproduction:** 65,533 distinct unsupported code points retained 65,533 additional allocations in the 1.1.1 object code. The corrected renderer retained **zero additional allocations** for the same sequence. Repeated supported CJK glyph lookups remain stable.
- **DIB owner regression:** 5,000 resize operations keep at most one owned DIB/DC pair; allocation/DC/selection failures preserve the old valid surface; reset/destruction returns the simulated handle count to zero. The previous sequence leaves 50/50 bitmaps unfreed when the test enforces Microsoft's documented selected-object deletion contract. This test uses counted API doubles, not a measurement of the Windows GDI implementation.
- **Chromium stress:** after 10 warm-up cycles, 100 file-browser open/load/close cycles plus 20 rapid close cycles finish with **0 blob URLs, 0 file images in the closed browser and 0 connected sound-node edges**. Forced-GC JavaScript heap: 2,380,236 -> 2,627,948 bytes (+247,712 bytes, about 242 KiB). Small browser/JIT/cache growth is not evidence of a leak; no claim of zero total retained heap is made.
- **Layout and portable regressions:** existing tests pass, including security, frame-buffer acknowledgement, UI layouts and the added lifetime tests.
- **Windows x64:** application and native GDI-counter test cross-compiled. The native test has not been executed here; real Windows GDI behavior, end-to-end playback memory, GPU allocations and libmpv/FFmpeg leaks remain unprofiled.

## Intentional bounded storage

The timeline thumbnail cache, remote thumbnail cache, known fallback glyphs, fonts, synthesized pairing sounds and decoder buffers are intentional. A rise during first use or a high working set alone does not prove a leak. The important distinction is whether repeated identical operations continue to add live resources after warm-up.

## Reproduce

- Portable: `bash tools/test.sh` (`dib_surface_test.cpp`, `glyph_memory_test.cpp`).
- Browser: install Playwright/Chromium, then `node tests/remote_memory.cjs`; `CUE_CHROMIUM_PATH` may select an existing executable. This uses a mock player connection and real browser DOM/fetch/audio lifetimes.
- Native Windows: build and run `tests/overlay_engine_test.cpp` using the command in its header. It checks actual GDI counters across surface/scene cycles and prints process private bytes. Build output is `build/overlay-memory-test.exe`.
- Before release: repeat open/close/resize, fullscreen, file changes and phone browsing on a real Windows PC; record private bytes, GDI objects, handles and GPU memory after warm-up and again after sustained use. A long-run native profiler is still needed before claiming the whole player is leak-free.

Reference: [Microsoft DeleteObject](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-deleteobject) documents that an object currently selected into a DC is not eligible for ordinary successful deletion. [SelectObject](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-selectobject) describes restoring the previously selected object.

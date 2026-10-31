# Disk I/O and RAM review: 1.1.1

Scope: Cue 1.1.1 application code plus the exact bundled mpv source (`ad5877edabdb05c0c62aad444722938d37666e2d`). This review identifies avoidable work and validates component limits; it does not measure the user's Windows disk or total player RAM.

## Disk activity

The preview helper opens the same media separately from playback and decodes on the CPU. It already runs at Windows background I/O/memory priority with `cache=no` and `demuxer-readahead-secs=0`. Those settings do not make its random reads free.

Previously, exploring the timeline enabled speculative background seeks at up to one request every 0.45 seconds, spanning roughly 65 distant timestamps. On a HDD or network share this can compete with playback. **1.1.1 removes this full-timeline prefetch.** Timeline/phone frames are generated on demand and cached. A first visit to an uncached point may wait for one decode; cached points remain immediate.

Ambient-light sampling is deliberately preserved: it samples the current media time at the existing cadence (about 0.9 seconds with ambient lighting enabled, 4 seconds for accent-only use; the initial warm-up remains 1.5 seconds). It can still produce extra reads and CPU work. Sampling frames are now transient and no longer populate/update the timeline cache. No claim that ambient lighting has zero I/O cost is made.

Settings were rewritten on periodic saves even with identical serialized data. 1.1.1 skips a repeated position save before scheduling a writer, and skips main-file writes when state is unchanged. A missing/outdated recovery copy may be refreshed once; repeated identical saves then leave both files untouched. Genuine changes retain the temporary-file/rename and recovery workflow. The periodic playing-position interval stays at 10 seconds.

The installer also excludes temporary build/sync leftovers so partial engine copies cannot inflate the installed package.

The generated blur shader is now reused when identical. The generated input bindings were already reused; their comparison now uses bounded scratch storage. Shader compilation caches are kept, because removing them would increase startup work.

Normal operation does not write a copy of the whole video: the bundled mpv default has disk packet caching off, and timeline frames stay in RAM. User-supplied mpv configuration can change engine defaults. A normal read of the video, OS cache activity and pagefile traffic must be distinguished from application-generated file writes.

## RAM limits and changes

- Timeline UI frame cache: **12 MiB of pixel-vector capacity**, at most **256 entries**. Accounting uses vector capacity; LRU eviction keeps recently used pictures. Map/node metadata adds a small count-bounded overhead. Oversized pictures are rejected without an automatic retry loop.
- Colour-only frames: released after colour analysis, preserving the sampling resolution.
- Remote shell thumbnail cache: existing maximum 400 small BMP entries, about 4.64 MiB of image bytes plus metadata. Three simultaneous shell-thumbnail jobs can temporarily need additional buffers.
- Exact bundled mpv defaults: forward packet limit **150 MiB**, backward limit **50 MiB**, local readahead target **1 second**; `cache=auto`. These are packet-cache limits/targets, not a total RAM cap, and may be slightly exceeded for technical reasons. Local playback does not necessarily fill these budgets; network sources may use more of them.
- Decoded reference frames, audio mixing, subtitles/fonts, UI surfaces, shell providers, GPU/driver allocations and the separate preview process add memory. A 12 MiB thumbnail budget is **not** a promise that the whole player uses 12 MiB.
- The memory lifecycle fixes from 1.1.1 remain in place. High working set alone does not establish a memory leak.

## Validation

- A controlled 400-image fixture (320×180 BGRA each) retains **87.89 MiB** under a count-only cache. The new cache stayed at or below **12 MiB** through 1,000 inserts (observed 11.87 MiB). This is a component stress fixture, not measured RAM savings on the user's media or PC.
- Tests cover capacity accounting, entry limits, LRU, replacement, oversized rejection and release on clear.
- After recovery-copy warm-up, 1,100 identical settings-save calls leave both file modification times unchanged. Real changes and corrupt-state recovery remain covered. Files are still read for comparison when the general save path runs.
- Portable regressions and Windows x64 cross-compilation are used. Native playback bandwidth, physical-disk active time, GPU memory and total RAM have not been measured here.
- `tools/measure_resources.ps1` is an optional, read-only Windows probe for Cue.exe processes (player and thumbnail helper). It records private bytes, working set and logical process I/O to CSV. It has not been executed in this Linux environment. Logical I/O includes more than physical disk reads and must not be interpreted as physical-disk busy percentage.

## Native check

With Cue open, run from the source directory:

```powershell
powershell -File tools\measure_resources.ps1 -Seconds 120
```

Compare the same file/device across revisions. Capture playback without timeline interaction, playback while exploring the timeline, pause and a closed-video/home phase. Sum the relevant player/helper rows; do not count shared DLL pages twice as private memory. Use Windows Resource Monitor or an ETW/WPR trace to correlate process activity with physical-disk busy time/latency and pagefile activity. The probe does not change Cue settings or stop the player.

## Remaining storage considerations

`cue.log` is append-only and verbose `mpv.log` is enabled only with the explicit `--log` switch. Downloaded update installers use unique temporary folders and can remain after an update. These can contribute to disk-space growth over time; this change does not delete existing logs/installers or perform a broad temporary-file cleanup. None of these observations proves a current disk-saturation incident.

## Sources

- Bundled `archives/mpv.tar.gz`, `demux/demux.c`: defaults and supported options for the exact engine distributed with Cue.
- [mpv cache and demuxer options](https://mpv.io/manual/master/): packet/cache behavior; current upstream defaults may differ from the bundled revision.
- [Microsoft GetProcessIoCounters](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getprocessiocounters) and [IO_COUNTERS](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-io_counters): process-I/O accounting.

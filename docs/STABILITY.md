# Additional stability review — Cue 1.1.1

This build consolidates the earlier phone UI, security, memory and disk changes under **1.1.1**, as requested. The temporary 1.1.2/1.1.3 development labels do not represent separate releases in this source tree. The playback engine DLL is unchanged.

## Findings fixed in this pass

| Finding | Impact | Change |
|---|---|---|
| UI writes directly to the preview helper's synchronous pipe | A slow/stuck decoder or repeated file changes can fill the pipe and freeze the UI | Dedicated writer with a bounded mailbox: 16 pending commands, 64 KiB total, 16 KiB per line. New open/close commands discard obsolete queued work. Failure disables optional previews |
| Preview reader uses its owner's address and pipe handle after timed shutdown; exit path leaves the read handle open | Potential stale-owner/handle access during restart; inconsistent cleanup | I/O workers own shared state and handles until completion. Shutdown terminates the optional child, cancels queued work, prevents further posts and deletes already-posted frame messages |
| Child process inherits all inheritable handles; job assignment errors ignored | Unintended resource retention and weakened child cleanup guarantee | Explicit two-handle inheritance list; require successful kill-on-close job configuration/assignment before resuming the child |
| Remote commands bounded, but connection lifecycle events unbounded | A paired client repeatedly reconnecting while the UI is busy can grow the event vector | 256-event hard limit, authentication backpressure at 192, command limit 128; room reserved for all active clients to disconnect |
| Remote shutdown changes sockets to blocking mode and sends to each phone | Multiple non-reading phones can delay the UI's join | Best-effort shutdown notification on nonblocking sockets; remove the extra worker wait |
| HTTP worker cleanup uses only the success path | Exceptions can leave resource counters/gates inconsistent; Winsock ownership can race shutdown | Scope cleanup for socket/counter/Winsock and thumbnail gate; acquire Winsock reference before launching each worker; suppress obsolete work/replies |
| Settings values cast before range validation; version components multiply without overflow checks | Corrupt/extreme numeric values cause undefined behavior and implausible window restoration | Safe integer conversion before clamping, geometry validation, bounded stored media times, saturating version parsing and updater asset-size rejection |
| Settings/language readers consume whole files, and JSON tree count is unbounded | A corrupt oversized file or a compact array can amplify startup RAM use | 4 MiB state-file read limit, 65,536 JSON value limit, existing depth limit retained; backup recovery tested |

Additional preview checks reject oversized command lines, line/NUL injection and non-finite frame times. Screenshot result nodes are released even for unexpected types, and stride bounds are checked without overflowing a multiplication.

## Validation and evidence

- Portable suite passed, including the existing 27,630 layout checks, 12,000 malformed remote inputs, memory/cache and disk-write regressions.
- New tests cover 100,000 command replacements, bounded bytes/count, cancellation of a waiting consumer, a 100,000-iteration lifecycle-event flood, extreme integers/version strings, oversized state-file recovery and JSON node limits.
- AddressSanitizer/UndefinedBehaviorSanitizer passed for the preview mailbox; UndefinedBehaviorSanitizer with float-cast-overflow checks passed for the settings/release tests. LeakSanitizer is not claimed: leak detection was disabled because this environment does not support its process inspection reliably.
- Chromium remote UI regression passed at six portrait/landscape sizes. The browser uses a mock player connection.
- Browser lifecycle stress: 10 warmups, 100 open/close cycles and 20 rapid-close cycles ended with **zero tracked blob URLs, grid images and audio graph edges**. Forced-GC JS heap changed from 2,380,332 to 2,606,804 bytes (+226,472 bytes). This is not proof of zero whole-app leaks.
- Windows x64 app/installer cross-compilation passed. The native `thumb_client_engine_test.cpp` deliberately starts a non-reading helper, floods file changes, restarts 50 times and checks handle counts. It was compiled, **not executed here**.
- Installer contents, CRC, executable identity, version and the unchanged engine DLL hash are checked separately during packaging.

## Remaining limits

No claim of a complete security audit, leak-free process or measured speedup on the user's Windows PC is made. Real Windows playback, codec/GPU allocations, Winsock/COM integration, stalled-process cancellation and iOS Safari still need native validation. Explorer providers can stall within the fixed worker budget; a stuck provider is not forcibly terminated in-process. HTTP/WS remains unencrypted, and bearer tokens remain in per-user storage. See [Security](SECURITY.md), [Memory](MEMORY.md) and [Disk/RAM](RESOURCES.md).

The 12 MiB budget applies to the timeline pixel cache, not the entire player. The existing engine buffering and ambient-light sampling behavior are preserved. Settings remain compatible with the previous working builds.

Since the public version stays 1.1.1, an already installed 1.1.1 will not detect this revision as a newer semantic version; install this revised package manually. This does not remove prior corrections.

## API references

- [Microsoft: anonymous pipe operations](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations) — synchronous writes can block when the pipe is full.
- [Microsoft: handle inheritance](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance) — restrict inherited handles using an explicit attribute list.
- [Microsoft: nonblocking socket I/O](https://learn.microsoft.com/en-us/windows/win32/winsock/nonblocking-i-o-2) — operations return rather than waiting for a peer.

# Security review: 1.1.1

Scope: first-party application code supplied with Cue 1.1, especially the LAN remote, file boundary, phone page and updater. This is a targeted review and hardening change, not an independent penetration test or a review of all vendored media libraries.

## Threat model and changes

| Boundary | Previous behavior | 1.1.1 |
|---|---|---|
| Browser origins | Literal Host only; no Origin check | Host must be the socket’s actual local IPv4 endpoint; WebSocket Origin must match it exactly; cross-site HTTP fetches refused |
| Pairing | Four digits; accepted while the screen was open | Six digits, two-minute lifetime, one use, automatic refresh while waiting; existing six-failure/minute lock retained |
| Credentials | RNG failure fell back to predictable state; tokens in URLs | BCrypt only, failure refuses credentials; 256-bit tokens, first WebSocket message / HTTP Bearer header |
| Revocation | Token checked at connection; off retained phones | Tokens rechecked on connections/commands; turning off clears remembered phones; old credentials rejected |
| Files | Case-insensitive string prefix; only exact dot segments rejected | Normal drive paths only; reject NUL, ADS, device names, trailing dots/spaces, UNC/device namespace, traversal and descendant reparse points; compare resolved handles |
| Untrusted input | Permissive JSON, open command names and numeric values | Bounded payloads, complete strict JSON, duplicate-key rejection, command allowlist and numeric ranges; WebSocket opcode/control checks |
| Resource use | Unbounded UI command queue; up to 48 file threads | 128 queued commands, 256 total events and authentication backpressure at 192 events; 20 frames/s with burst 40 per socket; 512 KiB outgoing cap; 5s unauthenticated timeout; six file workers, three thumbnail jobs |
| Browser content | No CSP / frame restriction | Fresh script nonce, restrictive CSP, frame denial, no-referrer, no-store for private responses, nosniff and permissions restrictions |
| Updates | Predictable temp path; missing digest could fall back to file size | Unique CSPRNG temp directory; create-new file; SHA-256 required before successful download completion |

The old token requirement already prevented ordinary unauthenticated remote commands. Missing Origin validation is defense in depth; this review did not demonstrate an unauthenticated account takeover. The path boundary issues concern an already paired client escaping an enabled share.

## Limits that remain

- **HTTP/WebSocket traffic is not encrypted.** A PIN is access control, not transport protection. A compromised LAN/router or on-path attacker remains a risk. Use a trusted LAN; do not expose the port to the internet. TLS with a trustworthy local certificate or a secure companion transport is the next security project. A self-signed warning is not a complete solution.
- Remembered tokens are bearer credentials stored in the phone’s localStorage and Cue’s per-user state file. This change does not add DPAPI protection at rest or a per-device management screen. Turning off revokes all devices.
- Private IPv4 filtering is not proof that a device is on the same Wi-Fi: routed private/VPN networks can also reach a listening endpoint. Access still requires pairing/a remembered token.
- Resolved-path and reparse checks restrict a remote-only attacker. A hostile local process with write access to shared directories can race path-based operations after validation. No claim of protection against a compromised Windows account is made.
- Explorer thumbnail providers and filesystem calls can stall. Work is bounded and responses invalidated on stop/share/credential changes; blocked OS calls are not forcibly terminated.
- The supplied libmpv/FFmpeg DLL was reused byte-for-byte. Codec vulnerabilities, native GPU performance and media-format coverage require their own review/testing.
- The application/installer remains unsigned. SHA-256 verification is not an Authenticode publisher signature.

## Validation

- Portable test suite passed, including the existing 27,630 layout checks and the new remote security test.
- AddressSanitizer/UndefinedBehaviorSanitizer passed for the parser/security test. LeakSanitizer was unavailable under the environment’s process restrictions.
- 12,000 deterministic malformed HTTP/WebSocket/JSON inputs, path boundary cases, numeric ranges, token migration, rate budgets and pointer routes in portrait/landscape.
- Browser regression: Chromium, 320×568, 375×667, 390×744, 390×844, 430×932, 844×390; home, folders, playing state, menu and six-digit pairing. The browser uses a mock player connection. Primary wheel/folder targets measured at least 44 CSS pixels; no horizontal overflow.
- Native scene renders inspected for portrait and small landscape windows.
- Windows x64 application and installer cross-compiled. The supplied engine DLL remains unchanged.
- **Not executed here:** real Windows Winsock/COM integration, real iOS Safari, GPU/playback benchmarks, or a complete adversarial network penetration test. Wine execution was blocked by the environment’s socket permissions.

Before publishing, exercise pairing/reconnect, wrong-code lockout, code expiry, turning off/revocation, folder toggle/junction rejection and fullscreen/playback on a real Windows PC with an iPhone/Android device.

Optional browser reproduction: install Playwright/Chromium, then `node tests/remote_browser.cjs`. `CUE_CHROMIUM_PATH` can point to an existing compatible Chromium executable. Native pairing preview: `tools/preview_remote.cpp`. Core tests: `bash tools/test.sh`.

## References

- [OWASP WebSocket Security Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/WebSocket_Security_Cheat_Sheet.html): Origin checks, authentication, limits and backpressure.
- [Microsoft GetFinalPathNameByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew): resolved paths.
- [Microsoft reparse point operations](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-operations): file attribute/reparse behavior.

# Security review: 1.1.1

Scope: first-party application code in Cue, especially the LAN remote, file boundary, phone page and updater. This is a targeted review and hardening change, not an independent penetration test or a review of all vendored media libraries.

## What changed after 1.1.1

| Boundary | Before | Now |
|---|---|---|
| Paired phones | The state file held the tokens themselves: a state file that someone read handed over every phone | The state file holds the credentials encrypted with DPAPI (base64 on non-Windows); the server never sees the encrypted form and never holds them on disk. A state file that someone reads is not a set of keys. |
| The listening socket | Bound to every adapter; only a filter after the connection refused the internet | Bound to the network address itself (the LAN address, or every adapter when there is none); the peer filter stays as the second layer |
| Pairing guesses | Six wrong codes from any address locked pairing for everyone | Six wrong codes lock that address only; a second, wider budget still covers guessing from many addresses at once |
| The pairing code | Compared with `==`, which stops at the first wrong digit | Compared without an early exit, so the time it takes says nothing about the code |
| The remote's requests | The WebSocket had a budget; the HTTP requests had none | Every connection has a budget for its requests too (40 a second, 120 in a burst) |
| The update's host | The address of the download came from the release, and any host was followed | Only GitHub's hosts are followed, redirects included, one at a time |
| The update's publisher | The digest was the only check | The publisher certificate is checked when one is configured (`CUE_PUBLISHER_THUMBPRINTS` in `src/core/version.h`); the project is not signed yet, so the log says so |
| The state file | Written and swapped in, but never flushed: a power cut could leave a torn file behind a backup that had already been replaced | The temporary file is created (never opened, so a link there is not written through), flushed to the device before the swap, and the directory entry is flushed after it |

## Transport encryption

The credential never crosses the wire after pairing. A connection proves itself by answering a challenge, and every HTTP request signs with the session of the connection that opened it.

- **Challenge–response** (`t:"chal"`): when a WebSocket opens, the server sends a fresh nonce; the phone answers with `HMAC-SHA256(credential, nonce)` and the `kid` of its credential. The server compares without an early exit.
- **Session-signed requests**: the server sends `sid` and `sk` (per-connection, random, forgotten with it) in the hello frame. Every HTTP request carries `Authorization: Cue <sid>:<counter>:<hmac>`, with the counter verified within a window to reject replays and reject far-ahead counters.
- The credential itself is sent once, at pairing, to the phone that pairs. From then on, only it is stored — encrypted with DPAPI.

The page serves its own CSP, nonces, and strict origins.

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

## Remaining limits

- **HTTP/WebSocket traffic is not encrypted.** The challenge–response protocol above proves the phone holds the credential, but a PIN is access control, not transport protection. A compromised LAN/router or on-path attacker remains a risk. Use a trusted LAN; do not expose the port to the internet. TLS with a trustworthy local certificate or a secure companion transport is the next security project. A self-signed warning is not a complete solution.
- Private IPv4 filtering is not proof that a device is on the same Wi-Fi: routed private/VPN networks can also reach a listening endpoint. Access still requires pairing/a remembered credential.
- Resolved-path and reparse checks restrict a remote-only attacker. A hostile local process with write access to shared directories can race path-based operations after validation. No claim of protection against a compromised Windows account is made.
- Explorer thumbnail providers and filesystem calls can stall. Work is bounded and responses invalidated on stop/share/credential changes; blocked OS calls are not forcibly terminated.
- The libmpv/FFmpeg DLL is unchanged. Codec vulnerabilities, native GPU performance and media-format coverage require their own review/testing.
- The application/installer remains unsigned. SHA-256 verification is not an Authenticode publisher signature.

## Verification

Current release validation is recorded in [VALIDATION.md](VALIDATION.md).
Portable tests do not establish native GPU performance or whole-process leak freedom.

## References

- [OWASP WebSocket Security Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/WebSocket_Security_Cheat_Sheet.html): Origin checks, authentication, limits and backpressure.
- [RFC 4231](https://tools.ietf.org/html/rfc4231): HMAC test vectors.
- [Microsoft GetFinalPathNameByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfinalpathnamebyhandlew): resolved paths.
- [Microsoft reparse point operations](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-operations): file attribute/reparse behavior.

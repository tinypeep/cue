# Remote and episode follow-up — 1.1.1

- Portrait pairing chooses an independent art scale using both available axes. At 1000×1780 with the playback scale at 2, pairing uses scale 4. QR modules remain integer pixels; folder chips wrap. Landscape retains the playback scale.
- Escape received through the player/engine prioritizes the active remote scene. Closing clears the first-frame reset flag and arms a fallback timer, so cancellation is not undone by a late frame. Drag handoff is posted, and keyboard focus returns to the owner after closure. Leaving the scene resumes a pause it initiated even if the pause acknowledgement was delayed. Going Home closes the scene without resuming media.
- System volume/mute key mappings no longer change Cue's own gain/mute. Arrow keys and the in-app controls still adjust Cue.
- The player's More menu includes Next episode when a next playable sibling exists. Numbered video episodes get a compact top toast in the final 10 seconds at the selected playback speed. Click the text to advance, the cross or Escape to cancel for this file. Pausing stops the countdown. Looping and the end-of-episode sleep timer prevent automatic advance. Advance happens at EOF, preserving the final seconds of content.
- The phone's linear timeline uses the dragged position for both its fill and knob. The lit outer-wheel dot has a generous angular hit area and starts continuous seeking; other wheel gestures retain their existing fine/coarse detents. Seeks commit at gesture release.

Portable regression covers portrait sizing, Esc scene state and next/cancel click targets. The Chromium test checks that moving the timeline to 80% sets both fill and knob to 80%, alongside the six existing viewport checks. A native portrait render was inspected. Real Windows keyboard focus/compositor and physical keyboard volume behavior, playback transitions and iOS Safari require device validation; cross-compilation is not native execution.

All previous 1.1.1 memory, resource and security corrections are retained. See STABILITY.md and SECURITY.md.

## Episode handoff correction

Opening media now takes ownership of its path argument before resetting discovery state. Previously, `open(next_ep_)` borrowed `next_ep_`, then cleared it before `loadfile`, sending an empty destination for player, phone and automatic requests. Empty open requests are now rejected before changing playback state.

Automatic advancement captures an owned destination at EOF, also handles the terminal END_FILE notification, and preserves a pending transition across idle notifications. Its timer consumes the captured path once; changing files, returning Home and cancelling invalidate it. The end-of-episode sleep timer continues to suppress advancement.

The portable EpisodeAdvance regression verifies ownership after discovery is cleared, duplicate EOF, one-shot consumption and cancellation. Windows playback itself still requires real-device validation.

## Series identity and next-file card

Next episode now compares the normalized title before the episode marker as well as season/episode numbers. It supports S01E02, 1x02 and episode-word markers, punctuation/spacing differences and leading bracketed release groups. A different show or an unidentifiable title is never an automatic continuation. Folder order can still supply a manual Up next suggestion. Number-only names and renamed series require manual selection; filename matching is conservative and is not a media database.

Player and phone distinguish automatic episodes from manual suggestions. The player card includes a single truncated filename line and a small 16:9 thumbnail beside its header. It reuses the shell thumbnail decoder used by the remote. One low-priority PC thumbnail worker at most is allowed globally; it starts within the last 90 seconds, never blocks the UI, and stale results cannot replace the new file's card. Only one 84×48 image is retained. If the Windows provider has no image, a folder placeholder remains visible.

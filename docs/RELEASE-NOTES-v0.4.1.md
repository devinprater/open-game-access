# Release notes — v0.4.1

The **v0.4.0 build crashed on the first emulated frame of every game.** This
release fixes that, and carries the Oct-2 Dissidia work that v0.4.0 predates.

⚠️ **If you installed v0.4.0, replace it.** No game would have booted.

## The crash, and why it happened

The v0.4.0 queue wiring replaced each per-frame `adapter->on_frame()` call with
`EndFrame(core)` — **including the one inside `EndFrame`**, which then recursed
until the stack overflowed on frame one of any emulated game. The adapter hook
is restored.

The guard is now a compile error, not a warning: our own `Core/` sources are
built with `-Werror=infinite-recursion` on the device, simulator and host
builds, so the same edit cannot land silently again. (Third-party emulator code
is untouched.)

## Speech

- **"Where am I" answers even right after an identical automatic line.** The
  exact-repeat gate used to swallow the reply when the per-frame menu watch had
  just said the same row. High-priority lines bypass the gate. The automatic
  watch still stays quiet on a steady row.
- **Completion reports match the right line.** The queue id now travels with
  each utterance instead of being looked up by text, which had three failure
  modes: lines over 63 bytes (most Dissidia dialogue and pop-up text) never
  reported done, so a VoiceOver drop could not trigger the retry; trimmed lines
  no longer matched their stored text; and repeated lines mapped to the newest
  copy.
- **Controllers.** MFi, DualSense, Xbox and other extended gamepads drive the
  console through the input bridge. Face buttons map by position (Nintendo
  layout on DS/GB, PlayStation on PSP); D-pad and left stick, L1/R1,
  Menu/Options. **L2/R2 are reader chord modifiers** — L2: where am I, stop,
  repeat, find path, tiles. R2: prev/next item and group, read item, game
  toggles. The on-screen game pad hides while a controller is connected, and
  connect/disconnect is spoken.

## Dissidia (PSP, ULUS10437)

All of the Oct-2 work, none of which reached hardware in v0.4.0:

- Story-dialogue **speaker names** from portrait IDs (Chaos, Garland, and the
  verified set) and **scene titles** on entry, from 137 scene tables built off
  the script FAQ; runtime scene ID is resolved by content scan, so it survives
  the heap moving every boot.
- Board **tutorial hints** (engage, DP-zero) with a once-only latch, and
  **enemy species names** for the live-verified keys.
- **Board pop-up descriptions** read verbatim from a pinned per-type text
  table. Unknown species still say "enemy here." — never a guess.

Unverified enemies remain unspoken rather than approximated.

## Performance

Long PSP sessions got choppy on device. Three fixes, each measured:

- the frame image moved to its own store, so the session no longer fires
  `objectWillChange` 60 times a second (which re-ran the body of the whole
  control panel on the emulator thread);
- PPSSPP logs at Warning by default instead of Debug on every channel
  (`PPSSPP_LOG_DEBUG=1` restores it for host investigations);
- thermal state changes go to the debug log, and a serious/critical state while
  playing is **spoken**, so a slowdown can be matched to throttling.

Host soak (`scripts/psp-soak.sh`): RSS flat at ~94 MB and ~7 ms/frame over
36,000 frames, from board and battle states.

## Also

The Windows tree and the WSL mirror are retired — `~/oga-work` is the only
checkout, so a stale copy can no longer be built by accident. Game data is
guarded by `scripts/git-hooks/pre-commit` (refuses staged ROMs, saves,
BIOS/firmware and build output) and CI runs `scripts/check-no-roms.sh` before
uploading.

## Downloads

| Platform | File | Notes |
|---|---|---|
| **iOS device** | `OpenGameAccess-v0.4.1.ipa` | **Unsigned** — re-sign with SideStore as usual. Built for arm64 iOS 17+. |
| **iOS Simulator** | `OpenGameAccess-simulator.zip` | macOS only to run. |

The IPA is unsigned on purpose: a sideloading tool can only re-sign a binary
that carries no signature.

## What is verified, and how

- **This build boots.** The device archive and the linked app are both checked
  for the self-call that broke v0.4.0 (0 found), and `scripts/verify-device.sh`
  now *fails* on a simulator binary, a link failure, or an app with no core in
  it — it previously could not fail at all.
- Host suites: adapters (dissidia 177, dbz 17, dq9 26, gba, osk), the
  announcement queue (49 checks) plus 7 sabotage builds that must fail, and
  build-flag parity. The Dissidia live-RAM proof runs the real game in the real
  PPSSPP core: adapter ready at frame 100, title tracked at 120, Where-Am-I
  naming a real row.
- **Not verified on hardware:** the queue's one-line-in-flight behaviour and the
  completion hooks with a screen reader on and off. Please work through
  `docs/device-test-queue-v04.md` and report what you hear — device, reader
  on/off, ROM, step, heard-vs-expected.

# Android ships a STALE, PARTIAL Game Boy reader set — and CI passes anyway

Measured 2026-10-07 against the shipped **v0.6.1** APK
(`app-gitHub-prod-debug.apk`, 57,208,591 bytes, 1573 entries).

## The two findings

### 1. The APK's reader set is an OLD SNAPSHOT, not the one iOS ships

There are **two copies** of the Game Boy / GBA reader set in this repo:

| | path | last touched | files |
|---|---|---|---|
| Android | `app/src/main/assets/lua/gb/` | **2026-09-15** (`c20716a`) | 6 loose `.lua` + subtrees = 195 entries |
| iOS canon | `Sources/OpenGameAccess/Resources/gba-lua/` | **2026-09-20** (`6a080a5`) | 23 entries incl. the shims |

The six loose scripts present in both (`gb.lua`, `gba.lua`, `pokemon.lua`,
`message.lua`, `serpent.lua`, `a-star.lua`) are **byte-identical** — so the
Android copy is not a modified fork. It is simply **five days out of date**, and
in those five days the shim layer was added.

**What the APK is missing** (present in the iOS canon, absent from Android):

    oga_bootstrap.lua     <- the loader that sets up the environment
    mgba_compat.lua       <- the mGBA <- BizHawk API shim
    crc32.lua  oga_audio.lua  oga_bit.lua  oga_capture.lua  oga_pure.lua
    tolk.lua   win-controls.lua   host-sim-rom.lua

`mgba_compat.lua` is the file that implements the reader's required API surface
(`registerexec`, `readbyterange`, …) for mGBA. Shipping the readers without it is
the same class of defect as v0.6.0-nes shipping "NES support" with no reader
assets: **the feature is announced and the piece that makes it work is absent.**

### 2. The NES readers are not in the Android APK at all

`nes-lua/` exists only under `Sources/OpenGameAccess/Resources/`. The Android
asset tree has **zero** NES entries (`app/src/main/assets/` contains no `nes`
path at all). v0.6.1's headline feature — the NES readers reaching the phone —
was verified on **iOS only**; the APK carries none of it.

### 3. The CI gate cannot catch either

    - name: Verify the APK carries the accessibility scripts
      grep -qi lua "$RUNNER_TEMP/apk-list.txt" || { echo "!! no Lua assets"; exit 1; }

`grep -qi lua` matched **197** entries in v0.6.1 — and it is satisfied by
`main.lua` (2.1 MB, the DS reader), by `assets/lua/gb/…` of any vintage, and by
the core's own Lua engine strings. It would have passed just as happily with the
GBA path completely absent. This is the **third instance of the same bug class**
in this repo, after the "gate that cannot fail" JIT check and the Lua-assets
check's own ancestor.

    ⛔ A GATE MUST ASSERT THE PRESENCE OF A FILE THAT ONLY EXISTS WHEN THE
       FEATURE WORKS. "Some .lua file exists" is not that file.

## Status (2026-10-07)

- **Fixed:** the 15 runtime files are now synced into `app/src/main/assets/lua/gb/`
  and committed (`bc9de43`).
- **Fixed:** the CI gate now asserts the specific files (verified by replaying
  both gates against the real v0.6.1 file list — the old one passed, the new one
  fails with the two files that commit adds).
- **Fixed:** the duplicate tree is GONE (`4870f45`). `scripts/android-apply-overlay.sh`
  step 1b stages `assets/lua/gb/` from the canonical tree at build time, excluding
  the two host dev tools, and refuses to stage a set missing the bootstrap or the
  shim. `scripts/check-reader-assets.sh` fails if a second tree reappears and is
  wired into the Android workflow BEFORE the build, so drift fails in seconds
  instead of after a 20-minute APK run.
- **Still open:** the NES readers are iOS-only. `nes-lua/` has no Android
  staging path; `app/src/main/assets/` has no NES entry at all.

## What a correct gate asserts

Per reader set, on the APK's own file list:

* `assets/lua/gb/oga_bootstrap.lua` — the loader, exists only in the current set
* `assets/lua/gb/mgba_compat.lua`   — the shim, the actual blocker
* `assets/lua/gb/gb.lua`, `gba.lua`, `pokemon.lua` — the readers themselves
* and for NES: whatever staging path Android will use, which does not exist yet

## The staging decision this exposes

`app/src/main/assets/lua/gb/` is a **checked-in duplicate** (197 tracked files)
of `Sources/OpenGameAccess/Resources/gba-lua/`. Keeping two copies is what let
them drift. The fix is to stage ONE canonical set at build time — the way
`scripts/android-apply-overlay.sh` already stages the Java/C++ overlay — rather
than maintaining a second hand-copied tree. Until that lands, any future reader
fix must be applied **twice**, and nothing enforces that it was.


---

## The restructure (2026-10-07, `4870f45`)

Syncing the files (the earlier commit) only reset the clock; this removed the
duplicate that caused the drift.

**One canonical tree:**
`Sources/OpenGameAccess/Resources/gba-lua/` — staged by the iOS build
(`Package.swift .copy()`) and now by Android
(`scripts/android-apply-overlay.sh` step 1b).

### Diffing the two trees before deleting one found TWO more divergences

This is the reason to compare before deleting rather than after:

1. **`sounds/` — 33 positional-audio WAVs — existed ONLY in the Android copy.**
   The readers name these paths directly
   (`scriptpath .. "sounds\\gba\\s_grass.wav"`, ~42 `audio.play` sites) and
   `oga_audio.lua` documents them as being in the reader tree, so the iOS bundle
   has never carried the sounds the reader asks for. There was no tree it could
   have taken them from. Moved into the canonical tree.
2. **`bizhawk_compat.lua` differs** (6745 vs 7509 bytes) and git records both
   copies as last touched by the SAME commit (`683e43b`) — the Android one has an
   inert `joypad.set()` with a comment describing it as the first mobile port's
   approach, while the canonical one implements it via `input.JoySet`. Its own
   comment states the consequence: an empty `joypad.set` leaves the release latch
   unable to engage, so a direction held while the modifier trigger is released
   leaks through to the game. Kept the canonical copy for the staged set; the
   top-level Android variant is a **genuine platform difference needing its own
   decision**, not a sync, and is left alone.

### The three gates, all proven by sabotage

| where | what it catches |
|---|---|
| `scripts/check-reader-assets.sh` | missing required files, missing sounds, a duplicate tree reappearing |
| `android-apk.yml` (post-build) | the same five files inside the built APK |
| `android-build-local.sh` | the same five files, for local builds (it had the same weak `grep -qi lua`) |

`check-reader-assets.sh` fails on each of three sabotages: duplicate tree
recreated, shim removed, sounds removed — and passes on the clean tree.

### A NOTE ON THE GATE'S OWN HISTORY

The original gate was `grep -qi lua`, which matched **197** entries in the shipped
v0.6.1 APK and would have passed with the reader path entirely absent. Both the
CI and local gates now assert files that exist ONLY when the feature works. The
broken shipped APK is the ideal fixture for proving such a gate: replay both
against its real file list and require the old one to pass and the new one to
fail.


---

## ⛔ The Android APK workflow does not run on `main` — so none of this was ever exercised

Measured while verifying the restructure: `android-apk.yml` triggers on

    on:
      push:
        tags: ['v*']
      workflow_dispatch:

**Neither is a `main` push.** Every commit to `main` — including the two that fix
the reader set and rewrite its gate — passes with the Android workflow never
having run. Its last green run (`37609804922`) was a *tag* push, `v0.6.1`, and
that is exactly the build whose readers were broken.

Two consequences:

1. **A gate on this workflow cannot protect `main`.** A change to the reader set,
   the overlay, the CMakeLists or the Kotlin can break the APK and nothing on a
   normal push will notice. The gate added here is real and proven, but it only
   fires when someone remembers to tag or dispatch.
2. **The shipped APK is still the broken build.** v0.6.1's APK has no
   `oga_bootstrap.lua` and no `mgba_compat.lua`. Fixing the source does not fix
   the artifact; the APK must be rebuilt and the release asset replaced.

To exercise the gate now: `gh workflow run android-apk.yml --ref main`.

**Worth deciding:** add `push: branches: [main]` (with a path filter for the
reader set, the overlay, and the Android sources) so the APK is built — or at
least the gate is checked — on every relevant change, rather than only when a
release is cut. A gate that only runs at release time is a gate that tells you
last.


---

## Verification by the ARTIFACT, not by a convenient command

Two of my own checks reported nonsense here, both because the command answered a
narrower question than the one asked. Recorded because this class of error is what
this whole document is about.

1. **`gh api .../contents/<dir>` `--jq length` returned 3 for the sounds.** The
   contents API lists the entries at a DIRECTORY path, so at `sounds/` it counted
   the three SUBDIRECTORIES (`common`, `gb`, `gba`), not the WAVs. The count of 33
   is only visible in a recursive tree:
   `gh api "repos/…/git/trees/main?recursive=1" --jq '[.tree[] | select(.path | test("gba-lua/sounds/.+\\.wav$"))] | length'`
   → **33**, matching the working tree and the iOS canon.
2. **`git show --name-status | awk '$1~/^A/'` reported 0 files added.** Git
   detected the moves as **renames** (`R100`), so the status column is `R`, not
   `A`. The files ARE in the canonical tree on the remote — `R100` means the
   content is identical and only the path changed, which is exactly what a move is.
   Filter on the PATH (`grep sounds/`), not on a status letter you assumed.

Neither changed the work; both would have been reported as facts. The habit that
catches them: after a mechanical check, confirm the SAME fact a second way that
cannot share the first one's blind spot — here, the recursive tree listing and the
working-tree file count.

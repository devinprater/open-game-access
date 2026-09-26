# Open Game Access — next work

## Product work

- [ ] **Dissidia first-battle vertical slice:** verify the YES/NO prompt with both confirm and cancel, then follow the supported title/setup/menu path into the first accessible battle screen. Record the speech and live RAM evidence. Do not infer battle state from the proposal alone.
- [ ] **Chrono Trigger DS research gate:** on the YQUE build, verify a small set of candidate fields (start with money and party stats) against live game screens before implementing an adapter. Treat current addresses as leads, not facts; anchor text by content scans, not fixed heap addresses.
- [ ] **Reconcile pending Windows-only work:** `scripts/check-trees.sh` currently reports 85 Windows-tree scripts that are not tracked in the canonical repo. Review them, then promote, consolidate, or discard each; target zero untracked scripts. Also review the Chrono Trigger DS notes, adapter-contribution guide, announcement-queue and Dissidia battle-audio proposals, and mGBA pin/build files; update README and status docs to match the resulting state.
- [ ] **Announcement queue design:** before adding ambient/per-frame cues, define interruption, priority, coalescing, and user-requested opponent/location announcements. Keep passive audio off until it can be tested against those rules.

## Immediate build blocker

- [ ] **Finish PSP simulator linking:** the simulator core archive now builds, but the full app link fails with unresolved `psp_*` references from `pokecore.cpp`. `Core/psp_core.cpp` provides these implementations but is not in the shared core source list; the current simulator build also does not fetch/build the PPSSPP dependency it includes. Decide whether to add a pinned, iOS-compatible PPSSPP subset or temporarily gate the PSP path, then verify the packaged app and simulator launch.

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile, and invalidate cached objects when build scripts change. Simulator and device core archives build locally; the remote simulator core-build step passed in [run 36212903265](https://github.com/devinprater/open-game-access/actions/runs/36212903265).
- [x] Make all three adapter tests link the complete four-adapter registry; `scripts/adapter-tests.sh` passed locally.
- [x] Add a Linux GitHub Actions workflow; the adapter-host-tests check passed on [PR #1](https://github.com/devinprater/open-game-access/pull/1).
- [ ] Re-run the full simulator workflow after resolving the PSP link blocker and verify the `.app` package and simulator launch.

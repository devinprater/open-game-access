# Open Game Access — next work

## Product work

- [ ] **Dissidia first-battle vertical slice:** verify the YES/NO prompt with both confirm and cancel, then follow the supported title/setup/menu path into the first accessible battle screen. Record the speech and live RAM evidence. Do not infer battle state from the proposal alone.
- [ ] **Chrono Trigger DS research gate:** on the YQUE build, verify a small set of candidate fields (start with money and party stats) against live game screens before implementing an adapter. Treat current addresses as leads, not facts; anchor text by content scans, not fixed heap addresses.
- [ ] **Reconcile pending research/proposals:** review the untracked Chrono Trigger DS notes, adapter-contribution guide, announcement-queue proposal, Dissidia battle-audio proposal, and mGBA pin/build files; explicitly promote, revise, or discard each, then update README and status docs to match the current adapter set.
- [ ] **Announcement queue design:** before adding ambient/per-frame cues, define interruption, priority, coalescing, and user-requested opponent/location announcements. Keep passive audio off until it can be tested against those rules.

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile, and invalidate cached objects when build scripts change. The simulator and device core archives both build locally.
- [x] Make each adapter test link the complete four-adapter registry; run all three standalone adapter tests through `scripts/adapter-tests.sh` (local run passed).
- [x] Add a Linux GitHub Actions workflow for the standalone adapter tests.
- [ ] After pushing, confirm the simulator workflow produces its app package and the new adapter-host-tests workflow passes on GitHub.

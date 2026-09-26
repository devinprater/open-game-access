# Open Game Access — next work

## Product work

- [x] **Dissidia first-battle vertical slice:** host-verified with synthetic RAM built from the validated layout (same rule as the other adapter tests: no PSP boot on this host). `dissidia-adapter-test.sh` now covers the YES/NO dialog confirm branch (YES speaks) and cancel branch (NO speaks) on both dialog systems, dialog nav re-reading RAM, the title -> Play Plan -> Bonus Day path, and the first accessible battle screen (self + foe speech). 56 checks pass locally; live-RAM re-proof still needs a PPSSPP boot once the real PSP core lands.
- [ ] **Chrono Trigger DS research gate:** on the YQUE build, verify a small set of candidate fields (start with money and party stats) against live game screens before implementing an adapter. Treat current addresses as leads, not facts; anchor text by content scans, not fixed heap addresses.
- [ ] **Reconcile pending Windows-only work:** `scripts/check-trees.sh` currently reports 85 Windows-tree scripts that are not tracked in the canonical repo. Review them, then promote, consolidate, or discard each; target zero untracked scripts. Also review the Chrono Trigger DS notes, adapter-contribution guide, announcement-queue and Dissidia battle-audio proposals, and mGBA pin/build files; update README and status docs to match the resulting state.
- [ ] **Announcement queue design:** before adding ambient/per-frame cues, define interruption, priority, coalescing, and user-requested opponent/location announcements. Keep passive audio off until it can be tested against those rules.

## Immediate build blocker

- [x] **Finish PSP simulator linking (gated):** the app link failed with unresolved `psp_*` from `pokecore.o` (run 36214860816) because `Core/psp_core.cpp` needs the full PPSSPP tree, which is neither fetched nor compiled. `Core/psp_stub.cpp` now implements the exact `psp_core.h` ABI as an explicit no-op gate (PSP loads fail loudly at runtime; NDS/GBA unaffected) and is in the shared `OGA_GLUE` list, so the simulator app links. Promoting the real core still means: pin PPSSPP in `bootstrap-deps.sh`, add the audited IR-interpreter + software-GPU TU subset to `core-sources.sh`, compile `psp_core.cpp` instead of the stub, and re-prove with a headless boot.

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile; validate cached objects against compiler/toolchain, effective flags, target, SDK metadata, and compiler-emitted header dependencies (including generated `flags.h`). `scripts/build-cache-test.sh` passes, and both archive builds pass locally with verified cache hits on the next run; the remote simulator core-build step passed in [run 36212903265](https://github.com/devinprater/open-game-access/actions/runs/36212903265).
- [x] Make all three adapter tests link the complete four-adapter registry; `scripts/adapter-tests.sh` passed locally.
- [x] Add a Linux GitHub Actions workflow; the adapter-host-tests check passed on [PR #1](https://github.com/devinprater/open-game-access/pull/1).
- [ ] Re-run the full simulator workflow after resolving the PSP link blocker and verify the `.app` package and simulator launch.

# Open Game Access — next work

## Product work

- [x] **Dissidia first-battle vertical slice:** host-verified with synthetic RAM built from the validated layout (same rule as the other adapter tests: no PSP boot on this host). `dissidia-adapter-test.sh` now covers the YES/NO dialog confirm branch (YES speaks) and cancel branch (NO speaks) on both dialog systems, dialog nav re-reading RAM, the title -> Play Plan -> Bonus Day path, and the first accessible battle screen (self + foe speech). 56 checks pass locally; live-RAM re-proof is now unblocked (real PSP core landed — see below) but not yet run.
- [ ] **Chrono Trigger DS research gate:** on the YQUE build, verify a small set of candidate fields (start with money and party stats) against live game screens before implementing an adapter. Treat current addresses as leads, not facts; anchor text by content scans, not fixed heap addresses.
- [ ] **Reconcile pending Windows-only work:** `scripts/check-trees.sh` currently reports 85 Windows-tree scripts that are not tracked in the canonical repo. Review them, then promote, consolidate, or discard each; target zero untracked scripts. Also review the Chrono Trigger DS notes, adapter-contribution guide, announcement-queue and Dissidia battle-audio proposals, and mGBA pin/build files; update README and status docs to match the resulting state.
- [ ] **Announcement queue design:** before adding ambient/per-frame cues, define interruption, priority, coalescing, and user-requested opponent/location announcements. Keep passive audio off until it can be tested against those rules.

## Immediate build blocker

- [x] **Finish PSP simulator linking (gated):** the app link failed with unresolved `psp_*` from `pokecore.o` (run 36214860816); `Core/psp_stub.cpp` gated it as an explicit no-op so the simulator app links.
- [x] **Promote the real PPSSPP core:** PPSSPP is pinned in `bootstrap-deps.sh` (`f293b10`, IR interpreter + software GPU only), the audited 386-TU subset lives in `core-sources.sh` (`PPSPP_CORE`/`PPSPP_EXT_*`/`PPSPP_LUA`/`PPSPP_X86*`), both build scripts compile it via the `ppspp` lang cases, and `scripts/psp-host-proof.sh` re-proves the pin from those same lists (Dissidia ULUS10437, 900/900 frames, live 480x272 framebuffer, clean shutdown). `scripts/ppsspp-subset-test.sh` guards the lists against upstream drift in CI. Remaining: PPSSPP runtime assets still need bundling before a game can boot on-device. Update 2026-09-26: iOS simulator CI is green with the real core (run 36242939387 — 640+ PPSSPP TUs under AppleClang, app links, boots on a simulated iPhone).

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile; validate cached objects against compiler/toolchain, effective flags, target, SDK metadata, and compiler-emitted header dependencies (including generated `flags.h`). `scripts/build-cache-test.sh` passes, and both archive builds pass locally with verified cache hits on the next run; the remote simulator core-build step passed in [run 36212903265](https://github.com/devinprater/open-game-access/actions/runs/36212903265).
- [x] Make all three adapter tests link the complete four-adapter registry; `scripts/adapter-tests.sh` passed locally.
- [x] Add a Linux GitHub Actions workflow; the adapter-host-tests check passed on [PR #1](https://github.com/devinprater/open-game-access/pull/1).
- [x] Re-ran the full simulator workflow after the link fixes: `.app` packages and boots on a simulated iPhone (run 36235169070, all steps green; screenshot shows the idle UI).

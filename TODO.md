# Open Game Access — next work

## Product work

- [x] **Dissidia first-battle vertical slice:** host-verified with synthetic RAM built from the validated layout (same rule as the other adapter tests: no PSP boot on this host). `dissidia-adapter-test.sh` now covers the YES/NO dialog confirm branch (YES speaks) and cancel branch (NO speaks) on both dialog systems, dialog nav re-reading RAM, the title -> Play Plan -> Bonus Day path, and the first accessible battle screen (self + foe speech). 56 checks pass locally; live-RAM re-proof is now unblocked (real PSP core landed — see below) but not yet run.
- [x] **Reconcile pending Windows-only work:** `scripts/check-trees.sh` reports 0 untracked scripts and 0 untracked docs (2026-09-27). The 85 Windows-only files were all obsolete one-shot commit/sync helpers whose payloads the repo had superseded (verified by content diff); deleted. The adapter-contribution guide and announcement-queue proposals are tracked under `docs/proposals/` (the latter sketches the C queue API behind the policy in `docs/design/announcement-queue.md`). The Chrono Trigger DS notes were removed (SNES version instead; git history keeps them). The Dissidia battle-audio spec is tracked at `docs/proposals/dissidia-battle-audio.md` (in-battle cues, speech for menus/queries only). The `reverse-engineering/ctds/` dir holds SNES ChronoAccess research (live WRAM verification) and is tracked. (Chrono Trigger DS research dropped — SNES version instead.)
- [x] **Announcement queue design:** `docs/design/announcement-queue.md` defines the four priority levels, the interruption matrix, same-key coalescing with rate caps, on-demand opponent/location queries, and the lock-on audio beacon. Passive speech stays off until a playtest passes the criteria in that doc.
- [x] **Announcement queue core:** `Core/announce.*` implements `docs/design/announcement-queue.md` (one line in flight, 3 levels, groups, dedup, expiry, time rate limit, bounds, stop, retry). `scripts/announce-test.sh`: 49 checks plus 7 sabotage builds that must fail; runs in the adapter-tests CI workflow.
- [x] **Wire the queue into hosts** (2026-10-01; sequenced in `docs/design/announcement-queue.md`
  "Host wiring"): (1) core owns one `AnnounceQueue` per `PokeCore` (`QueueSpeak` sink into the
  unchanged `speechCb`, `EndFrame` stamps `Host.now_ms` + ticks on all three console paths,
  the `StopSpeech` command and the script stop key clear the queue, additive
  `poke_announce_done` / `poke_announce_id_for_text` ABI); (2) ALL FIVE adapters migrated
  to `oga::announce` with per-site groups/priorities (Dissidia `Say`/`SayRaw`; FE `HostSay`;
  GBA/DBZ/DQ9 `Say`; null-queue fallback keeps the host-test stubs synchronous) plus
  Dissidia queue-mapping host tests (High interrupt, estimate pacing, group replacement);
  full suite green (dbz 17, dissidia 149, dq9 26, gba, osk); (3) iOS completion hooks: synth `didFinish`/`didCancel` +
  `announcementDidFinishNotification` report done via id lookup, the Stop-speech button also
  sends `StopSpeech` (raw 37, never a player button); (4) Android: per-utterance ids +
  `UtteranceProgressListener` in `MainActivity` (observed/logged). REMAINING: the Android
  native port (the app embeds melonDS-android, not the shared core, so adapter commands
  and `poke_announce_done` need the NDK port first) and device proof with VoiceOver and
  TalkBack on and off.
- [x] **Finish PSP simulator linking (gated):** the app link failed with unresolved `psp_*` from `pokecore.o` (run 36214860816); `Core/psp_stub.cpp` gated it as an explicit no-op so the simulator app links.
- [x] **Promote the real PPSSPP core:** PPSSPP is pinned in `bootstrap-deps.sh` (`f293b10`, IR interpreter + software GPU only), the audited 386-TU subset lives in `core-sources.sh` (`PPSPP_CORE`/`PPSPP_EXT_*`/`PPSPP_LUA`/`PPSPP_X86*`), both build scripts compile it via the `ppspp` lang cases, and `scripts/psp-host-proof.sh` re-proves the pin from those same lists (Dissidia ULUS10437, 900/900 frames, live 480x272 framebuffer, clean shutdown). `scripts/ppsspp-subset-test.sh` guards the lists against upstream drift in CI. Done 2026-09-26: PPSSPP runtime assets bundle with the app — the strace-proven
8-file manifest (PPSPP_ASSETS) is staged by scripts/ppsspp-stage-assets.sh
into the .app, verified by verify-sim-app.sh, and psp_load_rom fails loudly
naming any missing file. flash0 needs nothing: PPSSPP auto-installs it from
the game disc's updater partition on first boot (verified: fresh memstick
boots Dissidia 900/900). Remaining: no Swift PSP path exists yet (the picker
only offers nds/gba) — when it lands, it must call psp_set_asset_dir() with
the bundle path before psp_load_rom. Update 2026-09-26: both landed — the
picker offers iso/cso/pbp, loadROM points the core at the bundled
ppsspp-assets via poke_set_psp_asset_dir(), and the UI adapts per system
(GameSystem: PSP shows Cross/Circle/Triangle/Square with no script keys, GB
shows A/B with the verified P/E/K/J/L reader keys, the screen picker is
DS-only). iOS simulator CI green with all of it (run 36272995003). Update 2026-09-26: iOS simulator CI is green with the real core (run 36242939387 — 640+ PPSSPP TUs under AppleClang, app links, boots on a simulated iPhone).

## Game readers

- [x] **Dissidia title tap-to-read diagnosis + fix:** root cause was the
  ready gate, not the fingerprint — `Ready()` was `ManagerOk()` only and
  `CmdWhereAmI` gated on the manager before reaching the title, so on-device
  (manager slot not live on the pre-game title) every command was refused as
  "Game state is not ready yet." Fixed: title/setup screens are fingerprint
  readers — `Ready()` includes title/Play-Plan/Bonus-Day, `CmdWhereAmI`
  speaks them before the manager gate, and D-pad MenuNext/Prev/Left/Right
  re-read the title/setup cursor from RAM (no tracking to desync, same rule
  as the YES/NO dialog nav). Host tests cover manager-dead title speech,
  D-pad re-reads, and the still-not-ready neither-live case. Needs on-device
  re-proof with the same CSO (title row should now speak on Where-Am-I and
  on D-pad moves). Update 2026-09-30: menus now speak with no tap at all --
  the adapter gained a frame-polled identity watch (OnFrame, driven by
  poke_frame every frame): title/setup/dialog RAM cursors plus main-menu and
  options entry announce on change after a 2-frame confirm, battle/board and
  the toggled trackers suspend it. iOS InputBridge now forwards D-pad edges
  as MenuNext/Prev/Left/Right (the adapter.h contract -- previously only OSK
  keys were forwarded, so echo-tracked menus never moved). Host tests: 8 new
  watch cases (entry, move, steady-silence, fingerprint-break/restore, board
  and battle suspension, dialog, options). Needs on-device re-proof: title
  row should announce on appearance and on every D-pad move.
- [x] **Dissidia board pop-up descriptions:** arrivals read the pop-up verbatim
  from a pinned per-type TEXT table ("Stigma of Chaos. Engaging this piece
  finishes the level." / "Potion. Restores HP and EX Gauge to 100%."). Text
  verified twice (s109/s110 tooltip OCR + live prologue-3 RAM tile table at
  0x9C10C3C/0x9C10D02 -- heap, moves per boot, so TEXT is pinned, not address).
  Future: a real RAM reader -- record format known (u16 len, u16 0x0010?,
  01 01 81 FF magic, name, 1B 0A seps, wrapped desc lines) but index scheme +
  base pointer unknown (no absolute pointers into the block).
- [ ] **Dissidia enemy pop-up text:** the roster exists in RAM (heroes +
  enemy titles as a length-prefixed UTF-16LE name pool, e.g. prologue-3
  0x9C10878: Warrior of Light..Shantotto then False Stalwart..Counterfeit
  Youth), but marker->name is unmapped. Candidates: catalog O+8 (type0 reads
  1; possible one-based character index), catalog O+10 (0x98), and O+20 (6;
  possible level), all unverified. Falsified: slot+4 is not a name ID -- the
  enemy at (2,2) reads 0x0152=338, exactly the foe HP observed when that
  battle is instantiated -- and catalog O+0 (type0 0x8A, type2 0xCF, type5
  0x9B) is not a local/global record ordinal (counts do not match).
  Live retry 2026-09-30: clean boot/load reached the same chained board
  (M=0x08C168F0, P=0x9C115C0, B=0x9C11740, D=0x9C11940), and a no-press dump
  reproduced cursor (0,2), the sole enemy at (2,2), slot+4=338, and catalog
  fields O+8=1/O+10=0x98/O+20=6. The screen remained in the noninteractive
  board-camera presentation: repeated debugger D-pad presses were accepted
  while D+0x194/195 stayed (0,2), so no enemy tooltip appeared and no press
  was treated as evidence. Offline follow-up found an adjacent integer/offset
  table at 0x9C106F8..0x9C107BC and two ordered manikin-title sets in the
  pool, but did not find a pointer or proven field joining the active catalog
  record to a title. Decompile 2026-10-01: the board tooltip builder is
  FUN_001cfe78 (key->O via FUN_001c5d90, then name via O+10 through
  FUN_001f1750, title from +5, name text from +4 via
  FUN_00194e24->FUN_0024e5d8->FUN_000fd0f4; task6:5176/5249-5286,
  api-base:67587-67698; string lookup is FUN_00194df0 on DAT_00394218).
  So O+10 is the species/name index (0x98 observed), not display text --
  live proof still needs O+10 read + f1750 deref + OCR match on TWO
  different enemies. 2026-10-01: live board reached (Trunks save, Odyssey
  I-1 Order's Sanctuary, Epyon save chapters verified turning I->II->III).
  Tooltip OCR ground truth "False Hero / Lv 1 / BATTLE MAP / Order's
  Sanctuary" on big figure AND blue pawns (second species still open).
  Same-boot RAM diffs: tooltip latch pair (low u16 2->3 + 0xFFFF->0x0000)
  + hover flag 0->1; pool string present with tooltip closed (pool, not a
  display copy); NO in-RAM pointers to pool strings anywhere (species table
  lives outside user RAM) -- reader must use index/latch method, and all
  addresses shift per boot. Next: cursor-tile field (float-block +
  small-int candidates noted) + a second species name. Until then enemies
  stay "enemy here." -- never guess.
- [ ] **Dissidia Tutorial-mode prompts:** tutorial fights already get the
  battle/QTE path (fighters + prompts resolve there), but the scripted
  instruction sentences ("Bravery attacks: use circle") are OCR-only with no
  RAM source pinned. Next: find the prompt-text source and speak each new
  instruction tersely on change.
- [x] **Dissidia cutscene AD (2026-10-01):** best practices researched --
  present tense, describe-then-dialogue, name speakers (boxes have no
  names), voice unvoiced on-screen text, fit between lines, never over
  them. Opening narration + Chaos-speaks + Garland-answers described:
  script + 3 audio renders in docs/research/audio-description.md.
- [ ] **Dissidia dialogue-box reader:** speak cutscene boxes as
  "Speaker: line" ("Garland: Of course, my lord."). Boxes carry no
  speaker names (portraits only), so reader needs a portrait->speaker
  map per scene; AD describes scene once, reader owns all lines.
- [x] **Dissidia name-entry reader:** implemented as the universal PPSSPP OSK
  reader (`Core/osk_echo.h/.cpp` input-echo engine + `OskToggle/Type/Delete/
  Space/Shift/Finish` commands, `oga::Command::OskToggle..OskFinish`): Dissidia
  wires it with guest-buffer grounding (`OskParams` 0x09B3FAE4 chain, outtext
  0x09B3FC00 revalidated every use) and Play-Plan auto-exit; iOS auto-forwards
  pad A/B/X/Start/Select on the down-edge (silent unless OSK live). Host tests:
  `scripts/osk-echo-test.sh` + 8 OSK cases in the Dissidia suite, full
  `scripts/adapter-tests.sh` green. Needs on-device proof with the same CSO.
- [x] **FE11 menu reader:** DONE Oct 1 2026 (was: ready gate map-only, pre-map
  flow sat at "reader loading" — confirmed on-device 2026-09-27). Title, main
  menu, and difficulty now speak via content-anchored predicates in
  `Core/fe_access.cpp` (menu string-bank scan + stage byte 0x020E3CA8 +
  difficulty cursor flip-flop, all gated so drift yields silence, never a wrong
  line) with MenuState/Next/Prev/Left/Right in the adapter; ready gate is map
  OR tracked menu. Proven live on two boots (title/menu/Normal/Hard speech).
  Queued RE: save-file landing rows, file-select/preps screens (need a save),
  prologue advance. (Original brief: map out the menu screens with the same
  content-scan anchoring the map reader uses, add menu states to the ready gate
  and menu commands to the adapter.)

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile; validate cached objects against compiler/toolchain, effective flags, target, SDK metadata, and compiler-emitted header dependencies (including generated `flags.h`). `scripts/build-cache-test.sh` passes, and both archive builds pass locally with verified cache hits on the next run; the remote simulator core-build step passed in [run 36212903265](https://github.com/devinprater/open-game-access/actions/runs/36212903265).
- [x] Make all three adapter tests link the complete four-adapter registry; `scripts/adapter-tests.sh` passed locally.
- [x] Add a Linux GitHub Actions workflow; the adapter-host-tests check passed on [PR #1](https://github.com/devinprater/open-game-access/pull/1).
- [x] Re-ran the full simulator workflow after the link fixes: `.app` packages and boots on a simulated iPhone (run 36235169070, all steps green; screenshot shows the idle UI).

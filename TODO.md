# Open Game Access — next work

## Product work

- [x] **Dissidia first-battle vertical slice:** host-verified with synthetic RAM built from the validated layout (same rule as the other adapter tests: no PSP boot on this host). `dissidia-adapter-test.sh` now covers the YES/NO dialog confirm branch (YES speaks) and cancel branch (NO speaks) on both dialog systems, dialog nav re-reading RAM, the title -> Play Plan -> Bonus Day path, and the first accessible battle screen (self + foe speech). 56 checks pass locally; live-RAM re-proof is now unblocked (real PSP core landed — see below) but not yet run.
- [x] **Reconcile pending Windows-only work:** 0 untracked scripts and 0 untracked docs (2026-09-27). Update 2026-10-03: the Windows tree and the WSL mirror are retired — `~/oga-work` is the only tree (see `docs/where-things-run.md`); the sync/stage scripts were removed and `scripts/git-hooks/pre-commit` is the ROM guard. The 85 Windows-only files were all obsolete one-shot commit/sync helpers whose payloads the repo had superseded (verified by content diff); deleted. The adapter-contribution guide and announcement-queue proposals are tracked under `docs/proposals/` (the latter sketches the C queue API behind the policy in `docs/design/announcement-queue.md`). The Chrono Trigger DS notes were removed (SNES version instead; git history keeps them). The Dissidia battle-audio spec is tracked at `docs/proposals/dissidia-battle-audio.md` (in-battle cues, speech for menus/queries only). The `reverse-engineering/ctds/` dir holds SNES ChronoAccess research (live WRAM verification) and is tracked. (Chrono Trigger DS research dropped — SNES version instead.)
- [x] **The readers' positional sound cues reach the host (2026-10-07).** All 42
  `audio.play` sites in the reader set were silently dropped: `oga_audio.lua` was a
  recording stub with a `set_sink()` handoff that nothing ever called, so the pan —
  which IS the information (gb.lua pans a boulder's sound to the boulder's side) —
  never left Lua. Android had a working path already
  (`MGBAScript::setSoundCallback` → JNI → Kotlin), so the shared core was given the
  same seam rather than a second mechanism: `LuaPlaySound` → `_G.oga_play_sound`,
  `poke_set_sound_callback`, and `GbaSoundForward` to bridge them. `CuePlayer.swift`
  plays the reader's own WAVs with pan applied, OFF the emulator's render graph
  (the files are mono/stereo 8- and 16-bit at 44100 Hz, not the engine's 32768 Hz
  int16, so they cannot share it) and under the existing "Game sound" setting.
  Proven by driving the real `oga_audio.lua` with a stand-in host sink; the IPA's own
  bundled copy was then driven the same way. `scripts/cue-sink-test.sh` guards it
  with 4 sabotage mutations and runs in adapter-tests CI.
  ⚠ Host-side only so far: no phone has heard a cue yet.
  **RELEASED as v0.6.3** (IPA verified from its own published bytes: `platform ios`,
  unsigned, 33 WAVs and the `oga_play_sound` binding in the shipped binary). Still
  unconfirmed by ear on a device — that is the open part, not the code.
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
  `announcementDidFinishNotification` report done via id lookup (2026-10-03: ids now arrive
  with each line through `poke_set_speech_id_callback`; the text lookup missed lines over
  63 bytes, trimmed lines and repeats), the Stop-speech button also
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

- [ ] **PPSSPP on Android (DEFERRED 2026-10-03, don't start without a new decision):**
  PSP is complete and proven on iOS (`Core/psp_core.cpp`, PPSSPP pinned at `f293b10`,
  IR interpreter + software GPU, Dissidia ULUS10437 900/900 frames) but Android has
  NOTHING for it — the Android shell is melonDS-only.
  Three reasons it is parked rather than started:
    1. LICENSE IS PERMANENT. Linking PPSSPP (GPL-2.0-or-later) makes the whole
       distributed app a GPL combined work: anyone who gets the APK must be offered
       all of its source. Irreversible — no future closed build. See
       `docs/proposals/multi-core-mgba-ppsspp.md` §2.
    2. SIZE. PPSSPP is roughly 10x the melonDS core by source (~386 audited TUs on
       the iOS side already). Every APK build and every CI run gets slower.
    3. THE READER IS A PORT, NOT A CARRY-OVER. PPSSPP has no Lua, so the Dissidia
       reader would be rewritten natively against the `Native*` layer.
  Prerequisite before this is even discussable: the DS APK must build and ship.

- [x] **Android APK BUILDS (2026-10-03, run 37151443355, all steps green):** the first
  successful Android build. 51 MB APK, artifact `open-game-access-android` uploaded,
  and the Lua-assets check passes. Reached by retargeting the borrowed shell to our
  core's API, then compiling the core with `ENABLE_OGLRENDERER=OFF` (its GL is desktop
  GL, not GLES -- 2071 differing lines), then setting the build flags in CMake rather
  than Gradle. NOT yet done: install it on a device and boot a ROM.

- [x] **The GBA/GB code is COMPILED AND LINKED into the Android APK.** DONE
  2026-10-04, run 37189884773, every CI step green, 57 MB APK. Verified against the
  shipped .so rather than assumed: GBCoreCreate, GBIsROM, SM83Init, luaL_newstate and
  SzArEx_Open are all present. Before this, the library contained NDS/GPU/ARMJIT and
  ZERO of them -- the Game Boy/GBA code was copied into the tree and never built, and
  even Lua was missing, so the NDS Lua host was unwired too. 336 -> 510 translation
  units.

- [~] **THE GBA PATH IS NOW REACHABLE ON ANDROID (2026-10-04).** `GbBridge.kt`
  is a @JavascriptInterface object the launcher page calls; `MainActivity` routes
  the SAF picker result into it; `index.html` is a real launcher (select/start/
  stop, the pad built from mGBA's key mask, the reader's hotkeys). The page ASKS
  the native side whether the core is present instead of hardcoding the old
  "not compiled into this test build" line, which had gone stale on screen.
  Guarded by `scripts/android-gb-bridge-test.sh` (8 checks + 5 mutations in
  adapter-tests), which crosses the two languages over: every `oga_gb.<method>`
  the page calls must be exported, every `window.<fn>` Kotlin calls must be
  defined, every `external fun` must have its exact JNI symbol, and the page's key
  mask must match mGBA's. ⚠ NOT device-proven, and the speech is not meaningful
  yet (see the Game Boy entry above).
- [x] ~~BUT NOTHING CALLS IT YET~~ — superseded by the entry above.
  The native side is complete -- MGBAScriptJNI.cpp exports all 10
  Java_..._GbAccessibilityScript_* entry points and they link -- but no Kotlin or JS
  code invokes them. Concretely:
    * assets/index.html still says "emulator core integration is not compiled into
      this test build", its Select ROM button is a `// TODO`, and nothing calls
      hermes_tts beyond speech.
    * Nothing constructs a GbAccessibilityScript/GbRomResolver; the only reference to
      either is a doc comment.
    * The ROM picker is upstream melonDS's, which is DS-only -- its loadRom() has a
      GBA-slot parameter for DS games, not a standalone GBA loader.
  So "GBA works" is still NOT true. What is true: the code that makes it possible is
  now in the binary, which it never was. The remaining work is UI/plumbing, not
  emulation.

- [~] **Wire the overlay's GBA/GB sources into the Android build.** IN PROGRESS
  (2026-10-04). The four accessibility hosts (MGBACore/MGBARunner/MGBAScriptJNI/
  PokeScript.cpp) and the mGBA core are now compiled into the frontend target; the
  build went from 336 to ~498 translation units. Feasibility was measured with the
  real NDK before writing anything: 126/126 mGBA sources, 23/23 Game Boy sources,
  14/14 LZMA sources and 34/34 Lua sources compile for aarch64-linux-android26.
  Four things had to be discovered from CI logs, each a platform difference the iOS
  flag set does not cover:
    * HAVE_PTHREAD_SET_NAME_NP must be REMOVED (BSD spelling; bionic has the other one)
    * HAVE_STRTOF_L must be ADDED (bionic provides strtof_l; mGBA's fallback collides)
    * LZMA: iOS borrows PPSSPP's SDK copy, so mGBA's own third-party/lzma is compiled
    * version.c: mGBA generates it from git; Core/mgba_version_stub.cpp is reused
  ⚠ SCOPE CALL FOR DEVIN: I added M_CORE_GB, i.e. real Game Boy / Game Boy Color
  emulation, which iOS does NOT enable. The audited list is GBA-only, so this pulled
  in 23 more source files. Justification: the app's own UI and the shipped
  assets/lua/gb/ scripts cover .gb/.gbc as well as .gba. But it IS beyond "fix GBA",
  so flagging rather than assuming. Say the word and it comes back out.
  REMAINING: the link must succeed, then the app must load a .gba on the emulator.

- [x] **Android GBA/GB reader set was STALE, and the CI gate could not catch it**
  (2026-10-07, `bc9de43`). Measured against the shipped v0.6.1 APK: `assets/lua/gb/`
  was a 2026-09-15 snapshot missing `oga_bootstrap.lua`, `mgba_compat.lua`, `crc32.lua`,
  `encoding.lua` and all five `oga_*` helpers — the shim layer added 2026-09-20. So the
  GBA path shipped without its bootstrap or its mGBA API shim. iOS was fine
  (`Package.swift .copy()`s the whole dir). Synced the 15 runtime files and replaced the
  gate: it was `grep -qi lua`, which matched 197 entries and passed with the GBA path
  absent; it now asserts the files that exist ONLY when the feature works. Both gates
  replayed against the real APK file list to prove the difference. Full writeup:
  `docs/android-assets.md`.
  DONE (`4870f45`): `assets/lua/gb/` is now STAGED at build time from
  `Sources/.../gba-lua/` — one tree — with `scripts/check-reader-assets.sh` failing if a
  duplicate reappears. Two further divergences surfaced while restructuring: `sounds/`
  (33 WAVs the readers name) existed only on the Android side and is now canonical, and
  `bizhawk_compat.lua` genuinely differs between the platforms (its Android copy has an
  inert `joypad.set`) and still needs its own decision.
  STILL OPEN: the NES readers are iOS-only — `nes-lua/` has no Android staging path at
  all.

- [x] **Android APK builds on `main` (2026-10-07).** `android-apk.yml` now runs on
  `push: branches: [main]` with a path filter over the reader sets, the overlay,
  `app/src/main`, `Core` and the Android scripts, plus the tag trigger and
  `workflow_dispatch`. Measured: a `main` push built green, so the reader-set gate
  is exercised on every relevant change instead of only at release time. (Was:
  tag-only, so every commit that fixed the reader set landed with the APK unbuilt.)
- [x] **WITHDRAWN: "Android's `joypad.set` is inert" was wrong** (2026-10-07).
  Android's NDS host has no button-override path at all (`setJoypadState` is
  defined and never called), so an empty `joypad.set` is consistent, not broken.
  The error was reading Android's reader environment from iOS's: iOS loads
  `oga_bootstrap.lua` + `mgba_compat.lua`, Android loads only `pokemon.lua` and
  installs the surface in C. See `docs/android-joypad-set.md` (retracted).

- [~] **Android menus need TalkBack labels.** ⛔ **THE TWO DEFECTS THIS ENTRY NAMED
  WERE ALREADY FIXED** — measured against the shipped v0.6.3 APK, not the source:
  `mergeDescendants`/`contentDescription` are present in its dex and the pause dialog
  carries its title, so TalkBack announces the menu and reads each ROM row as one
  labelled item. What this entry described as outstanding had shipped. Scope per Devin:
  label the MENUS; do not label all 571 UI files (that would be fluff). The game itself
  is narrated by the Lua script, not by the UI.
  **The real gap found by sweeping for the CLASS rather than the named instances:**
  `ConfigurableRomItem` was labelled but its sibling `RomItem` was not, and RomItem is
  what the DSiWare ROM picker (`DSiWareRomListDialog.kt`) and the shortcut ROM picker
  (`ShortcutSetupActivity.kt`) use — so those two lists still read as text fragments,
  the exact failure the sibling's fix was written to prevent. Fixed in the overlay as
  step 7c, same label-from-the-ROM's-own-fields approach, no new words. Overlay verified
  idempotent and applied to a fresh upstream clone; APK build pending.
  **Achievement rows (step 7d) — Devin asked for these.** The shared
  `RomAchievementUi` row had NO semantics, so TalkBack read each one as loose
  fragments (title, description, "13/47", "250", "PTS") with nothing saying what the
  row was or whether it was unlocked. Now one merged stop labelled from the row's own
  data and its existing strings (Unlocked/Locked, Points, Missable, the progress pair)
  — no new words. ⛔ The merge is on the inner content Row, NOT the outer Column: the
  Column owns the expand/collapse click and contains the "View achievement" button,
  and merging there would swallow that button as a focusable action. Verified on a
  fresh upstream clone that the merge attaches to the inner Row and the button
  survives.
- [~] **Make the shell emulator-agnostic (started 2026-10-04):** plan at
  `docs/plans/emulator-agnostic-shell.md`. Three of its steps are DONE:
  * **Registry:** 14 consoles (Genesis/Dreamcast/3DS added -- each has an
    installed core), 58 tests + 6 mutations in CI. ⛔ It was in NO BUILD LIST, so
    on device it did not exist; now in OGA_GLUE and build-host.sh, and exposed to
    the UI through the C ABI in pokecore.h.
  * **`OgaCore` vtable** (`Core/oga_core.h`): the ~100 `isGba`/`isPsp` dispatch
    sites are gone. `poke_frame` and `poke_framebuffer` are one body each for
    every console. DS ops live in pokecore.cpp (they need its private type); the
    Game Boy and PSP ops plus the extension->backend resolver live in
    `Core/oga_core.cpp`. Verified: Pokemon Black .nds boots (900 frames, VRAM
    live), FireRed .gba boots (240x160, 64 colours, speech).
  * **iOS reads the registry:** hardware facts (screens, shoulders, sticks, the
    face-button list) come from the core; the script/hotkey tables deliberately
    stay in Swift, because they belong to the loaded script, not the console.
  NEXT: **the Game Boy reader's speech is MEANINGFUL, and the "numbers and nil"
  report was misdiagnosed** (2026-10-07). Measured on Pokemon Red (40000 frames,
  traceback-instrumented sink): the Game Boy path reads Oak's speech line by line,
  then the naming screen and the typed name -- ZERO `nil`, zero raw numbers, zero
  hook errors. The `nil`/numbers symptom belongs to the GBA TITLE-SCREEN path only
  and is expected there.
  ⛔ The real bug that run found: `registerexec` folded a RESET handler into the
  MOVEMENT poll. `pokemon.lua:1057-1058` registers `init_script` at the CPU entry
  vector (0x100 GB / 0x8000000 GBA) to survive a soft reset; the shim fired every
  registration on player movement, so `init_script` ran on EVERY STEP -- reloading
  the reader and re-speaking "Ready" 729 times in one run (caller proven by
  traceback: pokemon.lua:893 via mgba_compat.lua:342). Fixed: entry vectors live in
  their own table, never movement-polled, still reachable from the PC sample.
  Re-measured: "Ready" 729 -> 1, dialogue preserved. Gate:
  `scripts/registerexec-kind-test.sh` (3 mutations, CI).
  ✅ THE WORLD IS REACHABLE NOW (2026-10-07): `scripts/gba-reach-world.sh` walks the
  intro with a no-op script (120000 frames in ~20 s, measured) and captures a
  savestate; resuming it with the REAL reader boots straight into the world. No
  save file was needed and none exists in the ROM library or on archive.org.
  ⛔ AND IT MEASURED THE REAL GBA GAP: in-world, the reader emits a mix. Hooks whose
  body reads a CPU register (18 of gba.lua's 68, 6 of rse.lua's 19 -- the text and
  menu readers) fire from the movement poll a frame late and speak unrelated data
  ("49154:58718", "4", spaces). The screen-buffer path (Game Boy; GBA naming/title)
  is meaningful. So GBA in-world text/menu reading needs each of those hooks rebuilt
  around an OBSERVABLE EFFECT rather than the PC -- per-hook design work the shim's
  own header predicted, deliberately not attempted yet.
  Then NES as the first console neither existing backend resembles.
  ⚠ The step-3 work turned up two things recorded as done that were NOT
  reachable in a shipped build: the registry was in no build list, and nothing on
  Android called the Game Boy path at all. Every console step from here should
  end with a REACHABILITY check, not a build check.
  ⚠ ONE EXPECTATION TO CORRECT: Mario Kart 8 / Deluxe is a SWITCH game. No Switch
  emulation is viable on iOS or Android for sideloaded apps -- it needs hardware
  support no phone has, quite apart from the legal position. That title will not run
  here. Mario Kart 64 (N64) or Double Dash (GameCube) are the reachable ones.
  ⚠ PS2 and GameCube/Wii are listed in the registry but are NOT near-term: both need
  cores far heavier than anything currently in the app, and I would not plan around
  them. They are in the table so the UI can name them honestly, not as a roadmap.

- [x] **NES readers are IN the app (2026-10-07), and the previous release over-claimed
  them.** v0.6.0-nes shipped "Zelda 1 Access hosts and speaks" with no reader assets in
  the IPA at all: `Resources/nes-lua/` was gitignored, `Package.swift` never copied it,
  and `poke_set_script_dir` was called only for `gba`/`gbc`/`gb`. Fixed: the sets are
  bundled resources now; the core identifies the reader by the ROM's CRC32 (never a
  filename) via `kReaderSets` in `Core/mesen_core.cpp`; `poke_reader_set` exposes it
  through the ops table; the app stages a writable copy and points the backend at it.
  Two device-breaking wrapper bugs fixed on the way (a hard-coded
  `/home/devin/...` reader path, and `io.popen("ls")` to enumerate Data files, which
  iOS cannot run — DW would have been mute). Verified with real ROMs on the host
  (`scripts/nes-reader-set-test.sh`, speech required, CRC identity mutation-proved) and
  in the built IPA (17 reader files, byte-identical to source, `platform ios`,
  re-signable). ⚠ Still host-side only: no phone has run it. Zelda's map/room narration
  remains unobserved, DW's "Critical health" on a fresh boot is uninvestigated, and the
  mods' PowerShell SoundBridge (spatial cues) is not ported.

- [x] **Navigation/guidance prior art read (2026-10-07).** Studied buu420's *Foresight*
  (Chrono Trigger, Windows/Steam) and his Digimon World 2 work in `beetle-psx-libretro` for
  technique — **ideas only, credited; no code copied** (his GPL-3.0 / GPL-2.0-or-later, ours
  GPL-3.0, so reuse would be legal but attribution is the honest form). Written up at
  `docs/design/navigation-prior-art.md`.
  The transferable findings, in the order they would change our work:
  1. **Heading belongs in the search state.** Their nodes are `(point, xDir, yDir)` so equal
     paths can prefer fewer turns; the position budget is charged once per position, not per
     heading. Goals <=64, neighbours <=16, visited <=131072, and exceeding a bound is SPOKEN,
     never a silent hang.
  2. **Counted legs, not continuous bearings** — "left 3, then down 2", up to three legs
     announced, then only the new leg at each turn. Whole cardinal legs only: counting to a
     waypoint inside a leg restarts the instruction.
  3. ⛔ **Navigation and footstep counting must share ONE unit constant per map type.** Their
     mismatch (384 vs 256/128) gave four beats walking and three running on the same six steps.
     The tracker now THROWS when guidance and movement disagree.
  4. **Arrival is three states, not two** — ready / unreachable / **`ConfirmPending`**: in
     geometric reach but the game would not give Confirm right now. "Neither ready nor
     unreachable: wait there." Our readers have no middle state.
  5. **Destinations have APPROACH POINTS (plural)**, and rejecting one must be forgotten when
     the actor moves.
  6. **Visibility is the game's own test** (their `IsDrawn` = the native draw flag AND the
     camera window), and exits/chests need rectangle overlap, not a point test — a point sat
     exactly on the window's exclusive edge and hid the only exit.
  7. **Footstep fractions survive a pause**; only discontinuities, identity changes and scripted
     movement discard them; held-into-a-wall is silent; one bounded world step after key release.
  8. **Generated scene catalogs** (their 2.5 MB `game-navigation.json` comes from walking the
     game's own scripts, with `ExtractionWarnings`/`MissingScripts` recorded).
  9. **Coverage as a test**: every interactive actor in every scene is either offered or
     accounted for by a named owner — 1,523 offered, 450 accounted for, nothing missing. The
     same "enumerate the class" discipline we use for code, applied to game coverage.

  **For Chrono Trigger SNES this is the missing layer.** We already have the addresses
  (`reverse-engineering/ctds/prior-art.md`, verified live): location `0x0100`, tile
  `0x0102`/`0x0103`, context `0x0D13`, busy `0x0D76`, object position `0x1800`/`0x1880`,
  facing `0x1600`, and move flag/length `0x1A00`/`0x1A01` — the footstep source. What we lack
  is the passability graph, the heading-aware search, counted legs, and a generated target
  catalog. ⛔ His addresses are the STEAM PORT, not the SNES — do not assume they transfer.

## PSP (parked, with a measured capability note)

- [ ] **PPSSPP exec hooks — SURVEYED, NOT STARTED.** Read from the pinned source while looking for
  the PSP analogue of the GBA exec-hook work; written up in `docs/research/ppsspp-exec-hooks.md`.
  Headline: the IR interpreter (what this app runs) has a COMPLETE real exec-hook path
  (`IROp::Breakpoint` emitted by `IRFrontend::CheckBreakpoint`, fired in `IRRunBreakpoint`), with
  block-cache invalidation already handled. ⛔ The project's existing note that “the IR
  interpreter's memory ops bypass MemChecks, so use the classic interpreter” is about MEMORY
  breakpoints; EXEC breakpoints work on the fast IR path.
  It would make three reads more precise: a routine that runs and returns inside one frame, what
  called what (live confirmation of the board-tooltip decomp call path), and the ordering of
  several writes to one address in a frame. ⛔ All three are OBSERVABILITY, not speech — a
  firing hook says nothing to the player by itself, and anything turned into speech must pass the
  announcement queue's rate limits. PARKED because the native Dissidia reader works without it and
  no player-visible bug needs it today; start it when one of those three blocks a feature, with the
  same prove-the-primitive-then-measure-cost spike the GBA work used
  (`tools/gba-debug-hook-spike.c`).

- [x] **GBA exec hooks are REAL breakpoints now (2026-10-08, v0.6.4).** The reader's text/menu
  hooks read CPU registers and the movement poll read them a frame late, so in-world speech was
  garbage ("49154:58718", "4", spaces). mGBA HAS real breakpoints and the debugger was already
  compiled in; our frame driver called runFrame(), which does not check them. Now
  memory.registerexec installs a real breakpoint via the new oga_set_exec_hook binding, and
  gba_frame drives mDebuggerRunFrame only when hooks exist. Measured before/after on the same
  Emerald savestate and input: garbage -> "BAG", "CLOSE BAG", "Return to the field.". Cost ~2.2x
  baseline, still ~50x faster than real time. Gate: scripts/gba-exec-hook-test.sh (3 mutations).
  ⚠ Host-proven only; no device has run it.
- [x] **The Game Boy "numbers and nil" report was WRONG, and the real bug is fixed (v0.6.4).**
  Measured on Red: the GB path reads Oak's speech line by line with zero nil, zero raw numbers and
  zero hook errors -- that symptom is GBA-title-screen only. The real defect was a RESET handler
  (init_script at the entry vector) firing from the movement poll, re-speaking "Ready" 729 times in
  one run and rebuilding the reader every step. Now 729 -> 1, dialogue preserved. Gate:
  scripts/registerexec-kind-test.sh (3 mutations).

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
  small-int candidates noted) + a second species name. 2026-10-02: WIRED for 5 live-verified keys (commit 8b59694, 155/155 Dissidia
  checks): 0x30 False Hero (Trunks board), 0x137 Delusory Knight / 0x138 Transient
  Lion / 0x139 Imaginary Soldier / 0x13A Capricious Thief (Cecil board, EPYON 100%
  save Destiny Odyssey IV). Species key = s16[O+10]; keys globally unique per
  enemy (Trunks base 0x30, Cecil base 0x137). Table: kEnemySpecies in
  Core/dissidia_adapter.cpp. Unknown keys keep "enemy here." + log the key.
  Still open: Trunks 0x31/0x7F/0x80/0x32 (tooltips AND Opponent Info blank --
  Scan mechanic suspected), Terra board.
  Until then unverified enemies stay "enemy here." -- never guess.
- [x] **NEXT LIST coroutine 2026-10-02:** (1) Terra board banked 2/7
  (0x177/0x178 wired; 0x179-0x17D fight-gated, needs battle automation);
  (2) tutorial hints DONE (engage + DP-zero, eebdc25); (3) dialogue: portrait
  names + 137-scene tables + runtime scene-ID/title all DONE
  (044b59a/d7db670/889eecd/1796757, 175/175 green).
- [ ] **Dialogue remainder:** (a) advance-map (portrait change -> table order
  -> per-line speaker, needs box-line segmentation validation); (b) portrait
  address boot-stability proof across boots; (c) current-line text anchor
  (heap, no pointer found); (d) portrait table growth per scene walked.
- [ ] **Trunks unknowns via Scan** (0x31/0x7F/0x80/0x32 tooltips blank +
  Opponent Info blank, likely Scan-gated), ~2-4 h uncertain.
- [ ] **FE11 past-prologue save,** open-ended hours+. **UNBLOCKED + PARTLY DONE 2026-10-08:**
  two USA saves exist (`~/fe/saves/`). Continue works with `fe11-usa-finalboss.sav` (reaches
  Chapter Saves -> Endgame; 15 units, map cursor valid) but NOT with `fe11-usa-ch10.sav` (returns
  to the main menu). The Chapter Saves screen is now read (`FeFileSelectActive`, stage byte
  0x020E3CA8 = 02, two boots). Still open: the highlighted-slot cursor (no ordinal found;
  0x0224F540 is static), the Map Savepoints list, and battle-preparation/unit-list screens.
- [ ] **Device build:** v0.4.0 predates all Oct-2 Dissidia work; cut a new
  release so the board hints, portrait names, and scene titles reach hardware.
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
- [ ] **Dissidia dialogue-box reader (partly done 2026-10-02):** scene title
  on entry + speaker name per portrait change (verified IDs) are wired and
  tested. Still missing: per-line speaker via advance-map, quoting the line
  text itself (needs current-line anchor), portraits beyond the 5 verified.
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

- [x] **CI builds the DEVICE IPA (2026-10-07).** The simulator workflow was always
  green, so "iOS CI works" was true while the only artifact Devin installs was
  hand-built on one box and attached by hand — a release whose IPA is unreproducible
  from its own tag, and a device build nothing verified. New
  `.github/workflows/ios-ipa.yml` builds the device core, packages an UNSIGNED ipa, and
  on a `v*` tag attaches it as `OpenGameAccess-<tag>.ipa` (the name the hand-upload
  path used, so no link breaks).
  ⛔ `scripts/build-core.sh` hardcoded xtool's artifactbundle SDK — that is WHY the
  device build had no CI. It now resolves `SDK=` -> `SDKROOT=` -> `xcrun --sdk
  iphoneos` -> xtool's bundle, last, so local behaviour is unchanged (verified: a real
  local build still archives the same size).
  `scripts/verify-ipa.sh` asserts on the UNPACKED artifact (platform `ios`, unsigned,
  reader sets by name, >=33 cue WAVs, the cue binding, no game data);
  `scripts/verify-ipa-test.sh` mutates the real IPA into each known-bad shape and
  requires rejection.
  **Three bugs in the plumbing surfaced, each invisible until it ran somewhere new:**
  1. `find -iregex` is GNU-only — on the macOS runner it errored, returned empty, and
     read as "no game data", passing a deliberately-ROMmed IPA. Portable `-iname` plus
     a probe that proves the search works before an empty result is trusted.
  2. The workflow's path filter listed scripts by name but NOT `verify-ipa.sh`, so the
     commit fixing a bug in that gate could not trigger the gate. Now matches classes
     (`build-*.sh`, `verify-ipa*.sh`).
  3. No `permissions:` — the token was read-only and `action-gh-release` failed with
     "Resource not accessible by integration". Only reachable on a TAG, which is the
     one run a release gets; found by exercising the tag path on a disposable tag.
  Verified: a full tag run went green and the released IPA was downloaded back and
  passed the gate independently. (Two CI builds of identical source differ by 46 bytes
  — `LC_UUID` is random per link. Content is identical.)

## Verification

- [x] Add `MGBA_DEFS` and `MGBA_INC` specifically to the `gba_core.cpp` C++ compile; validate cached objects against compiler/toolchain, effective flags, target, SDK metadata, and compiler-emitted header dependencies (including generated `flags.h`). `scripts/build-cache-test.sh` passes, and both archive builds pass locally with verified cache hits on the next run; the remote simulator core-build step passed in [run 36212903265](https://github.com/devinprater/open-game-access/actions/runs/36212903265).
- [x] Make all three adapter tests link the complete four-adapter registry; `scripts/adapter-tests.sh` passed locally.
- [x] Add a Linux GitHub Actions workflow; the adapter-host-tests check passed on [PR #1](https://github.com/devinprater/open-game-access/pull/1).
- [x] Re-ran the full simulator workflow after the link fixes: `.app` packages and boots on a simulated iPhone (run 36235169070, all steps green; screenshot shows the idle UI).

2026-10-02: task1 banked 2/7 Terra species (0x177/0x178 wired, tests 160/160); 0x179-0x17D fight-gated. Task2 DONE: board hints (engage + DP-zero, verbatim pool).

# Final Fantasy screen-reader mods — mining notes (2026-11-06)

Fuller write-up behind §32 of `accessibility-tricks.md`. Mined at Devin's
ask (approved 2026-11-06) from the packages staged on sonix in
`~/sarah/oga-materials/` plus the authors' public source repositories,
cloned to `~/sarah/oga-materials/repos/` on sonix (`bs`, `ffpr`, `ffx`).

**Standing caveat: none of the three mods was run.** Everything below
comes from reading source code, data files, configs and documentation.
Coverage numbers, accuracy claims and "it works" statements are the
authors' own. Both bladestorm360 projects are unusually disciplined
about this distinction in their own docs — they track "implemented /
offline verified" separately from "gameplay confirmed," and several
headline features were still awaiting gameplay confirmation at the
versions mined. Blind Soldier's README does the same ("implemented"
vs "played").

---

## 1. FFPRCore-Screen-Reader v1.0 — bladestorm360 (FF I–VI Pixel Remaster)

### What was examined

- Staged package (SHA256-verified zip, extracted): 29 files, ~25 MB —
  a compiled native DLL (~11 MB), `prism.dll`, SDL3, OpenAL Soft with
  HRTF data, license/source notices, and a 32 KB ReadMe (`.md` + `.txt`
  identical content). The shipped binary itself is closed; nothing in
  the package is readable logic.
- Public repo (full source, cloned): 1,122 files — 354 `.cpp` + 225
  `.h`, 152 `.md` docs, per-game data packs. Read in depth:
  `docs/FEATURES.md` (the porting checklist, 25 KB), the shipped
  `readme.md`, `docs/pathfinding_checkpoint_2026_10_14.md`,
  `src/core/field/baked_router.h`, `src/core/fieldnav/autowalk.h`,
  `src/core/audio/hrtf.h`, `src/core/settings.h`, `data/ff1/puzzles.json`,
  and the `src/core/{field,fieldnav,audio,menus,shop}` file layout.
  Not read line-by-line: the bulk of the 354 C++ files (battle,
  dialogue, glue layers), the docs/ folder's audit and follow-up notes,
  and the other five games' data packs beyond confirming identical
  structure.

### What was found

- **Architecture:** one native C++ core (IL2CPP hooks via MinHook) for
  all six Pixel Remasters; per-game differences live in `data/ffN/`
  JSON/bin packs, including small override files for individual wrong
  labels. It's a rewrite of the author's five older per-game
  C#/MelonLoader mods (FF6 was added in the rewrite); the rewrite was
  graded against `docs/FEATURES.md`, a checklist extracted from the old
  code, and kept the old settings key names so preferences carry over.
- **Routing:** offline bake (`tools/bake_maps.py` → `maps.bin`) plus a
  pure-data A* router over whole sub-maps — layers, direction masks,
  hidden passages (player-only), warp edges, transport groups on world
  maps — replacing the game's ~31-tile-window searcher. The same
  router code runs offline (`tools/route_check.cpp`) against the baked
  data. Unknown maps fall back to the game's searcher. Trap routes get
  special handling (safe-first routing, required-drop fallback,
  per-goal repeat protection — see the pathfinding checkpoint doc).
- **Beacon/soundscape:** two beacon modes (Follow route / Object
  location), Shift+F6, saved preference, cadence kept during movement
  and replanning, arrival silence with resume-on-move-away. A separate
  Soundscape fields one repeating cue per nearby eligible entity with
  per-kind timbres, pitch offsets between same-kind neighbours, and
  defer-don't-drop voice scheduling. HRTF via OpenAL Soft loopback
  rendered into the SDL stream (never opens a device), north-up fixed
  frame, automatic stereo+pitch fallback, per-mode volume memory, 2×
  boost. Cue WAVs are user-replaceable.
- **Speech/menus:** Prism 0.18.3 (pinned by `prism.lock.json`);
  announcement deduplication (old mods' `AnnouncementDeduplicator`,
  inventoried in `docs/DEDUP_INVENTORY.md`); menu position
  announcements; I/U/Shift+I detail keys; equipment comparison display
  with its own verbosity settings; battle results as a reviewable
  phase buffer; EXP tick sound; same-named-enemy letters (FF5);
  mod text in 12 languages; Japanese entity-name translation data.
- **Settings:** `ffpr_screen_reader.ini`, in-game mod menu grouped
  Speech / Navigation / Navigation audio / Battle; audio master switch
  preserves individual prefs; conservative defaults (nearly all audio
  navigation off by default); strict hotkey hygiene (foreground-gated,
  Alt suppresses, unmodified F-keys only). User waypoints persist in
  `UserData/waypoints.json` with their own category system.
- **Puzzle guides:** authored `puzzles.json` step data per game
  (FF1 sample read: Citadel of Trials; header notes it was authored
  from map dumps and "not yet tested in game" — the author's own
  caveat).

### What couldn't be examined

- Runtime behaviour of any of it (not run). Several v1.0 features are
  marked by the author as offline-checked but awaiting listening/
  gameplay confirmation; FFIII/FFVI automatic airship boarding is
  disabled in the shipped build and FFVI flight autowalk is
  unconfirmed — per the shipped readme's Known Issues.
- The five predecessor C# mods live in separate repos (not cloned);
  their behaviour is represented here through the author's own
  porting checklist, which states code was preferred over docs where
  they disagreed.

---

## 2. FFX-X2-Screen-Reader v1.0.2 — bladestorm360 (FF X / X-2 HD Remaster)

### What was examined

- Staged package (SHA256-verified zip, extracted): only 6 files,
  ~11 MB — `InstallFFXScreenReader.exe` plus three substantial text
  docs: `README.txt` (5.7 KB), `CONTROLS.txt` (47 KB, the full player
  guide), `CHANGELOG.txt` (37 KB), and `SHA256SUMS.txt`. The mod DLL
  itself is delivered by the installer, not loose in the zip.
- Public repo (full source, cloned): 1,037 files — native C++ under
  `src/native` (the production root), a C# installer under
  `src/installer`, legacy JS/Frida material kept as reference. Read in
  depth: `Docs/features/navigation/index.md`, `Docs/features/runtime/
  settings.md` (the full settings table, generated from
  `src/native/settings.cpp`), `Docs/features/puzzles/index.md`,
  `Docs/features/index.md`, `Docs/features/distribution/index.md` +
  `security_20261024.md` header, and the shipped `CONTROLS.txt`.
  Not read line-by-line: most of `src/native/*.cpp`, the ~50 session
  and tester-report docs in `Docs/`, and the JS reference code.

### What was found

- **Navigation core:** scene capture on a worker with strict identity
  checks; one committed route shared by speech, beacon, reachability
  and auto walk; geometry decoded from the game's collision including
  dynamic gate groups; routes terminate at interaction zones;
  3D water routing with baked cross-water links; scene budgets stated
  as transport limits (500 workers / 128 actors / 32,767 vertices),
  not coverage claims.
- **Filtering honesty:** hide-unreachable is default-off and removes
  only proved-unreachable targets; unknown stays listed. Same posture
  in identification (native activity + actor binding + real
  interaction geometry must agree) and labelling (treasure type-only;
  no invented entities; developer names usable for pathfinding by the
  player's explicit exception).
- **Audio:** stereo + behind-pitch + proximity rate; beacon/wall/
  arrival volumes separate; wall range in walking steps; generation-
  tagged cue cancellation and a 100 ms staleness cutoff for
  time-critical tones; mixing owned by a single SDL pipeline that
  native callbacks never block on. The docs explicitly note full HRTF
  is *not* in this mod — a useful contrast with FFPR, and stated
  rather than implied.
- **Auto walk:** default off, speak-path separately default off,
  immediate cancel on player input and an enumerated cancel list,
  no auto-interact on arrival, native input handback that survives
  worker failure.
- **Guides and assists:** F11 step reading with F9/F10 stepping and
  Shift+F11 state-inference resync; auto-solve as separate default-off
  interception of qualified native script entries only, refusing on
  identity-check failure; per-minigame assists (lightning, Jecht Shot,
  Blitzball auto-win new in 1.0.2, chocobo/butterfly, Gagazet, Sin
  collection, Overdrives) each default-off and context-scoped, with
  their manual cues default-on and independent; voiced-dialogue policy
  (Suppress / Braille only / Speak anyway) for the natively voiced
  game; Sphere Grid routing/planning support in menus.
- **Settings:** persisted INI (`FFXAccessibility.ini`), F8 spoken menu
  (Speech / Navigation audio / Navigation / Miscellaneous), ~35
  documented settings with defaults, ranges and steps; installer
  preserves the file across updates.
- **Installer/distribution:** file verification, backup + removal of
  conflicting mods and leftovers, launch-redirect repair with
  backup/restore, read-only check mode, SHA256SUMS published; 1.0.2
  added an exact-artifact AV scan gate and DLL-search hardening after
  a Defender block, with the unsigned-build gap and an unresolved
  historical detection stated in the open.

### What couldn't be examined

- Runtime behaviour (not run); the docs themselves mark most features
  "offline verified, gameplay confirmation pending," with a few
  user-confirmed exceptions (e.g. Sea of Sorrow routes, Baaj seals,
  Besaid sphere collection).
- The X-2 side: the release is titled FFX-X2 and the settings/menu
  work targets the HD Remaster bundle, but the deep navigation/guide
  documentation read here is FFX-focused; X-2 coverage depth was not
  established from the material read.

---

## 3. Blind Soldier v0.10.11 — buu420 (original FF VII PC)

### What was examined

- Staged packages: all four zips listed but only the main portable
  extracted (`Blind-Soldier-Portable.zip` → `extracted-portable/`,
  7,082 entries). The two AMM zips are the same file sets wrapped as
  `files/` + `manifest.json` (verified by entry/extension counts), and
  the 2013-x86 portable is the legacy-runtime variant (4,551 entries);
  neither was extracted. Inside the portable: the Reloaded-II runtime
  tree, the mod folder `Reloaded-II/Mods/ff7.accessibility.reloaded/`
  (config, ModConfig.json), and its Assets — `cutscene-voice/` (1,514
  hash-named `.ogg`), `movies/` (100 movies × `.ogg` + `.json` +
  `.credits.txt`), `footsteps/` (per-area sets, e.g. `cosmo/` with 148
  files from the game's own sound bank). Assets are triplicated across
  `Assets/`, `x64/Assets/`, `x86/Assets/`. Audio was inventoried and
  counted, not sampled.
- Public repo (full source, cloned): 3,685 files — 1,019 C# files
  across `Ff7.Accessibility.{Core,Reloaded,LegacyLayout,
  Steam2026X64,Runtime.Abstractions}` plus extensive test projects,
  268 `.md`, build/verify PowerShell with its own tests, and 1,813
  `.ogg` in-tree. Read in depth: `README.md` (57 KB — coverage/status,
  story guidance, descriptions/movies, navigation, auto walk, progress
  indicators, mod keys, controller navigation, mod settings sections),
  `Ff7.Accessibility.Reloaded/CutsceneVoiceManifest.cs`,
  `Ff7.Accessibility.Core/AudioDescriptionLevel.cs`, and the shipped
  `Configuration/config.json` (first ~100 lines of a much larger file).
  Not read line-by-line: the field-navigation internals (contracts in
  `LegacyLayout`, per-area navigators and their many test files), the
  guide-route-audit docs, and the Steam2026X64 runtime.

### What was found

- **Navigation:** category→target→route model with identical controls
  on field, world map and in the submarine mission. Field categories:
  Exits / Story / NPCs / Objects; world: Locations / Story /
  Transportation / Events / Chocobo Tracks. Routes over the native
  walkmesh (world map: native triangles + connected regions filtered
  by current character/vehicle reachability); collinear leg merging
  for speech; live tracking with a stray threshold before re-routing;
  pause/resume across battles. Field targets with no safe route stay
  selectable and report "direction unavailable."
- **Progress:** native Windows progress control, percent of the whole
  route including elevation, backtracking lowers it, intervals
  5/10/15/20 %, session toggle on F5.
- **Auto walk:** directional inputs only, never Confirm; releases at
  interaction points; enumerated release conditions (dialogue, menus,
  movies, battles, control locks, loading, focus loss, unreadable
  state, route failure, arrival); ladder climbing incl. horizontal;
  submarine rules that keep auto travel away from Emerald Weapon
  (stop above destination, manual descent, sighting announced,
  sight-loss pauses then stops).
- **Story guidance:** definitions keyed to progress state + field
  scripts, opening → Northern Crater plus Wutai's optional line;
  catalog tests run against the installed field scripts; explicitly
  not a walkthrough and makes no choices.
- **Audio description:** three tiers as detailed in §32.13 — full-mix
  per-movie tracks with JSON cue schedules and `credits.txt`
  provenance (cloned voice "Brice," permission documented, generated
  locally with Qwen3-TTS-12Hz-1.7B-Base); 846 content-addressed short
  clips (filename = SHA-256 of exact text, manifest carries durations,
  no normalisation, malformed/missing → TTS fallback); 93 battle
  animations / 175 cues, once per save, resettable, pause-synced.
  AD master volume 50–300 % in 25 % steps with per-track calibration
  and a limiter, independent of SR voice/music/cues.
- **Cues and config:** shipped `config.json` is a large per-feature
  flag + timing-constant surface (exit cues 80/400 units @ 3,200 ms;
  ladder 1,600 ms, mount 700 ms; footstep walk/run 500/300 ms against
  measured speed; highway cadences 320/700/260 ms with 1,200/900
  distance hysteresis). Player overrides live in
  `player-settings.json`, preserved by the installer; the F11 spoken
  settings menu stays audible with mod speech off and flags
  restart-required changes. Battle status keys read the game's
  *visible* party state only.
- **Honesty apparatus:** README separates implemented from played;
  tester coverage is enumerated by area (opening → Wutai approach,
  including recorded failures/workarounds); dual runtimes (2013 x86
  FFNx legacy, modern x64 Steam 2026) with parity test projects;
  every build script ships with its own tests, and package-verify
  scripts exist for both portables.

### What couldn't be examined

- Runtime behaviour (not run); no complete playthrough exists even by
  the author's account, so story-guidance coverage past the Wutai
  approach rests on the catalog's automated script tests, not play.
- The movie `.json` cue schedules: confirmed present per movie; the
  one sampled (opening) read as empty on a head read, so schedule
  format details are asserted from file presence + the manifest/
  tracker code names, not from a parsed schedule.
- Per-area navigator internals and the guide-route-audit ledgers were
  surveyed by file/test names only.
- The AMM packages' `manifest.json` files were not opened; AMM
  packaging is characterised from structure (files/ + manifest),
  not from the manifest contents.

---

## Cross-cutting observations

- **All three authors converge independently** on the same safety
  contract for auto walk (never confirm, release on enumerated states,
  arrival ≠ interaction), the same defaults posture (assists off, cues
  on, speech on), and the same honesty practice (implemented vs
  confirmed tracked separately). That convergence is itself evidence
  these are the right defaults for OGA.
- **Prism is now the shared speech layer** for this whole family:
  FFPR and FFX pin Prism 0.18.3; FFX's installer actively removes
  Tolk; Blind Soldier also speaks through Prism. FFPR's predecessor
  mods used Tolk — the migration is complete across all three.
- **Data beats code, three times:** FFPR's baked maps + JSON data
  packs, FFX's generated catalogs, Blind Soldier's state-keyed story
  definitions and hash-keyed clips. Every one of these systems can be
  extended or corrected by editing data, and two of the three are
  validated offline against the game's own assets.
- **The biggest divergence** is spatial audio: FFPR ships real HRTF
  with a north-up frame; FFX deliberately ships stereo+pitch and says
  so; Blind Soldier (from the material read) leans on speech,
  progress and proximity cues rather than a spatialised beacon. All
  three positions are documented choices, not accidents.

*End of notes.*

# Kingdom Hearts: Birth by Sleep (PSP, ULUS10505) — reverse-engineering notes

Adapter: `Core/khbbs_adapter.cpp` (id `khbbs`). Bench: `~/khbbs-scan/`
(hunt benches khbbs-hunt1..5, live bench khbbs-live, outside the repo).
Research digest: `~/workspace/games/kh-bbs/research.md`; cheat-code hunt
seeds: `~/workspace/games/kh-bbs/hunt-seeds.md`.

## The build (verified off the disc itself, 2026-10-17)

- PARAM.SFO: DISC_ID ULUS10505, TITLE "KINGDOM HEARTS Birth by Sleep",
  DISC_VERSION 1.00, APP_VER 01.00, PSP_SYSTEM_VER 6.20.
- UMD_DATA.BIN: `ULUS-10505|AB9E6266F4DD7BC1|0001|G` — the USA retail
  build. (OpenKH documents three US builds in this family — a sample, the
  retail, and a Canadian ULUS-10505F; this is the retail. Addresses below
  are claimed for THIS build only.)
- The disc image used is the Redump CHD (the same dump as the project
  owner's Dropbox copy; MD5 a72f77f0c907122114da3006b468627e, extracted
  ISO 1,767,473,152 bytes).
- Boot proof (repo psp-proof): gameid=ULUS10505, 900/900 frames, live
  480x272 framebuffer, clean shutdown.

## Architecture: one EBOOT, nine overlay modules

The EBOOT (3,708,616 bytes decrypted; type EXEC, first load segment at
0x08804018, entry 0x08A8BD58, ~19 MB BSS) is a shell. The game proper
loads one overlay ELF per mode from PSP_GAME/USRDIR/MODULE:

- TITLE.ELF (title + New Game flow), MENU_CAMP.ELF (the pause/camp
  menu), MENU_TERM.ELF, MISSION.ELF (battle/gameplay), SHOP.ELF,
  CHARA_REPORT.ELF (reports), BATTLE_DICE.ELF (Command Board),
  FRUIT_BALL.ELF, KEYBLADE_RIDE.ELF.

All nine are relocatable (type 0xFFA8) linked against a shared overlay
base near 0x08B8BF08. Consequence: state addresses are mode-specific
until proven shared — the verified map below is TITLE-module state, and
nothing in it is claimed for camp/battle. Ghidra project KH_BBS at
~/khbbs-scan/ghidra (EBOOT.BIN.dec imported with ElfLoader,
MIPS:LE:32:default; full auto-analysis completed).

Game data lives in PSP_GAME/USRDIR/BBS0.DAT..BBS4.DAT (.bbsa archives;
formats documented by OpenKH — see the research digest for the world/
room index, the .ctd text files, and the documented .bin tables).

## The boot flow (screenshot-verified, hunts 1-4)

1. Auto-save INFORMATION dialog ("This game uses an auto-save feature…")
   — up within the first 900 frames; dismissed with CROSS only (Start
   does nothing).
2. "No system data found. System data will be created automatically the
   next time you save your game. Proceed?" — Yes/No, cursor starts on
   **No**; LEFT to Yes, CROSS to proceed.
3. Opening movie (Ventus). START skips it.
4. Title menu (TITLE.ELF): **NEW GAME / CONTINUE / INSTALL GAME**.
   START also confirms here (a second Start tap on NEW GAME is how
   hunt4 first reached the difficulty select).
5. NEW GAME → difficulty select: "Select a game mode." — **Beginner
   Mode / Standard Mode / Proud Mode / Critical Mode**, cursor opens on
   Standard; the description line under the list changes per item.
   CIRCLE backs out to the title.

## The verified map (hunts 4-5, two independent cold boots)

Method: screenshot-verified sequence hunting — a value counts only if
it matches the highlighted item at every step of a D-pad walk (idle
control pair included, wrap included) and is the UNIQUE exact match in
all 24 MB of user RAM. Both cursors passed on the first analysis run
(one hit each).

Title-flow state struct (heap; identical addresses on both boots —
PPSSPP's allocation is deterministic for this path):

- Title cursor: u16 @ **0x09920D76** (current), +2 previous, **+4 item
  count (3)**. 0-based: 0 New Game, 1 Continue, 2 Install Game. Wraps.
- Submenu gate pointer: u32 @ **0x09920D8C** — 0 while the title menu is
  on top; **0x09922EB0** while the difficulty select is open.
- Difficulty cursor: u16 @ **0x09922FA6** (current), +2 previous,
  **+4 item count (4)**. 0 Beginner, 1 Standard, 2 Proud, 3 Critical.
  Wraps.

Traps recorded (both live-proven, both covered by host tests):

- **The stale submenu cursor.** Backing out of the difficulty select
  leaves its cursor holding the last value (hunt5 x0→x1: still 1 on the
  title screen). The cursor can never gate its own screen; the submenu
  pointer at 0x09920D8C is the only "which screen is on top" witness.
- **The boot-dialog fake.** On the boot dialogs the title cursor and
  the gate pointer both read 0 — formally a title menu at NEW GAME.
  The +4 count field reads garbage there (2189), not 3. Both adapter
  gates therefore require the count field before believing a cursor:
  zeroed RAM and pre-title states fail closed.

## Adapter state (2026-10-17)

Wired and live-verified (khbbs-live, cold boot, the real adapter
attached through the bench Host): title-menu WhereAmI and nav echo,
difficulty-select WhereAmI and nav echo, ready() on exactly those two
gates, honest "not mapped" speech elsewhere, DumpState anchor scan.

Host test: `scripts/khbbs-adapter-test.sh` — 25 checks, including the
two traps above as synthetic plants. Full adapter suite green the day
it landed (dbz 17, dq9 26, mku 56, khbbs 25, Dissidia all-pass, osk,
gba). NOTE for the eventual GitHub push: `scripts/test-strength-baseline.txt`
(on GitHub main) needs the row `Core/khbbs_adapter_test.cpp` with a
strength of **25** — the same one-line omission that turned the MK
adapter's CI gate red.

## The New Game flow, continued (hunts 6-15, 2026-10-17/18)

- Confirming a difficulty pops a second dialog: "Play the game in
  Standard Mode? You can't change modes later. Choose carefully!" —
  Yes/No, cursor starts on **No** again (LEFT to Yes, CROSS).
- Then the game plays the **prologue cinematic sequence** — several
  scenes (Keyblade Graveyard, Destiny Islands, Land of Departure).
  START pauses a scene (movie PAUSE: Resume / Skip Scene); DOWN +
  CROSS skips the current scene only. About seven skips clear it.
- **There is NO character select after the prologue.** The game drops
  straight into a **forced Ventus tutorial** in the Land of Departure
  (objective: "Head for the summit!"). OpenKH's DP13 character-select
  map belongs to a later point in the flow (the story split after the
  tutorial exam is the working hypothesis — unverified).
- Field basics (screenshot-verified): HUD shows the Commands list
  (Aero / Quick Blitz / Strike Raid / Attack), the D-LINK gauge, the
  FOCUS gauge, and the HP arc. **CROSS executes the selected command**
  (the list's bottom row is literally "✕ Attack"); **CIRCLE jumps**
  (per the pause screen's own hints); the analog stick walks
  (camera-relative); SELECT does nothing. START in the field opens a
  pause overlay with control hints and the current objective — **no
  camp menu is reachable in the tutorial's opening minutes**.
- A "thought" box ("I'll get the best view from the summit!") is
  non-modal: it ignores every button and clears on its own as Ventus
  changes zones. Not a dialog; do not build dismissal logic for it.
- **The deck does not cycle in the tutorial opening**: D-pad taps and
  30-frame holds both left Attack selected. Whether cycling unlocks
  with the training battle, or needs battle state generally, is open.
- **CWCheat stat-block seeds FALSIFIED for the live game.** The whole
  0x099287C0-0x09928810 window (munny seed 0x099287DC, HP seed
  0x099287E8) reads zero during tutorial gameplay, in two independent
  dumps. Treat the codes as written for a different state/build; HP
  and munny need a genuine hunt (damage/munny deltas), not seeds.
- Player position: a six-dump movement scan found ~2,955 float words
  that track Ventus's movement (entity structs mirror the position in
  many places). Crowning the authoritative coordinate needs a
  controlled single-axis experiment — that word is also the key to
  navigating to the summit battle without eyes.

## Player position — CROWNED (hunts 15-17, same state lineage)

**x @ 0x09961630, y @ 0x09961634, z @ 0x09961638** (floats; part of a
matrix-style record with 1.00 at +12). Evidence: idle-stable; tracks
every walk in a six-dump scan (spawn (0.00, 0.04, -7.11); north wall
z = -12.19; west corner (-9.91, 0.01, 13.80)); a jump arcs y
0.01 -> 0.53 -> 1.25 -> 1.33 (peak) -> 0.01 (landed) with x/z pinned.
Two impostor classes recorded: an interpolated copy nearby
(0x099750C0: close but lagging/offset — the ghost-drain pattern), and
a second matrix sharing x/z whose +4 is height-derived, not height
(0x09F0D2C0: +4 runs ~y + 1.0). Heap addresses on THIS lineage —
cross-boot stability not yet proven (the title structs were stable
across boots; entity structs are unproven).

## Leaving the courtyard (hunts 18-25, position-steered)

- The north side (z = -12.19) is a wall the whole way; the white
  "stairs" beside the tower are not the route (Ventus wedges in the
  corner at (-1.78, -12.19)).
- The exit is SOUTH: from spawn, +z crosses the courtyard, transitions
  zones (coordinates jump to ~(35, -10, 4) — new frame), and reaches a
  mountain path and a training plateau with hanging ring targets.
- Approaching the rings triggers the attack lesson: "Press ✕ to
  attack the ring! Keep attacking to string together a powerful
  combo." Attacking wakes the battle UI up: the Command Gauge fills
  and a Finish row ("Hero") appears over the command list.
  INFORMATION banner: "Attack the single hanging ring!"
- Steering lesson (controller v3): estimate the full 2x2 stick->world
  matrix from two probes — a rotation-only model CANNOT work (the
  stick/world map includes an axis flip). Open-space steering is
  reliable; probes taken while pressed against a wall poison the
  estimate (wall slides), so re-probe only on free movement.

## The Command Deck cursor — CROWNED (hunts 25-28)

**u32 @ 0x099964EC: 0 = Quick Blitz, 1 = Aero, 2 = Strike Raid.**
Unique exact match in all 24 MB for the clean five-dump sequence,
then confirmed on six further states including both wraps. D-pad
DOWN steps +1 (mod deck size), UP steps -1. The cycle covers the
deck commands ONLY: the "✕ Attack" row is not part of it (Cross
always attacks; Triangle executes the highlighted deck command, and
executing fills the Command Gauge toward the Finish row). Deck size
will grow as commands are added — the mapping above is for the
starting three-command deck. The deck stays inert until the ring
lesson sequence wakes it (courtyard D-pad presses did nothing); the
word reads 0 (= Quick Blitz) while dormant, so the index alone
cannot gate speech — a "deck live" witness is still owed before
this is wired into the adapter.

## PLAYER HP — CROWNED via the decomp (2026-10-18, user-steered)

The bench hunts stalled, so per the user's direction the answer came
from Ghidra (project KH_BBS at ~/khbbs-scan/ghidra; scripts
KhDecompile/KhCallers/KhOffSearch in ~/khbbs-scan/scripts):

- The CWCheat "Infinite HP" code patches code at 0x0893280C; the
  ORIGINAL instruction there is `lh a0, 0x380(s0)`, inside
  FUN_089327e8, which decompiles cleanly to a percentage check:
  `(*(short*)(actor + 0x380) * 100) / *(short*)(actor + 0x382) < 26`
  — a low-HP warning. So the actor struct carries **current HP as
  u16 at +0x380 and max HP as u16 at +0x382**.
- The spawn/init function FUN_08928330 writes both from a params
  record: cur = params+0x70, max = params+0x74, and leaves
  fingerprints: **u16 at actor+0x330 == 1**, **flags word at
  actor+0x234 has bit 0x20 set**, pointer at actor+0x27c.
- A live scan for (cur == max, plausible HP) + those fingerprints
  gives a UNIQUE hit in every field dump tried: **player actor base
  0x09976090 — current HP u16 @ 0x09976410, max HP u16 @ 0x09976412,
  both 35** (Ventus's starting HP), stable across the courtyard,
  the training plateau, and the ring states. Actor state word at
  +0x220 reads 1 in the field; +0x58 holds a live pointer.
- Gate bonus: at the TITLE screen the whole block reads zero — so
  max HP > 0 (with the fingerprints) is a clean "in the field"
  witness for the adapter. Still owed: cross-boot stability (all
  KH field dumps so far descend from one boot lineage) and an
  actual damage delta (no enemy has hit Ventus yet) — the layout
  itself is decomp-proven, so a damage check is confirmation, not
  discovery.

## COMMAND GAUGE — CROWNED via decomp + actor-window scan (2026-10-18)

With the actor base known, a scan restricted to the actor window
(0x09976090 + 0x800) over the hunt26 ring series leaves two rising
u16 counters; the decomp separates them:

- **Command Gauge: u16 at actor+0x226 — live 0x099762B6.** Ring
  series values 5 → 9 → 15 → 21 → 27 → 33 → 36 (≈ +3 per connecting
  hit); 27 through the hunt31 series and tail (the gauge does not
  decay in the field); 87 by the hunt30 pond state after hunt29's
  long session. Code evidence: FUN_08985fa0 compares
  `*(ushort*)(actor + 0x226)` against a threshold read from a
  definition record's +0x74 (style/finish availability), FUN_089831a8
  writes it (the add path), FUN_08927eb8 read-modify-writes it (the
  per-hit increment), FUN_0892b998 zeroes it (area/battle reset).
- The other riser, **u16 at actor+0x320** (4, 8, 16, 24, 32, 40, 43
  over the same series), is read alongside the gauge by FUN_08983aac
  but its role is UNIDENTIFIED — a per-hit statistic (hits or damage
  dealt) is the working guess. Not the Focus gauge (that starts at
  MAX; this starts near 0).

Adapter implication: HP + gauge + deck cursor are all live in the
actor/deck blocks now. The deck-live gate is the last wiring
blocker for field speech; actor presence (max HP > 0) plus the
tutorial-progress flags are the current witness candidates.

**WIRED + LIVE-VERIFIED (2026-10-18 night):** the adapter now
speaks the field. InField() requires max HP in 1..999 AND the
+0x330 mark AND the +0x234 flag bit (all-zero at title = fails
closed). WhereAmI in the field: "Ventus, health 35 of 35.
Command: Aero. Gauge 27." Deck echo is CHANGE-ONLY (the cursor
word provably does not move while the deck is dormant, so a nav
command that changed nothing stays silent — the dormant-window
problem dissolves without a separate live witness). Field checks
run BEFORE the title/difficulty checks in both WhereAmI and nav,
because the title structures can hold stale-but-valid values
under the field. Host test: 31 checks (6 new field cases,
including the stale-title coexistence plant). Full suite green.
Live proof (khbbs-live2 vs hunt28/end.ppst): ready=1; field line
spoke with gauge 49; DOWN/DOWN/UP echoed Aero → Strike Raid →
Aero. Baseline note for the future push: khbbs strength is now
31.

## Playthrough state (hunts 32-39, 2026-10-18 night)

- Branch discipline matters: hunts 31-33 accidentally ran on the
  PRE-completion branch (hunt25's mid-lesson state). The live
  lineage is hunt26→30 (lesson 1 complete). In that branch the
  pause objective reads: **"Use commands to hit the group of
  rings!"** (lesson 2). The hunt31 branch's objective still says
  "Attack the single hanging ring!" — lesson 1 wants a COMBO
  string, not spaced single hits.
- The group-of-rings stand is located: an ornate golden stand with
  several rings on chains at the plateau's SOUTH TERRACE, around
  (29, 19) — screenshot-verified (hunt38 "at"). Deck commands home
  to the nearest attackable target, so from mid-plateau Quick Blitz
  lunges at the big ring instead; he must be parked at the stand
  itself for the group hits to register. Six command executions
  from mid-plateau did not complete the lesson.
- In-bench closed-loop steering (stick→world matrix, re-solved per
  leg) works on open ground (hunt36/38 arrived) but is fragile:
  the basis goes stale when the camera yaws mid-route, wall slides
  poison it (hunt35 drove him into the pond cliff at (24.2, 7.4)),
  and hunt39's inter-command re-steers flung him to (47, 12).
  Bench states saved while PAUSED (every hunt ending in an
  objective check) silently eat the next hunt's inputs until START
  dismisses the pause — and even the dismiss tap occasionally
  fails to register on a freshly loaded state.
- Lesson 2 completion, the remaining tutorial beats, the first
  real fight (damage delta + lock-on), and the camp menu all wait
  on this playthrough thread. Fixtures from GameFAQs (research.md)
  remain the shortcut option.

## Tutorial-progress flags (hunts 26-29) — witnesses, not a gate

Five words share one profile: 0 at spawn, 0 on plateau arrival, 0
during the first attacks, switched on when the ring lesson completes
and on through all later field play: **0x09961158 / 0x09961160 /
0x09961168 read 3** (stride 8, in the player-entity neighborhood) and
**0x099895CC / 0x099895D0 read 1**. They are tutorial-progress bits
(the 3s may be per-command unlock state), useful corroboration that
the deck SHOULD be live, but they do not distinguish menus or other
modules — the deck-live gate is still owed. After the ring lesson
the plateau is a sandbox (commands execute, the gauge builds, no
further prompts); the Command Gauge's word and player HP both still
need a real fight — no damage source has been reached yet.

## Next hunts (in order)

1. Battle state: the Command Gauge word (attack-series deltas), the
   "deck live" gate witness for the cursor above, and player HP by
   damage deltas when a real fight starts (the tutorial's sparring
   match is the expected first damage source). A first gauge pass
   over the hunt26 m-series came back NEGATIVE (286 monotonic
   float risers, all clock/animation-shaped; none gauge-shaped) —
   the m-series mixes ring hits with lesson machinery. The clean
   design is a dedicated series AT a ring: one connecting hit, one
   dump, repeat, then an idle decay tail. That design ran as hunt31
   and ALSO came back negative: the visible gauge sliver saturates
   almost immediately (the Finish threshold is low), so the series
   is flat after the first hit or two, and step/decay/plateau scans
   isolate only framebuffer noise and UI-struct fractions. The
   remaining design is a minimal pair — dump, ONE connecting hit,
   dump — repeated at a second ring, intersecting the two deltas.
   Gauge encoding may be per-mille int in a UI heap block rather
   than a float in the entity block.
2. The camp menu (MENU_CAMP.ELF) once the game allows it: items,
   cursor, the Command Deck editor, and the status screen (the honest
   route to the real stat block).
3. Character select wherever it actually lives (post-tutorial story
   split hypothesis); the world/room state word (OpenKH index) is the
   gate thread.
4. Position stability check: re-derive 0x09961630 on an independent
   cold boot before the adapter ever reads it.
5. Saves as fixtures: GameFAQs hosts per-character endgame saves and
   an all-stories-cleared save (research digest has the page); a
   mid-game save unlocks hunts 1-2 without playing the tutorial blind.

## Tutorial lessons 2-3 (2026-10-18, hunts 34-50)

- LESSON 2 ("Use commands to hit the group of rings!") — the group is at
  the plateau's NORTH ring site (~(30, 41)), NOT the arrival terrace.
  After lesson 1 completes, a short cutscene TELEPORTS Ventus there
  (position jumps (40.26, 29.36) -> (30.37, 40.81) between the two
  prizes info pages, hunt27 p0->p2). Six hunts of firing at the south
  terrace rings did nothing; parking at (28.3-31, 40.7) and executing
  commands completed the lesson within 8 cycles (hunt47).
- LESSON 3 (lock-on): info pages -> "Try locking on to a ring!" L+R
  together (6f) locks on: purple reticle on the target ring, camera
  tracks it while walking (hunt49). Post-lesson info: "Once you're
  locked on, you can switch targets using L or R, or tap both at once
  to exit lock-on."
- Then a cutscene shows a treasure chest (Sliding Dash) on a ledge;
  control resumes at (22.71, -8.99, 35.79).
- BENCH TRAP (lineage discipline): hunts ending on an objective check
  leave the game PAUSED; hunts ending on panorama/field snapshots do
  NOT. A "dismiss pause" START tap on an unpaused state OPENS the
  pause and silently eats the whole run (hunt44: position never moved
  in 12 minutes). Rule: know the loaded state's pause status from its
  lineage; hunts that open the pause for an objective check must close
  it before ending.
- Steering v4 + FaceStep (12f analog step toward a point using the
  stick->world basis) works well for re-aiming between command lunges.
- Entity/props position table: stride 0x30 records at ~0x09F0D320 in
  field dumps carry (x, y, z) triples (ring stand parts at the south
  terrace: (37.18,-6.96,27.32), (39.17,-6.96,28.86) elevated; ground
  items at y ~ -8.9). Useful for locating set pieces without screenshots.

## Game-library fields (2026-10-18)
- Per the proposal in docs/proposals/adapter-game-library.md, the
  Adapter struct gained trailing `coverage` (one line) and `notes`
  (player-facing page text) members; every registered adapter is
  populated, KHBBS and MKU with full notes incl. their unmapped gaps.
  Host test grew 31 -> 34 checks (registry-level guards: khbbs
  coverage non-empty, khbbs notes name ULUS10505, mku notes name the
  QUIT GAME gap). Full adapter suite green. NOTE for the future
  test-strength baseline push: khbbs strength is now 34.

## Lessons 4+ / the Sliding Dash chest (2026-10-18, hunts 51-66) — PARKED
- Objective (pause page, verified): "Press X while near certain people
  or objects to interact with them. Move next to the chest and 'Open'
  it!" The chest holds Sliding Dash (walkthroughs).
- NOT FOUND yet. The whole training map was surveyed at ground level:
  it is ONE continuous frame (no coordinate jump at the "zone
  transition"; the path south of the plateau is the same map, z down
  to ~-2 ending at a cliff wall). Plateau floor y=-8.99, pond dip
  -9.24, path dip -9.49 at (37.2, 11.0). Every "ledge" candidate
  failed verification: the plateau's north line z~47 is open (hunt57
  probes), the west rocks are decoration, and hunt58's "east shelf"
  at y=-8.47 was a MIRAGE — its Step snapshots were all taken at the
  same point of a jump arc; standing grounded on those exact spots
  reads -8.99 (hunt65).
- Static-geometry triples at (30.30,-7.27,-3.50) and (38.70,-7.27,
  -3.50) flank the path's south end 1.7 above the floor; jump apex
  from the floor is y=-7.68, 0.4 short. Unresolved.
- Decision (2026-10-18): park the chest; take a GameFAQs save fixture
  past the tutorial to unblock the camp-menu and battle-state hunts.
  The chest is a one-off interact; the adapter's objective speech
  (the pause page already exposes the objective text) is the real
  deliverable for it.

## Bench traps (hard-won, 2026-10-18)
- The bench binary HANGS FOREVER on a failed savestate load (prints
  "state load failed" then sits in its loop). Always wrap in timeout,
  and verify the input .ppst exists before launching.
- Snapshot() writes .png + .ram per tag but a .ppst ONLY for the
  final end state. Mid-hunt tags are NOT loadable bases.
- Always mkdir the output dir first: writes fail silently otherwise
  (hunt57's outputs, incl. its end state, were lost this way).
- The steering basis (b_sx/b_wx/...) is UNINITIALIZED until the first
  SteerTo/Probe in the process. A stick lambda called before any
  steer computes a zero determinant and silently does nothing
  (hunt63's jump).
- A hunt that ends mid-jump (hunt58) leaves a state whose first probe
  measures the fall, poisoning the next hunt's steering.

## Save fixture + second cold boot (2026-10-18, load1-6)
- GameFAQs fixture installed into the bench memstick: darkstar-x97's
  100% save (all stories, Critical) — ULUS10505 system data + 4 slots.
  Slot 1: Terra, Mirage Arena Hub, LV 99, 76:03. Bench's prior
  savedata backed up at ~/khbbs-scan/savedata-backup-pretutorial.
- TITLE MAP IS DATA-DEPENDENT: with archives present the title has
  FOUR items — NEW GAME / CONTINUE / TRINITY ARCHIVES / INSTALL GAME
  (fresh profile: 3, no Trinity Archives). Item order is prefix-
  stable; INSTALL GAME is always last. The adapter's title gate
  (count == 3) fails closed on the 4-item title: generalizing the
  title table by count is owed.
- TITLE STRUCT ADDRESSES ARE BOOT-STABLE: cursor/prev/count at
  0x09920D76/78/7A and the gate pointer at 0x09920D8C read identically
  on this independent cold boot (count 4, gate 0 on the title).
- FIELD STRUCTS ARE NOT BOOT-STABLE: on this boot the tutorial
  lineage's actor base (0x09976090) and position (0x09961630) read
  all zeros in the field. The adapter's field gate fails closed
  (honest) but field speech needs the actor located per boot — a
  pointer chain from static memory, or an attach-time fingerprint
  scan (the spawn fingerprints from FUN_08928330 are known).

## Camp menu FOUND + save-side stat block (2026-10-18, camp1)
- The camp (main) menu opens with START in the field (post-tutorial);
  in the Mirage Arena hub it shows 9 items: Command Decks, Items,
  Abilities, D-Links, Stats, Reports, Command Board, Config, Game
  Help. Center card reads the character (Terra LV 99, HP 105/105,
  FP 100/100). Bottom help line per item ("Install commands in your
  Command Deck or meld new commands." for Command Decks).
- SAVE-SIDE PLAYER BLOCK LOCATED (the CWCheat layout was right; its
  anchor floats): munny u32 = 999999 at block+0xDC; the block sits at
  (1MB-aligned heap base) + 0x287DC. Fixture lineage base 0x09F00000
  (munny at 0x09F287DC); cheat-author base 0x09900000. Neighbours:
  second 999999 at +0xE0, LV u16 (=99) at +0xE4, u16 pairs (112,100)
  at +0xE8 and (100,100) at +0xEC — the menu's HP 105 does NOT appear
  in this window, so cur/max HP as displayed is computed or stored
  elsewhere; do not crown the +0xE8 pair as HP.
- The in-field ACTOR was NOT found in the hub with the tutorial
  fingerprints (no (105,105) u16 pair anywhere): the hub likely does
  not spawn the battle actor. Battle-state work needs a mission.

## Camp menu cursor CROWNED (2026-10-18, camp1-3)
- Cursor u16 @ 0x09919656 (heap, this lineage): 0..8, wraps both
  ends; unique exact-sequence hit over an 11-dump DOWN/UP series.
- Item count u16 @ 0x0991965A == 9 (same +4 anchor pattern as the
  title/difficulty structs). The cursor value PERSISTS STALE after
  the menu closes - never trust it alone.
- ACTIVE witnesses (BSS, stable across both menu dumps, zero in
  title/field/closed): u32 @ 0x08B5BEF8 == 8 AND u32 @ 0x08BD194C
  == 2 while the camp menu is open.
- Items in order: Command Decks, Items, Abilities, D-Links, Stats,
  Reports, Command Board, Config, Game Help.

## Camp menu WIRED + LIVE-VERIFIED (2026-10-18)
- Adapter: OnCampMenu() = witnesses 8/2 + count 9 + cursor < 9.
  WhereAmI speaks "Camp menu. <Item>, <n> of 9."; nav speaks the
  landed row. Camp is checked BEFORE the field (the menu opens on
  top of the field). ready() includes it.
- Host test: 38 checks (4 new camp cases incl. the stale-cursor
  trap: witnesses killed -> camp silent despite a valid cursor).
  FULL SUITE GREEN (dbz 17, dq9 26, mku 56, khbbs 38).
- LIVE (khbbs-live2 vs camp2/end.ppst, menu open at Game Help):
  "Camp menu. Game Help, 9 of 9." -> DOWN wraps "Command Decks,
  1 of 9" -> "Items, 2 of 9" -> UP "Command Decks, 1 of 9".
- Command Decks submenu seen (camp4): Edit Deck, Meld Commands,
  Command List, Choose Deck, Finish Commands (5 items, own cursor,
  unhunted). Terra's deck in the fixture ("Plantilla 1") shows all
  Battle Command slots empty, Action Command Jump, Shotlock Flame
  Salvo Lv 4.
- STILL OWED: title gate generalization for the 4-item title
  (Trinity Archives), deck editor + other camp submenus, per-boot
  actor location, lock-on/damage (needs a mission, not the hub).

## Edit Deck screen — first look (2026-10-18, camp5-6)
- Command Decks -> Edit Deck shows three groups: Battle Commands
  (7 slots, all empty in this fixture deck), Action Commands (Jump
  + empties), Shotlock Command (Flame Salvo Lv 4). A hand cursor
  marks focus: RIGHT moves Battle -> Action, DOWN moves Action ->
  Shotlock, LEFT returns to Battle. Focus geometry (per-slot or
  per-group), slot contents and the install list are unhunted.

## Camp sub-screens (2026-10-18, camp7-13)
- Decks submenu cursor: u16 @ 0x09919716 (0..4, wraps; unique hit).
  Items: Edit Deck, Meld Commands, Command List, Choose Deck,
  Finish Commands.
- Edit Deck (group view): focus word u16 @ 0x09919694: 0 Battle
  Commands, 1 Action Commands, 2 Shotlock Command (unique hit over
  a 9-dump walk). Navigation is SPATIAL, not a ring: DOWN from
  Battle lands on Shotlock; Action <-> Shotlock swap on DOWN/UP;
  LEFT/RIGHT swap columns.
- Cross on a group opens its SLOT LIST (Battle: 8 slots, numbered;
  header verbs: Square Shortcut, Triangle Reorder, Cross Remove,
  Select Abilities; help "Select a command to install."). Slot
  cursor: u16 @ 0x099251D6 (0..7, wraps; u16-unique; two u8 copies
  at 0x09925272 / 0x0992527C). NOTE: the index persists under the
  inventory (it names the slot being edited).
- Cross on a slot opens the COMMAND INVENTORY (right pane): "---"
  (none) then owned commands with level + ability tags (Quick
  Blitz Lv3, Meteor Crash Lv5, Sliding Dash x2, Fire Dash x2,
  Dark Haze x2, ... in this save), filter tabs (All + categories,
  L/R), help line = highlighted command's description. The hand
  icon LAGS one row behind the true (red-bar) selection in
  snapshots - trust RAM, not the glove. Inventory cursor unhunted.
- Choose Deck screen seen: Plantilla 1/2/3, Triangle = Rename.
- Camp-wide witnesses (0x08B5BEF8 == 8, 0x08BD194C == 2) hold on
  every camp screen; 0x08BD18A4 reads 3 on all camp screens vs 2
  in the field (screen-stack depth). Per-screen gates still owed
  before wiring sub-screen speech.

## Camp sub-screens WIRED + LIVE-VERIFIED (2026-10-18, camp15)
- Inventory cursor CROWNED: u16 @ 0x099252A8 (same widget struct
  as the slot cursor; a copy rides at 0x09932D06).
- No page word exists, so the adapter TRACKS the camp screen
  (CampTrack in khbbs_adapter.cpp), keyed on verified off-screen
  behaviour: sub/group words freeze under deeper screens; the
  slot word reads garbage (2361) until the slot list opens (first
  plausible value = arrival); the inventory word holds 0 under
  the slot list (only a change counts). Two live-run bugs fixed
  by this data: a garbage "Row 4441" promotion, and slot moves
  swallowed by a poisoned remembered value.
- Speech: decks submenu rows ("Command List, 3 of 5"), Edit Deck
  groups, "Slot N of 8" (count read from the widget's +4 word),
  inventory "Row N" (contents not read yet).
- Host test 46 checks; FULL SUITE GREEN. LIVE (khbbs-live3, full
  dive from camp1/end): main -> decks submenu -> editor groups
  (Shotlock/Action/Battle) -> "Slot 2 of 8" -> "Row 2" /
  "Command list. Row 2." - every line correct.
- Known v1 limits: a sub-screen is named on WhereAmI only after
  its cursor has moved (or the dive was inferred); other camp
  items' sub-screens speak the item name generically.

## Battle hunt - navigation findings (2026-10-18, battle1-20)
- All four fixture slots: Terra / Ventus / Aqua all parked at
  "Mirage Arena Hub" LV 99; slot 4 is FINAL EPISODE, parked on the
  World Map (LV 99, 32821 munny).
- The hub has NO battle actor: no (105,105) u16 HP pair anywhere
  in the heap on the fixture lineage. Arena battles are missions
  (MISSION.ELF) started at the lobby console (KH Wiki: console on
  one side of the lobby, transporter ring on the other; event
  starts at lobby center with Cross then Start). The console was
  NOT located by screenshot-guided walking (battle1-5 sweeps).
- World map (reached via save point -> Save menu -> World Map):
  glider free-flies with the analog stick (slow acceleration,
  long coast; D-pad does nothing; START opens Pause: Resume /
  Open Menu / Save; Cross = Dash, Circle = Land per the Commands
  box). SELECTED-WORLD byte: u8 @ 0x099658C0 - 0x0F while parked
  on the Mirage Arena node (banner "Mirage Arena / COMBAT LV - /
  TREASURES -%" showing), 0 in transit and in EVERY dump taken
  around the neighbouring island world (Dwarf Woodlands icon) and
  Castle of Dreams, across ~50 tap/raster positions. So the
  glider was never inside the selection radius: positioning by
  timed taps has ~10-unit error, radius seems ~2 units.
- Near-miss instruments (all in the 0x09965xxx-0x09969xxx map
  state block): ODOMETER float @ 0x09965B6C (+2.0/frame at full
  stick, cumulative across the savestate chain: 444 at the
  arena, 954 mid-transit, 3126/3670 at the island, 4258 at the
  castle); a copy at half value sits at 0x0996D6AC area. HEADING
  float @ 0x0996D690 (42 at the arena, 130 at the island, 159 at
  the castle; +1/+2 companions). Neither is a position pair -
  repeated matrix-copy scans (622 / 283 / 539 candidates) never
  produced a crownable (x,z). Glider POSITION remains uncrowned;
  next route is the decomp (world-map node table + selection
  test) or a parked-at-node differential scan.
- Status: battle state (lock-on word, HP damage delta) BLOCKED
  on reaching any enemy encounter. HP/gauge/deck remain
  decomp-proven + tutorial-lineage-verified.

## Four-item title GENERALIZED + LIVE-VERIFIED (2026-10-18, live4)
- Adapter title gate now accepts count 3 or 4 and picks the table
  by the count word (fresh: New Game / Continue / Install Game;
  cleared: + Trinity Archives at index 2, Install Game last).
  Unknown counts and cursor == count still fail closed.
- Host test 51 checks (5 new: the 4-item rows, cursor-at-count and
  count-5 refusals). FULL SUITE GREEN.
- LIVE (khbbs-live4 vs load3/end.ppst, the fixture title):
  "Title menu. New Game, 1 of 4." -> "Continue, 2 of 4" ->
  "Trinity Archives, 3 of 4" -> "Install Game, 4 of 4" -> wrap ->
  "New Game, 1 of 4". Every line correct.

## Per-boot actor location WIRED + LIVE-VERIFIED (2026-10-18, live2/live5)
- The strengthened spawn fingerprint (flags bit 0x20 + mark 1 +
  HP pair sane + state 1..3 + live +0x58 pointer + gauge <= 1000)
  is a UNIQUE hit in every field/hub/camp dump on both lineages
  and EMPTY at both title screens. (The weak 3-field version
  matched dozens of heap lookalikes - the extra fields matter.)
- SURPRISE: the fixture HUB does have the player actor, at
  0x0996C250 - the earlier "no actor in the hub" verdict came from
  searching for the menu's 105/105 pair; the actor reads 145/145
  (effective HP with ability boosts, most likely; the camp card's
  105 is the base figure). Proof it is the player: its gauge
  (+0x226) and action counter (+0x320) respond to Terra's own
  attacks and movement across battle1/battle5 dumps, and it
  persists under the camp menu.
- Adapter: LocateActor() sweeps 0x09800000-0x0A000000, accepts
  only a unique hit, caches it; InField() validates the cache
  cheaply and re-sweeps at most once per 3 s of host time.
  Field speech on a non-tutorial base speaks "Health N of M.
  Gauge G." only - the name and deck word are tutorial-lineage
  facts and are not spoken for other boots.
- Host test 56 checks (relocation plant at the real fixture base
  + values; cold-sweep twin ambiguity fails closed). FULL SUITE
  GREEN. LIVE: live2 (tutorial) unchanged - full Ventus line +
  deck echoes; live5 (fixture hub, load7/end) - ready=1,
  "Health 145 of 145. Gauge 2." - the first field speech on that
  lineage.
- Still owed for battle: a damage delta and the lock-on word
  (needs an enemy encounter - see the navigation findings above).
  The located actor does make one thing cheaper: any future
  fixture-lineage field work can trust HP/gauge speech in-world.

## Battle state REACHED + damage delta CONFIRMED; lock-on located (2026-10-18 night, battle21-31)

The battle hunt is UNBLOCKED. Route in: the save fixture's slot 4
(FINAL EPISODE) loads Aqua onto the world map, parked just south of
Radiant Garden; she drifts into its ring on her own (selected-world
byte 0x099658C0 reads 0x06 there, matching OpenKH's index).

World-map corrections to the battle6-era notes:
- The Land button is TRIANGLE, not Circle (the Commands-box icon
  was misread at low resolution; a full-res crop shows the triangle
  glyph plainly). Triangle opens a "Visit This World / Leave"
  dialog whose cursor STARTS ON LEAVE: UP, then Cross, visits.
- The float at 0x09965B6C is NOT a pure odometer: it climbs
  +1/frame even parked and idle (+2/frame at full-stick flight).
  It is a map clock of some kind; do not use it as distance.

Visiting Radiant Garden in the Final Episode plays a story scene
(Terranort), skippable via the movie pause (START -> DOWN -> Cross),
then field control in the central plaza with Unversed. Aqua's FE
save sits at 1 HP of 149, so the first hit killed her; the
game-over menu's RETRY restarts the fight in place at full HP
with the SAME actor base, while CONTINUE abandons to the world
map (actor struct freed). The game-over cursor position varies
between deaths; check the screenshot before pressing.

DAMAGE DELTA CONFIRMED (battle27, actor 0x09980B10): cur HP at
actor+0x380 stepped 149 -> 89 -> 57 -> 49 -> 25 -> 0 under enemy
hits, max fixed at 149, gauge at +0x226 rising 3 -> 56 through
the fight. Together with the tutorial lineage this makes three
lineages where the actor layout is live-proven.

State word (+0x220) semantics, all FE-verified: 1 = field/active
combat, 12 = field (out of combat), 15 = dead, 28 = respawn
transition. ADAPTER FIX (wired + tested): ActorPlausible's state
test widened from {1,2,3} to 1..255 - with the old test the FE
actor was unlocatable. Validated on ten reference dumps (unique
hit in every field dump, zero on both title screens).

Spawn mark (+0x330 low u16): 1 normally, 2 WHILE LOCKED ON
(battle30/31 toggle differential: 1 unlocked, 2 in both locked
dumps, 1 again after L+R unlock). ADAPTER FIX (wired + tested):
ActorPlausible accepts mark 1 or 2 - requiring 1 made the field
vanish for the duration of any lock-on. Host tests now 60,
full suite green; live-verified on the locked fight state
(khbbs-live6 vs battle30/fight.ppst: "Health 121 of 149.
Gauge 12.") and regressed on the tutorial and hub states.

Lock-on anatomy (located, NOT yet wired to speech): at lock-on,
actor+0x33C and a record in the actor's component cluster both
take the target's engagement id (19 in these fights); actor+0x350
latches the same id and keeps it after unlocking (a "last target"
latch, not a live flag). The cluster record (lineage address
0x0998CA90; [player ptr][target object ptr at +0x3C][target
position floats at +0x50]) zeroes its target pointer on unlock.
+0x33C also reads nonzero in some unlocked dumps (an engagement
id set by the game), so it is not by itself a lock flag; the
mark==2 word is the clean lock signal. Still owed: target
identity/direction for foe queries (the record's target object
is not an actor-layout struct; its +0x58 points onward), and a
second lock event against a different enemy to confirm the id
changes with the target.

## Character word FOUND + WIRED (2026-10-18 late, differentials on existing dumps)

The player block sits at a 1 MB-aligned base on every bench
lineage: 0x09F00000 for the tutorial lineage too (the CWCheat
author's base was 0x09900000-aligned, so the adapter locates it,
never assumes). Anchors, consistent across two dumps per lineage
and re-verified on 13 dumps total: munny u32 @ block+0x287DC,
level u16 @ +0x287E4, and a max-HP mirror u16 @ +0x287E6 that
equals the actor's max HP exactly on all three lineages
(35 / 145 / 149). The CHARACTER BYTE is u8 @ block+0x287F7:
0 = Ventus, 1 = Terra, 2 = Aqua, constant in field, camp, hub,
fight, and world-map dumps.

ADAPTER WIRING: ResolveCharacter() runs when the actor is
located - it sweeps the aligned bases for the block whose max-HP
mirror equals the actor's max HP (with level 1..99, munny
<= 999999, character byte <= 2 as corroboration; unique hit
only) and caches the character id. FieldLine now names any
located actor ("Terra, health 145 of 145. Gauge 2."); the deck
half of the line stays tutorial-only, and an unidentified actor
still speaks bare health/gauge. Host tests 60 (the relocated
checks now plant the block and expect the names). Live-verified
on all three lineages: Ventus tutorial (unchanged full line),
Terra hub, and Aqua's locked FE fight ("Aqua, health 121 of 149.
Gauge 12.").

## Inventory row contents - probes 1-3 NEGATIVE (2026-10-19)

Goal: speak command names/levels for camp inventory rows instead
of row numbers. The fixture's Battle list (camp14/v0) is known
exactly: ---, Quick Blitz Lv3, Meteor Crash Lv5, Sliding Dash
Lv3 x2, Fire Dash Lv4 x2, Dark Haze Lv4 x2.
- Probe 1: the level sequence [3,5,3,3,4,4,4,4] as a strided
  u16 or u8 array (strides 4-128 B, every phase, whole RAM,
  camp14/v0 + camp13/inv): NO HIT. Levels are not a plain
  strided array (packed, pointer-listed, or stored in another
  order than the sorted display).
- Probe 2: the name strings exist in a static table in the image
  (0x08D95539+: Quick Blitz, Meteor Crash, Sliding Dash, Fire
  Dash, Dark Haze packed consecutively = id order) and as heap
  TEXT-RENDER runs around 0x09C5xxxx (shared style pointer
  0x09C177C0, inline name + UTF-16 copy). The runs are UI layout
  objects, not the data model; levels render as separate runs.
- Probe 3: no window in the save block (0x09F28000-0x09F29000)
  holds the level multiset in any byte layout.
Next route: MENU_CAMP.ELF decomp - find the list builder and the
owned-command record layout it walks; the id->name half is
already solved by the static table above (plus the disc's .bin
command tables as cross-check).

## Lock-on STATE speech WIRED + LIVE-VERIFIED (2026-10-19)

The lock-on mark now drives speech, the strongest verified signal
from the battle hunts:

- khbbs_on_frame polls the mark while InField(): the 1 -> 2 edge
  speaks "Locked on." once (group "lock", Normal priority). Losing
  the lock stays silent (usually deliberate; the field line reports
  current state on request). g_lastLock resets on attach/detach and
  whenever the field gate drops.
- FieldLine appends " Locked on." whenever the mark reads 2, on
  both the tutorial and relocated-actor branches.
- Host tests: 64 CHECKs (the FE section plants mark 2 and checks
  the suffix, the single edge announcement, and no re-announce on a
  held lock). Full adapter suite green.
- LIVE VERIFIED (khbbs-live8, fresh adapter object, vs
  battle30/fight.ppst — the locked mid-fight state): ready=1,
  "Aqua, health 121 of 149. Gauge 12. Locked on."

Foe identity/direction probe (2026-10-19, NEGATIVE): the lock
record's target object (0x0998DBB0, stable across battle30/31
locked dumps) and its +0x58 chain object (0x09994D70) were dumped
and scanned. Both are scene-graph-shaped: dense pointer webs, 1.0
float runs, and the chain object carries position-like triples at
+0x80/+0x90 matching the record's floats. No HP-shaped u16 pair
survived scrutiny in either object's first 0x400 bytes. Foe speech
still needs: a second lock event against a DIFFERENT enemy (does
actor+0x33C's engagement id change with the target?) and a route
from that id to an enemy definition — decomp of the targeting code
is the likely path.

## Inventory contents, probes 4-7 + the FIXED definition table (2026-10-19)

Major structural find: the command NAME string blob and the command
DEFINITION table sit at the SAME addresses in every lineage checked
(tutorial hunt11, fixture hub load7, FE fight battle27, camp13/14):

- Name blob: packed ASCII names in id order from ~0x08D95400
  (finish/shotlock names, then battle commands at Quick Blitz
  0x08D95539, magic, items, ice-cream flavours, ...).
- Definition table: base 0x08B3492C, 475 records x 16 bytes.
  Record: +0x00 name pointer; +0x04 pointer into a parallel
  8-byte-stride array at 0x08B343xx (descriptions?); +0x08.. bytes
  = category/params (battle commands carry 02 at +0x08);
  +0x0F = a per-category ascending counter (Demolition 01,
  Quick Blitz 02, Blitz 03, Magic Hour 04, Meteor Crash 05,
  Sliding Dash 06, Fire Dash 07, Dark Haze 08, Sonic Blade 09...).
  Quick Blitz's global record index is 91. Id -> name resolution
  is therefore SOLVED and boot-stable, whichever id the owned
  records turn out to use.

Probes 4-7 (all NEGATIVE, camp13/inv.ram + camp14/v0.ram): the
picker's 8 rows [QB L3, MC L5, SD L3 x2, FD L4 x2, DH L4 x2] do not
exist in RAM as (a) id arrays in display order under either id
hypothesis (global index 91.. or last-byte 2,5,6,6,7,7,8,8) at any
width/stride, (b) the same multisets in ANY order in any contiguous
u16 window up to 24 wide, (c) pointer sequences to def records or
name strings, and (d) nothing anywhere in RAM points at an
individual def record (the table is reached by base+index math).
The disc side is closed too: the names appear in NO extracted file
(module ELFs, EBOOT, BBS0-4.DAT archives are packed), and the save
files are encrypted (0.4% zero bytes, no plaintext).

Also learned: the fixture's Terra deck ("Plantilla 1") has ALL
EIGHT battle slots EMPTY (camp11/x1.png) — there is no installed
deck to pattern-hunt on this lineage.

Next route (decisive, replaces more pattern guessing): a
before/after differential on the SAME character in the SAME
session — snapshot the picker, meld two commands into one (or buy
one at a shop), snapshot again; the owned-record region is whatever
changed by exactly those rows. Meld/shop UI cursors are unmapped,
so that hunt comes first.

## Meld screen MAPPED end-to-end + a full meld performed (2026-10-19, camps 15-22)

The Meld Commands screen (Decks submenu index 1) is fully explored:

- Panel rows: SLOT 1 / SLOT 2 / ITEM, then the NEW COMMAND panel
  with its OK button. DOWN on the panel moves the glove
  SLOT 1 -> SLOT 2 -> ITEM -> OK. Help text tracks the step
  ("Select the first/second command to meld.", "Select a
  synthesis item...", "Proceed with melding.").
- Cross opens the focused slot's command list. While a list is
  open, the EDITED slot's box live-previews the highlighted row;
  other slots show their committed contents. (This preview is
  what made early screenshots look like phantom selections.)
- MELD LIST CURSOR CROWNED: u16 @ 0x099252BE followed the walk
  [0,1,2,3,4,3] exactly (analyze-kh unique-but-one hit) and
  PERSISTS after the list closes; a sibling at 0x09924FC6 tracks
  it while the list is open but is overwritten by render data on
  close. The word sits 0x16 past the deck picker's inventory
  cursor (0x099252A8) - same cursor struct family.
- Slot-1 picks always commit (any command can be an ingredient).
  SLOT-2 PICKS ARE PAIR-VALIDATED: Cross on a command that cannot
  meld with slot 1 is a silent no-op (this, not input failure,
  was camp15's mystery). TRIANGLE in the list toggles the
  usable-only filter (top-right label flips "Hide Unusable" /
  "Show All"); with the filter on, the slot-2 list shows only
  valid partners.
- A valid pair fills the NEW COMMAND preview. Meld flow: fill
  both slots -> ITEM (a synthesis crystal, or none) -> OK ->
  "Meld these commands?" Yes/No -> "Melding complete!" with the
  result at Lv1.
- PERFORMED: Fire Dash (Combo Plus copy) + Thunder Surge ->
  Sonic Blade Lv1 (no crystal). Post-meld panel verified empty.

The owned-record region STILL resisted the before/after
differential: a same-session pre/post meld diff (camp21/start vs
camp22/after) shows ~28k stable-changed words of UI churn; the
save-side player block (0x09F287xx) is completely untouched by
melding, so owned commands do not live there. Packed byte
counters near 0x09F279DC-E0/F4 changed by +-1 across the meld
(2->1, 2->3, 4->3 in byte 0 of three words) - noted as a weak
lead only. The list-builder decomp in MENU_CAMP.ELF remains the
route: the builder reads the owned records, so its stride and
field offsets settle the layout directly.

## Command inventory contents — CRACKED + WIRED + LIVE-VERIFIED (2026-10-19)

The route that finally worked was the decomp after all — but of
the EBOOT, not MENU_CAMP.ELF (the module contains no pointer to
the definition table; the table is EBOOT-owned).

- DEFINITION TABLE: base 0x08B34924 (the earlier probes used an
  alignment 8 bytes late), 475 records x 16 bytes, static.
  +0 family, +1 sub-type, +8 name pointer (runtime-localized
  packed blob). Family 2 = deck commands; sub-types 1 battle,
  2 magic, 3 item, 12 group, 13 special, 7 finish. Quick Blitz
  is record 91. Id -> name is boot-stable and solved.
- OWNED-INSTANCE POOL: while camp is open, static BSS word
  u32 @ 0x08B75E70 points at the camp work block; that address
  IS the pool base on the fixture lineage (0x9F262B8 in every
  camp dump; 0 in field/title dumps). Instance record, 10 bytes:
  u16 def id, u8 level (a COUNT for items), u8 zero, u16 growth,
  u16 ability def id, u16 flags. 573 shaped slots, 179 nonzero.
  A consumed instance keeps its level/ability bytes; its id is
  zeroed (the filter must skip id 0).
- MELD DIFF PROOF (camp21 -> camp22): melding overwrote the
  Thunder Surge slot in place with the result (Sonic Blade) and
  zeroed the consumed Fire Dash slot's id byte — 17 bytes total
  in the pool band; the save-side player block was untouched.
  Owned commands live in the pool, nowhere else.
- LIST CONSTRUCTION: family-2 instances sorted by def id, stable
  (pool order breaks ties). Reproduces the camp14 screen row for
  row. Ability ids resolve through the same def table.
- CURSOR SEMANTICS CORRECTED: u16 @ 0x099252A8 is 0-based over
  the command rows; the "---" none row pinned above them is NOT
  counted (the on-screen glove lags one row and misled the first
  read). FILTER TAB word u16 @ 0x099252A4: 0 All, 1 Battle,
  2 Magic, 3 Items, 4 Friendship (group + special subs). Every
  tab press resets the cursor to 0. UP past the top wraps to
  total-1 (live9: cursor 149 on the All tab).
- SPEECH WIRED (InvRowText in the adapter): per speech call the
  pool pointer is validated (fail closed), the pool is scanned
  and filtered by the live tab, stable-sorted, and the row
  speaks "Name, level N[, with Ability], r of total"; items
  speak "Name, N in stock". Any failure falls back to "Row n".
  Host tests: 71 checks (incl. tab filters, unknown-tab and
  no-pool fallbacks), full adapter suite green.
- TRACKER FLAW FOUND + FIXED: CampTrack originally promoted
  DecksSub -> Editor only when the group-focus word MOVED, so a
  straight Cross-only dive (group stays on Battle = 0) stranded
  the tracker at the submenu and every deeper screen echoed
  "Edit Deck, 1 of 5" (live9/10). Word table re-measured on a
  straight dive (live11): the slot word reads 2361 (garbage) on
  main/submenu/editor and turns plausible exactly when the slot
  list opens, then STAYS plausible all session — so promotion
  keys on the garbage->plausible TRANSITION (slotArrived), and
  the DecksSub -> Editor -> SlotList promotions cascade within
  one call. Editor and submenu are word-identical at entry; the
  one-move naming lag there (and inventory-after-slot-list)
  stands as documented design.
- TRACKING MOVED TO on_frame: CampTrack only sampled on command
  calls, so a SILENT dive (no commands during the dive) skipped
  the slot word's garbage phase and slotArrived could never
  fire (live13). khbbs_on_frame now runs CampTrack whenever the
  camp witnesses are up. Bench lesson: the live benches'
  RunFrames never called the adapter's on_frame — the real host
  does it every frame; live14's harness now does too (earlier
  live verifies passed only because they pumped command calls
  at every step).
- LIVE VERIFIED (khbbs-live14, fresh adapter object, silent
  dive from load7/end.ppst): "Edit Deck. Slot 1 of 8." ->
  DOWN: "Meteor Crash, level 5, with Combo Plus, 2 of 150" ->
  Magic tab DOWN: "Fira, level 3, 2 of 65" -> Items tab DOWN:
  "Hi-Potion, 50 in stock, 2 of 16" -> Friendship tab DOWN:
  "Group Cura, level 4, 2 of 13". All match the live13 tab
  screenshots row for row.

## Meld screen — MAPPED END-TO-END (2026-10-19, camp15-22)

- Meld list cursor: u16 @ 0x099252BE (0x09924FC6 is a render
  copy, overwritten when the list closes).
- Panel: SLOT 1 / SLOT 2 / ITEM + NEW COMMAND preview + OK;
  DOWN walks the glove; the edited slot's box live-previews the
  highlighted row while its list is open.
- Slot-2 picks are pair-validated (invalid pair = silent
  no-op). TRIANGLE toggles the usable-only filter ("Hide
  Unusable" / "Show All").
- A full meld was performed: Fire Dash Lv4 (Combo Plus copy)
  + Thunder Surge Lv4, no crystal -> Sonic Blade Lv1 via the
  "Meld these commands?" dialog (cursor starts on Yes).

## Lock-on foe readout — enemy actor FOUND (2026-10-19, battle32-41)

- The FE Radiant Garden fight fields ONE enemy at a time (a Red
  Hot Chili); R-taps never switch (nothing to switch to), and
  scripted Aqua died twice trying to out-cast it (battle33/34).
  The Mirage Arena hub was also probed (battle35-39): the blue
  ring is the world-map return warp; the floating console under
  the orb did not respond to Cross from any rim position tried.
  Arena event entry remains unmapped.
- ENEMY ACTOR (PROVEN by a dedicated damage series, battle41):
  the locked target's entity block base = its scene object
  - 0x1B8. Enemies share the player's actor layout for HP:
  cur/max u16 at block+0x380/+0x382. The Chili: base
  0x0998D9F8, max HP 1200, series 1200 -> 1167 -> 1109 -> 1076
  across the casts that connected (early casts whiffed while it
  hopped; its position in the lock record moved every sample).
  The scene id (19) is the first word of the scene object AND a
  field at block+0x1B8; whether it is a per-spawn index or an
  enemy-type id is STILL open (needs a second simultaneous
  enemy; this fight never fields two).
- The lock record's position (rec+0x50) tracks the target live.
- Dead ends: the target object and its +0x58 chain are scene
  graph/transform data (no HP); the enemy actor's +0x194/+0x1B4
  pointer (0x09960AC0) is a massively shared scene object, not
  a definition record; its static pointers (+0x150/+0x15C/
  +0x1D4/+0x1E8) are component vtable tables, not stat records.
  Type-id candidates inside the enemy block (+0x168=18,
  +0x174=45, +0x18C=35) need a second enemy species to diff.
- NOT WIRED YET (deliberate): foe HP/direction speech waits for
  a second-fight confirmation of the -0x1B8 relation plus a
  player-position source for direction/distance. A blind
  float-triple walk-diff for the player position produced 3815
  candidates (render scratch + view matrices) — that route
  needs the player entity block identified first, e.g. via the
  player actor's component cluster around the lock record.

## Lock-on foe readout — WIRED + LIVE-VERIFIED (2026-10-19)

- PLAYER POSITION found inside the lock record itself: +0x40
  holds the player's position, +0x50 the target's (verified
  across lock/unlock/walk/death dumps; the start-of-fight
  distance computes to 9.0, matching the screenshot).
- LOCATOR: the record's address floats per boot; it is found by
  its +0x38 back-pointer to the actor, disambiguated by the full
  chain — target's scene id == actor's engagement id (+0x33C),
  sane HP pair at target-0x1B8, finite position floats. In the
  FE dumps 15 words carry the back-pointer at that offset; only
  the true record passes every check. Unique hit or nothing.
- SPEECH: the field line's lock suffix becomes "Locked on.
  Enemy health %u of %u, %u unit(s) away." whenever the whole
  chain re-validates on that call; otherwise the plain
  "Locked on." Host tests 73 (real FE values planted; fail-closed
  check with the engagement id zeroed). LIVE (khbbs-live15):
  fight.ppst -> "Enemy health 1200 of 1200, 9 units away";
  battle41 end state -> "1018 of 1200", distance tracked the
  target live between reads (1 -> 8 as it hopped away).
- STILL OPEN: the target's NAME. The scene id (19) is not yet
  proven to be a type id (needs a fight with 2+ enemies up at
  once); direction/bearing is meaningless while locked (the
  camera faces the target) and belongs to the future cue layer.
- Enemy-block census (2026-10-19): sweeping the battle26/27/30/41
  dumps for entity blocks (scene-header signature at +0x1B8 plus
  a sane HP pair) finds exactly ONE enemy in every dump — this
  fight is single-enemy from entry through the retry restart.
  The name question has no second sample anywhere on disk; it
  waits on reaching any multi-enemy fight (arena entry, or
  steering the Final Episode glider to another world's packs).

## Foe / lock QUERIES wired (2026-10-19, per the user's steer to
## study how the other mods do it)

Pattern source: OGA's own Dissidia adapter + docs/proposals/
dissidia-battle-audio.md + docs/research/
how-mods-make-mechanics-accessible.md. Dissidia's battle mapping:
NextAlly -> the foe query (opponent HP, distance, vertical),
NextEnemy -> the lock-state query, the remaining unit-cycling
commands -> the self readout. Speech carries STATE (health,
distance, above/below); horizontal direction is deliberately
NOT spoken - the lock-on camera already faces the target, and
the mods survey puts direction in the cue layer (clock positions
are the convention for navigation queries, not locked combat).
Dissidia's foe query does not speak a name either.

BBS wiring (Core/khbbs_adapter.cpp): the LockClause validation
was factored into ReadFoe(cur, max, dist, dy) so the field line,
the foe query and the lock query share one fail-closed chain.
In the field (camp closed):
- NextAlly -> CmdFoe: unlocked "Not locked on."; locked +
  verified "Enemy health %u of %u. %u away[, above you|,
  below you]." (height threshold +/-5, Dissidia's); locked +
  unverifiable "Lock lost."
- NextEnemy -> CmdLockQuery: "Lock off." / "Locked on. %u
  away." / "Lock lost."
- PrevAlly / PrevEnemy / NextUnactedAlly -> the self field line.
Outside the field the five commands keep the not-mapped reply.
Library notes updated to describe both queries.

Host tests 80 (7 new, planted on the real FE record/block).
LIVE (khbbs-live16 vs battle30/fight.ppst, fresh adapter
object): locked -> NextEnemy "Locked on. 9 away.", NextAlly
"Enemy health 1200 of 1200. 9 away.", PrevEnemy the full self
line; after a real L+R unlock -> "Lock off." / "Not locked on."

## BEACON data feed wired (2026-10-19, the user's follow-up ask)

The beacon itself lives app-side (docs/design/cue-synth.md,
BUILT): a centred low beep (660 Hz for an enemy) whose pulse
quickens ~1.6 Hz at 45 units -> ~11 Hz at 4 units, mixed into
the emulator's own audio, OFF by default in Settings, silent
when nothing is locked. The core polls poke_cue_snapshot()
every frame, which dispatches generically to the attached
adapter's cue_snapshot member and stays silent when the member
is NULL. Dissidia implements it; BBS passed nullptr, so the
beacon could never sound in BBS. That was the whole gap.

khbbs_cue_snapshot (Core/khbbs_adapter.cpp) now fills the
snapshot from the same validated reads as speech: battle =
field actor live and camp closed (BBS has no separate battle
screen; the synth is silent for battle-without-lock anyway);
locked + dist only when the spawn mark reads 2 AND ReadFoe's
full chain validates — otherwise locked stays false and dist
stays 0 (never a stale distance). is_core and core_gain are
always false: BBS locks target enemies only, and there is no
EX-pickup analog. All fields are cleared before filling, the
function never speaks, and it returns false off the field.
The actor state word (1 = active combat vs 12 = out of combat
on the FE lineage) is a possible future refinement of
"battle"; not used — unproven for this purpose.

Host tests 84 (4 new: unlocked snapshot clean, verified lock
feeds dist ~9.0, unverifiable target silent, off-field refusal
clears). LIVE (khbbs-live17 polls vs battle30/fight.ppst,
fresh adapter object): locked -> battle=1 locked=1 dist=9.01;
after a real L+R unlock -> locked=0 dist=0.00; zero speech
from the polls. Library coverage/notes now mention the cue.
Honest limit: the sound itself plays only in the app build on
the user's phone; per the synth's own doc it has not been
heard by its owner yet even for Dissidia.

## NAVIGATION, first crowns (2026-10-19, completion-list item 1)

WORLD + ROOM — CROWNED at the differential level. Scanning six
field dumps from three worlds (Land of Departure hunts 28/47,
Mirage Arena load7/camp2, Radiant Garden battle27) for bytes
reading the OpenKH world id in every dump left exactly FOUR
offsets in 24 MB, and the two static ones carry the room too:
  u8 @ 0x08B61CA0 = world id, u8 @ 0x08B61CA1 = room id
  u8 @ 0x08B759C0 = world id, u8 @ 0x08B759C1 = room id (copy)
Values: LoD tutorial = world 1 room 5; Mirage Arena hub =
15/1; Radiant Garden plaza = 6/13. On title-era dumps (hunt5)
both pairs read 0xFF/0xFF — the no-map sentinel, so the speech
gate is world != 0xFF. The two copies agreed in all six field
dumps (the adapter can require agreement — the two-source rule
comes free). The other two hits are heap copies (0x09F179C0
world only; 0x09F22E44 world+room inside a busier struct).
STILL OWED: a live room CHANGE in one world (all samples so
far are one room per world), and room-id -> name matching
against the OpenKH room lists. Scripts: worlddiff.py.

PLAYER POSITION — the lock record tracks it UNLOCKED too.
In battle27 field0/lock1 (Radiant Garden), the known record
(0x0998CA90 on that lineage) holds target ptr = 0 while its
+0x40 triple moves with Aqua: (-10.39, 1.11, -0.69) ->
(-18.01, 1.11, 1.66). So in combat areas the adapter's
existing record locator (relaxed: no engagement-id demand)
yields position without any lock. The Mirage Arena HUB has
no such record (no combat): the only moving candidate is a
scene-node-shaped struct at 0x099752D0 whose +0x40-area
floats shift slightly between load7 w2 and camp2 m0 —
UNPROVEN, likely the player's scene-graph transform. Position
for peaceful areas stays open; combat-area position is the
first wiring target. Scripts: rechunt.py / rechunt2.py.

WORLD/ROOM SPEECH WIRED + LIVE-VERIFIED (2026-10-19, khbbs-live18):
The full-dump rescan (1,465 snapshots, seek-read of both pairs)
closed the two owed proofs: within one world the room byte
takes several values (Land of Departure reads rooms 1, 4, 5,
7 across snapshots), and single Theater bench runs show the
pair changing mid-run as scenes load their maps (ad27:
(4,15) -> (4,9); ad91b: (7,50) -> (10,2)). Room ids match
the OpenKH dictionary exactly: (1,5) Mountain Path, (1,7)
Forecourt, (1,4) Ventus's Room, (6,13) Central Square,
(15,1) Hub, (17,1) World Map, (4,9) Bridge, (4,15) Aurora's
Chamber, (2,6) Courtyard, (3,10) Foyer, (13,1) Badlands,
(7,50) Lanes Between, (10,2) Island Beach. NEW FINDING on
copy B (0x08B759C0): it reads 0xFF during Theater playback
while copy A tracks the scene's map — B is not a plain
duplicate. In gameplay snapshots (~1,000) the copies agree
without exception, so LocationClause() requires agreement
and stays silent otherwise (a transition can only
under-speak, never mis-speak). Adapter: kWorldAddrA/B,
kWorldNames (0x00..0x12; world 7 unnamed on purpose — its
maps are events-only and the room name carries them),
kRoomNames (287 entries generated from the OpenKH page;
placeholder names like "wm01"/"RESERVED" dropped), and
CmdWhereAmI's field branch now prepends the clause:
"Radiant Garden, Central Square. <field line>". Host
tests 88 (4 new: agreement speaks, disagreement silent,
world alone for an unnamed room, 0xFF silent); full suite
green. LIVE (live18, fresh adapter object): battle30 ->
"Radiant Garden, Central Square. Aqua, health 121 of 149.
Gauge 12. Locked on. Enemy health 1200 of 1200, 9 units
away."; load7 -> "Mirage Arena, Hub. Terra, health 145 of
145. Gauge 2."; hunt28 -> "Land of Departure, Mountain
Path. Ventus, health 35 of 35. Command: Quick Blitz.
Gauge 49." STILL OPEN in navigation: position speech in
peaceful areas (combat-area position via the lock record
is proven from dumps but not yet wired as speech), and
directions to exits/objectives (the .ngd navigation grids
in the game data are the documented lead).

PLAYER POSITION WIRED + LIVE-VERIFIED (2026-10-19, khbbs-live18;
benches khbbs-nav1/nav2): The peaceful-area suspect is the
player's engagement component — the SAME struct class as the
lock-on record, present in every field area with target word
0 while nothing is engaged. Hub movement differentials
(nav1/nav2, fixture lineage, node 0x099752D0): the +0x40
triple froze through a 60-frame idle control, tracked two
walk legs at 15-frame sampling smoothly, and clamped dead
at the south railing while the stick stayed held. Identity:
descriptor words +0x04 = 0x08B73DD0 and +0x1C = 0x08B73DB4
(fixed), +0x0C a STATE descriptor (0x08B73DE0 unlocked /
0x08B73E10 locked on — correlated with the actor's spawn
mark across 12 battle27/30/41 dumps), owner back-pointer
+0x38 == actor. Census on five dumps (hub field/camp,
combat unlocked/locked, tutorial): exactly one
descriptor+owner hit per dump; the tutorial's two extra
owner-matching look-alikes carry no descriptor words.
Adapter: LocatePlayerNode() (unique hit or nothing),
NodePlausible() re-validated per call, cache reset with
the actor's; PositionClause() prepends "Position <x>, <z>. "
(ground-plane, rounded) to the field WhereAmI line. First
live run caught the state-descriptor variation (battle30
spoke no position while locked) — the two-value fingerprint
above is the fix. Host tests 91 (3 position checks); full
suite green. LIVE (live18): hub -> "Mirage Arena, Hub.
Position 0, -43. Terra, health 145 of 145. Gauge 2.";
battle30 -> "Radiant Garden, Central Square. Position -5,
0. Aqua, health 121 of 149. Gauge 12. Locked on. Enemy
health 1200 of 1200, 9 units away."; hunt28 -> "Land of
Departure, Mountain Path. Position 29, 42. Ventus, ...".
STILL OPEN in navigation: directions to exits/objectives —
the position readout now makes room surveys possible
(walk to a landmark, record its coordinates); per-room
exit/landmark tables and spoken bearings are the remaining
work, with the .ngd navigation grids as the data lead.

DIRECTIONS — state of the hunt (2026-10-19, navigation tail):
Mechanism designed, data not yet collected. (1) FACING:
actor+0xAB8 is NOT a facing vector — it held one constant
diagonal per area across walk legs ((-0.859, 0, 0.863) in
the hub through both nav1 legs; (0.549, 0, 0.549) in the
Radiant Garden fight, ~40 degrees off the true direction
to the locked target). The working design is velocity-
derived facing: khbbs_on_frame already polls per frame;
track the located component's position, and when it moves
>= 1 unit from the last anchor, facing = that delta
(spoken bearings as clock positions relative to facing,
the frame the mods survey prescribes). WIRED 2026-10-21:
FaceTrack() runs per frame on the located component
(EnsureNode, factored out of PositionClause), keeps the
last >= 1-unit leg as the facing delta, and resets on
component/actor change and on leaving the field;
FacingVector() hands out the normalized delta and
DumpState logs it ("facing (1.00, 0.00)
(movement-derived)") as the bench's verification surface.
Host tests 95 (4 new: no facing before the first leg, a
+x leg, a sub-unit shuffle ignored, a +z leg re-tracks).
Nothing speaks a bearing yet — that waits on the landmark
tables below.
(2) LANDMARKS: the survey method is proven — the nav2 /
nav3 benches sample the position live, and a room's exit
is where the world/room words flip mid-walk. Hub facts so
far: save point at (0, -43) (the fixture spawn stands on
it); the raised central platform's rim blocks ~1.5 units
north of it (nav3: z clamps at -44.46 under a held stick).
SURVEY STARTED 2026-10-21 (khbbs-survey1, a parameterized
leg bench: stick direction per run, samples every 15
frames, early stop on a world/room flip or a position
clamp; built from obj/khbbs-survey1.o + obj/khbbs_adapter.o
+ obj/announce.o + ~/mku-scan/libppsspp.a + the PPSSPP
ffmpeg archives). Four straight legs from the spawn
(live18-hub state), all staying in world 15 room 1 — no
exit on any straight line, because the spawn dais is
ringed: stick (0,-1) clamps at the rim, z = -44.46;
stick (1,0) walks -x and clamps at x = -3.24; stick
(-1,0) walks +x and clamps at x = +3.24 (the dais is
~6.5 units wide, and the stick axes are camera-relative:
+x stick moves -x in room space here). Stick (0,1)
travels +z the farthest, ~7.4 units off the dais to
z = -35.5, then clamps — wall or far rim is unresolved.
Exits therefore need two-segment routes (off the dais,
then along the floor); that is the survey's next pass.
(3) OBJECT CENSUS — NEGATIVE in the hub: the combat
scene-header shape (id at block+0x1B8, 0x00011000 at +8,
8 at +12, translation at +0xAB8) matches NOTHING in the
hub dump; hub props/interactables use a different system
(the map's .olo spawn table, loaded who-knows-where, is
the next lead). Directions therefore wait on per-room
walked surveys or the spawn-table hunt; the position and
room readouts above are the substrate either way.

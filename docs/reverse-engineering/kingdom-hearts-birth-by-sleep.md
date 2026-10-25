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
  on top; while the difficulty select is open it holds that object's
  heap address. **The address moves with the profile**: 0x09922EB0 on a
  no-system-data profile, **0x09922ED0** with system data (see the
  moved-object trap below). Never exact-match the value.
- Difficulty cursor: u16 @ **the object + 0xF6** (current), +0xF8
  previous, **+0xFA item count (4)**. 0 Beginner, 1 Standard, 2 Proud,
  3 Critical. Wraps. Object identity, verified in every
  difficulty-open dump in both lineages (nine dumps): u32 @ object+0
  == **0xFFFFFFFF**, u32 @ object+0x0C == **0x08B90160**. The adapter
  follows the gate pointer, verifies those two words, then reads the
  cursor/count at the derived offsets.
- "Title is up and answering input" witnesses (2026-10-22,
  khbbs-titlegate, six cold boots): flow phase u32 @ **0x09920CD0**
  (1 = boot logos, 2 = opening movie, 3 = title screen owns the
  flow); title step u8 @ **0x09920D98** (walks 1, 2 across the logo
  cards, 3 while the title fades in, 4 once the menu is idle); fade
  word u32 @ **0x09920F44** (ramps during the title fade-in, returns
  to exactly 0 in the same few frames the step reaches 4). The
  adapter's title gate requires all three on top of the pointer /
  count / cursor checks. During the movie the word around the step
  byte is reused by the player — the phase word is what closes the
  gate there. The difficulty round-trip leaves all three in their
  title state, so the gate reopens on return, matching the game.

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
- **The early-title fake (found 2026-10-21 in the app, fixed
  2026-10-22).** The count check was NOT sufficient: TITLE.ELF
  writes the real count and cursor when the title struct is built,
  before the boot logos finish, so the old gate fired at the Disney
  logo fade and stayed fired through the logos, the movie and the
  title fade-in — announcing the menu over a white transition while
  the D-pad did nothing. Only the phase/step/fade witness triple
  above separates "struct built" from "menu on screen".
- **The moved difficulty object (found 2026-10-21, fixed
  2026-10-22, khbbs-diffgate).** The map above was verified on the
  hunts' no-system-data lineage (title count 3). On a system-data
  profile (title count 4 — Trinity Archives; SYSTEM.DAT alone
  drives it, game slots removed changed nothing) the object lands
  at 0x09922ED0, +0x20: the title leaves one extra 0x20 heap block
  ahead of it, and the neighbouring blocks are already allocated
  at title time on that profile, while on the fresh profile they
  appear only when the difficulty screen opens. Placement is
  deterministic per profile (three boots each lineage, plus the
  original hunt dumps). The old gate exact-matched 0x09922EB0 and
  read the cursor/count at fixed addresses, so on system-data
  cold boots it read zeros and failed closed — the difficulty
  select was entirely unspoken there. The fix follows the gate
  pointer and verifies the object's header words (above); cursor
  walks were re-proven by screenshot at both bases, and the fixed
  adapter speaks the screen end-to-end on both lineages. Related
  behaviour, both lineages: while the "Play the game in ... Mode?"
  confirm dialog is up, the pointer and identity hold but the
  count word reads 2 (the dialog's own option count), so the
  difficulty gate closes during the dialog — unchanged from the
  old gate's behaviour on the lineage where it worked.

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

## Meld Commands speech — WIRED + LIVE-VERIFIED (2026-10-24, meld runs 1-8)

The screen is wired end to end: panel focus, both ingredient
lists, the Synthesis (item) list, picks, refusals, the confirm
dialog and the result. Anchors are all chain-relative — the
screen relocates between camp visits (visit 1 pane 0x0992E250 /
widget 0x09925170; a later visit pane 0x099344E0 / widget
0x09933070); only the chain is stable:

- The decks object stays in registry slot 6 for the whole
  screen. Meld pane = decks +0x9C, verified head 0xFFFFFFFF +
  class tag 0x08B9CF88 @ +0x0C (a different class from the slot
  list's pane, so CampPaneObj fails closed here). Meld widget =
  pane +0x88, head + tag 0x08B9CEB8 @ +0x0C.
- Widget words (u16): +0x66 panel focus (0 Slot 1, 1 Slot 2,
  2 Item, 3 OK — OK joins the ring only once a valid pair is
  set; while a list is open the word freezes at the row the list
  belongs to, which is how the tracker knows which slot a list
  edits); +0x6A panel row count (3, or 4 with a pair set);
  +0x74 phase flags (0x200 a list is open, 0x400 the "Meld
  these commands?" dialog is up, 0x800 "Melding complete!" is
  up); +0x14A command-list tab; +0x14E command-list cursor;
  +0x150 Synthesis-list cursor; +0x163 usable-only filter flag.
  (The 2026-10-19 "cursor @ 0x099252BE" is widget +0x14E on the
  fixture's first-visit allocation.)
- The confirm dialog's Yes/No cursor is stored NOWHERE
  readable. The static word at 0x08B5BEE8 that sparse
  stop-sampling first suggested is a blink-phase word (opposite
  values for the same glove state in different runs; run 8's
  tracker spoke Yes/No alternately every frame from it). Run
  11's frame-series probe (90 consecutive frames per glove
  state, across the overlay statics and the camp heap) found
  the cursor only as scene-graph position floats of the glove
  graphic (95.0 for Yes, 180.0 for No, in unanchored heap
  nodes). The adapter therefore tracks the dialog cursor from
  the host's Left/Right echoes: it starts on Yes
  (screenshot-proven), either direction flips it in the
  two-option ring, and a dialog fall the completion phase
  never blessed speaks "Melding cancelled."
- Cursor staleness (proven twice): the cursor words are NOT
  rewritten when a list opens or the filter flips — the display
  re-bases to row 0 while the word keeps its old value. The
  tracker holds a resync-pending state (row = 0) until the word
  next changes; tab switches DO rewrite the word (to 0), so they
  adopt immediately. Same family as the deck editor's
  post-deck-switch seed.
- The decks sub word (+0x126) CHURNS under this screen (0, 1,
  2231, 2360, 2449, 49024, 35520 all observed) and a plausible
  churn value used to corrupt the tracker into announcing the
  Edit Deck submenu row for the whole visit. The meld branch
  outranks the decks cascade while the pane is up, never reads
  the sub word, and hands back a freshly seeded DecksSub on
  exit.

Row construction (run 6 photographed all 156 All-tab rows;
run 7 measured every tab by its wrap word): the pool's deck
commands (family 2) WITHOUT the item stacks (sub 3 never
appears), plus the action commands (family 3, which the pool
also holds — 13 instances the deck inventory's family-2 filter
never sees). Family 2 orders by def id; family 3 orders by the
def table's menu-order byte (+7) — Slide (def 243, order 19)
sits after Sonic Impact (def 258, order 15). Tabs: All = 147
commands + the nine Synthesis crystals appended as rows
147-155 (Shimmering, Fleeting, Pulsing, Wellspring, Soothing,
Hungry, Abounding, Chaos, Secret Gem — names from the static
item-name block at 0x08DFA106+; their stack counts are painted
from a store that was not located, so counts are not spoken);
Attacks (sub 1) = 56; Magic (sub 2) = 65; Friendship (subs
12+13) = 13; Movement (family 3 sub 4) = 6; Defense (family 3
sub 5) = 5; Reprisals (family 3 sub 6) = 2; Synthesis = the
nine crystals alone as a command-list tab (its rows set the
item when picked, like the All tab's tail). The filter flag at
+0x163 is a u8 — the byte at +0x164 is 0xFF, so a u16 read
there is always nonzero (run 8's first wiring read it as u16
and every row fell back to the row number). Row text is
the inventory's ("Fire Dash, level 4, with Combo Plus, 6 of
156"); duplicate copies' abilities can permute between the
pool order and the game's render order (observed on Quake and
Dark Haze duplicates — the multiset per def always matches).

Picks and refusals are INFERRED, because nothing in RAM records
a commitment: the bench swept the heap band, the overlay BSS
and every camp object for a picked def id / pool slot /
instance pointer across the pick transition and found only
render scratch, and the list's rows are never materialized as
ids or pointers anywhere (two full-RAM scans). The mechanism
used instead is the host's pad echo (adapter.h: Cross arrives
as OskType, Circle as OskDelete, press edges only, both
platforms): a Cross while a list is open is snapshotted (slot,
tab, row); if the list then closes without a Circle, the pick
is announced from the snapshot ("Slot 1: Fire Dash, level 4,
with Combo Plus." — a crystal-tail row of the All tab announces
as the Item pick instead, matching the game, which treats it
as the item step's shortcut); if 50 tracker passes pass with
the list still open, the press was refused and the tracker
says so once ("That pair won't meld." in slot 2's list,
"That's an item, not a command." on a crystal row, otherwise
"That command can't be melded."). The result is read from the
pool diff: the pool is snapshotted when the dialog opens, and
when the completion flag rises, the slot that held an
ingredient and now holds a different command names the result
("Melding complete. Sonic Blade, level 1."). The NEW COMMAND
preview's result is computed at render time and stored
nowhere readable, so it is not announced at pair time. The
result announcement is itself deferred: at the completion
banner's first frame both ingredient slots still read empty —
the created command is written into its slot a moment later
(run 10's dumps) — so the tracker retries the diff over the
following passes and only then falls back to the bare line.

What remains silent, and why:
- The usable-only filter's rows (TRIANGLE). For slot 2 the
  filtered list is the first ingredient's valid partners —
  recipe data the adapter does not carry — so filtered rows
  speak as row numbers ("Row 4"), and a pick made from a
  filtered list announces the slot without a name ("Slot 2
  set.").
- Crystal stack counts (see above).

Host tests: 176 checks (the meld checks plant the pane/widget
chain and the pool and drive entry, panel, list, pick, refusal,
cancel, the echo-tracked dialog, completion, cancellation,
Synthesis, filter fallback and exit). Live proof: bench run
meld14 — a complete Fire Dash (Combo Plus) + Thunder
Surge + Fleeting Crystal meld performed through the adapter
with screenshots matched against every spoken line, including
a spoken refusal on an action command in the Movement tab.
The proof iterations caught three bugs the host tests could
not: the u16 filter-flag read (run 8), the blink-word dialog
cursor (runs 8/11), and a spurious refusal at the execute
press (runs 12/13) — the meld execution itself raises the list
phase bit with the glove at 3 (OK), so list rises are now
gated on the glove sitting on a list row (0-2), and press
snapshots carry their list-session id. All are fixed above.

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
FIRST TABLE WIRED 2026-10-22: the Mirage Arena Hub (world
15 room 1). Its landmark set came from the room's own .olo
spawn data rather than a walk survey (games/kh-bbs/
hub-landmarks.md; the room proved to be two floors joined
only by a transporter), and the Guide query now speaks the
current floor's landmarks with distances and clock
bearings relative to the movement facing (CmdHubGuide;
12 host-test plants cover both floors, the z split, and
the fail-silent states). Other rooms still wait on their
tables.

GUIDE BEYOND THE HUB — SURVEYED 2026-10-24 (bench guide1;
survey checkpoint ~/khbbs-scan/guide-progress.md; per-room
notes in games/kh-bbs/guide-landmarks.md): the Guide now
speaks in eight more rooms — Olympus Coliseum's Vestibule
(8,2), Coliseum Gates (8,1) and Town Near Thebes (8,5), and
Radiant Garden's Entryway (6,2), Central Square (6,3),
Castle Town (6,5), Merlin's House (6,8), Fountain Court
(6,7, balcony only) and Outer Gardens (6,12) — through
CmdRoomGuide, a per-(world, room) table lookup behind the
same gating as the Hub (camp/Reports silent, both location
copies must agree, field + located position required) and
the same SpeakMarks line builder, so the spoken forms are
identical: distance only before the first movement leg,
clock bearings after. The Hub's own path and table are
untouched. 19 new host checks pin every table from stands
the survey stood on, two rooms' bearing math, and the
fail-closed rooms; each room was then proven live on the
bench (Guide at spawn + after a walked leg, against the
position logs).

Survey rules, the ones that made the Hub trustworthy: a
landmark's position is measured by walking to it (position
log at contact), never eyeballed from the layout data —
the .olo origins are candidates, and the table ships the
origin only once a walked stand, a screenshot, an
interaction prompt or a walked room flip proves the thing
is what the line names. Exits are landmarks named by the
room they lead to (kRoomNames' own names) and count only
when the flip through them was walked and the destination
read off the world/room words. Anything unreachable or
unverifiable is excluded and recorded, never tabled from
data alone. Notable findings, per room:

- Vestibule: five residents (save point, Moogle Shop, the
  large chest, Hercules, Phil) + the door back. Hades is
  in the story-pattern spawn data only — two live censuses
  of the finished save show he is NOT in the room, and the
  dark south alcove is decoration, not a door.
- Coliseum Gates: one reachable chest + both doors. Two
  more chests sit south of the terrace wall in a strip
  walking cannot reach from this room — excluded.
- Town Near Thebes: walked end to end; nothing but
  hittable pots and Unversed spawns, so its table is the
  single exit. The return flip was walked in the proof
  phase (the last unverified door).
- Entryway: only the save point and its two doors exist
  in the room. Its north door lands at the FAR end of the
  Outer Gardens (arrival stand (0,-119.24) against the
  (0,-123.25) trigger) — the gardens' near door, by the
  arrival terrace, flips to the Purification Facility.
- Central Square: two levels (entrance terrace, upper
  ring, center basin) spoken with ground-plane distances,
  like the Hub's floors. Both small chests walked (west
  ring; east chest via the south ring). The west door is
  SHUT on the finished save — the player was stood inside
  its trigger and pushed to the wall (x -33.4) with no
  flip — so it is not named; its destination was never
  read (presumably Fountain Court, unproven).
- Castle Town: the Moogle an earlier bench reported as
  gated IS reachable — the "gate" is its own shop counter;
  the Shop prompt arms at the counter's west end, 2.26
  from the Moogle's origin. Six marks incl. the small door
  to Merlin's House and the south door to Fountain Court.
- Merlin's House: Merlin (Talk prompt + screenshot), the
  small chest, and the west door back — its trigger was
  bisected live (safe stand x -5.87, flip walking to
  -6.40) and centered by the arrival-offset pattern
  (~3.7 in from the spawn stand) the other rooms show.
- Fountain Court: only the entrance balcony is tabled.
  It is fully railed — no drop, no stair — so the court's
  main floor (three chests in the layout data) is reached
  from a west door whose other side this lineage never
  reached. Balcony table = the one proven door.
- Outer Gardens: a 177-unit corridor; both chests walked
  (~1.0 contacts), both doors flipped. No save point
  exists in this room.

REMAINING UNSURVEYED: the Purification Facility (6,11) —
entered (arrival on its high terrace at (45, 9.41, -18);
the door back is its (45, 7.25, -22) trigger), a vertical
maze of ledges the survey did not finish; the Land of
Departure rooms — Mountain Path's exits are story-blocked
in the only reachable state (Ventus mid-training: walking
the south path fires his "little workout" line and the
path seals; both training-ring tools were contact-walked
but are not tabled without their exits), and post-story
the world leaves the map; Fountain Court's main floor;
Central Square's west door destination; and every other
world. The Guide fails closed in all of them.

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

## Camp menu cold-boot audit + RELOCATION fix (2026-10-22, khbbs-campgate)
Question (from the difficulty hunt): the difficulty object shifts
+0x20 whenever SYSTEM.DAT exists — do the camp cursors, mapped on
the fixture lineage only, shift the same way on cold boots?
Bench: khbbs-campgate (cold boot -> title -> Continue -> load ->
Hub field -> START camp -> full cursor walks + adapter speech,
on std = SYSTEM.DAT + saves and nosys = saves only).
- COLD BOOTS: NO SHIFT. On both lineages the first camp build of
  a session lands exactly on the fixture addresses (screen object
  0x099195F0, slot widget base 0x09925170), every cursor tracked
  its walk, and even the off-screen stale-garbage fingerprints
  are identical across lineages (slot word 2361 / inventory word
  19268 on the main list, slot count garbage 1280 / inventory
  4440 on the submenu, slot word garbage 2361 in the editor).
  The camp heap does not allocate the extra title-build block
  that moves the difficulty object.
- BUT the objects RELOCATE WITHIN a session (cg-p7/cg-p8 RAM
  dumps, exact-sequence hunts over DOWN/DOWN/UP walks): every
  dive/return cycle rebuilds the screen objects at fresh heap
  addresses. Main object 0x099195F0 -> 0x0992E020 -> 0x0992E0C0
  over two cycles; decks object -> 0x0991F4F0 in cycle 2; slot
  widget 0x09925170 -> 0x09922CF0 on the second visit (cg-p9).
  The fixed addresses read garbage from the second build on
  (main word 49024, decks word 2361, group word 65534) — so the
  pre-fix adapter would have gone quiet/wrong on any repeat camp
  visit in a real session.
- THE LOCATOR (DiffObj pattern): a per-session screen registry
  at 0x09919264 — address stable on fixture dumps and cold boots
  of both lineages. Slot 3 = main screen object, slot 6 = decks
  screen object, each zeroed while its screen is unbuilt. Both
  carry the standard identity (head 0xFFFFFFFF @ +0x00, class tag
  @ +0x0C: main 0x08BAD400, decks 0x08BA59D0 — invariant across
  13 dumps / 3 lineages). Fields: main cursor +0x66, count +0x6A;
  decks group focus +0xA4, decks cursor +0x126, slot-screen
  pointer +0x9C. The slot screen object (tag 0x08BA29E0) is 0 on
  the submenu and in the editor's group stage — it is written
  when the slot list first opens and stays live through the
  inventory; its +0x84 points at the slot widget: slot cursor
  +0x66, slot count +0x6A, filter tab +0x134, inventory cursor
  +0x138 (slot/inventory cursors 0xD2 apart in every sample).
  Chain proven by a same-address pointer-holder intersection
  across cg-p9 visits 1 and 2.
- CHURN BUG (found by the audit, fixed): on cold boots the
  decks-submenu word does NOT freeze under the slot list — opening
  the list rewrites it with heap churn (0, 1, 2360 across identical
  dives; frame trace cg-p4b: slot arrived f=7465, churn f=7469).
  A plausible churn value demoted CampTrack to the submenu and
  the adapter spoke "Meld Commands, 2 of 5" through the slot list
  and inventory (cg-std1/3/4). Fix: slot arrival outranks a
  same-frame sub move in the Editor branch, and the SlotList /
  Inventory branches ignore sub moves outright (no genuine path
  leaves either screen straight for the submenu; g_lastSub still
  syncs silently at the top of CampTrack).
- ADAPTER: all camp reads now go through CampMainObj /
  CampDecksObj / CampPaneObj / CampSlotWidget; an unresolved
  word reads as -1 (no signal / primes arrival); a tracked
  SlotList/Inventory whose widget stops resolving has returned
  to the editor (widget freed on back-out). CampTrack's Main
  gate is the registry object + count identity (the camp-active
  BSS witnesses are unchanged).
- HOST TESTS: 128 checks (was 108 at the difficulty fix):
  plants rebuilt as the real object graph at epoch-1 addresses,
  +5 delayed-churn plants, +8 relocation plants at the REAL
  cycle-2 addresses (0x0992E020 / 0x0991F4F0 / 0x09928310 /
  0x09922CF0) incl. a freed-widget return, +1 registry impostor
  (right head, decks tag in the main slot, plausible cursor and
  count — not followed). Full suite green; test-strength 129
  sites vs 97 floor.
- LIVE (locator build): cg-std6 + cg-nosys3 full flows (every
  camp screen speaks, both lineages), cg-p8b (main epochs 1-3
  and decks epochs 1-2 walks speak), cg-p9b + cg-p9nosys (slot
  list and inventory speak on visits 1 AND 2, both lineages;
  visit-2 WhereAmI "Edit Deck. Slot 1 of 8." where the fixed
  addresses read slot count 0). The inventory checkpoint
  WhereAmI still names the slot list until the inventory cursor
  first moves — pre-existing, by-design (the word holds 0 under
  the list, so row 0 is indistinguishable until it moves).

## Camp sub-screens WIRED + LIVE-VERIFIED (2026-10-24, khbbs-subcamp sub1-sub4)
Question (completion-list follow-on): the camp main menu's other
rows — Items, Abilities, D-Links, Stats, Reports, Config, Game
Help — spoke only the item's name generically (Reports spoke the
FIELD line; it kills the camp witnesses). What anchors each
screen, where is its cursor, and where do its rows live?
Bench: khbbs-subcamp (survey walks with RAM dumps + screenshots at
every stop on the 100% fixture save), sub2 (deep dives), sub3
(Config-only re-run), sub4 (live proof with the wired adapter).
- THE SHAPE (universal): registry slot 6 holds the CURRENT
  sub-screen object while one is open (0 on the main list). Each
  screen object carries the standard identity (head 0xFFFFFFFF
  @ +0x00, class tag @ +0x0C) with its own tag, and every screen's
  list cursor sits at +0x66 — inline on the object (Stats, Game
  Help, Reports) or on a child list widget the object points at
  (Items, Abilities, D-Links, Config; the widget has its own
  head/tag). Counts ride at the WIDGET's +0x6C, not +0x6A —
  Abilities 29, D-Links 8, Config 9 (General) / 6 (Multiplayer).
  The fixed lists (Items 2, Stats 3, Game Help 6) have no usable
  count word. Objects relocate per rebuild as before; every read
  follows the registry/pointer chain live.
- ITEMS (obj tag 0x08B946C0; category widget via obj+0x260, tag
  0x08B94790): two fixed rows — Equip, Stock (wraps). The deeper
  lists stay open (see below).
- ABILITIES (obj 0x08B8F720; widget via obj+0x7C, tag 0x08B8F7F0):
  rows are 12-byte records at widget+0x80 — def id @ +0 (family 9,
  names via the def table), group @ +2 low byte (0 Prizes,
  1 Stats, 2 Support; the high byte flags each group's first
  row), install capacity @ +4 (pips << 8), installed count @ +6.
  29 rows on the fixture, zero-id terminated; the +0x6C count
  agrees. Speech: "Treasure Magnet, 5 installed, 1 of 29".
- D-LINKS (obj 0x08BB2960; cursor widget via obj+0x220, tag
  0x08BB2A30): rows are 20-byte records at the OBJECT's +0x80,
  def id @ +0 (family 7 sub 14, the 375..383 id set). Fixture:
  Ventus, Aqua, Cinderella, Maleficent, Zack, Experiment 626,
  Peter Pan, Pete (screen order, not id order), zero-terminated;
  +0x6C count 8 agrees. A +0x0C record field reads 1 on all rows
  (maybe the ON flag — unverified, not spoken).
- STATS (obj 0x08BA8B00, cursor inline): three fixed rows —
  Basic Stats, Command Styles, Finish Commands. The right pane
  is display-only (no second cursor found).
- CONFIG (obj 0x08B92910; widget via obj+0x7C, tag 0x08B929E0):
  cursor wraps both ways. Two tabs (L/R, also flipped by a
  d-pad RIGHT/LEFT in the bench walk); NO stable tab word
  survives the switch (widget+0x300 is a one-frame transition
  flag) — the count word names the tab (9 = General, 6 =
  Multiplayer). General rows: Controls, Camera Up/Down, Camera
  Left/Right, Command Deck, Maps, Color Depth, CPU Speed,
  Install Data, Game Mode. Multiplayer rows: Nickname, Armor
  Color, Room Selection, Room, Record Sharing, D-Link Sharing.
  VALUES are NOT wired: an Install Data flip moved widget+0x170
  (0->1->0) but a Camera Up/Down flip left no clean word;
  values are the next hunt, along with the Multiplayer free
  values (nickname text, armor RGB percentages).
- GAME HELP (obj 0x08BB4CD8, cursor inline): six fixed topics —
  Command Deck, Action Commands, Dimension Links, Adaptive
  Combat, Mirage Arena, Sticker Album. The right pane lists the
  topic's articles but takes no focus (RIGHT does nothing);
  article text screens are a deeper, unprobed level.
- REPORTS — its own module, its own anchor. While Reports is
  open the camp witnesses are dead (witB 0) and the registry
  reads 0xFFFFFFFF-filled (the camp heap arena is released), so
  the pre-fix adapter fell through to the field line. Reports
  builds its screen object (head 0xFFFFFFFF, tag 0x08B8E2F0,
  cursor inline @ +0x66, count @ +0x6A = 9) in its own heap, and
  a STATIC cell at 0x0991C214 names it: 0 in the field, on the
  main list and inside every other camp screen; the object
  pointer inside Reports, stable across two opposite dive
  histories (Reports first vs fifth). Nine fixed categories:
  Story, Secret Reports, Game Records, Character Files, The
  Unversed, Ice Cream Guide, Command Collection, Treasures,
  Sticker Album. Open on an early save: whether locked
  categories hide (the adapter trusts the count word + fixed
  order) is unverified. Category contents are a deeper level.
- COMMAND BOARD is not a camp sub-screen: it leaves camp
  entirely to the board game's own title (New Game / Continue /
  How to Play); witnesses die and it stays "not mapped yet".
- ADAPTER: CampSubObj(tag) / CampSubWidget(obj, off, tag)
  locators + ReportsObj(); CampTrack names Reports when the
  witnesses are dead and each sub-screen by its slot-6 tag
  before the decks demotion; CampRowFor/CampWhere resolve rows
  live per screen and fall back to the item name on frames that
  do not resolve (the pre-hunt behaviour). Speech gates widened
  with ReportsUp() (WhereAmI, MenuNav, on-frame tracking, Hub
  Guide, foe/lock/self queries, the beacon cue feed) so the
  field stays quiet while Reports is up.
- HOST TESTS: 155 checks (was 140): +15 plants covering every
  screen's WhereAmI/nav lines, the Config tab swap, an
  unresolvable-widget fallback, Reports beating the stale title
  plant with dead witnesses, and the return to the main list.
  Test-strength floor for this file raised 141 -> 156 sites.
  Full adapter suite green.
- LIVE (sub4, std fixture): field line intact before camp; every
  screen above spoken with screenshot-matched rows while
  walking (see ~/khbbs-scan/subcamp/NOTES.md for the transcript).
  Not pushed.
- STILL OPEN from this hunt: the Items deep lists. Equip is a
  keyblade picker (equipped blade boxed on top, owned list
  below) whose cursor matched NO exact-sequence hunt — the
  selection is likely stored as an id/pointer, not an index.
  Stock is a two-column item grid: a cursor word exists
  (widget 0x0992D200+0x66 in sub2, tag 0x08B97648, stepping
  0,2,4,6 per DOWN; reached via Items obj+0x268 -> object tag
  0x08B97578 -> its +0x80), but item names are NOT in the def
  table, so contents need the game's separate item table.

## Low-health warning — WIRED + LIVE-VERIFIED (2026-10-24, khbbs-lowhp1 + khbbs-lowhp-live)

The adapter now says "Low health." once when the player's HP
falls into the danger zone. The trigger is the GAME'S OWN
criterion, proven three ways:

- Decomp: FUN_089327e8 — the per-actor HUD update the old
  CWCheat "Infinite HP" code led to (see PLAYER HP — CROWNED) —
  boosts the HP gauge's presentation byte exactly when
  `(cur * 100) / max < 26` (integer math on signed shorts) AND a
  second gate passes: FUN_089326a8(actor, 0x1bf), a status-byte
  lookup whose table (0x08B3492B, 0x10-byte entries, entry byte =
  slot) gives slot 23 for id 0x1bf, i.e. the byte at actor+0x39A.
  The boost writes base+4 to actor+0x5F1 and hands it to the HUD
  object at actor+0x58. (Decompiled 2026-10-24 for this item;
  log ~/khbbs-scan/decomp-326a8.log.)
- Bench khbbs-lowhp1 (battle30 fight state; Aqua, max HP 149,
  the gate byte reads 1 in normal play): cur HP pinned down a
  staircase — actor+0x5F1 read 49 at EVERY step at or above the
  line (149/121/75/47/40/39) and 53 (base+4) at EVERY step below
  it (38/30/20/10/5), flipping between cur 39 (26%) and cur 38
  (25%), exactly the decomp's boundary, and back to 49 on heal.
- The gauge itself: the HP arc around the portrait depletes
  with HP, and in the low state it pulses — a/b screenshot pairs
  9 frames apart at 3% HP differ in ~5,851 corner pixels vs
  ~500 of background motion at healthy HP.

The adapter computes the fraction with the game's own
arithmetic rather than reading +0x5F1: turning that byte into a
boolean would need its un-boosted base, which lives in a global
object the adapter does not locate. The game's second gate (the
actor+0x39A status byte) is not replicated either — it read 1
throughout normal play, and the fraction alone matched the
presentation at every probed step.

Latch design (adapter-side, on top of the game's line):
speak once on entering pct < 26; stay silent while low; re-arm
only at pct >= 31 (five points of hysteresis, so a heal landing
just over the line plus one small hit cannot toggle it). The
latch is keyed to (actor base, max HP), so a new boot, save, or
character re-arms it; it survives leaving and re-entering the
field (door transitions at low HP do not re-warn). The watch
runs only while the field actor is live (InField, the same gate
as field speech): title, difficulty, and cutscene flows never
reach it. The camp menu and Reports sit on the field with live
HP words, so the spoken line additionally requires both closed;
the latch still tracks under a menu (a potion used in camp
re-arms silently), and a drop first noticed under a menu latches
silently rather than firing late on menu exit. Death is the
game-over item's territory: ActorPlausible already rejects
cur HP 0, so the field gate drops at death, and a defensive
cur == 0 branch clears the latch without speaking. The line is
High priority — the latch and re-arm gap already make chatter
impossible, and High keeps a re-armed warning from being
swallowed by the consecutive-identical-line dedup.

Proof: 12 new host checks (188 total, floor raised 177 -> 189)
cover the exact boundary (52/200 = 26% silent, 51/200 = 25%
speaks), enter-once, deeper-damage silence, shallow-heal
hysteresis (30% does not re-arm), re-arm at 31% and a second
warning, death silence, post-death recovery, and camp-menu
silence with no late firing. Live run khbbs-lowhp-live (the
real adapter on the battle30 fight, HP pinned per frame):
silent at 121 (81%), one "Low health." at 38 (25%), silent at
15, silent through a heal to 44 (29%) and a second drop to 38,
then a second "Low health." after healing to 47 (31%) and
dropping to 38 again — exactly two speech lines in the whole
run, both the warning; screenshots match the HP at each step
(~/khbbs-scan/lowhp-live/).

Limits: live-probed on Aqua's lineage only (battle30/battleFE
family). The fraction is character-independent integer math on
the crowned HP words, and the decomp test is per-actor generic
code, so Terra/Ventus are expected to behave identically — but
their gauges were not separately probed. The alarm SOUND the
game plays in the low state was not isolated (the bench does
not record audio); the warning keys on the visual/code state,
not the beep.

## Game-over menu — MAPPED + WIRED + LIVE-VERIFIED (2026-10-24, bench go1/go2/go3/go-dbg + khbbs-menus-live)

Death in the field fades to a three-row menu over the fallen
character: **Continue / Retry / Load Game**. There is no
separate "GAME OVER" splash — the menu over the body on black
is the first interactive screen. Speech now: on arrival
"Game over. <row>, N of 3." (the opening row is read live — it
was Continue in this session's deaths, but an older capture
showed the highlight on Load Game, and this doc already
recorded that the opening position varies), nav echoes of the
landed row, and WhereAmI in the same arrival form.

Mapping (khbbs-go1..go3, battle30 fight state, Aqua vs Red Hot
Chili, cur HP pinned to 1): death at frame 318, deterministic
across four runs, with and without the adapter attached; the
menu is fully up at death +394 (frame 712). The rows live in a
small list object (at 0x099BC5A0 on this lineage):

    +0x00 u16 0xFFFF   +0x02 u16 0xFFF8   +0x04 u16 0
    +0x06 u16 CURSOR   +0x08 u16 0        +0x0A u16 COUNT (3)

The cursor word is the UNIQUE exact match over 24 MB of user
RAM for the whole D-pad walk (DOWN x3, UP x3: 0,1,2,0,2,1,0 —
it wraps both ways), and the six-word signature occurs exactly
once in every menu dump and nowhere in fight, death,
transition, hub, title, difficulty or camp dumps. The object
sits inside the battle scene object (pointers to the scene
object at +0 relative to it were chased: struct = scene +0x130),
so its address can float per lineage: the adapter locates it by
signature scan, unique hit only.

The gate is a conjunction, because the object is NOT freed
when the menu closes — the signature still reads perfectly on
the world map after Continue and under the Load screen:

- the corpse witness: the player actor still reads dead (cur
  HP 0, state word 15) at the last base where it verified
  alive. InField() keeps that base current (g_lastActorBase);
  the witness re-reads it live every frame instead of latching
  "he died" on the frame the field gate drops — the first
  live run missed the menu outright on that single-frame race
  (the HP and state words do not necessarily land together).
  After Continue the struct is freed (state reads 0); after
  Retry the same base is alive at full HP.
- the overlay fields: two fields of the same object, at +0x14
  and +0x16, read 0 in all seven menu dumps (five boots) and
  non-zero whenever another screen owns the display: 14 and 2
  under the Load screen, 5 and 2 on the world map. (A third
  word corroborates: +0x12 reads 1279 in every menu dump,
  4607 under the Load screen, 5120 on the world map.)

The death fade stays silent: the object is not built yet, so
the scan finds nothing.

FALSIFIED WITNESS, do not re-crown: three words at
0x099FB41A4/B4/214 read 16 in every game-over menu dump of the
go1/go3 sessions and 17 in their fight/world-map/load dumps —
a perfect-looking module-state gate. khbbs-go-dbg (same
lineage, same menu, live session instrumented at menu time)
read 0 at all three. They were lineage-session junk that
happened to correlate across the dump sessions; the in-object
overlay fields above are the witnesses that survived a live
session.

Outcomes, each driven live (go2, go3, go-dbg):
- Retry: the fight restarts in place at full HP with the SAME
  actor base (state 28 respawn transition, then state 1
  fighting; the signature is gone — the area is rebuilt).
- Continue: abandons to the world map (world ids 06 -> 17),
  the glider parked inside Radiant Garden's ring, the actor
  struct freed.
- Load Game: the game's own Load screen (slot list, Cross =
  Enter, Circle = Back) opens OVER the still-drawn game-over
  menu; the corpse and the signature both persist under it,
  which is what the overlay fields exist to separate. Speech
  for the Load screen itself is separate work (not wired).

The arrival edge is debounced: the first working live run
proved the gate drops for a frame or two during a cursor move
(the arrival line re-fired mid-menu, ahead of the nav echo),
so a rise speaks only after the gate was down for 30+ frames;
a Load-screen round trip (hundreds of frames) still
re-announces on return, which is also how backing out of the
Load screen behaves.

Proof: 15 new host checks (game-over share of the 214 total,
floor 215 sites) cover arrival, no-repeat, the flicker
knockout, nav echoes, WhereAmI, ready, and one knockout per
witness (no death, signature absent mid-fade, Load-screen
overlay values, freed actor, wrong count). Live run
khbbs-menus-live mode=go (real adapter on the fight state):
one "Low health." at frame 121 (the pin held her at 1 HP
alive — the warning working as designed), death at 318,
silence through the fade, "Game over. Continue, 1 of 3." at
frame 712 (the exact frame the menu completed), WhereAmI in
kind, single echoes "Retry, 2 of 3." / "Load Game, 3 of 3." /
"Retry, 2 of 3." matched to screenshots, then Retry restarted
the fight (141/149 after 600 frames of combat) with no
further speech (~/khbbs-scan/live-go/).

Limits: mapped and proven on the Final Episode lineage
(Aqua, Radiant Garden field fight) only. Deaths in other
contexts (boss/story fights, the tutorial, other worlds) were
not driven; the gate fails closed wherever its witnesses do
not verify, so an unproven lineage gets silence, not wrong
speech. If the adapter is attached only after the actor is
already dead (never seen alive), the corpse witness has no
base and the menu stays silent for that death. Load Game's
deeper flow (choosing a slot, its confirmations) is unmapped,
as is the Load screen's own speech.

## Movie pause menu — MAPPED + WIRED + LIVE-VERIFIED (2026-10-24, bench mp1/mp3/mp4 + khbbs-menus-live)

START during an in-game scene pauses it: a "PAUSE" menu with
**Resume / Skip Scene**, opening on Resume, wrapping both
ways. Speech now: on arrival "Paused. <row>, N of 2.", nav
echoes, and WhereAmI in the same form.

First, a negative: the OPENING MOVIE (cold boot, before the
title) has no pause menu at all — mp1 proved START skips it
outright (flow phase 2 -> 3, title fade). The pause menu
exists only on in-game scenes. The mapped instance is the
Final Episode's Radiant Garden story scene (Terranort),
reached by loading the FE save, visiting from the world map,
and pausing once the scene plays; the prologue scene's menu
is the same one the New Game flow notes describe (the doc's
earlier START -> DOWN -> Cross skip), but the prologue itself
was not re-driven for this wiring.

The menu's words are statics in the menu-system area:

    cursor u16 @ 0x09918D76    count u16 @ 0x09918D7A (2)
    word   u16 @ 0x09918D82    word  u16 @ 0x09918D84

The cursor word is the unique exact match for the pause walk
(0,1,0,1,1 across arrival, DOWN, DOWN-wrap, UP-wrap, idle).
The count word alone is NOT the gate: it reads 2 while the
scene plays unpaused once the object exists, and keeps
reading 2 after Resume. The two state words carry the gate —
1024 and 255 in every paused dump; 5120-5121 and 5 in the
scene playing, the visit dialog, the world map, the fight,
the game-over menu, and after resuming. Other lineages read
unrelated junk at all four words (camp, title, hub,
difficulty dumps), which fails the conjunction and stays
silent; the stale game-over signature can also be sitting in
RAM during a scene (same session, earlier death) without
interfering — the two gates share no witness.

Outcomes (mp4, deterministic single choices read off the
live cursor word — an earlier blind two-UPs "resume" in mp3
actually wrapped the 2-row menu back to Skip Scene, which is
why these runs assert the cursor from RAM before confirming):
- Resume: the menu closes (state words back to 5120/5) and
  the scene plays on — proven by pausing AGAIN: a second
  START re-opened the same menu over a later scene frame.
- Skip Scene: the scene ends at once, no confirmation
  sub-dialog (gate words already closed 45 frames after the
  confirm), into field control in Central Square at full HP,
  with the fight the scene was introducing starting
  (INFORMATION banner, Terranort across the square; the
  field actor is back at 0x09980B10, state 28 -> fight).

Proof: 11 new host checks (movie-pause share of the 214
total) cover arrival, no-repeat, nav echoes, WhereAmI, ready,
the unpaused-scene look-alike (count lingering at 2 with the
playing state words), and one knockout per witness (missing
count, missing 1024 word, cursor out of range). Live run
khbbs-menus-live mode=mp (real adapter, FE save parked at
Radiant Garden): silence through the scene, "Paused. Resume,
1 of 2." on START, WhereAmI in kind, echoes "Skip Scene, 2 of
2." / "Resume, 1 of 2.", Resume, a second "Paused. Resume, 1
of 2." on the re-pause, then Skip Scene landed in the plaza
fight (screenshot: boss battle under way) — 6 pause lines,
all matched to screenshots (~/khbbs-scan/live-mp/). The run
also recorded one unrelated "Low health." as the visit
dialog opened — see the side-finding below.

Limits: proven on the Final Episode story scene only;
pausing other scenes (prologue, mid-world story scenes,
other episodes) uses the same menu per the game's design
and the New Game flow notes, but was not re-driven here.
The opening movie has no pause (START skips it), so nothing
speaks there by design.

Side-finding (pre-existing, observed 2026-10-24, NOT changed
by this work): on the Final Episode world map the adapter
locates a plausible field actor — Aqua at the save's 1 HP —
so the low-health warning fires once there (mp run, frame
68, as the visit dialog opened). The warning is right about
her HP; the field gate is simply loose on the world map (the
glider-side actor passes the field fingerprints). Recorded
here so a future world-map pass can decide whether the
warning should be field-only in fact as well as in name.

## World map — MAPPED + WIRED + LIVE-VERIFIED (2026-10-24, bench wm1 + khbbs-wm-live); the movie-pause side-finding CLOSED

The world map is world 0x11, room 1 in the map-id space: a
star map the player flies with the analog stick (the
character's Keyblade glider). The D-pad does nothing on the
map itself. Flying into a world's ring selects it — the
banner shows the world's name, COMBAT LV, TREASURES % (and
a NEW marker) — Triangle opens the visit dialog there, and
START opens the map's own pause menu. Reached from a save
point's World Map row, from game-over Continue, and when
finishing or leaving worlds.

Speech now: on arrival "World map." (plus the selected
world's name when arriving parked in a ring: "World map.
Radiant Garden."), selection changes speak the world's name
alone ("Radiant Garden."), WhereAmI gives the same map form,
the visit dialog speaks its arrival ("Visit Radiant Garden?
Leave, 2 of 2.") and nav echoes, and the map's pause speaks
"Paused. <row>, N of 3." with echoes and WhereAmI in kind.

Witnesses, proven on two lineages (a Final Episode save
parked at Radiant Garden, mp-park.ppst; the 100% fixture's
Terra parked at Mirage Arena, battle19/end.ppst), driven by
hand in bench wm1 with a screenshot and full-RAM dump at
every step:

- **The session witness**: BOTH map-id copies
  (0x08B61CA0 / 0x08B759C0) hold world 17, room 1 for the
  WHOLE map session — parked, in flight (sel 0), with the
  visit dialog open, with the pause open, in transit. That
  conjunction is OnWorldMap(), and it is also the field
  gate's new exclusion (below).
- **Selected world**: u32 @ 0x099658C0, low byte the world
  id (upper bytes read 0 in every dump) — 6 parked at
  Radiant Garden, 15 at Mirage Arena, 0 in open space.
  Reconfirms the word the 2026-10-18 hunts first read.
  Flight facts: one 30-frame full-stick burst exits a ring;
  after a burst the glider COASTS straight (the odometer
  kept growing ~1/frame over 2400 idle frames), so leaving
  is easy and re-entry needs reverse bursts with per-burst
  sel polling. The world does NOT pause under the visit
  dialog (odometer grows) but DOES freeze under the pause.
- **The visit dialog**: rows **Visit This World / Leave**,
  cursor STARTS on Leave, D-pad wraps, Circle backs out
  (dialog closes, still parked in the ring), choosing Leave
  closes it, choosing Visit This World leaves the map for
  that world (FE: into the Terranort scene; fixture: into
  the Mirage Arena Hub). The dialog's list object RELOCATES
  per open and persists after close — the game-over object's
  family. First FE open at 0x0999FD90, second at 0x0999FD10;
  fixture at 0x099A11F0 then 0x099A1170. Locator (the
  game-over method): unique signature scan for u16s
  [+0]=0xE7FF, [+2]=0xFFF8, [+4]=0, [+6]=cursor<2,
  [+8]=prev<2, [+0xA]=count==2 — exactly one hit in every
  dialog dump, zero hits in park dumps before the first
  open. ACTIVE witness: bit 28 of the u32 @ obj+0x10 is
  clear only while the dialog is open (u16 @ +0x12 reads
  0x04 open, 0x14 closed; nine dumps, both lineages). Row:
  cursor @ obj+6. False-positive check: a stale count-2
  list object in battle37 field dumps (0x099193F0) reads
  head 0xFDFF and its active bit set — excluded twice over.
  One live wart, fixed: the gate drops for a few frames
  during a cursor move, so the arrival edge is debounced
  (speaks only after 30+ frames down), the game-over
  pattern — khbbs-wm-live's first run re-fired the arrival
  line ahead of both nav echoes; the second run is clean.
- **The map's pause menu**: **Resume / Open Menu / Save**,
  opens on Resume, wraps both ways — on the SAME menu-system
  statics as the movie pause (cursor u16 @ 0x09918D76,
  count u16 @ 0x09918D7A == 3, state words 1024 @
  0x09918D82 and 255 @ 0x09918D84 while open; closed reads
  5120/5 with the count lingering at 3). An old battle19
  dump's w82=1060 reading was superseded: controlled dumps
  on both lineages read 1024. The movie-pause gate requires
  count 2, so the two never cross-fire. Choosing Open Menu
  opens the camp menu (camp's own speech owns the screen;
  the InField exclusion does not disturb it — camp is
  checked before the field everywhere). Choosing Save opens
  the save screen (its speech is the save/load item's job).

**The field-gate fix (the side-finding above, closed).**
InField() now returns false when both map-id copies read
17/1, ahead of the actor cache and the sweep. On the map
this keeps the glider-side actor from passing the field
fingerprints, so: the low-HP warning stays silent there,
WhereAmI/foe/lock queries no longer produce the field form,
the beacon feed refuses, and g_lastActorBase is no longer
clobbered on the map — the game-over gate's corpse witness
now survives a Continue (death -> map -> visit) untouched.

**Proof.** 28 new host checks (242 total, floor 243):
arrival in open space and parked on a world, no-repeat,
selection changes with a 6-frame confirmation (a two-frame
graze stays silent), leaving a ring stays silent, WhereAmI
forms, **the low-HP pair — a plausible actor at 1 HP is
silent on the map and warns in a real field with the same
plant**, the visit dialog (arrival on Leave, echoes,
WhereAmI, the closed-lingering object, the 0xFDFF
look-alike, relocation adoption, the three-frame flicker
debounce), the map pause (arrival, echoes, WhereAmI,
closed-with-lingering-count, and the same statics off the
map staying silent). Live run khbbs-wm-live mode=wm (real
adapter, FE save parked at Radiant Garden at 1 HP): 11
speech lines, all matched to screenshots
(~/khbbs-scan/wmlive-wm2/) — arrival, WhereAmI, dialog
arrival and both echoes, Circle out in silence, pause
arrival and both echoes, paused WhereAmI, Resume, fly out
in silence, fly back in to a spoken "Radiant Garden.", and
**zero "Low health." lines in the whole run** — the exact
spot the old code fired it once (mp run, frame 68). Mode=hp
(the battle30 field fight) re-proves the other side against
the new build: pin to 38 (25%) speaks it once, deeper
damage stays silent, heal and re-pin speaks it a second
time — 2 lines, as designed.

**Limits.**
- **No "N of M" for selections.** The selectable set is
  campaign state and no live witness for it could be
  crowned: heap stride scans found no materialized node
  table in any map dump; a differential of the two lineages'
  runtime save blocks (0x09F00000-0x09F30000, complete vs
  fresh) was far too noisy to crown a flag array (candidates
  near 0x09F252CC-0x09F25464 are other progress data,
  unproven); the loaded map module's image (KEYBLADE_RIDE,
  single PT_LOAD at 0x08B8BF10) held one clean stride-8
  world-id sequence [1,2,3,4,5,6,8,9,10,11,12,13] at
  0x08B94890 that is a RED HERRING — (id, string-pointer)
  pairs for a trigger/trophy table (adjacent ASCII
  "tr_wi_KG:0", "tr_wi_LE:0", "trofy_00"); and a
  node-position scan (world id followed by plausible floats
  at strides 16-96) over RAM and the module found nothing.
  Membership questions also stay open (Land of Departure
  and Command Board as map nodes are unverified). Selection
  speech is therefore the world's name alone — read live
  from the selection word — rather than a position from a
  hardcoded list. A future pass could try a time-boxed
  Ghidra hunt of the map module for the node-visibility
  test.
- The COMBAT LV / TREASURES % banner values are not spoken
  (never hunted).
- Proven on the Final Episode and fixture-Terra lineages;
  mid-campaign saves with fewer worlds unlocked were not
  driven (their selection speech reads the same live word,
  so names will be right; nothing here depends on the
  unlocked count).
- Choosing Visit This World was resolved live in earlier
  sessions (both lineages) rather than in the wm-live tape,
  which backs out with Circle to keep the run on the map;
  the tape's dialog walk covers open, both rows, and the
  back-out.

## Save / load screens — MAPPED + WIRED + LIVE-VERIFIED (2026-10-24, bench sl1-sl9)

The Save and Load screens are NOT game UI. They are the PSP
firmware's savedata utility dialog (sceUtilitySavedata in its
LIST modes — LISTLOAD 4, LISTSAVE 5), rendered host-side by the
emulator. Entered from four places: the save point's menu and
the world map's Start pause (Save); the title's Continue and
the game-over menu's Load Game row (Load, over the menu — the
game-over section's overlay fields, 14/2 under it, are what
keep that menu's gate down while the dialog owns the screen).
Everything below follows from the dialog being the firmware's:

- **The live selection is a host-side variable.** An 18-dump
  walk of the open dialog (idle-pair controls, exact-sequence
  matching, equal-iff-same-index pattern matching over the
  full 32 MB) found ZERO guest words that move with the
  selection; dumps with different selections differ only in
  the animated field behind the dialog plus one timer byte.
  PPSSPP's source agrees (`currentSelectedSave` is a C++
  member of PSPSaveDialog). The cursor is therefore
  ECHO-TRACKED — the meld confirm dialog's precedent — seeded
  at arrival (below) and moved by MenuNext/MenuPrev echoes.
  The dialog's list CLAMPS at both ends, never wraps
  (PSPSaveDialog::DisplaySaveList), and the tracker clamps
  identically, so echo counting cannot drift.
- **A slot's stored details never enter guest RAM.** The
  dialog reads each slot's PARAM.SFO from the memory stick,
  host-side (character, location, "LV 99 999999 munny",
  "Play Time 76:03", date, size — all screenshot-captured).
  The one SFO block in RAM (wide strings at 0x09FFEB60:
  title / subtitle / detail) is the CURRENT session's save
  template — what a new save would write — identical at every
  selection (its Play Time runs one minute ahead of the
  stored slot's). It must never be spoken as a slot's
  contents. SAVEDATA.DAT files are encrypted on the stick and
  their plaintext for OTHER slots is nowhere in RAM; the
  game's own dir enumeration does not exist (no dirent
  buffers). So rows speak the slot's NUMBER and position
  only, and occupied-vs-empty is unknowable — which forces
  the overwrite-confirm design below.
- **The sub-states have no RAM signature either.** Confirm
  dumps differ from list and done dumps in no stable word;
  the "Saving" / "Save completed" strings appear nowhere in
  RAM while on screen. Sub-state is echo-tracked like the
  cursor.

### The gate

Two witnesses, each covering the other's failure mode:

- Work-area words u32 @ 0x09FFF5C4 / 0x09FFF5C8 (the utility
  dialog's fixed arena at the top of RAM): the same pointer
  values in all 5 open samples (both modes, three lineages,
  four routes), 0 in all 6 closed samples (field, save-point
  menu, title, camp, world map, title after backing out).
  They LINGER at open values briefly after a load completes
  (one capture has the field already resumed) — a pre-check,
  never the proof.
- The game's SceUtilitySavedataParam block, by signature:
  size u32 == 0x600, gameName "ULUS10505" @ +0x3C, fileName
  "SAVEDATA.DAT" @ +0x64, mode 4/5 @ +0x30. Exactly one hit
  in every open dump, none in any no-dialog dump — but the
  block LINGERS, fully formed, after a LOAD session closes
  (its result word @ +0x1C then reads 1 for cancel, 0 for
  success; after SAVE sessions the block is gone). Arena AND
  param together are the gate; the block's address is found
  by scan on the rise and cached (it relocates per lineage:
  0x09943ED0 / 0x0994A220 / 0x099766A0 / 0x09941420 observed).

From the block: mode (+0x30), the slot-name list pointer
(+0x60; 20-byte names counted until an empty entry — 200
names, "0000".."0199", so positions speak as "Slot N of
200"), the focus request (+0x5C8) and focus name (+0x4C).

### Arrival focus (PPSSPP InitStart, replicated where possible)

- FOCUS_NAME (both save routes): the index of the focus
  name in the list, else 0. Hub session: name empty → slot
  1. World-map Final Episode session: name "0003" → slot 4,
  announced live as "Slot 4 of 200, current save."
- FOCUS_FIRSTLIST → 0; FOCUS_LASTLIST → last. (Not used by
  this game; implemented from the same source.)
- FOCUS_LATEST (both load routes): the most recently
  modified save — a memory-stick fact the guest never sees.
  The tracker opens UNSEEDED: arrival is "Load." alone, nav
  echoes narrow a possible range with the same clamp, and
  rows speak only once the range collapses to one slot.
  Nothing is announced from an unproven position.

### The flows, frame-verified

- Save, empty slot: Cross goes straight to the write;
  "Saving. Please Wait..." shows for only a few frames
  (the write of ~72 KB is fast), then "Save completed",
  dismissed with Circle only. Lands back on the menu the
  dialog came from (save-point menu; map pause — still open,
  cursor still on Save).
- Save, occupied slot: Cross asks "Do you want to overwrite
  the data?" with NO focused (the safe default).
  Left/Right move (Left at No → Yes; Right at Yes → No);
  Cross answers; Circle or No returns to the LIST (the
  dialog does not exit). Yes runs the write as above.
- Load: Cross on a slot starts at once ("Loading. Please
  Wait..." ~120 frames on the slow lineage), then "Load
  completed", dismissed with Circle OR Cross. The game
  then loads: from the title, gameplay resumes at the
  loaded save's position (Terra's Hub save → standing on
  the Hub save point, (0, -43), full health); from
  game-over, the same — the fight is abandoned for the
  loaded save's field. (Cross-dismiss is bench-proven at
  every "Load completed"; Circle also dismisses it per
  the dialog source but was not separately driven.)
- Load, empty slot (source-derived, not bench-driven):
  the load fails ("Load failed — The data is corrupted.")
  and Circle there returns to the list.
- Circle in either list cancels the dialog (result 1) back
  to the calling screen; from game-over's Load, the menu
  underneath re-announces on return (its own debounce).

### Real saves and loads, driven (the bench firsts)

Backup before touching anything:
~/khbbs-scan/savedata-backup-20261024-saveload/ (full
SAVEDATA tree + SHA256SUMS.txt, byte-verified against the
stick). The savestate-restored lineage writes fine here
(the MK bench's no-write trap does not apply to this game):

- sl5 (Hub save point): saved into the first empty slot —
  new directory ULUS105050004 appeared (ICON0/PARAM.SFO/
  PIC1/SAVEDATA.DAT); its PARAM.SFO detail reads Terra /
  Mirage Arena Hub / LV 99 / 999999 munny / Play Time 76:04,
  the session's own data. ULUS10505/SYSTEM.DAT also updated
  (the game's own bookkeeping).
- sl6 (Hub): overwrote slot 1 through the confirm —
  ULUS105050000/SAVEDATA.DAT sha256 a9645d26… → 24620114….
  A second adapter-driven overwrite in sl9 changed it
  again (→ 249b60e2…), and sl8b then LOADED that very file
  back from a cold boot and resumed from it — the
  write/read pair is closed.
- sl8 (world map pause): saved the Final Episode session
  into empty slot 6 — ULUS105050005 created (158 KB,
  2026/10/24 10:25).
- sl8b (cold boot, no savestate in the lineage): title →
  Continue → load → field. sl7: death → game-over → Load
  Game → load → field. Both completed end to end.

### What's spoken (khbbs-sl7/sl8/sl8b/sl9 speech tapes)

- Arrival: "Save." / "Load.", plus the seeded row when the
  seed is provable ("Save. Slot 4 of 200, current save.").
- Rows on nav echoes (change-only; clamped presses silent):
  "Slot N of 200" (+ ", current save" on the session's own
  named file in Save).
- Overwrite confirm: "Overwrite this save? Yes." / "No."
  as the choice moves; WhereAmI: "Save screen. Overwrite
  this save? No." A first Cross in the save list announces
  nothing — occupancy is unknowable, so the tracker waits:
  a following Left proves the confirm (and speaks it); a
  following Cross with Yes chosen speaks "Saving.", and
  the dialog closing after a certain write speaks "Saved."
  The indistinguishable pair — Cross, Cross (the No answer
  returning to the list vs an empty slot's write starting)
  — stays silent by design rather than guess, and the
  screens around the dialog name where the player lands.
- Load: "Loading." on the slot Cross; completion is the
  loaded field's own speech, not a line of this screen's.
- WhereAmI: "Save screen." / "Load screen." + row or
  sub-state line. The branch precedes camp in both command
  handlers — the camp witnesses are SET under this dialog
  from the save point, and WhereAmI used to answer "Camp
  menu." there; CampTrack is likewise skipped while the
  dialog is up.

Two tracker disciplines from the sl7 tape: (1) the press
that OPENS the dialog can echo after the gate has risen —
its game-over Cross both opened Load and, echoed late,
started a phantom "Loading." — so echoes are ignored for
the first 8 frames after seeding; (2) a dialog that closes
and reopens inside the 30-frame arrival debounce re-seeds
its tracker silently (only the announcement debounces).
The dismiss timeout that separates "DONE screen waiting"
from "failed transfer returned to the list" is 300 frames:
sl7's real load took ~120, and a 90-frame timeout fired
mid-load on the first tape.

Side-fix on this flow's exit: the world map pause's word
at 0x09918D84 reads 255 at a fresh open but settles at 4
after the pause has hosted this dialog (sl4 cancel return
and sl8 completed-save return alike; cursor, count and
word 82 stay live). MapPauseUp now accepts both values —
before the fix, WhereAmI on the still-open pause after a
save answered "World map." The movie pause keeps the
strict 255: no dialog opens over it.

### Limits

- Slot contents (character / location / level / munny /
  play time) and occupied-vs-empty are not spoken — they
  exist only host-side (proof above). The one exception is
  the "current save" suffix, from the param's own focus
  name.
- The Load screen's opening slot is unprovable
  (FOCUS_LATEST); rows begin once the position is certain.
  A player who loads the focused save without navigating
  hears "Load." and "Loading." only.
- A direct Cross, Cross save (empty slot, no Left) gets
  no "Saved." line — indistinguishable from answering No.
  The write itself is unaffected.
- The empty-slot load failure is source-derived
  (PSPSaveDialog DS_LOAD_FAILED), not bench-driven: the
  tracker's return-to-list path for it is host-tested.
- The gate assumes the arena words are zero whenever no
  utility dialog is up; a different utility dialog (the
  boot INFORMATION dialog is a message dialog) opening
  while a stale savedata param block lingers could in
  principle false-open it — no such sequence exists in the
  mapped flows (the boot dialog precedes any savedata
  param), and the mode/name signature still has to match.
- Proven on the fixture-Terra and Final Episode lineages;
  the dialog is firmware, so other saves' sessions differ
  only in the param's focus inputs, which are read live.

## Cleared-save title + Trinity Archives — FIXED, MAPPED + WIRED + LIVE-VERIFIED (2026-10-24, bench arch1-arch8 + khbbs-arch6)

### The cleared title's actual shape

On a save with clear data (the darkstar-x97 fixture; the Final
Episode save behaves the same) the title menu has FOUR rows:
NEW GAME / CONTINUE / **TRINITY ARCHIVES** / INSTALL GAME.
The count word reads 4 and the existing table already named
the extra row, so at boot the old adapter spoke the cleared
title correctly (arch6: "Title menu. New Game, 1 of 4.", full
walk with wrap, screenshots matched). The standard title is
unchanged: 3 rows on a fresh stick (arch6 fresh mode: the
auto-save notice, then "No system data found... Proceed?"
with the cursor starting on No — LEFT, CROSS — then the
3-row title speaks and walks exactly as before).

### ⛔ THE PHASE WORD IS A FLOW COUNTER, NOT A SCREEN ID

The real defect was behind the Archives door. The title
gate's phase word (u32 @ 0x09920CD0) does not identify the
title screen — it COUNTS scene changes inside the title
module (frame-logged across two full round trips, arch4/
arch5/arch7):

  3 = title from boot → 4 = Trinity Archives → 5 = title
  again → 6 = Archives again → 7 = title → ...

Odd phases are title instances; even phases >= 4 are
Archives instances. The difficulty select consumes NO phase
step (its round trip leaves the phase untouched). The old
gate (phase == 3) therefore spoke the title at boot and then
went silent FOREVER after a single Archives visit — every
later title instance lives at phase 5, 7, 9, ...

The gate now accepts any odd phase >= 3, and pays for the
wider net with a no-world-loaded guard: both map-id bytes
(u8 @ 0x08B61CA0 and u8 @ 0x08B759C0) must read the 0xFF
no-map sentinel, as they do at every real title. That guard
is what keeps the stale-but-valid title words that linger
under a loaded field (proven to read cursor 1 / count 4 /
submenu 0 / step 4 in the Hub) from ever speaking as a
title: with a world loaded the title gate is closed, full
stop. Gate proofs, both variants: host plants for phases
3/5/7 speaking, phase 4 (title words planted) silent,
phase 5 with a world loaded silent; live: arch6's returned
title at phase 5 spoke "Title menu. New Game, 1 of 4." and
opened the difficulty select (which read the shifted
system-data object at 0x09922ED0 and spoke the live cursor,
Standard Mode) and backed out to a speaking title again.

One timing fact for drivers: after an Archives exit the
title's fade-in step outlasts a fixed 400-frame wait (step
read 3; the poll needed 530 frames in arch6). Poll the gate
words; do not fixed-wait.

### The Trinity Archives, mapped

Top level (widget base 0x099214D0, stable across every cold
boot driven): header u16 @ +0x00 = 0xDFFF, @ +0x02 = 0xFFF8,
cursor u16 @ 0x099214D6, count u16 @ 0x099214DA == 5. Five
rows, wrapping, each with a description line on screen:

  Timeline ....... "See how the story connects together."
  Theater ........ "Rewatch past cutscenes."
  Game Stats ..... "See how far along you are in the game."
  Trophies ....... "Browse the different trophies you have
                   earned."
  Secret Reports . "Read the secret writings you have
                   uncovered."
  (plus a COMPLETE 100% banner on the fixture)

The child witness u32 @ 0x099214E4 is TWO fields, not a
boolean (arch7 frame log): the HIGH half is the sub-state —
0x0002 while any child is open (held ~50 frames while a
child closes), 0x0006 for exactly two frames when the
top-level cursor moves; the LOW half reads 0x0008 at the top
after any child visit (0x0000 only before the first). "A
child is open" is the high-half test. The first wiring's
exact equalities (== 0 at the top, == 0x00020008 in a child)
failed live both ways: silent after one child round trip,
and a two-frame gate drop on every top-level cursor move
that reset the arrival watcher and double-spoke every row.
The top cursor stays parked on the open child's row, which
is how the child gates know which child they are looking
at.

What each row opens (all five driven, screenshots matched):
- Timeline: a story-segment chart (three character lanes,
  world-medallion nodes, "Select a story segment.") opening
  prose text pages (scroll, L/R pages, Triangle photo). Not
  a list — NOT wired.
- Theater: a plain list of six stories — Terra / Ventus /
  Aqua / Final Episode / Where the Heart Goes / Blank
  Points ("Select a cutscene."). WIRED. It shares the child
  list widget at base 0x09927640 (header word @ +0x02 =
  0xFFF8) with controller count u16 @ 0x09927704 == 6 and
  cursor u16 @ 0x09927708. Selecting a story opens that
  story's cutscene list — a deeper level, NOT wired
  (playback pause is already covered by the movie-pause
  work).
- Game Stats: a static page (Combat / Mini-games / Story /
  Item Collection percentages, total play time — 183:22:54
  on the fixture). No cursor — NOT wired.
- Trophies: an icon grid, glove cursor, no text labels at
  this level — NOT wired.
- Secret Reports: a text list. WIRED. Controller cluster
  (arch7/arch8): cursor u16 @ 0x09927C18, previous @ +0x02
  trailing exactly (12 at the wrap step), count u16 @
  0x09927C20 read live, visible-window word u16 @
  0x09927C22 == 10. ⛔ The arch5 addresses for this screen
  (shared-widget cursor 0x09927646, controller 0x099276C8,
  "last index" 0x099276D0) were a stale-widget misread —
  arch6 proved those words junk while Reports was open.
  The real list on the fixture has **13 rows: Xehanort's
  Letter + Reports 1..12** — walked one slow tap at a time
  with screenshots: Report 12 is the bottom row and the
  next DOWN wraps to the Letter. The fixture simply holds
  no Report 13; the count is read live and rows are named
  by index (0 = Letter, i = Report i), so a fuller
  collection speaks "Xehanort's Report 13, 14 of 14" by
  construction. A sibling widget struct at 0x09927B96
  mirrors the within-window cursor (pins at 9 while the
  window scrolls) with a constant 14 at 0x09927B8C — noted,
  not used. Bench-driving note: this list drops 4-frame
  presses at ~49-frame intervals outright (arch6's first
  walk never moved); 8-frame presses with long settles are
  the reliable recipe (arch7/arch8).

### What's spoken

- Title, both variants, at every visit: "Title menu.
  <row>, N of 4." (or "of 3"), nav echoes, wrap included.
- Archives top level: arrival "Trinity Archives. <row>,
  N of 5." after a 30-frame stable gate (children checked
  first in the watcher, the same placement rule as the
  other menus), WhereAmI "Trinity Archives. <row>, N of
  5.", nav echoes; backing out of a child re-announces the
  top level on its parked row.
- Theater: arrival "Theater. <story>, N of 6.", WhereAmI
  "Trinity Archives. Theater. <story>, N of 6.", nav
  echoes with wrap.
- Secret Reports: arrival "Secret Reports. <row>, N of
  M.", WhereAmI "Trinity Archives. Secret Reports. <row>,
  N of M.", nav echoes with wrap — N/M read live.
- Backing out to the title re-speaks the title at its new
  phase (the headline fix).

Host tests: 304 checks (22 new: the phase-5/7 titles, the
parity and loaded-map negatives, the top level at phases 4
and 6, the witness-residue and move-pulse plants, the
unmapped-child negative, Theater and Reports plants with
wrong-count negatives, the split fingerprint-twins check);
baseline floor 305 sites.

### Limits

- Fixed widget/controller addresses were proven on the
  fixture profile across many cold boots; a different stick
  profile was not driven into the Archives (the title-side
  relocation precedent — the difficulty object moves on a
  system-data profile — did not repeat here: the Archives
  widget sat at 0x099214D0 on every boot of the only
  cleared profile available).
- Timeline, Game Stats and Trophies are documented, not
  spoken (a chart, a static page, an unlabeled icon grid).
  Theater's per-story cutscene lists (level 2) are mapped
  as existing, not wired.
- Report row names derive from the index; a collection
  with a GAP (a middle report missing) was not driven —
  whether the game files collected reports contiguously
  or leaves holes is unproven, and the spoken numbers
  would follow the list positions either way.
- Install Game's own flow (and any phase steps it takes)
  was deliberately not driven: it can write game data.

## Mirage Arena event menu / g10vs00 — IDENTIFIED + WIRED + LIVE-VERIFIED (2026-10-24, bench g10–g10l)

The Hub's one unidentified interactable is settled: `g10vs00`
at (0, −5.50, 8.20) is the lobby's **Log In terminal** — the
Mirage Arena's online event console, the big dark machine
with the sloped control panel and the circular green
holographic screen on the lobby's north side. Its own `.pmo`
is a ~1 cm invisible cube (a pure logic fixture; the visible
machine is room geometry), and OpenKH's object dictionary
names it "Arena Entrance Device (VS)". The room's
event-pattern variant spawns a second copy at (0.15, −6,
9.10) without the save point/terminal/Moogle set — an event
fixture for the same console.

Bench proof (runs g10/g10b, screenshots are the ground
truth): Terra clamps ~2.5 units out from the console on
every side and the field command slot shows the prompt
**"✕ Log In"**. Cross opens the **MIRAGE ARENA menu**:
**New Event** (help "Host a new event.") / **Active Events
— 0 event(s)** ("Show a list of existing events.") /
**D-Links** ("View the players you can D-Link with.");
footer MEDALS 99999 / ARENA LV 30. It opens on New Event
and wraps both ways (D-pad walk with a RAM dump per row).
One Circle exits to the field, the prompt re-arms, and a
second Cross reopens deterministically; Triangle does
nothing there. Controls: at the Moogle the prompt is
"✕ Medals" and Cross opens the Medal Shop Buy list; at an
empty platform spot the slot shows plain "✕ Attack" and
Cross only swings. By-product observation for the
arena-entry item: the centre terminal (`g11vs00` at (0,0))
shows NO prompt and Cross attacks when stood on exactly,
facing north — its interaction semantics belong to that
item, not this one.

NEW EVENT opens a mode list — **Versus Mode** (greyed
while offline) / **Arena Mode** ("Join forces with other
players against a common enemy. You can also choose to
battle alone.") / **Command Board** / **Rumble Racing** —
which is the arena-entry queue item's first screen: MAPPED
here, deliberately NOT wired (below). Active Events and
D-Links are no-ops with nobody in range; they would list
other players' sessions, so their sub-lists cannot be
exercised on this bench at all.

### The gate — a widget with two homes, and a lingering twin

The menu's widget (base = its screen word; cursor at
+0x0E, previous-row tell at +0x10, count at +0x12) exists
in TWO homes with one layout:

- **Home A @ 0x09919208** — fresh opens from the field
  (statics in the menu-system block beside the camp
  registry). The first open and every re-open land here.
- **Home B @ 0x0991BCB8** — after choosing New Event and
  backing out of the mode list, the returning menu is
  REBUILT at B while A reads freed junk (52692/2272/…),
  even though the menu is visibly up and navigable
  (g10d's frame trace + g10e's returned-menu row dumps,
  which track the cursor at B and nowhere else; proven
  stable across both returns of g10f and again in g10l).

The signature, identical at both homes: screen == 2,
+0x04 == 15, +0x08 == 0xDBFF, +0x0A == 0xFFF8, +0x0C == 0,
count == 3, cursor < 3. Two conjunctions complete the
gate:

- **The menu-system active word** (u32 @ 0x08B5BEF8 == 8 —
  the camp gate's witness A; this menu is built on the
  same menu system). It is load-bearing: when the menu
  closes after a mode-child trip, the home-B widget is
  NEVER torn down — its block lingers with the signature
  intact while the player walks the field (the g10c/g10f
  end dumps match home B in the field) — and only the
  active word (8 through every phase of the flow, 0 in the
  Hub field before and after; g10k polled two full
  open/dive/return/close cycles) separates "menu up" from
  "menu gone, widget stale". Without it, post-close
  WhereAmI answered with the menu line over the field.
- **Camp excluded**: camp shares the active word, but its
  witness B (u32 @ 0x08BD194C == 2) reads 0 throughout the
  event flow, so `CampActive()` stays false here and the
  event gate's `!CampActive()` term costs nothing live
  while closing the camp coexistence hole by construction.

On the mode child both homes fail (A reads zeros; B holds
the child's own ghost with screen 3 and count 0), so the
child stays silent and backing out re-announces the top
menu. One opening transient is worth knowing: the
widget's +0x0C word reads 26442 for the first ~30 frames
after Cross (a half-built widget), so the arrival line
fires a beat after the menu becomes visible — the same
settle the game-over debounce already tolerates.

Negative space: a sweep of all **2,478 bench RAM dumps**
finds the full widget signature ONLY in event-menu states
(fresh opens at A, returns at B) and in post-close field
dumps where the active word is 0 — never in camp, meld,
title, Archives, world map, save/load, fights, tutorials,
or the field. Live, the Medal Shop and the transporter's
play-mode menu (the two Hub screens with no dumps in the
corpus) were also driven with the adapter attached and
stayed silent (g10f/g10l).

### What speaks

- Arrival (game-over-style 30-frame debounce): "Mirage
  Arena. New Event, 1 of 3." — and the same line again on
  return from the mode child (the down-stretch is
  hundreds of frames).
- WhereAmI in the menu: "Mirage Arena. <row>, N of 3."
- MenuNext/MenuPrev echo the landed row ("Active Events,
  2 of 3"), read live from whichever home is live.
- The lobby Guide gained the landmark: "Log In terminal"
  at (0, 8.20) — from the save-point approach it reads
  e.g. "Log In terminal, 2 away." at the console itself.

Full live transcript (g10l): ring menu silent, Medal Shop
silent, fresh-open arrival + both nav echoes + WhereAmI,
dive 1 silent in the child with re-announce on return,
WhereAmI + nav at home B, dive 2 re-announcing again,
post-close WhereAmI back to the field line, reopen
announcing again, Guide naming all four lobby landmarks.

### Two traps this item cost, recorded for the next one

- **A decimal-to-hex slip can pass every host test.**
  The widget's +0x08 word is decimal 56319 = 0xDBFF; an
  early draft checked 0xDBEF (56303) in BOTH the adapter
  and the test plants, so the suite was green while the
  live gate never fired. Settled by an instrumented build
  printing the checked values next to the verdict
  (… 56319 … -> false). Plant constants from dump values
  and distrust any hex conversion done by eye.
- **First-open statics are not the whole story.** The
  original 3-word gate at home A worked in g10c and would
  have shipped blind to the relocation, the stale widget,
  and the child ghost. Walking the FULL cycle (open →
  child → return → close → reopen) with dumps at every
  state is what exposed all three.

Host tests: 322 checks, floor 323 sites (the section
plants both homes, every witness knockout, the child
ghost, and the stale-widget-with-word-down case).

Limits: anchors proven on the fixture lineage (Terra,
Mirage Arena Hub); home B's address was stable across
every return driven (g10e/g10f/g10l) — if a future return
lands elsewhere the gate fails closed (silence), never
false. The mode list is wired by the arena-entry work
below; the Active Events / D-Links sub-lists need other
players in range and cannot be exercised on this bench.

## New Event mode list + Arena setup + event state — WIRED + LIVE-VERIFIED (2026-10-25, bench cb1–cb6)

The arena-entry queue item: the New Event flow end to end,
from the mode list through the Arena setup screen to the
pad, Ready, and Begin.

### The mode list (bench cb1–cb3)

NEW EVENT's child is a four-row list: **Versus Mode /
Arena Mode / Command Board / Rumble Racing**, opening on
Arena Mode, wrapping both ways (help texts: Versus "Battle
other players." / Arena "Join forces with other players
against a common enemy. You can also choose to battle
alone." / Command Board "Play a Command Board game." /
Rumble "Take on other players in Rumble Racing."). It is
the SAME widget family as the top menu (signature +0x04
15, +0x08 0xDBFF, +0x0A 0xFFF8, +0x0C 0, cursor +0x0E) with
screen word 3; its count word reads 0, so the cursor is
bounded by the four walked rows instead.

**The family widget floats.** cb1's cycle found the top
menu at 0x09919208, the mode child at 0x0991BCB8 — and,
after one Arena-setup round trip, BOTH rebuilt elsewhere
(top 0x0991E278, mode 0x0991B5B8, top again 0x0991E348).
The g10 two-home gate went silently blind on the rebuilt
menu (WhereAmI answered the field line over the visibly
open menu). The gate is now a scan: `EvLocate()` keeps a
sticky base re-validated per call and otherwise scans
0x09910000..0x09960000 demanding exactly one shaped block;
`EvScreen()` discriminates top (screen 2, count 3,
cursor < 3) from mode (screen 3, cursor < 4), both behind
the flow word (u32 @ 0x08B5BEF8 == 8) and !CampActive().
Exactly one family widget is live at a time in every
dump; a closed menu leaves a stale shaped block behind
with the flow word at 0, which stays silent.

**The mode arrival is stability-gated.** cb3's per-frame
poll across the dive: the top widget tears down ~frame 5,
the mode widget validates at frame 11 with cursor 0, and
the game writes its start row (1, Arena Mode) at frame 15.
An arrival on the first valid frame would name Versus
Mode, a row the player is not on — so `WatchModeMenu`
speaks only once the cursor has held still 10 frames,
armed by a 30+ frame down-stretch (the archives-watch
shape). Spoken: arrival "New Event. Arena Mode, 2 of 4.",
nav echoes both directions with wraps, WhereAmI "Mirage
Arena. New Event. <row>, N of 4." Cross on **Versus Mode
is inert offline** — no dialog, the list unchanged (cb2);
the row is greyed because there is no one to battle.

### The Arena Mode setup screen (bench cb1/cb3/cb4)

Cross on Arena Mode replaces the list with the setup
form: banner "NEW EVENT", tab "ARENA MODE", focus rows
**Battle → Players → OK** (wrapping), help line per focus
("Select a battle." / "Select the number of players." /
"Proceed with these settings."), footer MEDALS 99999 /
ARENA LV 30. The Battle row shows the event name with
Risk (stars) and Rounds beside it; some battles also draw
an "Arena LV n" unlock tag above the name.

The screen's widget is of the **Trinity Archives class**
(header +0x00 0xDFFF, +0x02 0xFFF8, cursor +0x06, count
+0x0A), here count 3, focus at +0x06 (0 Battle, 1 Players,
2 OK). Every proven sighting sits at 0x0991BD60; the
adapter locates it by the same exactly-one scan as the
event family rather than trusting the address, gated by
flow == 8, !CampActive(), and neither sibling menu up.
The battle index rides at widget +0xD8 (cb4's full Right
cycle: 0..12 then wrap) and the Players count at +0x18.
The widget lingers fully shaped after OK (focus 2) with
the flow word down — the flow term is what keeps it
silent in the field.

**Thirteen battles**, in list order, with the facts the
screen shows (cb4's screenshots): Day of Reckoning
(Risk 1, 4 rounds) / Wheels of Misfortune (1, 4) / Risky
Riches (LV 8, 1, 3) / Weaver Fever (2, 4) / Sinister
Sentinel (LV 3, 2, 5) / Dead Ringer (LV 5, 2, 5) /
Combined Threat (LV 7, 3, 6) / Treasure Tussle (LV 15,
3, 3) / Harsh Punishment (LV 10, 3, 6) / A Time to Chill
(LV 13, 4, 8) / Copycat Crisis (LV 17, 4, 8) / Keepers of
the Arena (LV 20, 5, 9) / Villains' Vendetta (LV 30,
5, 8). Nothing is shipped as adapter data: the names are
walked from the game's packed string table at
**0x08E1CC30** (the thirteen names in list order; the
walk validates the table by its first entry and every
string for printable ASCII, so a misread degrades to no
name, never a wrong one), and Risk/Rounds are read from
the static mission table at **0x08E17AA0** (13 records ×
16 bytes: id +0x00 == index, constant 100/28 at
+0x02/+0x03, Risk +0x06, Rounds +0x0C; a medal reward at
+0x08 — 50 rising to 500 — is not shown on this screen
and is not spoken). Name and facts degrade independently
behind their own witnesses.

The **Players selector is inert offline**: its arrows
draw dimmed and Left/Right change nothing (cb4's probe).
The row speaks the count word as drawn (1 on the
fixture); in an ad-hoc session the same word is what the
screen shows.

Spoken: arrival "Arena Mode setup. Battle. Day of
Reckoning. Risk 1 of 5. Rounds 4." (the same settle-gated
arrival discipline as the mode list, on the focus/battle
pair); focus echoes ("Players. 1." / "OK." / the battle
line); battle cycling speaks the landed battle's line;
WhereAmI "Mirage Arena. Arena Mode setup. <focus line>."
`khbbs_ready()` includes the screen.

**Limit — list alignment at low Arena LV.** The full
list is proven at Arena LV 30, where all thirteen battles
are unlocked. The "Arena LV n" tag marks an unlock
requirement; whether a lower-level player's selector
lists locked battles (misalignment impossible) or omits
them (the game's own list build would shift indices) is
not provable on this fixture — no low-LV save exists on
the bench. If the list is filtered there, names/facts
would misalign by the count of omitted battles; the
failure mode is documented rather than hidden.

### After OK: the pad, Ready, Begin (bench cb4/cb6)

Cross on OK closes the menu (flow 8 → 0) with the event
set; the player stands in the Hub by the terminal. The
tail of the flow has no menus: walk onto the sunken
center pad (0, ~0.3), where the field prompt becomes
"✕ Ready"; Cross raises an INFORMATION banner ("Once
everyone is ready, press the START button to begin!") and
a "START Begin" chip, and the prompt becomes "✕ Cancel";
START loads the arena (world 15, room 3), where the
existing battle speech takes over.

The spoken state for this tail hangs on the **info-text
pointer at 0x09F3A38C**: the address of the hint line the
event flow currently wants read, or 0 when no event is
pending. Proven values across cb4's dump chain (plain
field, every menu, after OK, at the pad, ready, begun):
0 everywhere except — event pending → 0x08E1BE70 ("Move
to the middle of the hub when you're ready, then press
⟨Cross⟩."); ready → 0x08E1BEB0 ("Once everyone is
ready…"). `WatchArenaEvent` (Hub field only: world 15
room 1, InField) keys on the pointer but speaks only when
the string at it matches the proven text: pending speaks
"Event set: <battle>. Move to the middle of the hub and
press Cross when ready." (the battle name latched from
the setup visit; unnamed if the adapter attached over a
pending event it never saw set); pending → ready speaks
"Ready. Press START to begin."; ready → pending speaks
"Ready cancelled."; any → 0 stays silent (Begin is
covered by the fight itself). Two candidate witnesses
were rejected on evidence: 0x08B5BE1C (reads 3, 1, 7
across dumps of the SAME state — not a state word) and
0x08B5D144 (32 in both the ready and begun dumps).

The field prompt itself ("✕ Ready" on the pad) is not
spoken — the same limit as every field prompt (the
terminal's "✕ Log In", the Moogle's "✕ Medals"): no
fail-closed witness for the prompt slot has been found,
and the event-set line already tells the player where to
go and what to press.

Host tests: the khbbs suite plants the setup screen at a
relocation home (arrival, focus echoes with wrap, battle
cycling, WhereAmI, two-widget ambiguity, count/focus/
index/flow knockouts, and both degradation directions)
and the event-state chain against a planted Hub actor
(set line naming the latched battle, ready, cancel,
re-ready, silent begin, unrelated pointer, corrupted
text). The rumble form and the event-end watch have
their own section (arrival, focus echoes, the packed
value word's knockouts, both-forms-shaped silence, the
race's set line, the armed/unearned summary window).
Suite at 526 checks, floor 527 sites after the whole
New Event arc (483 / 484 before the mode list).

RETURN PATH + THE EVENT-END SUMMARY (cb5b/cb8/cb9/
cb9b, 2026-10-25): the event-end window is now
identified. When an arena event finishes, the game
shows a SUMMARY screen in the arena room (world 15,
room 3, flow word raised): the battle's name, the play
time, category rankings (Damage, Friendship, Style,
each with a 1st/2nd placing), an obtained-medals
count-up, and an MVP line (cb9b's frame-triggered
capture, endwindow dumps: "Day of Reckoning", play
time 0:04:48, medals counting 0 → 75). The screen is
NON-interactive — it counts up and returns the player
to the Hub unaided — on a loss as on a win: a clean
no-input death runs HP to 0, the death animation and
white fade play, the same window passes, and Terra is
back in the Hub ON THE PAD (room 1, (0,0)) at full
health. The player is never trapped on a screen that
needs input. The adapter marks the summary's arrival
with one neutral line, "Event over." — a watch that
arms on presence in the arena room with the flow word
down (the room is reachable only by starting an event)
and fires once when the word rises; the summary's
contents stay unspoken (their data source is unhunted
and the screen is up only seconds). The same window
used to trip the setup gate ("Arena Mode setup. OK."
once per event, cb5b/cb9) — fixed the same day:
SetupState requires world 15 / room 1, the only place
the real screen exists, with a host test pinning it.

RUMBLE RACING (benches cb11/cb12): the mode list's
fourth row opens a sibling setup form in the same
widget class (same home, row count 4): Course / Laps /
Players / OK. Its values pack into the word at +0xD8 —
byte0 course index (0 Country Chase, 1 Disney Drive,
2 Grand Spree, 3 Castle Circuit; wraps), byte1 laps − 1
(5 by default), byte2 players − 2 (6 by default; the
selector is LIVE here, 2..6, wrapping — solo play
fills the grid with CPU racers), byte3 = 0xFF sentinel.
Crowned across cb12's sixteen dumps against
screenshots. The course names ship as adapter data —
the game's name table for them is heap-built at
session addresses (seen packed at 0x0931E5C8), unlike
the arena battles' static table. WIRED like the arena
form: arrival ("Rumble Racing setup. Course. Country
Chase."), focus echoes through the nav commands, the
WhereAmI branch, the same settle discipline; the two
forms exclude each other in their gates so a stale
sibling can never double-speak (host-tested with both
shaped at once). OK sets the event through the SAME
hub flow — the pending/ready hint strings are the
identical texts (0x08E1BE70/0x08E1BEB0 in cb12's
dumps) — and the event-set line names the race
("Event set: Rumble Racing, Castle Circuit. ...") from
the rumble visit's latch. The race itself runs in
world 12 room 10: real-time glider racing (lap and
position HUD, command prompts for Barrier / Shot
Charge / Brake / Accelerate) and is NOT narrated, per
the high-action doctrine; its end does not raise the
arena room's flow window, so "Event over." does not
cover races — the player simply lands back in the Hub.

VERSUS MODE: proven inert offline (cb2/cb11) — the
mode list renders the row greyed, Cross does nothing,
and there is no setup form to wire. It needs other
players over ad-hoc; nothing about it is speakable
from a solo session, and the player notes say so.

## Field dialogue boxes — WIRED + LIVE-VERIFIED (2026-10-24, bench dlg1–dlg4)

Field text boxes (NPC talk, examines, tutorial prompts and
info boxes) now speak their text. Three findings made it
possible; each is proven against screenshots, and the whole
corpus was swept for false positives before wiring.

### The text: resident message tables ("CTD")

The displayed string never sits in a live buffer — the game
typesets it on demand from the area's message table. A table
is a "@CTD" container (magic u32 0x44544340) resident in RAM:
header +0x08 = first id category << 16, +0x10 = entry start
(0x20), +0x14 = entry end, +0x18 = string pool start. Entries
are 12 bytes {id, offset, stringIndex}, count =
(end − start)/12; one file holds several id categories in
sequence (the Land of Departure file: 75 entries of 0x032Cxxxx
then 17 of 0x032Dxxxx). The string at table+offset is up to 3
NUL bytes, then ASCII with escapes, NUL-terminated:

- 0x0A = newline (spoken as a space; the box's lines are one
  flowing text)
- 0xF9 + letter = color/format (Y = highlight, P = colored
  term, X = end) — formatting only, both bytes dropped
- 0xF1 + code = a button/control icon. Decoded names, every
  one screenshot-proven: **B1 = Cross** (ring prompt), **AE =
  Triangle** and **C5 = D-pad** (deck info box), **B4 = L**
  and **B3 = R** (lock-on info, hunt49: "switch targets using
  [L] or [R]" — the glyphs are lettered, in string order),
  **B0 = Square** ("Use <B0> to defend yourself… block if
  stationary, slide if moving" is BBS's Square, and the font
  order AE/B0/B1 = △/□/✕ agrees). Unproven codes are skipped,
  never guessed.

About seven tables are resident at once (the area table, a
7-entry scene table, name tables, duplicated scene tables at
high addresses); the adapter censuses them by magic, keeps
the ones whose entries actually resolve to text, and dedupes
ids first-wins (the duplicates are byte-identical).

### The live signal: event command records + activation serial

Dialogue is issued by event scripts as message commands.
Their command records sit in a heap pool (0x70-byte stride;
the pool FLOATS — 0x099A04B0.. for the plateau's first lesson
set, 0x099A5A20.. after the lesson-2 teleport, 0x0999FB..
for the lock-on lesson, same lineage). Per record:

- +0x00: pointer to the record's own +0x24 word (fingerprint)
- +0x24: current command's first parameter — the message id
  for a message command
- +0x2C: **activation serial** — 0xFFFFFFFF while dormant;
  when a message command activates, exactly ONE record's
  serial becomes a small positive number from a global
  counter. The id stays for the whole display; the record is
  recycled when the script moves on.

The pool holds dormant copies of every message the area's
scripts can show (several records share one id — the ring
prompt had four), so the serial is the load-bearing field.
Speech is per-record edge-driven: a record carrying a serial
it hasn't spoken yet speaks once (after 2 stable frames);
consumption is remembered per record, never by global serial
order — records recycle after every box and a save load can
restart the counter, both of which a global-order latch gets
wrong (the first host-test draft re-spoke a talk line exactly
that way).

What reads as "pages" is either one box showing its full
text at once (up to three lines) or the NEXT message command
(prizes → deck). Speaking each activation's full text once
covers both. Successive boxes in a conversation are
successive commands, so NPC conversations page correctly by
construction.

### Corpus proof (before wiring)

An offline implementation of the same fingerprint swept all
2,478 bench dumps. Exactly five activations exist in the
whole corpus, all tutorial info boxes, each matching its
screenshot word for word: serial 3 ring prompt (dlg1/a00),
5 prizes (hunt26/m6), 6 Command Deck (hunt27/p1), 9 lock-on
info (hunt47/c1), 10 target-switch info (hunt49/atk — its
screenshot settled L/R above). NO activation appears in any
camp, title, Archives, world-map, game-over or movie dump.

### The banner verdict: NOT this channel

The INFORMATION banner ("Attack the single hanging ring!",
dlg1/cend) is the objective HUD, not a message box: while it
was up the record pool was already freed, and the objective's
message id (0x032C2300 — objectives are their own band of the
same table) appears nowhere in RAM outside the table itself.
Speaking objectives needs a separate hunt for the objective
state. It stays unspoken.

### The movie-scene negative

Voiced scenes are a different system: mp1's playing-scene
dump holds no command records at all, so the gate cannot
fire there. In-engine message windows, if a scene uses them,
are field dialogue by construction and speak as such.

### What's spoken (live proof, khbbs-dlg4)

One deterministic run (hunt24 end state; dismiss, complete
lesson 1, page the info boxes) with the real adapter ticked
per frame produced exactly three lines, each verified
against the run's own screenshots (byte-identical to the
dlg3 capture run):

- @2: "Press Cross to attack the ring! Keep attacking to
  string together a powerful combo."
- @1719: "You can earn prizes by defeating enemies or
  hitting things in the field. Green prizes restore HP.
  Yellow ones give you munny."
- @2211: "Your Command Deck contains special moves. Use
  D-pad to select one, then press Triangle to use it once
  its gauge is filled."

Nothing on close, nothing on idle, no dormant line (Terra's
"I've got time to spare!" record sat armed the whole run and
never fired — it speaks only if its record activates).

### Guards that keep it honest

- A decoded line must look like dialogue (≥4 chars, with a
  space or sentence punctuation): the resident tables include
  command/keyblade NAME lists, and a stray record carrying a
  name id must not speak a lone name (host-tested with the
  real 0x00FA table's "Attack").
- Decode fails closed on any byte outside ASCII + F9/F1
  escapes: one leftover table entry holds Shift-JIS bytes
  (id 0x032D8500) and stays silent (host-tested).
- The watch stands down while camp, the save/load dialog,
  the Archives, game over, movie pause, the event menu, the
  world map, title or difficulty own the screen.
- Host tests: 335 checks, floor 336 sites (dormant silence,
  1-frame ghost, activation speech with exact decoded text,
  idle no-repeat, close, pool relocation, late talk
  activation, name-table guard, undecodable silence +
  recovery, NUL-prefix variant).

### Limits

- Proven end-to-end on the tutorial lineage (Ventus, Land of
  Departure). The mechanism is area-agnostic (census by magic
  + fingerprint, no fixed addresses), and the fixture
  lineage's dumps contain no activations to contradict it —
  but no NPC conversation has been driven live yet; Terra's
  talk record on the plateau is the standing test case.
- No speaker name: boxes that show one carry it outside the
  message string (not located); speech is the text alone.
- Choice prompts speak their full text, options included
  (the options are lines of the message); the choice CURSOR
  is a different widget and is not tracked — a follow-up if
  it proves to live somewhere findable.
- A box already up when the adapter attaches speaks (a
  resumed conversation should be audible); a box that opens
  and closes entirely inside another screen's ownership
  (camp over a field event) would be missed — not observed
  in practice.
- Non-US text is out of scope by construction: the decode
  fails closed on it rather than garbling it.

## The Command Board — SURVEYED, GP records crowned, screens NOT yet wired (2026-10-25, benches cb7–cb7d)

The board game is reachable two ways: the camp menu's
Command Board row, and the arena terminal's mode list.
One bench trap first, because it looks like a game fact
and is not: from the g10l-lineage SAVESTATE the camp row
renders and echoes correctly but Cross never activates
it (24+ presses, frame-level probes; the Items row in
the same camp instance opens fine — benches cb7/cb7b/
cb7c). From a real boot + title-screen save load the
same row works (cb7d, repeating the subcamp route).
Treat the inert row as a savestate-lineage artifact,
not game behavior; the root cause was not found and is
not worth more time.

ENTRY FLOW (all screenshot-verified, cb7d; dumps in
~/khbbs-scan/cb7d/):

1. Camp → Command Board row → the board module loads
   (camp witnesses die; the adapter goes silent — a new
   screen family).
2. Title screen "THE COMMAND BOARD": menu New Game /
   Continue / How to Play, cursor starts on New Game;
   help lines "Start a new Command Board game." /
   "Resume a game in progress." / "Learn how to play
   the game." (bt-dn1/bt-dn2/bt-up1 probes; Continue is
   selectable when a game is in progress, as on the
   fixture save.)
3. New Game → board select, "Select a board." — a
   carousel; the default is the Keyblade Board (the
   Land of Departure board), GP Goal 5000, opponents
   Ventus and Aqua (for Terra), description "The
   thrills are never more than a roll away on this
   compact board. (Land of Departure)". The other
   boards (from the game's trophy strings): Royal,
   Spaceship, Toon, Hunny Pot, Skull, Secret.
4. Goal step: "Select a goal." — same screen, the GP
   Goal selector takes focus.
5. Confirm: "Play on this board?" Yes / No — the
   cursor starts on YES (opposite of the shops/meld
   convention).
6. Intro card: "Land of Departure / The Land of
   Departure / GP Goal 5000".
7. Playing Order screen (the rolled/saved order shown
   as 1ST/2ND/3RD portraits) + "Shuffle the playing
   order?" Yes / No (starts on Yes).
8. Dice roll for order: "Press ✕ to roll the dice!
   Higher rolls get to play first." Each Cross rolls
   for the next player; then "Here's the new playing
   order." (cb7d: Ventus 1ST, Terra 2ND, Aqua 3RD.)

IN GAME (cb7d gp0–gp4): a "TURN 1" banner names the
player whose turn starts; the human player's turn opens
a 4-row menu — Roll / Hand / Stats / Options (cursor on
Roll). Roll throws the die, movement-direction arrows
appear around the player's panel, and the move commits
over the following steps. The standings panel (right
edge) shows per player: laps, name, GP on hand, total
GP — Ventus ended his first turn at 925 on hand / 1000
total after buying a panel.

RAM STATE (partially crowned):

- Player GP records live at fixed addresses in the
  session region: u32 GP-on-hand at 0x09F2DBD0 (Terra),
  0x09F2DBF4 (Ventus), 0x09F2DC18 (Aqua) — stride 0x24,
  CHARACTER order (not turn order). Crowned by the
  1000→925 transition across Ventus's first turn
  (unique in a full-RAM hunt). The records' full field
  layout is not yet mapped; the words around them churn
  with render state.
- The board module's own state sits around 0x0982xxxx
  (title-screen diffs churn there — hundreds of small
  diffs; no cursor crowned yet).
- NOT yet found: the liveness gate for "a board game
  is running", each player's panel position (it commits
  only when a move's animation completes — the gp3/gp4
  dumps bracket the direction-choice step, before the
  commit), the dice value as a word, the hand (the
  turn menu's Hand screen), and the total-GP field.

NEXT EXPERIMENTS (in order): (1) a bench that brackets
a COMPLETED move with dumps — the position field lights
up inside the moving player's record, which also fixes
the record base; (2) open the Hand screen and diff for
the hand array (command ids resolve through the same
tables the camp inventory reads); (3) the module-
liveness word from a title-vs-field dump pair in the
0x0982xxxx region, filtered by stability across the
gp dumps. With (3)+(GP)+(1) the minimum core wires as a
standings/position query during board play; the title
and setup screens follow once their cursors crown.
Nothing board-side is spoken today, and the player
notes say so.

## Battle gauges: Focus / shotlock + D-Link — WIRED + LIVE-VERIFIED (2026-10-24, bench khbbs-foc1–foc8)

Question (completion-list item: Focus/shotlock and D-Link in
battle): the field picture spoke HP and the Command Gauge, but
nothing about Focus, the shotlock, or the D-Link — not even that
a link was active. Where do the two gauges live, what signals
mark their events, and which of them are clean enough to speak?

### The gauges object

One heap object carries both resources; its address floats per
boot (0x099842F0 hub/camp lineage, 0x09994700 LoD, 0x099A4720
the Final Episode fight lineage), but a FIXED global holds it:
u32 @ **0x08B73BB8** (written by its constructor FUN_08919c88,
zeroed by its destructor FUN_08919cdc — CWCheat's "Full Focus
Gauge" store addresses bracket those two functions, which is
how the object was found). Layout, from decompiling its
per-frame updater **FUN_0891db48** and confirming every field
live (bench foc1–foc7):

- +0x3C f32 — D-Link gauge 0..100; +0x40 f32 — pending fill
  (prizes add here; the updater transfers it into +0x3C over
  time). +0x44 f32 — Focus gauge 0..100; +0x48 f32 — Focus
  budget (income lands here; converts into +0x44 at 2.0/frame).
- +0x50 u32 — state: **1 while shotlock aiming, 2 while a fired
  shotlock resolves**, 0 otherwise.
- +0x54 u32 — flags: **0x10000** Focus is full (set by the game
  at the fill crossing, clear while Focus < 100); **0x100**
  D-Link primed (set at the fill crossing, HELD through a link,
  cleared when the drain empties the gauge); **0x20000**
  shotlock aiming; **0x200** D-Link active. (0x2 = the HP-ghost
  sync bit, unrelated; 0x800 = a suspend bit seen outside
  battle.)
- +0x70/+0x72/+0x74 s16 — current HP mirror, ghost HP (drains
  toward current in 10% steps — the HP ghost bar), max HP.
- +0x80 u16 — **the active link partner's def id** (0 while
  unlinked; see below).

The object exists wherever the player does (field, hub, camp,
battle — hub flags read 0x100900), so the watch gates its SPEECH
to the field picture rather than to the object's presence. The
destructor also mirrors the gauges as shorts into the
persistent record at 0x09F17A10 (+0x22 Focus, +0x24 D-Link),
but only at finalize — live values come from the object.

### Shotlock mechanics (corrects the naive model)

Bench foc1/foc2 drove the full cycle on the Radiant Garden
fight (Aqua, Lightbloom, cap 20):

- Aim = **hold L+R** (R alone toggles lock-on — an earlier
  brief's "hold R" was wrong). While aiming: time slows, a
  TIME LEFT countdown runs, the actor's state is 24 and its
  spawn mark is **4**, Focus drains as locks accumulate and
  STOPS at the lock cap (100 → 67.5 at MAX 20; the drain is not
  a clean per-lock constant, and the exact economy was not
  needed).
- **Releasing L+R CANCELS with a full refund** to the Focus
  value at aim entry (the entry snapshot rides at +0x4C).
  **Pressing Cross FIRES**: state +0x50 goes 2 the same frame
  the aim flag falls, Focus stays spent, and the hits land over
  the following seconds (foe 1200 → 1017+ at 25/hit in foc2).
- Taking damage feeds the Focus budget (+4 per hit observed)
  and can knock the player out of the aim — a cancel, refund
  included. Dealing damage also feeds it (foc8's refill:
  attacking took Focus 73 → 100 and the full line spoke).

### D-Link mechanics

- **D-pad RIGHT** opens the battle list (a scrolling window
  over the roster; Aqua's: Donald, Mickey, Terra, Ventus, Pete,
  …; the cursor opens on Ventus). LEFT returns to the deck.
  **Cross is always Attack**, list open or not; **Triangle
  confirms** the highlighted partner.
- Activation (foc5/foc6/foc7, three partners): actor state 25
  for ~30 frames, then flag 0x200 sets, **HP refills to full**,
  the deck and portrait swap to the partner's (screenshot-
  verified: "VENTUS" / "TERRA" / "MICKEY" splashes, their decks
  and portrait icons), and the gauge drains ≈ 3/second (a full
  gauge lasts ~33 s). At empty the primed bit clears, ~2 frames
  later 0x200 clears, the player's own deck returns.
- **Partner identity**: the gauges object's +0x80 holds the
  partner's def id while linked (372 Mickey, 383 Ventus,
  384 Terra; 0 in every unlinked dump). Resolved through the
  def table (0x08B34924, name ptr +8): family 7 sub 14 —
  371 Vanitas, 372 Mickey, 373 Donald, 374 Goofy, 375
  Experiment 626, 376 Peter Pan, 377 Maleficent, 378
  Cinderella, 379 Snow White, 380 Zack, 381 Pete, 382 Aqua,
  383 Ventus, 384 Terra. The +0x88/+0x90 words are the link's
  granted abilities (family 8 sub 11: Haste, Auto-Teleport,
  …), not deck commands.

### What speaks (WatchBattleGauges + the field-line clause)

- Field line (WhereAmI / self readout) gains: " Focus 73.
  D-Link 45." — with "Focus full." / "D-Link full." at the
  top, and " Linked to Ventus, D-Link 62." while a link runs.
- Events, each an edge of a game-owned flag bit, latched like
  the low-HP warning (tracked always, spoken only with the
  field in front — never under camp or Reports; first live
  sight primes silently):
  - "Focus full." — 0x10000 rising.
  - "D-Link ready." — 0x100 rising.
  - "D-Link: Ventus." — 0x200 rising, name from +0x80 via
    DefName (fallback "D-Link active." if it won't resolve).
  - "D-Link ended." — 0x200 falling.
  - "Shotlock." — 0x20000 rising. At its fall: "Shotlock
    fired." when the firing state (2) was seen for that aim,
    else "Shotlock cancelled." (a cancel at full Focus also
    speaks the refund's "Focus full." first — both are true).

### The mark-4 gate fix (load-bearing, corroboration-gated)

During the shotlock the actor's spawn mark is 4, which the old
ActorPlausible rejected — InField() dropped for the whole aim,
exactly when the shotlock watch must see. The mark stays 4
through the firing animation too (the gauges aim flag clears
the frame the shot fires; the mark does not), so the acceptance
cannot key on the flag. Nor can mark 4 be accepted bare: a
full-corpus sweep (all 2,760 bench dumps, an offline replica
of the fingerprint) found 34 dumps whose hit set changes —
every one an old=0 dump gaining a mark-4 struct that is NOT
the player: an enemy block in the Radiant Garden fight dumps
(battle27/28 — state 35, HP 5/512; in four battle27 dumps it
would have made the located actor AMBIGUOUS and dropped the
field), and UI structs on screens whose gauges object does
not exist (ad91, state 50; the Archives timeline, state 102).
The player's mark 4 is therefore accepted only with the
shotlock's own corroboration: **actor state == 24** (the
shotlock action state, constant through aim and firing in
every foc trace) **and the gauges object live**. Both held in
every true shotlock dump (foc1/foc2/battle40) and failed in
every false one; the same corpus sweep under the final rule
changes no dump's hit set except the true shotlock rescues
(the aim dumps, where the old rules found no actor at all).
on_frame's lock edge also treats 4 as lock-held, so a shotlock
no longer reads as a lost lock followed by a spurious
"Locked on." The foe / lock QUERIES still key on mark == 2
exactly, as proven in battle41.

### Live proof (khbbs-foc8, the real adapter on the fight)

One continuous fight spoke, in order: the WhereAmI line with
"Focus full. D-Link full." — "Shotlock." (aim, Focus 100 →
73.33 at MAX) — "Shotlock fired." (Cross; Focus stayed spent;
foe 1200 → 950 over the following phases) — "Focus full."
(refilled by attacking) — "Shotlock." — "Focus full." +
"Shotlock cancelled." (release; refunded) — "D-Link: Ventus."
(RIGHT, Triangle; HP refilled) — WhereAmI "Linked to Ventus,
D-Link 98." — "D-Link ended." (gauge pinned to 1.5 by the
bench, drained out by the game) — WhereAmI back to "D-Link 0."
An honest extra: phase 4 opened with a "Locked on." — the
lock had genuinely been lost and re-acquired during phase 3's
melee, the pre-existing edge working as designed. Screenshots
and the full log: ~/khbbs-scan/foc8/.

Host tests: 358 checks, floor 359 sites (priming silence,
readout percents, each event once and never repeated without a
state change, the fire and cancel aim endings, the mark-4
corroboration gate, the unnamed fallback, the named
activation, camp-menu silence with no late fire, the
absent-object stand-down). The foc8 proof above was re-run
against the final corroboration-gated build and reproduced
every line (log: foc8-final.log).

### Limits

- **Lock-on pip count: NOT spoken, and resisted hard.**
  Exact-pattern scans for the sequence (0 locks, 3, 20 at MAX,
  20) across u8/u16/u32/float over the foc1 dump series and the
  actor/gauges/foe neighbourhoods found no word — the count is
  a per-target list length the HUD recomputes per frame. The
  max-lock milestone has no unique witness either: the Focus
  drain also pauses when the reticle holds no enemy, so
  "drain stopped" ≠ "MAX". No milestone line is wired.
- The battle D-Link LIST itself does not speak (its cursor is
  the field deck-cursor problem, still open); what speaks is
  the activation, with the partner's name. The camp D-Links
  page — the roster with levels — already speaks through camp.
- The link's Finish command is not announced: no distinct
  signal was isolated for its availability window (the low-
  gauge screenshots show the partner deck unchanged).
- Proven on the Final Episode fight lineage (Aqua). The
  signals are the game's own object + flag bits (located by a
  fixed global, not heap fingerprints), and the host plants
  cover the state machine; other lineages re-verify cheaply
  because the global is constant.

## Shops — the Medal Shop and the Moogle Command Shops (2026-10-24/25)

Both shop families are wired: the Mirage Arena Medal Shop (medals)
and the Moogle "Command Shop" (munny; proven at Olympus Coliseum —
the same shop stands in several worlds). The Medal Shop is
buy-only; the Command Shop has Buy / Sell / Exit. Benches:
`~/khbbs-scan/khbbs-shop1.cpp` … `shop21.cpp`, findings in
`~/khbbs-scan/shop-progress.md`.

### Screens and gates

Shops run in their own module (SHOP.ELF); the camp screen registry
and the camp menu-active word are garbage/zero inside, so the gate
is shop-native, resting on three corroborations:

1. **Phase words** (SHOP.ELF BSS, fixed while the module is
   loaded): main u16 @ `0x09918E8C` — 2 = Command Shop top level,
   4 = buy list, 5 = sell list, 6 = buy quantity/confirmation,
   7 = sell quantity/confirmation (the Medal Shop uses 4/6 only).
   Sub u16 @ `0x09918F10` — 5 at the top level, 40 on a
   confirmation step, 30 while a confirmed transaction processes,
   10 elsewhere. Camp/field/title dumps read garbage here.
   Phase announcements latch only after **4 stable frames**: the
   quantity screen's opening shows the confirm sub-value for
   exactly 2 frames (frame-traced), which fooled a 2-frame latch
   into announcing the confirmation before the quantity prompt.
2. **The list widget** (heap, floats per lineage; signature-scanned
   in `0x09910000–0x09960000`, never hardcoded): cursor u16 @
   +0x34, current-list count u16 @ +0x36 with a mirror @ +0x3E,
   tab u16 @ +0x04 (lists only — the top widget's +0x04 is a type
   word), and a back-pointer @ +0x2C to the shop state block
   `0x09918AC0`. The count must equal the row count the adapter
   computes for that tab (below). The back-pointer is what
   separates the top-level widget from the dozens of static
   count-3 structs in the heap — without it the top level never
   opened live. The Command Shop has three widgets (top / buy /
   sell), the Medal Shop one. Widgets linger after the shop
   closes; the phase word drops the gate.
3. **The stock table** (below), without which the gate stays shut.

Quantity is a u16 @ `0x09918F14` (up/down ±1, left/right ±10; a
slam reached 121 — no 99 cap observed). The confirmation defaults
to **No** in both shops; its cursor is a one-hot pair in the heap,
so the choice is tracked from the player's own presses (the meld
dialog precedent). A completed transaction is proven by the
currency word moving — medals u32 @ player block +0x287E0, munny
@ +0x287DC — with the traded row's name frozen at confirm entry.
(One fixture quirk: a 1000-medal purchase moved the stored word
999999 → 99999; the word matched the on-screen balance at every
checkpoint, and the adapter speaks the word.)

### Stock, rows, names

The stock is a file image loaded with the shop (an ARC sibling:
`mdlXXShop.bin` for the Medal Shop, `cmdXXShop.bin` for the
Command Shop), magic `"SHP\0"` at +0, a type u16 at +6 and a u32
count at +8. **The type word selects the format, and the formats
differ**: type 2 (Medal) = 8-byte records `{u16 id, u8 kind
(1 = command, 0 = item), u8 sub, u16 ?, u16 price}`; type 1
(Command) = bare ascending u32 **def ids only — no prices**.

Rows are never materialized per tab; they are rebuilt by filtering
the table through the def table's family/sub bytes (the meld
scheme): family 2 sub 1 = Attacks, sub 2 = Magic, sub 3 = Items
(battle item commands), subs 12/13 = Friendship; family 3 sub 4 =
Movement, sub 5 = Defense, sub 6 = Reprisals; family 5 = Shotlocks
(the Medal Shop's ETC tab; the Command Shop has 8 tabs, no
Shotlocks). The Medal Shop's plain items appear only in All.

The Command Shop's visible list is its 87 ids minus four defs the
game never displays (94 Meteor Crash, 99 Chaos Blade, 110 Ars
Solum, 130 Break Time — meld-only/special commands), verified
row-for-row against a full photographed walk of the 83 rows.

**Prices are not in RAM** for the Command Shop in any flat,
indexed or near-id form (exhaustive searches). They are per-def
game constants, shipped as adapter data (`kShopPrices`, harvested
from the live buy screens for all 83 visible rows; three hidden
defs priced through the sell formula — 110 exactly: its LV6 sell
price 4500 = 4000 × 9/8). 130 Break Time's price was never needed
or found; its rows speak without one. Medal Shop prices ride in
its table records.

**Item names — settled.** They ride the same resident message
tables as the field dialogue: category `0x2800`, message id
`0x28000000 | item_id`, resolved through the dialogue census.
All 16 Medal Shop items resolve and match their screenshots
(Risky Ticket 150 … Giga Attack Recipe 2000). Command names come
from the def table as usual.

**Sell rows** are the command pool's family-2 **and family-3**
instances sorted by def id, pool order breaking ties — the
widget's 165 rows = 152 + 13 on the fixture; finishers (family 1)
and shotlocks (family 5) are never sellable. Sell price is
computed: `floor(buy × (level + 3) / 8) + (ability ? floor(buy / 4)
: 0)`, fitted exactly on ~20 live rows (level and ability from
the pool instance). Item-command stacks are single rows whose +2
byte is the held count; they speak their count, not a price.

**"In stock"** has three proven sources: a command's pool
instance count (a live purchase moved it 2 → 3); an item
command's held count (its pool instance's +2 byte — Potion reads
60); and for plain items, the player block's inventory section —
category tables of `{u16 id, u16 count}` pairs (buying one
Shimmering Crystal moved its pair 50 → 51). The adapter speaks a
plain item's stock only when the pair sits in an ascending id run
with plausible neighbours and is unique in the section; anything
less stays unspoken.

### What's spoken

Arrival ("Medal Shop. …" / "Moogle Shop. …") with the landed row
and the balance; top-level rows (Buy / Sell / Exit); buy and sell
rows with name, price and position; item rows with their stock;
tab landings ("Attacks. …", incl. "No items." for empty tabs);
the quantity prompt and its echo; the confirmation in the game's
own words ("Trade in these medals?" / "Complete this purchase?" /
"Complete this sale?", choice tracked from presses, starting on
No); completion ("Bought …" / "Sold …" + the new balance); and
WhereAmI on every shop screen.

### Limits

- **Refusals are silent in the game and unwired.** Pressing Cross
  on a Medal Shop ticket row (Sentinel Ticket, one already owned)
  never entered the quantity step — consistent with a one-own
  limit, but the mechanism is a single observation, so nothing is
  wired for it. Can't-afford and full-inventory states were not
  probed (both fixtures are rich); no refusal speech exists.
- **No synthesis screen exists in either shop as mapped** — the
  Medal Shop is buy-only and the Moogle is a Command Shop with
  Buy/Sell/Exit. (Command melding lives in camp, already wired.)
- Radiant Garden's Moogle (`n11ex00`, room 5) stands behind a
  gate/wall the walker could not pass and teleports do not stick;
  that room's shop was never opened. The Command Shop wiring was
  proven at Olympus Coliseum's identical shop (`n10ex00`).
- The shop gate fails closed if two list widgets ever share a
  row count (the count cross-check is part of the signature);
  the phase words make a wrong match unable to speak wrong rows.
- A widget's tab/cursor words are only read while its list is
  the phase's active list; the dormant widgets hold stale values.

## Sound-first battle feedback — AUDITED + WIRED (2026-10-24/25, bench khbbs-snd1/snd2/snd3)

The owner's rule (2026-10-24): never speak what the game already
cues with a sound; in high-action play prefer sounds over speech.
This pass audited every battle line the Focus/D-Link batch added
(plus the older lock-on line) against the game's own audio, then
rebuilt the feedback around the settled design. Menus, camp,
shops, the world map and dialogue are untouched — their speech
stays.

### The audio audit (bench khbbs-snd1 + khbbs-snd2)

Two bench fights (battle30's Radiant Garden state, Aqua vs the
1200 HP foe) with the game's audio captured per frame
(`psp_read_audio`, 32768 Hz) and every adapter-visible event
frame-stamped against it. Method: 256-sample bucket peaks;
baseline = the fight's own music with no combat events (peak
median 0.25, p99 ≈ 0.51 of full scale); an event counts as
sounded when its tight window (first 0.12 s) clearly beats that
baseline. Verdicts:

| line / event | verdict | evidence |
|---|---|---|
| "Shotlock fired." | REMOVED | the fire transition (aim fall + firing state) peaks at 1.000 in the tight window — the shot is fully sounded by the game |
| "Shotlock." (aim start) | kept | aim entry peaks 0.31 in both runs, BELOW the music's own p99 — no cue at the entry instant; the line is also the only proof a hold engaged rather than re-locking |
| "Shotlock cancelled." | kept | the only sound at a clean cancel (snd2 @680) is a 0.80 aim-exit whoosh — the same exit sound every aim end shares; it cannot say "cancelled, Focus refunded", and the refund is the fact that matters |
| "Focus full." | became a sound | no cue of its own: the one loud window (snd1 @2012) is fully explained by the hit that fed the gauge across the line 2 frames earlier; the clean crossing (snd2 @680) shows only the shared aim-exit whoosh |
| "D-Link ready." | became a sound | by the owner's split (gauge-full = sound); the primed crossing itself was not captured on the bench (a pinned gauge does not move for prizes — snd2) — recorded as uncaptured, not as silent |
| "D-Link: <name>." | kept | activation IS sounded (jingle; snd1 tight 0.67, snd2 to 1.00) — but a jingle cannot name the partner, and the name is the content (owner's split: big state changes keep speech) |
| "D-Link ended." | kept | tight peaks 0.45 / 0.64 across the two runs — at most a soft tick at the music's own level; effectively uncued |
| "Locked on." | kept | the engage tick measures 0.32–0.58 against a music p99 of 0.51 — at the music's level, not a reliable cue |
| lock loss | stays silent | 0.25–0.64, inconsistent; nothing reliable to duplicate, and prior behavior was already silent |
| "Low health." | kept | the owner's decision; the milestone family below now speaks the 25% crossing alongside it |
| hits (both sides) | beeps added | every HP drop lands with a full-scale game impact (foe hits median peak 0.99, own hits 1.00, n=15/49) — the impact carries no value, so the beep adds the value as pitch |
| field-line gauge text ("Focus full." etc. in Where Am I) | kept | a query ANSWER, not an event — the same reason exact foe numbers stay on the foe query |

### The health beeps (WatchHealthCues, beside WatchLowHp)

- A hit that drops the TRACKED FOE's HP raises a one-shot foe
  beep; a hit on the player raises an own beep. Events ride the
  cue snapshot as trailing one-frame fields (foe_beep/hz/pan,
  own_beep/hz/pan, focus_cue, dlink_cue) — the core_gain pattern
  from the Dissidia work — and out through the new core call
  `poke_cue_health()`. The snapshot consumes them only on a poll
  that passes its field gates; detach/attach clears them.
- PITCH IS LOGARITHMIC in the new health fraction (perceived
  pitch is roughly log frequency, so equal health steps sound
  like equal pitch steps): foe 1200 Hz at full falling to
  300 Hz (bright tick, ~35 ms), own 800 falling to 200 Hz
  (duller thud register, ~55 ms). PAN: foe RIGHT +0.8, own LEFT
  −0.8 (the owner's sides). Register AND pan both differ, so the
  sides stay separable through a mono speaker. Waveforms,
  durations and levels are the synth's side — specified in
  docs/design/cue-synth.md ("One-shot event fields"), NOT yet
  implemented in the app; until the synth grows the recipes,
  the events are computed and delivered but not heard.
- MILESTONES: crossing 75 / 50 / 25 speaks "Foe health N
  percent." / "Your health N percent."; crossing 10 speaks
  "Foe nearly gone." / "Your health nearly gone." The percent
  is the game's own integer arithmetic, (cur*100)/max. A plunge
  through several crossings speaks only the lowest. A drop to
  0 raises NOTHING (a death has the game's own sound and flow).
- LATCHES: per subject (actor / foe block + max HP). Priming is
  silent with at-or-below milestones pre-latched (a fight joined
  at 40% speaks 25 on the way down, never 75/50). Healing back
  to milestone + 5 re-arms (the low-HP watch's hysteresis
  shape).
- THE TRACKED FOE is the lock-on target while locked. While
  unlocked it is the last locked foe, followed through its
  entity block while the block still validates (same max HP,
  sane HP, live scene word) — foe HP only drops because the
  player hit it, so "the foe the player just hit" and "the last
  locked foe" coincide in this game's fights. LIMIT: a foe that
  was NEVER locked is not followed — finding it would take a
  per-frame actor census, and the one census the adapter has
  (the lock-record sweep, ~2M reads) is throttled to 1/s for
  exactly that reason. While locked, the watch rides the cached
  record (ReadFoeBlock — ReadFoe's validation minus distance
  and minus the sweep; the proven query paths keep their own
  code untouched).
- Events stash only with the field in front (camp/Reports
  gate), the watches' usual discipline: hits cannot land under
  a menu, and a stale beep after one closes would misdate the
  hit.
- RESEARCH the design borrowed from (the owner asked for the
  lookup): Mortal Kombat 1's official accessibility "Health
  Meter" cue sounds at exactly 75/50/25/critical — this
  milestone set verbatim (WB Games support documentation);
  Street Fighter 6's designed hit/health/Drive indicator
  sounds (hit-connect vs blocked get distinct sounds; its
  distance cue rises in pitch AND rate); the academic
  informative-sound literature (critical health wants a low,
  dull, slow character — the own-side thud register); and the
  known failure mode of REPEATING low-HP alarms, which the
  per-hit event shape avoids. The project's own Dissidia
  doctrine supplied the frame: one family per kind, identity
  in the timbre, thresholds with hysteresis, silence is a cue.

### The gauge-full split (decision: configurable)

In the default SOUND mode, a Focus-full or D-Link-primed edge
stashes an earcon pulse (Focus 1560 Hz / D-Link 1180 Hz —
the beacon family's bright carriers, centred, ~60 ms; recipes
in cue-synth.md) instead of speaking. The seam is
`oga::KhbbsHudGaugeCues(bool)` at the top of the adapter: pass
false and the spoken "Focus full." / "D-Link ready." lines
return and no pulses are raised. It is a setting, not session
state — attach/detach do not reset it. THE FUTURE PER-GAME
SETTINGS PANEL BINDS HERE (its "HUD gauge cues: sounds /
speech" control for this game); no panel was built in this
pass. The low-HP warning and the milestone lines are not
behind the seam — they are speech by the owner's decisions in
both modes.

### Proof

- Host tests: 425 checks, all passing (floor raised 359 ->
  425). The gauges section now asserts the earcons and the
  fired shotlock's silence; a dedicated section drives the
  whole ladder on both sides (beep pitch/pan values, milestone
  latching and re-arm, plunge rule, silent prime mid-fight,
  unlocked tracking and its validation end, killing-blow
  silence, and the seam in both modes). The low-HP and
  world-map sections' expectations were updated where the
  milestone lines now legitimately speak alongside.
- Live proof: bench khbbs-snd3 + khbbs-snd4 (the new adapter
  in a real fight, cue snapshot polled every frame). snd3's
  melee: dozens of foe beeps on the right, pitch falling
  1155 -> 494 Hz as the foe dropped 1167 -> 431, with "Foe
  health 75 percent." and "Foe health 50 percent." spoken at
  the crossings; own beeps on the left tracking Aqua's HP the
  same way; "D-Link: Ventus." and "D-Link ended." spoken;
  two shotlock cancels, each speaking only "Shotlock
  cancelled." while the refund raised the Focus earcon. No
  "Focus full." / "D-Link ready." line anywhere. snd4: three
  genuine firings (the firing state entered and exited) with
  the aim spoken and the shot itself silent — the shots were
  EMPTY, because this fight's foe is a ranged kiter that stays
  outside lock range (17–23 units at every aim), so no
  foe-damage beeps came from the shots themselves; foe damage
  rides the identical drop path the melee proved. A bonus from
  snd4: a counterattack volley after one shot produced a burst
  of own beeps with falling pitch, and the hits refilled Focus
  through full — earcon, no line.

### Remaining app-side work (explicitly not done here)

- DONE 2026-10-24 (app queue item): both apps now voice the events.
  iOS's CueSynth polls `poke_cue_health()` per frame and renders the
  four recipes in cue-synth.md; Android gained its synth the same day
  (it had none at all — beacon included) behind a new Settings
  "Battle cue sounds" switch, off by default like iOS's. The core
  bridge grew a per-frame snapshot cache so the three cue polls share
  one snapshot and no poll starves another (details in
  docs/design/cue-synth.md, "App-side implementation"). Verification
  there is compile/contract/algorithm level: neither app's new code
  has been built on this machine (no Xcode/Android SDK here) and no
  device has sounded a beep yet — that is the remaining proof.
- The per-game settings panel (separate queue item) binds
  `oga::KhbbsHudGaugeCues(bool)`; today the default (sound
  mode) is compiled in.

## Enemy NAME — the foe readout and the lock-on moment name the foe (2026-10-24/25, bench khbbs-name1–name10)

The open question since the foe readout landed: the target's scene
id (entity block +0x1B8) was never proven to be a TYPE id, and with
one enemy on screen an instance id and a type id look alike. Settled
now, in three steps.

### The scene id is an instance index — proven, not assumed

Every scene object embeds its object code as an ASCII string at
scene object +0x88 (foe entity block +0x240). A census keyed on
that string (small id word at string−0x88, 0x00011000 at
string−0x80) shows the id running in spawn order across types:
the Mirage Arena lobby holds g11vs00 id 19, g10vs00 id 18, TWO
g12vs00 transporter rings at ids 20 and 21, g01ex00 id 22,
n13ex00 id 25. One fight (battle41) holds p02ex00 id 15, g12rg00
17, g18rg00 18, the locked target 19, its keyblade 21. Same-type
actors always differ; the code is the constant. (This census also
corrects a standing mislabel: battle41's target, long called a
Red Hot Chili in these notes, carries code b51ex00 — the
screenshots agree, a tall dark figure, no chili pot. Its HP
series was real; its name was not.)

### The name source: the game's own journal

The journal's Unversed entries are message category 0x3800,
name part at id 0x38000000 | (entry << 8): entry 0x81 Flood,
0x82 Scrapper, 0x83 Bruiser, 0x84 Red Hot Chili, 0x85
Monotrucker, 0x86 Thornbite, 0x87 Shoegazer, 0x88 Spiderchest,
0x89 Archraven, 0x8A Hareraiser, 0x8B Jellyshade, 0x8C Tank
Toppler, 0x8D Vile Phial, 0x8E Sonic Blaster, 0x8F Triple
Wrecker, 0x90 Wild Bruiser, 0x91 Blue Sea Salt, 0x92 Yellow
Mustard, 0x93 Mandrake, 0x94 Buckle Bruiser, 0x95 Chrono
Twister, 0x96 Axe Flapper, 0x97 Prize Pod, 0x98 Glidewinder,
0x99 Blobmob (dumped from the Reports-open image, bench
sub1/s5). Entry = enemy number + 0x80, in order, for all 25 —
the same numbering the object codes carry (m01..m25). The table
is NOT resident during a fight (a fight dump holds five message
tables, none of them 0x3800; it loads with the Reports screen),
so the proven mapping ships as adapter data, the pattern the
harvested shop prices already use.

### Wiring

ReadFoe's locator preamble is extracted as FoeSceneObject()
(shared, unchanged semantics). ReadFoeName() reads the 8 code
bytes at target +0x88 and resolves through FoeTypeName():
strict shape [a-z] digit digit [a-z] [a-z] digit digit NUL,
'm' codes with the ex/vs infix only, numbers 1..25 (the
trailing variant digits are spawn variants — m24ex02 is still
Glidewinder). Speech: the foe query "Bruiser. Health 75 of 75.
3 away."; the lock query "Locked on: Bruiser. 9 away."; the
field line's clause " Locked on. Bruiser health …"; the lock-on
edge "Locked on: Bruiser." (the name only — the audit measured
the lock tick at music level, so this stays a query-grade
answer, no chatter). Every surface falls back to the exact
pre-name line when the code does not resolve. The health-beep /
milestone tracked-foe logic is untouched: the name is a type
label, tracking stays per-actor (two same-type foes behave —
proven live and in host tests). The registry notes and coverage
line describe the naming now; AdapterLibraryData.kt
regenerated, --check green.

### Live proof (Mirage Arena, "Day of Reckoning", bench name8–name10)

Arena entry mapped on the way (queue item 15's first screens):
Log In terminal → New Event → Arena Mode (the mode list's
cursor starts there) → event screen → Down Down → OK → walk
onto the sunken pad at the lobby's middle (0, ~0; the field
prompt lights "✕ Ready") → Cross → START Begin. The event
fields escalating waves; every spoken name was cross-checked
against the actor census of the same moment's RAM dump and the
screenshots:

- m01vs00 Flood ×5 (25 HP, scene ids 32/35/37/39/43 — the
  instance spread again) + m03vs00 Bruiser (75 HP, id 41):
  "Locked on: Flood.", "Flood. Health 25 of 25. 2 away."
- m02vs00 Scrapper ×6 (50 HP): "Locked on: Scrapper.",
  "Scrapper. Health 50 of 50. 5 away."
- m04vs00 Red Hot Chili ×2-3 (25 HP): named at lock and query.
- m06vs00 Thornbite ×3 (60 HP): "Thornbite. Health 1 of 60.
  4 away." alongside the sound-first "Foe nearly gone."
- m03vs00 Bruiser packs (three at once in later waves):
  "Bruiser. Health 16 of 75. 3 away." with the reticle on the
  Bruiser in cmb10.png and the census block at exactly 16/75.
- m05vs00 Monotrucker (55 HP) and m09vs00 Archraven ×4
  (60 HP): "Locked on: Monotrucker.", "Archraven. Health 60
  of 60. 10 away."

Seven types live-proven (m01, m02, m03, m04, m05, m06, m09);
the other 18 table entries rest on the journal mapping alone
(same mechanism, same key) and are listed as such. A lock taken
in the first frames of a wave's spawn can still announce the
plain "Locked on." — the code read fails closed while the actor
settles; the name is there on the next query or re-lock.

### Limits — what stays unnamed, on purpose

- Boss ('b') codes: the journal's boss entries (0x9A Mad
  Treant … 0xA4 Trinity Armor) are sequenced, not keyed by the
  b-number, and character bosses have no Unversed entry at
  all. No b-code resolves. (The event above closed with a boss
  object, b10vs00 at 1200 HP — read by health/distance only.)
- The Jellyshade variants m27/m28/m32 and the arena Heartless
  m40/m41/m42 have no journal entry of their own keyed to their
  number — unmapped rather than guessed.
- Host tests: 436 checks (11 new: the four named surfaces,
  lock-edge naming, same-type bystander, variant-suffix and
  vs-infix names, boss/variant/range/garbage negatives);
  strength floor raised 425 → 436.

## Multi-enemy awareness — the count and the roster (2026-10-25, bench khbbs-multi1)

The foe readout answers for ONE foe. In a pack fight the player
also needs: how many foes are there, and where are the others?
Built query-driven only, per the sound-first doctrine: battles
are high-action, so nothing here narrates. Three surfaces:

- The field line (and so Where Am I) appends " N enemies."
  when two or more validated foes are in the fight.
- The foe query: locked, it appends " N other enemies.";
  unlocked, it answers "Not locked on. N enemies." when any
  foe is present (the plain refusal stands when there are none).
- A new command, **Enemies** (appended to the ABI as value 43;
  iOS button "Enemies", symbol person.3; Android constant and
  generated library data in step), speaks the roster: a count
  line, then each foe nearest-first — "Bruiser, 4 away,
  2 o'clock, locked on." Bearings are clock positions from the
  movement-derived facing when it is live, and are omitted —
  never invented — when it is not, or when the foe is too close
  to call (< 1 unit of ground offset). At most 8 entries are
  spoken, then "And N more."

### Enumeration: the census

One pass over 0x09800000..0x0A000000, one signature read per
word (2,097,152 reads — the same order as LocateLockRecord's
proven sweep, and it runs **at query time only**, cached for
1.5 s, never on the frame path; the tracked-foe beep logic is
untouched and never pays for it). A word is a scene object when
the signature 0x00011000 sits at obj+0x08; it is a **foe** when
all of these hold:

- the object code at obj+0x88 has the strict code shape and an
  'm' or 'b' prefix (the player is 'p', props/NPCs are 'g'/'n',
  weapons 'w' — the lobby's transporter rings, the Log In
  terminal and the player's own keyblade all fall out here);
- the scene id at obj+0x00 is a small index (≤ 100000);
- the HP pair at (obj−0x1B8)+0x380/+0x382 is sane: max in
  10..20000 and 0 < cur ≤ max (the dead, the not-yet-spawned,
  the 0-HP props and the boss's 1-HP keyblade fall out here).

Measured cost: 17–26 ms per full census on the bench host
(whose reads go through the emulator's debug interface; the
shipped host reads memory directly). The 1.5 s cache means a
burst of queries — roster, then foe, then Where Am I — pays
for one sweep in the app.

### A foe's position: the bone palette, candidate 3

No fixed offset holds a foe's position (the +0x8F0 word that
works for some foes reads zero or garbage for others — the
three Bruisers in the cmb5 dump, while both Chilis beside them
were exact). What every scene object does embed is a palette
of bone matrices. A well-formed candidate in obj+0x400..
obj+0xC00 is a translation row (x, y, z, exactly 1.0f) preceded
within 0x30 by three rotation rows of unit length (±0.03) with
w = 0. Validated over the name8/9/10 dump corpus: 175 foe
instances, 153 (87%) carry at least four candidates; against
the 20 locked foes whose true position the lock record gives,
candidate **index 3** (ascending offset) matched exactly 17
times, no other index matched ever, and 3 had no well-formed
palette at all (a Scrapper mid-spawn, a flying Archraven, a
Thornbite mid-death at 1 HP). The roster therefore takes
candidate 3 or claims **no position**: the foe is still counted
and named, its entry just carries no distance, and unpositioned
foes sort after positioned ones. Distances are 3D from the
player's engagement-node position — ReadFoe's convention.

### No wave event, on purpose

A latched "new wave" announcement was considered and rejected:
(a) it would put the census sweep on a frame cadence — the
cost this design exists to avoid; (b) a foe's death is already
carried by the hit beeps and milestone speech, and the game
sounds spawns itself; (c) the count is one button press away
and always current when asked. Deaths are never double-reported.

### Live proof (Mirage Arena "Day of Reckoning", bench khbbs-multi1)

From the name10 bench's mid-fight state; every spoken line was
checked against an independent re-implementation of the census
over the same moment's RAM dump (multi_census.py), plus the
screenshots:

- Unlocked: "6 enemies." — three Red Hot Chilis at census
  distances 8.70 / 12.37 / 12.61 spoken as 9 / 12 / 13 away;
  three Bruisers counted and named with no distance (no
  readable palette at that instant — fail-closed, as offline).
  Foe query "Not locked on. 6 enemies."; Where Am I "… 6
  enemies."
- Locked (the nearest Chili): roster "Red Hot Chili, 9 away,
  locked on."; foe query "Red Hot Chili. Health 25 of 25.
  9 away. 5 other enemies."; the lock query agrees at 9 away.
- After ~40 fight iterations and a wave change: "7 enemies." —
  Bruiser at 5.77 spoken 6 away, three Archravens at
  12.04 / 12.49 / 15.84 spoken 12 / 12 / 16 away, with clock
  bearings (2 / 1 / 5 / 8 o'clock — the facing was live after
  the fight's movement), three more foes named without
  distance; Where Am I agrees: 7 enemies. The screenshot shows
  an Archraven in Terra's face, mid-fight.

### Limits

- The census is query-time truth, cached up to 1.5 s: asked in
  a burst, the surfaces agree with each other; a foe dying
  between the sweep and the speech is possible in principle.
- ~13% of foe instances (corpus measure) have no readable
  palette at a given instant — mid-spawn, flying, mid-death —
  and speak name-only. In the live pack above, half the roster
  was name-only at the first ask; positions came and went as
  the fight moved. The count never depends on positions.
- Roster order is by distance among positioned foes; a
  positionless foe that is actually nearest still sorts last.
  Its existence and name are never hidden.
- Census cap 16 foes; roster speaks at most 8 entries plus
  "And N more." No fight observed comes close (7 is the most
  seen, in the arena event above).
- Names follow the foe-name rules: journal-proven types only;
  bosses and unproven codes count and read as "Enemy".
- Host tests: 449 checks (13 new: planted scene objects —
  pack composition, prop/dead/broken-code/player exclusions,
  roster order and the locked mark, the count on all three
  surfaces, clock bearings after a movement leg, the
  no-position fail-closed entry, the empty field); strength
  floor raised 436 → 449. The full adapter gate, the Swift
  ABI mirror (43 Enemies) and the Android launcher contract
  (enum now 44) all pass; the Android library was regenerated
  with the Enemies button and title.

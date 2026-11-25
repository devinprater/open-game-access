# Mortal Kombat Trilogy (PlayStation, USA, SLUS-00330)

Reader: `Core/mkt_adapter.cpp`, map: `Core/mkt_adapter.h` (the shipped map is the table below).
Host test: `Core/mkt_adapter_test.cpp` (`scripts/mkt-adapter-test.sh`). Measured 2026-11-24 and 25
on the owner's own copy of the disc, run headless in the repo's PCSX ReARMed core with
`tools/ps1-map-harness.cpp`. The disc and BIOS were read from where they live and never copied
into the repo.

## How it was measured

`scripts/ps1-map-harness.sh DISC.cue BIOSDIR OUTDIR SCRIPT` boots the disc, plays a script of pad
presses (`tap`, `hold`, `run`), and writes named 2 MiB RAM images (`dump`), screenshots (`shot`),
and whole-machine states (`save`/`load`). `tools/ps1-ramdiff.py` and a few throwaway scripts then
diffed the images. The method for every address was the same:

1. put the game in a known state a few times (cursor on each item, a hit taken, a round lost);
2. find the addresses whose value follows the state, in the images from those states;
3. confirm the survivor in *other* images (a different fighter, another arena, other menus) and
   against the screenshot taken at the same frame;
4. only then put it in the map.

The harness can also attach the real reader to the live RAM (`adapter on`, `cmd where`, `cue`) and
print everything it says with its frame number. That run, through the title screen, main menu,
character select, a whole fight (round start, hits, three milestones, a lost round, round 2) and
the pause, is what the speech list below was checked against. The HLE BIOS was used (the core
found no BIOS in the folder it was given, and the game boots on it).

All addresses are KSEG0 (`0x80......`), little-endian. "N states" counts the saved RAM images the
address was checked in.

## The shipped map

| Field | Address | Value | Confirmed |
| --- | --- | --- | --- |
| screen id, byte A | `0x800283BC` | main menu 2, options hub 1, Game Konfigure 11, character select 33, fight 29, continue 0 | 30+ states, six screens |
| screen id, byte B | `0x80028408` | 2, 4, 11, 5, 29, 0 (same order) | same states |
| main menu cursor | `0x80032662` | 0 Mortal Kombat, 1 2 on 2, 2 8 Player, 3 Options | 7 states; stays on 3 after you leave through Options, so it is only read on screen 2 |
| options hub cursor | `0x80033638` | 0 Game, 1 Kontrol, 3 Sound and Music, 4 Exit (the "?" icon is skipped when moving, so 2 was never seen) | 9 states. `0x800334C6` and `0x800334CA` held the same values |
| Game Konfigure row | `0x80033676` | 0 to 6 seen, rows in screen order | 7 states, the only match. Row 11 (Exit) is inferred from the row count |
| difficulty | `0x800324A4` | 1 easy, 2 medium, 3 hard | changed with the setting in 3 states. 0 and 4 are inferred from the game's list (VERY EASY, EASY, MEDIUM, HARD, VERY HARD at `0x800F8CDC`) |
| character select slot | `0x80033ACE` | 0 to 29 seen | 30+ states |
| slot to fighter table | `0x800FA8D6` | 32 halfwords: 23, 29, 8, 1, 28, 26, 0, 18, 4, 22, 19, 2, 7, 13, 16, 11, 9, 14, 3, 6, 20, 21, 5, 10, 32, 25, 24, 30, 31, 15, 12, 17 | found by searching RAM for the slot to fighter pairs measured below; 14 slots |
| fight flag | `0x80032478` | 1 in every fight image (inc. between rounds and paused), 0 on every menu, select, ladder, loading and continue image | 113 fight images |
| P1 / P2 fighter id | `0x80032198` / `0x80032310` | 0 Kano, 1 Sonya, 2 Jax, 3 Nightwolf, 4 Sub-Zero, 5 Stryker, 6 Sindel, 7 Sektor, 8 Cyrax, 9 Kung Lao, 10 Kabal, 11 Sheeva, 12 Shang Tsung, 13 Liu Kang, 14 Smoke, 15 Kitana, 16 Jade, 17 Mileena, 18 Scorpion, 19 Reptile, 20 Ermac, 21 Sub-Zero (a second one), 23 Noob Saibot, 26 Rain, 28 Rayden, 29 Baraka, 31 Johnny Cage | each id read off the on-screen health bar name in a fight (17 fighters). Not seen: 22, 24, 25, 27, 30, 32 |
| P1 / P2 health | `0x8003219E` / `0x80032316` | halfword, full = 166 (`0xA6`), 0 = empty | hits, KOs and the refill, two fights, two characters. Matches the published GameShark codes for the v1.0 pressing |
| P1 / P2 round wins | `0x800321A4` / `0x8003231C` | word | P2's went 0, 1, 2, 0 over a whole lost match. P1's is the same struct 0x178 earlier and was never seen to move (the idle fighter never won a round): UNCONFIRMED |
| P1 / P2 object pointer | `0x8003218C` / `0x80032304` | pointer into main RAM | the two fighter objects swap slots between fights (`0x800293E8` and `0x8002939C`), so positions are read through these. Seen both ways round |
| x position | object + `0x12` | signed halfword, whole units (the high half of a 16.16 fixed-point word at +0x10) | followed the fighter in 113 images against screenshots. Larger x is further right |
| arena gap | | near 20, far 250 | smallest gap seen 13, median 130, largest 247 |
| pause flag | `0x800327A4` | 1 while paused | 12 states, 0 in 30+ others |

### Fighter names

Ids 0 to 20, 21, 23, 26, 28, 29 and 31 were each read off a health bar. The game's own
"`<NAME> WINS`" string table (`0x80013A68`) runs two entries behind the ids from 23 up (Noob Saibot
is its entry 21 but id 23), which gives the unseen ones: 24 Motaro, 25 Shao Kahn, 27 Chameleon,
30 Kintaro, 32 Goro. Those five are **inferred, not seen**. Id 22 is unknown and is spoken as
"Fighter 22". Id 21 shows the name SUB-ZERO on the bar, like id 4.

### Other tables found in RAM (not used yet)

| What | Address |
| --- | --- |
| `<NAME> WINS` strings, then DRAW, FLAWLESS VICTORY, DOUBLE FLAWLESS | `0x80013A68` |
| arena names, `KOMBAT ZONE: ...`, 19 of them | `0x800FA992` |
| main menu words (MORTAL, KOMBAT, 2 ON 2, 8 PLAYER, OPTIONS) | `0x800F846A` |
| Game Konfigure labels and the value words (ON, OFF, ENABLED, ANYONE - LOAD ...) | `0x800F8C39` |
| pause menu words: PAUSED, CONTINUE, QUIT, QUIT GAME?, YES, NO | `0x8001037B` |

## What the reader speaks

- **Main menu.** "Main menu. Mortal Kombat." then each box as the highlight moves.
- **Options.** "Options. Game Konfigure." then each icon.
- **Game Konfigure.** "Game Konfigure. Difficulty, Medium." then each row, and the difficulty again
  when it changes. Rows are only read after arriving from the options hub, because the "choose your
  destiny" ladder shows the same screen id.
- **Character select.** "Choose your fighter. Noob Saibot." and then each fighter as the cursor moves.
- **Fight.** "Round 1. Sub-Zero versus Sindel." at the start; a pitch-mapped beep on every hit (foe on
  the right, you on the left, MKU's pitch range), with "Your health 75 percent" and the other
  milestones at 75, 50, 25 and nearly gone; the closeness beacon (MKU's fight-proximity cue,
  silent while paused); "Foe on your left." when the foe crosses over; "Foe wins the round. 0 to 1.",
  "You win the round. 1 to 1.", "Jade wins the match."; "Round 2." after the refill; "Paused." and
  "Resumed."; "Continue? Press start." after a lost match.
- **Where Am I / Status / Menu state.** The screen and cursor, or in a fight the round, both
  health bars and where the foe is. Dump writes the map state and a text scan to the reading log.

## Not confirmed

- **P1's round-wins counter** (`0x800321A4`), by symmetry with P2's. The reader's "You win the round"
  line depends on it. The match-end line for a win says "You win the match."
- **Ids 24, 25, 27, 30, 32** (names inferred), **id 22** (unknown).
- **The pause menu.** Start gives only PAUSED. **Select** while paused opens CONTINUE / QUIT, and
  QUIT opens "QUIT GAME?" with NO / YES (seen on screen). Their cursor was not found: the only
  things that changed with it were sprite colour records (`0x80029F0C` and its neighbours, 38 and
  11 swapping), not a row index. Not spoken.
- **Game Konfigure values other than the difficulty.** Left and right did nothing on the other
  rows in the harness (Blood, Timer, and so on), so their value addresses were never found. Their
  words are in RAM (`0x800F8C39`).
- **Hub icon 2 ("?")** was never reached by the cursor, so its index is a guess; it is called
  "Secret Konfigure", after a string in RAM.
- **The round timer.** Not found. It counts down about once per 33 frames from 99; it is not a
  decreasing byte, halfword or word at 70-frame spacing, nor BCD. Needs a disassembly (Ghidra on
  the game executable) to find where the HUD reads it.
- **The second player** (versus mode): the harness has only pad 1, so P2's character select cursor
  and a second human were never seen. The P2 fighter id, health, wins, and object are confirmed
  as the foe in a one-player fight.
- **Fatality, "Finish him", flawless and draw lines.** The strings exist (see tables); the events
  were not mapped.
- **A second pressing.** The published v1.1 GameShark codes put P1 health at `0x80032146` and
  fighter ids at `0x800282E2`/`0x8002832E`. This disc is the v1.0 layout (health matched v1.0's
  `0x8003219E`). A v1.1 disc would read wrong values; the fight flag and health gate would likely
  fail closed (health above full is refused), but it has not been run.

## Candidates for later

- A mapping run with Ghidra on `SLUS_003.30` for the timer, the pause menu cursor, the 2P cursor.
- Arena name on character select / the ladder (the table above).
- Round timer, winning streaks and the continue countdown, once mapped.

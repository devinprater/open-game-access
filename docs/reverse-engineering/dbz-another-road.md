# DBZ: Shin Budokai — Another Road (PSP) — reader investigation

**Status: DECOMPILE WORKING. MENU/TITLE/SHOP MODULES LOCATED + DECOMPILED.
TASK SYSTEM UNDERSTOOD. NO LIVE PROBING YET.** A menu reader is now a
bounded task; it is not built.

## 1. Identity (verified, not from the filename)

| | |
|---|---|
| File | `Dropbox/Games/PSP/Dragon Ball Z - Shin Budokai - Another Road (USA).iso` (505 MB) |
| PARAM.SFO | `ULUS10234`, v1.00, `DRAGON BALL Z SHIN BUDOKAI ANOTHER ROAD` |
| PPSSPP save | `ULUS102340000` (Sept 13 — prior play) matches |
| EBOOT.BIN | 3,134,272 bytes, magic `~PSP`, tag string `DBZP_0039` = encrypted PRX |
| EBOOT.dec | 3,133,933 bytes, MIPS ELF32 (`pspdecrypt`, tag C0CB167C type 1) |
| Ghidra | project `C:\Users\Devin Prater\oga-ghidra-dbzar`, program `EBOOT.dec`, **6,551 functions**, analysis **43 s** |

Pipeline that works (same as Steins;Gate, so reuse it):
ISO list/save via `scripts/oga-iso-extract.py` → `pspdecrypt -o EBOOT.dec
EBOOT.BIN` (WSL, `$HOME/pspdecrypt-src`) → `analyzeHeadless` import +
analysis → Java post-scripts (`scripts/DbzArQuery.java`,
`scripts/DbzArDecomp.java`, `scripts/DbzArDecomp2.java`).

⛔ Ghidra-flag lessons (all hit on this binary, all read as Ghidra bugs):
- There is NO `-saveProject` flag in 12.x (`Bad argument`).
- `-recursive` requires an explicit depth (`Invalid recursion depth: null`);
  drop it when using `-process <file>` directly.
- Project names collide case-insensitively on Windows: creating `DbzAr`
  when `DBZAR` exists fails with `LockException: Unable to lock project`.
  Reuse the exact existing name.
- `Found conflicting program file in project: /EBOOT.dec` means a previous
  run already imported it — switch to `-process` (analyze), don't re-import.
- `imageBase=00000000`: file offsets, NOT runtime addresses. PSP game code
  loads at `0x08804000` (Steins;Gate measurement), so runtime ≈ file + base.
  VERIFY live before trusting any address (see §6).

## 2. Module map (from the binary's own log tags)

28 distinct `[XX]` tags. The ones that matter for a reader:

| tag | hits | meaning |
|---|---|---|
| `[MENU]` | 3 | menu module: MESSAGE / UPDATE / DRAW |
| `[TITLE]` | 2 | title screen: UPDATE / DRAW |
| `[SHOP]` | 2 | shop: CURSOR / MONEY |
| `[SYS]` | 11 | system incl. `[SYS] AUTO SAVE` |
| `[AR]` | 27 | Another Road story mode |
| `[BTL]` | 3 | battle |
| `[SCR]` | 16 | script |
| `[SND]`/`[AR]BGM`/`[SCR] BGM`/`[UTL] BGM` | — | sound/music |
| `[NET]`/`[LOBBY]` | — | adhoc multiplayer (skip) |
| `[CARD]`/`[DECK]`/`[SLOT]`/`[ALBUM]`/`[EDIT]`/`[PAUSE]` | — | card/album systems, pause |

## 3. The task system (the key architectural find)

Everything UI is a **named task with callback slots**:

```c
// create: returns task handle
FUN_001408a8(name, id, 4, 0) → FUN_00141698(0, name, id, 4)
// attach callback: writes func at handle+0x20
FUN_00140a30(handle, func) { if (handle) *(handle + 0x20) = func; }
```

Each module registers MESSAGE / UPDATE / DRAW handlers the same way.

## 4. Located + decompiled functions (file offsets; add load base)

**Menu module** — state struct at file-offset `0xC139C`:
- `FUN_000e1afc` (init, 512 bytes): registers `[MENU] MESSAGE`→`FUN_000e2374`,
  `[MENU] UPDATE`→`FUN_000e2468`, `[MENU] DRAW`→`FUN_000e28b8`. Takes a menu
  id in `param_1` (hi16 → `0x34670`, lo16 → `0x3466c`); `0xFFFFFFFE` = default.
  Screen vtable pointer at `0x34660`.
- `FUN_000e2374` (message pump, 244 bytes): walks a **linked list at
  `[0xC139C+100]`**, dispatching on `node[2]-0xC`: 1=init?, 2/3/4=store
  `node[3]` at `+0x74/+0x78/+0x7C`, else reset + re-register `FUN_000e2604`.
  **This queue is a prime live-probe target.**
- `FUN_000e2468` (update, 188 bytes): calls `FUN_000e3c88/2d70/2b88/31e0`,
  then dispatches the screen vtable `(*(*0x34660+0xC))(obj,2,0)`, then
  **re-registers `FUN_000e2524` as the next update** (state-machine chain).
- `FUN_000e28b8` (draw, 180 bytes): returns early unless byte at
  `0xC139C+0x81` is nonzero (**visibility flag**); reads **screen id at
  `0xC139C+0x70`** (0xE/0x13/0x14 skip two draw calls); calls screen vtable
  `+0x14` draw; ends with `FUN_000e2ea8(0xf0,0xe6,0xffff,0x1b8)`.

**Title module** — `FUN_00120d40` (336 bytes): registers `[TITLE]
UPDATE`→`FUN_00120ffc`, `[TITLE] DRAW`→`FUN_00121698`. Same task shape.

**Shop module** — `FUN_000ebbb8` (672 bytes): allocates 0x5064-byte state,
clamps money display at 999,999,999, registers `[SHOP] CURSOR`→`FUN_000ebec8`,
`[SHOP] MONEY`→`FUN_000ebfec`.

## 5. Message/data archives (static inventory)

`data_sys_us.afs` (81 MB, 609 files): 360×`#AMB`, 115×`#AMT`, 38×`!FLD`,
32×`#SPX`, 22×`#MG`, 18×RIFF, **8×`#MSG`**, 5×`#MAP`, 1×PSMF (20 MB movie).

`#MSG` format: magic, `0x14`, u32=3, u32=0, u16=1?, **u16 count at +18**,
offset table at `0x1C`. Decoded **457 message IDs** (saved at
`%LOCALAPPDATA%\Temp\dbz-ar-extract\dbz-msg-all.txt`):
`MSG_AR_CITY_*`, `MSG_AR_CLEAR_*`, `MSG_AR_FIELDPLAY_*` (268),
`MSG_FIELD_*`, `MSG_CHARACTER_ID_*` (28), `MSG_AR_MISSION_*`,
`MSG_AR_FRIEND_SENZU`/`MSG_CMT_*` (103).
⛔ These tables hold IDs, NOT display text.

22 `#MG` configs map the story content set: `ev_004_XX.spx` scripts paired
with `MSG_AR_004_XX.msg` message sources (build paths `../../AR_SCR/SPX`,
`../../AR_MSG/JP`). Display text is NOT plaintext ASCII in `data_sys_us.afs`
or `data_sys_cmn_pic.afs` (132 MB, 564 files: 535×`#AMT`, 29×RIFF; only hit
is `GOKU` inside compressed bytes) — it sits inside `#AMT` containers or
compiled `.MGB`s. The decompile (§4) shows the runtime path; follow
`sceLibFont` usage + the MSG-ID→text resolver rather than unpacking 535
containers blind.

## 6. Next session, in order

1. Boot ULUS10234 in PPSSPP (`C:/Program Files/PPSSPP/PPSSPPWindows64.exe`;
   debugger already enabled in `ppsspp.ini`) with the proven
   `scripts/psp-probe.mjs` harness. — DONE, see §7.
2. Confirm the load base. — DONE: `0x08804000` (§7).
3. Live-poll the message queue at `[base+0xC139C+100]` and the screen vtable
   at `[base+0x34660]` across menu transitions. — PARTLY DONE: main-menu
   cursor found another way (§7); queue/vtable still unprobed.
4. Resolve the MSG-ID→text path (MGB loader) to turn IDs into speakable labels.
5. Do NOT commit ISO-derived extracts; Temp only (no-ROMs rule).

## 7. LIVE RESULTS — main menu + options SOLVED (2026-09-17)

Harness fixes this session:
- `psp-probe.mjs press` sent `{button:[b]}` (array); PPSSPP 1.20.4 rejects it
  (`Invalid 'button' parameter type`). Fixed to plain string (same shape as
  the proven `psp-input-check.mjs`). `psp-walk.mjs` passes strings through —
  unaffected.
- `vision_analyze` rejects `.ppm` — convert to PNG first (PIL; captures can
  be truncated, use `LOAD_TRUNCATED_IMAGES`).
- Vision misreads the highlight TWICE when the 7th item is selected (attributed
  it to Network Battle, then Profile Card). Fix: crop the right-side stack 2x
  and ask which row has the white border. **RAM wins over pixels when a
  verified address contradicts vision.**

Findings (all screen-verified with screenshots):
- Boot path: PRESS START (Cooler attract) → title logo → in-game Load screen
  (Sept 13 save found) → cross → Load confirm → cross → Main Menu.
- **Main-menu cursor: u32 at `0x08C36F98`** (heap, NOT the decompiled
  `0xC139C` struct — that module drives submenus, not the main menu).
  Mapping: 0=Another Road, 1=Arcade, 2=Z Trial, 3=Network Battle, 4=Training,
  5=Profile Card, 6=Options(? — 7th item, label cut off at frame bottom).
- Verified 0→1→2 (dumps), exact 3,2,1,2,3 track across a 5-dump up/up/down/down
  sweep (sole matching word in 24 MB), fresh-transition checks 3→4 (Training
  on screen) and 6→5 (Profile Card crop-confirmed).
- **Same address drives the Options submenu**, reset to 0 on entry, cursor
  persists per screen (main still 6 after returning from Options).
- Options items: 0=Assig[n…] (truncated), 1=Sound, 2=Save/Load, 3=Connection
  Style, 4=Screen Display, 5=Voice Select (toggle English↔Japanese via
  left/right; left voice row). Left user's setting on English.
- ⛔ `psp-watch.mjs` event timestamps mark press COMPLETION (it awaits the
  hold), so changes appear to "lead" presses by ~hold time and get attributed
  to the preceding `wait`. Nearly caused a true cursor (0x08C36F98) to be
  discarded. Trust multi-dump exact tracks over watch attribution; or subtract
  the hold duration when reading watch output.
- Profile: Nickname G, Power Level 10000, Money 0.

## 8. LIVE RESULTS — training mode + battle HUD decompiled (2026-09-17)

Flow: main Training → character select (Goku [Normal] for PLAYER) → Game
Settings (STAGE=Mountains, Rounds 2, Time ∞; focus cycles STAGE→Rounds→Time,
cross advances fields, START does nothing) → battle, Goku mirror.

**Training pause menu** (START): Continue / COM Action / COM Level / Def. Ki
Blast Wv / Counterattack / Break-fall / Display / Assign buttons / Camera
Angle. COM Action values cycled: Do Nothing → Guard 1 → … → Practice →
**Match** (Match greys out the defensive rows = real fight). COM Level Weak.
Display is single-valued (`Status & Co…`, no numeric mode). Left COM on
**Match** — live opponent for next session.

**HUD layout** (P1 left, P2 right): ki pips (small green squares) / long
green HP bar with yellow recent-damage section / portrait + `Goku ®` /
blast-stock slashes + big count / light-blue aura bar / ∞ timer center /
`MAX HITS` counters bottom.

**Decompiled battle HUD renderer: `FUN_000dbd68`** (1136 bytes, caller
`FUN_000dadfc`): reads battle globals at EBOOT vaddrs `0x9F450` (timer
ticks, mm:ss via /0xE10 /0x3C), `0x9F454` (hits, `%02dHIT`), `0x9F456`
(damage, `%05ddmg`), and per-fighter HP as **short at
`[fighter_ptr+0x14]+0x10`** (bar width = HP×0.571), fighter pointers at
`0x341E8/0x341F0`. Related: `[SYS] HITSTOP`→`FUN_000627ac`,
`[PLY] HIT EFCT`→`FUN_000868c4`.

**⛔ Overlay architecture (read before trusting an EBOOT absolute):**
`%05ddmg`/`%02dHIT` exist ONLY in EBOOT rodata (base `0x08804000`), yet
EBOOT-global reads at runtime give garbage — battle/menu code runs from
**overlays that swap per game-state** (at menu time a 2nd `[MENU] MESSAGE`
copy sat at `0x09AF18A0`; in battle that region holds battle-overlay code).
Struct-RELATIVE offsets from the decompile (+0x14/+0x10, +0x20 callback
slot) are the stable part (P4G lesson); absolute EBOOT data addresses are
not. Next: find the battle overlay's own globals/table, not EBOOT's.

**Training-regen rule:** HP visibly regenerates (P1 ~20% → near-full minutes
later; yellow recent-damage section refills). Any HP diff must be
hit-tight (dump within seconds of a verified `N dmg` popup) or it drowns in
regen. Transformations change the game too — the Match COM went Super
Saiyan mid-fight (max-HP shift suspect for the wild value jumps).

**Enemy-attack data:** COM in Match mode dashes, combos (P1 ate a full
beating standing still), transforms. Transformations are a trackable event
class for cues.

**Not yet found:** live HP words, blast-stock word (candidate `0x08AE5570`
disproven: decayed 3→2→2→0 = countdown, while HUD showed 3–6), ki words,
positions, story-mode map/enemy tracking. Button bindings screen unopened
(down+cross from Display backed out to gameplay instead).

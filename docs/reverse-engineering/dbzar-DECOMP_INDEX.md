# DECOMP_INDEX.md — DBZ: Shin Budokai — Another Road (PSP, `ULUS10234` v1.00)

Address → meaning map for the Ghidra decompilation. **Search this first** before touching RAM.
`size`/`callers` are measured from the Ghidra project (`function-index.txt`); meanings cite the notes.

> **Placeholder resolved.** The workspace directive referenced `<DECOMP_PATH>` / `<ISO_PATH>`
> literals. No such paths exist. The real artifacts are in §1.

**Status: DECOMPILE WORKING. Menu/title/shop modules located + decompiled. Task system understood.
Main menu + options SOLVED live. Battle HUD decompiled. Not built.**

---

## 1. Paths and entry points (the real `<DECOMP_PATH>`)

| what | path |
|---|---|
| **PRIMARY decomp workspace** | `C:\Users\Devin Prater\oga-ghidra-dbzar\` (project name **`DBZAR`**) |
| program inside the project | **`EBOOT.dec`** (MIPS ELF32) |
| machine-readable function dump | `...\oga-ghidra-dbzar\function-index.txt` (**6,551 functions**) |
| ISO (runtime verification only) | `Dragon Ball Z - Shin Budokai - Another Road (USA).iso` (505 MB) |
| EBOOT.BIN | 3,134,272 bytes, magic `~PSP`, tag `DBZP_0039` (encrypted PRX) |
| EBOOT.dec | 3,133,933 bytes (`pspdecrypt`, tag C0CB167C type 1) |
| notes | `~/oga-work/docs/reverse-engineering/dbz-another-road.md` |
| PPSSPP save | `ULUS102340000` |

### ✅ The load base: RESOLVED — **`0x08804000`** (re-derived live 2026-10-06)

The two docs conflicted; this was settled by measurement, not by preferring one.

**Derivation (the reliable method):** five strings whose ELF vaddr is known were located in a
24 MiB live RAM dump of the running game; `base = RAM_addr - vaddr`:

| string | ELF vaddr | found at (RAM) | ⇒ base |
|---|---|---|---|
| `%05ddmg` | `0x001A3D30` | `0x089A7D30` | `0x08804000` |
| `%02dHIT` | `0x001A3D24` | `0x089A7D24` | `0x08804000` |
| `[SYS] AUTO SAVE` | `0x001A4EE8` | `0x089A8EE8` | `0x08804000` |
| `data_sys_us` | `0x001A4598` | `0x089A8598` | `0x08804000` |
| `[TITLE]` | `0x001A4F40` | `0x089A8F40` | `0x08804000` |

**All five agree.** `base = 0x08804000`, 4-byte aligned. So `RAM = 0x08804000 + ELF_vaddr` for this
build — the same arithmetic as Steins;Gate, but **that is a measured coincidence here, not a rule
to inherit**: derive it per build.

⛔ **The Tag Team doc's aside that "Another Road needed a derived base (`0x0898036A`)" is DISPROVEN.**
Five independent strings place the base at `0x08804000`; `0x0898036A` appears exactly once in the
notes, in a parenthetical, **with no derivation recorded**, and it is **not 4-byte aligned**
(`0x0898036A % 4 == 2`) — so it cannot be a module load base at all. The corrected value is
`0x08804000`. (Annotated in the Tag Team doc too.)

Derived runtime addresses: menu state struct **`0x088C539C`** (file-offset `0xC139C` + base), screen
vtable register **`0x08838660`** (file-offset `0x34660` + base).

---

## 2. How to query the decomp (the pipeline, verified)

```
.iso --extract PSP_GAME/SYSDIR/EBOOT.BIN--> '~PSP'
     --pspdecrypt -o EBOOT.dec--> MIPS ELF32
     --analyzeHeadless (43 s)--> DBZAR / EBOOT.dec  (6,551 functions)
```

- `run-index.bat` (in the project dir) regenerates `function-index.txt`.
- Per-report scripts: `scripts/DbzArQuery.java`, `DbzArDecomp.java`, `DbzArDecomp2.java`,
  `DbzArBtl.java`, `DbzArBtl2.java`.

⛔ **Ghidra-flag lessons** (all hit on this binary, all *read* as Ghidra bugs):
- There is **no `-saveProject` flag in 12.x** (`Bad argument`).
- `-recursive` requires an explicit depth (`Invalid recursion depth: null`); drop it with `-process`.
- Project names collide case-insensitively on Windows — creating `DbzAr` when `DBZAR` exists fails
  with `LockException: Unable to lock project`. **Reuse the exact existing name.**
- `Found conflicting program file in project: /EBOOT.dec` means a previous run already imported it —
  switch to `-process`, do not re-import.
- `imageBase=00000000` ⇒ **file offsets, NOT runtime addresses**.
- ⛔ **The code runs from OVERLAYS that swap per game-state.** `%05ddmg`/`%02dHIT` exist only in
  EBOOT rodata, yet EBOOT-global reads at runtime give garbage — at menu time a 2nd `[MENU] MESSAGE`
  copy sat at `0x09AF18A0`; in battle that region holds battle-overlay code. **Struct-relative
  offsets from the decompile are the stable part; absolute EBOOT data addresses are not.**

---

## 3. Function index — by system

Format: `FUN_addr  size  callers  meaning`.

### 3.1 The task system (the key architectural find)

Everything UI is a **named task with callback slots**:
```c
FUN_001408a8(name, id, 4, 0) → FUN_00141698(0, name, id, 4)   // create, returns task handle
FUN_00140a30(handle, func) { if (handle) *(handle + 0x20) = func; }   // attach callback
```

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_001408a8` | 44 | 162 | **task create** wrapper |
| `FUN_00141698` | 528 | 1 | task create implementation |
| `FUN_00140a30` | 16 | 420 | **attach callback** — writes func at `handle+0x20` |

Each module registers MESSAGE / UPDATE / DRAW handlers the same way.

### 3.2 Menu module (state struct at file-offset `0xC139C`; screen vtable at `0x34660`)

| function | size | callers | meaning |
|---|---|---|---|
| `FUN_000e1afc` | 512 | 1 | **menu init** — registers `[MENU] MESSAGE`→`FUN_000e2374`, `UPDATE`→`FUN_000e2468`, `DRAW`→`FUN_000e28b8`; takes a menu id (`hi16→0x34670`, `lo16→0x3466c`), `0xFFFFFFFE` = default |
| `FUN_000e2374` | 244 | 0 | **message pump** — walks a **linked list at `[0xC139C+100]`**, dispatching on `node[2]-0xC` (2/3/4 store `node[3]` at `+0x74/+0x78/+0x7C`); else reset + re-register `FUN_000e2604`. **Prime live-probe target.** |
| `FUN_000e2468` | 188 | 0 | menu **update** — calls `FUN_000e3c88/2d70/2b88/31e0`, dispatches the screen vtable `(*(*0x34660+0xC))(obj,2,0)`, re-registers `FUN_000e2524` |
| `FUN_000e28b8` | 180 | 0 | menu **draw** — early-out unless byte at `0xC139C+0x81` (visibility); reads **screen id at `0xC139C+0x70`** (0xE/0x13/0x14 skip two draws); ends `FUN_000e2ea8(0xf0,0xe6,0xffff,0x1b8)` |
| `FUN_000e2604` | 256 | 0 | next-update re-register |
| `FUN_000e2524` | 224 | 1 | state-machine chain (re-register) |
| `FUN_000e3c88` | 72 | 5 | called by the update |
| `FUN_000e2ea8` | 644 | 1 | draw tail |

### 3.3 Title module
| function | size | callers | meaning |
|---|---|---|---|
| `FUN_00120d40` | 336 | 0 | title init (registers UPDATE/DRAW) |
| `FUN_00120ffc` | 116 | 0 | `[TITLE] UPDATE` |
| `FUN_00121698` | 52 | 0 | `[TITLE] DRAW` |

### 3.4 Shop module
| function | size | callers | meaning |
|---|---|---|---|
| `FUN_000ebbb8` | 672 | 1 | shop init — allocates `0x5064` state, clamps money display at 999,999,999 |
| `FUN_000ebec8` | 292 | 0 | `[SHOP] CURSOR` |
| `FUN_000ebfec` | 172 | 0 | `[SHOP] MONEY` |

### 3.5 Battle HUD (decompiled)
| function | size | callers | meaning |
|---|---|---|---|
| `FUN_000dbd68` | 1136 | 1 | **battle HUD renderer** |
| `FUN_000dadfc` | 196 | 1 | the HUD renderer's caller — reads battle globals |
| `FUN_000627ac` | 136 | 6 | `[SYS] HITSTOP` |
| `FUN_000868c4` | 172 | 3 | `[PLY] HIT EFCT` |

The HUD renderer reads: timer ticks at EBOOT vaddr `0x9F450` (mm:ss via `/0xE10` `/0x3C`), hits at
`0x9F454` (`%02dHIT`), damage at `0x9F456` (`%05ddmg`), and per-fighter HP as a **short at
`[fighter_ptr+0x14]+0x10`** (bar width = HP × 0.571), with fighter pointers at `0x341E8`/`0x341F0`.

### 3.6 Module map (from the binary's own `[XX]` log tags — 28 distinct)

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

---

## 4. Live-verified RAM (from the decompile + screen checks)

| what | address | notes |
|---|---|---|
| **menu cursor** | `0x08BA1D18` | u32, **heap** — the selected index. ⛔ **RE-DERIVED 2026-10-06**; the September address `0x08C36F98` is stale: on a fresh boot it read 0 and held texture bytes (the heap is reallocated every boot) |
| **list length** | `0x08BA1D14` | u32 — **the screen discriminator.** 7 on the main menu, 6 in Options. Required, because index 0 is "Another Road" on one screen and "Assign Buttons" on the other |
| main-menu cursor (STALE) | ~~`0x08C36F98`~~ | ⛔ **do not use** — a September-session heap address; reads 0 on a later boot |
| same address | — | also drives the **Options submenu**, reset to 0 on entry, persists per screen |

Main-menu mapping: `0`=Another Road · `1`=Arcade · `2`=Z Trial · `3`=Network Battle · `4`=Training ·
`5`=Profile Card · `6`=Options.
Options items: `0`=Assign… · `1`=Sound · `2`=Save/Load · `3`=Connection Style · `4`=Screen Display ·
`5`=Voice Select (toggle EN↔JP with left/right).

⛔ **RAM wins over pixels when a verified address contradicts vision** — vision misread the highlight
twice on the 7th item.
⛔ **`psp-watch.mjs` timestamps mark press COMPLETION** (it awaits the hold), so changes look like
they "lead" presses — trust multi-dump exact tracks, or subtract the hold duration.
⛔ **Training-regen rule:** HP visibly regenerates, so any HP diff must be hit-tight (dump within
seconds of a verified `N dmg` popup) or it drowns in regen. Transformations also shift max-HP.

---

## 5. Message/data archives (static inventory)

- `data_sys_us.afs` (81 MB, 609 files): 360×`#AMB`, 115×`#AMT`, 38×`!FLD`, 32×`#SPX`, 22×`#MG`,
  18×RIFF, **8×`#MSG`**, 5×`#MAP`, 1×PSMF movie.
- `#MSG` format: magic, `0x14`, u32=3, u32=0, u16=1?, **u16 count at +18**, offset table at `0x1C`.
  **457 message IDs decoded** — `MSG_AR_CITY_*`, `MSG_AR_CLEAR_*`, `MSG_AR_FIELDPLAY_*` (268),
  `MSG_FIELD_*`, `MSG_CHARACTER_ID_*` (28), `MSG_AR_MISSION_*`, `MSG_AR_FRIEND_SENZU`/`MSG_CMT_*` (103).
- 22 `#MG` configs map story content: `ev_004_XX.spx` scripts paired with `MSG_AR_004_XX.msg`.

⛔ **These tables hold IDs, NOT display text.** Display text is **not plaintext ASCII** in
`data_sys_us.afs` or `data_sys_cmn_pic.afs` — it sits inside `#AMT` containers or compiled `.MGB`s.
**Follow `sceLibFont` usage + the MSG-ID→text resolver rather than unpacking 535 containers blind.**
⛔ Do **not** commit ISO-derived extracts — Temp only (no-ROMs rule).

---

## 5b. ANOTHER ROAD story mode (decompiled; see `dbzar-story-mode.md`)

| function | size | meaning |
|---|---|---|
| `FUN_000323bc` | 408 | **AR MAIN** — story-mode entry; registers `"AR MAIN"`/`"AR MAIN DRAW"` |
| `FUN_000328c4` | 328 | chapter handler A (when already in AR) |
| `FUN_000325e8` | 460 | chapter handler B — **advances/decrements the chapter index** on pad flags 0x40 / 0x10 |
| `FUN_000213c4` | 12 | returns `0x6b90` = the **AR state struct base** (33 callers) |
| `FUN_000219e4` | 212 | `AR UPDATE GAMECLEAR` — reads AR struct `+0x6e4` (vs -1) and `+0x6f0` |
| `FUN_00021ecc`/`00024888`/`000243f0` | 232/220/180 | `[AR] FIELD EVENT` |
| `FUN_0002213c` `FUN_00023000` | 84/92 | `[AR] BATTLE MODE` / `[AR] BTL START` |
| `FUN_00021ab8` `FUN_00024d0c` | 152/108 | `AR UPDATE GAMEOVER` |
| `FUN_00021940` | 136 | `AR UPDATE BACK TO MENU` |
| `FUN_000287f8` | 140 | `[AR] FIELD RESULT` |
| `FUN_00018420` | 140 | `[AR] CITY UPDATE` / `[AR] CITY DRAW` |
| `FUN_0001fec0` | 251 | `[AR] PAUSE` |

**State (`.data`, live-verified):** `DAT_001b11d4` (RAM **`0x89B51D4`**) = **Another Road mode
flag** (measured 0 → 1 on entry); `DAT_001b11d5` = **chapter index**; `DAT_001b11d8` (RAM
`0x89B51D8`) = **chapter table**, 7 entries `0x601C5, 0x601C5, 0x601C3, 0x601C4, 0x601C8,
0x601C6, 0x601C7` then 0. Chapter id = `table[DAT_001b11d5]`.

**Content:** 24 chapters (`MSG_AR_CLEAR_*` = 24), 268 `MSG_AR_FIELDPLAY_*` dialogue ids, 24
`MSG_AR_CITY_*`, 5 `MSG_AR_MISSION_*`. A **Chapter Select** screen exists (TOTAL complete %,
City DF. %) — reached: main menu → Another Road → cross.

⛔ Open: the Chapter Select **browse cursor** is not found (it is not the menu struct, and an
ordinal hunt gives 0). ⛔ `cRam00099dac` (the chapter count) is an untrustworthy `cRam` address —
at runtime it lands in `.text`. ⛔ The `0x74` file-offset/vaddr skew recurs: the chapter table is
at file offset `0x1B124C`, vaddr `0x1B11D8`.

---

## 5c. ANOTHER ROAD **FIELD MODE** — the player's own mode (live-verified)

The story mode is a free-flight field where the player defends cities. Full write-up:
`docs/research/dbzar-field-mode.md`. The adapter is `Core/dbzar_adapter.cpp`.

⛔ **`EBOOT.dec` IS RELOCATABLE.** `.rel.text` patches `lui`/`addiu` pairs on load, so a
static `addiu r, r, 0x1780` is the relocation ADDEND, not the address. The two field arrays
sit ~2 MiB ABOVE their static values. Read the LIVE instruction
(`scripts/dbzar-live-addresses.mjs`) — never do the arithmetic from the file.

| what | address | note |
|---|---|---|
| ENTITY array | **`0x08A852D0`** | 37 (`0x25`) slots x `0xF0`; `FUN_000172fc(i) = i*0xF0 + base` |
| entity `+0x00/+0x04/+0x08` | float x / y / z | |
| entity `+0x30` | i32 team | `0/1` player side, `2/3` enemy side, `-1` unused |
| entity `+0xD1` | byte | visible-this-frame (the city module sets it from a 50.0 check) |
| CITY array | **`0x08A876B0`** | 5 slots x `0x70`; `FUN_000184c8(i) = i*0x70 + base` |
| city `+0x00` | u16 id | `0` = empty slot |
| city `+0x10` / `+0x18` | float x / z | the proximity test reads these two |
| city `+0x20` / `+0x24` | i32 current / max health | **percent = `+0x20 / +0x24`** |
| city `+0x28` | float radius | its square is the proximity bound |
| AR mode flag | **`0x089B51D4`** | EBOOT `.data`, NOT relocated; `1` = in Another Road (live-verified) |
| chapter index | `0x089B51D5` | live value only inside Another Road |
| chapter table | `0x089B51D8` | u32 per chapter, 7 entries then 0 |

**The damage bands are the game's own.** `FUN_0001a90c` computes `ratio = [+0x20]/[+0x24]`
and swaps up to three task handles as it crosses **0.8 / 0.5 / 0.3**. Announce those edges.

**City module:** `FUN_00018420` registers `[AR] CITY UPDATE` → `FUN_0001adf4` and
`[AR] CITY DRAW` → `FUN_000198ac`. The update walks the 5 cities, runs `FUN_0001946c` and
`FUN_0001964c` (proximity: entities 0..2 for the player side, 3..0x24 for the enemy side),
repairs a city the player is over, and calls `FUN_0001a90c` for the band swap.
`FUN_00019100` walks the 37 entities and sets the `+0xD1` visibility byte.
`FUN_00019a48` sorts the entities by depth; `FUN_00019bec` sorts the 5 cities the same way.

⛔ **`FUN_0001964c` DOES NOT STORE WHICH ENTITY IT FOUND**, so "which enemy is attacking
this city" cannot be read from it. That is an open item, not a missed address.

## 6. Open targets

See **`dbzar-story-status.md`** for the one-page state of the Another Road work.

1. ✅ **RESOLVED — the MSG-ID→text path.** `FUN_000da040` is the loader: `'#'`/`'!'` load flag,
   `"MSG"` magic, COUNT at `+0x12`, and two RELATIVE pointer arrays — `+0x14` = NAMES, `+0x18` =
   TEXT (UTF-16LE). 7,485 messages resolved (160 in the ELF, 7,425 in `data_sys_us.afs`).
   ⛔ The text is UTF-16LE; a NUL scan truncates it to one character. Scripts:
   `scripts/dbzar/msg_resolve_elf.py`, `msg_resolve_afs.py`.
2. **Find the battle overlay's own globals** — not EBOOT's (the overlay-swap trap, §2).
3. Live-poll the message queue at `[base+0xC139C+100]` and the screen vtable at `[base+0x34660]`
   across menu transitions (still unprobed).
4. ✅ **RESOLVED — load base `0x08804000`** (§1), derived 5 ways. Derive again per build; do not
   inherit it.
5. ⭐ **STORY: auto-track the current line.** Not solved. The reader
   (`scripts/psp-ar-story-reader.mjs`) reports the active container and every line's full text,
   but not "line 4 of 7". All RAM-scan approaches are negative (§`dbzar-story-status.md`).
   ⛔ Do NOT hook `FUN_000d9bdc` (it is the FONT preload lookup) and do NOT chase `0x8AB7C10`.
   Next instrument: a **READ data breakpoint on the container's text-pointer array**
   (`container + 0x18`) — data breakpoints work, and fire on the fetch that matters — then the
   `ev_*.spx` script VM.
6. **Chapter Select has no list cursor** — it is a **map** (`FUN_00008590` draws 32 linked nodes of
   10 bytes, positions in nibbles, camera at `DAT_001ac2a1`/`DAT_001ac2a2`). Not needed by a reader.

---

## 7. Source-of-truth documents

| document | contents |
|---|---|
| `~/oga-work/docs/reverse-engineering/dbz-another-road.md` | the full record (211 lines) |
| `%LOCALAPPDATA%\Temp\dbz-ar-extract\dbz-msg-all.txt` | the 457 decoded message IDs |
| `...\oga-ghidra-dbzar\function-index.txt` | **generated** full function inventory (6,551 rows) |

*Regenerate the inventory:* `cmd.exe /c "...\oga-ghidra-dbzar\run-index.bat"`.

# DECOMP_INDEX.md — Fire Emblem: Shadow Dragon (NDS, `YFEE` rev 0, USA)

Address → meaning map for the Ghidra decompilation. **Search this first** before touching
RAM or the ROM. `size`/`callers` figures are measured from the Ghidra project
(`function-index.txt`), not remembered; meanings cite the notes.

> **Placeholder resolved.** The workspace directive referenced `<DECOMP_PATH>` / `<ISO_PATH>`
> literals. No such paths exist. The real artifacts are in §1 — that is what the placeholders
> mean for this title.

---

## 1. Paths and entry points (the real `<DECOMP_PATH>`)

| what | path |
|---|---|
| **PRIMARY decomp workspace** | `C:\Users\Devin Prater\oga-ghidra\proj\` (project name **`Fe11Arm9`**) |
| program inside the project | **`arm9.bin`** (BinaryLoader, base `0x02000000`, `ARM:LE:32:v5t`) |
| machine-readable function dump | `...\oga-ghidra\proj\function-index.txt` (**4,056 functions**) |
| symbol source (ds-decomp) | `%LOCALAPPDATA%\Temp\fe11-us\config\YFEE01\arm9\symbols.txt` + `overlays/ov000/symbols.txt` |
| ROM (identity only) | USA, `YFEE` rev 0, 64 MiB, sha256 `bebe9ce0d727a61f1676dd8360e2d4fe7be936b9123e187f106c6cbd555714d7` |
| notes / consumer | `~/oga-work/docs/reverse-engineering/fe11.md`; machine-readable `reverse-engineering/fe11/symbols.json` |

⛔ **This project uses REAL ds-decomp C++ symbol names** (`Unit::GetMaxHp`, `Force::Get`,
`HashTable::Get1`) — **1,055 of the 4,056 functions are named**; the rest are `FUN_xxxxxxxx`.
Search by name first (`GetMaxHp`, `MoveToForce`), then by address. This is unlike the PSP
projects, which are almost entirely `FUN_`-named.

⛔ **`bss` MUST be mapped before any structure query, or every global resolves to nothing.**
`dsd rom extract` gives `arm9.bin` = **file bytes only**; `.bss` (768 KB) has no bytes in the
file, so Ghidra has no memory there and `gUnitList` / `gForces` / `gFE11Database` return
**ZERO HITS** — which reads as "nothing accesses this field" but is really "this address is not
mapped". This happened three times before being diagnosed. Map the blocks first (see §2).

---

## 2. How to query the decomp (the pipeline, verified)

**Module map** (from `config/YFEE01/arm9/delinks.txt`):

| section | range |
|---|---|
| `.text` | `0x02000000`–`0x020C4690` |
| `.rodata` | `0x020C515C`–`0x020C9B10` |
| `.data` | `0x020C9B60`–`0x020E3CA0` |
| `.bss` | `0x020E3CA0`–`0x021A23E0` (768 KB, no bytes in file) |
| overlay load window | `0x021A23E0`–`0x02230000` (ov000–ov011 share it) |
| ITCM | `0x01FF8000`–`0x02000000` (outside main RAM) |
| DTCM | `0x027E0000`–`0x027E1000` (outside main RAM) |

`arm9.bin` is exactly `.text`+`.data` (933,024 bytes); the ROM header's `arm9_size`
(952,952) includes padding, and importing the padded file makes `.bss` conflict.

**The build and query wrappers** (all in `...\oga-ghidra\proj\`):

- `oga-build-project.bat` — the correct end-to-end rebuild: (1) import `arm9.bin`, (2) map
  `.bss`, (3) map the overlay window, (4) import dsd symbols.
- `oga-map-modules.bat` — maps `.bss` + overlay window + ITCM + DTCM so every global resolves.
- `oga-query.bat "<Script.java>" [args…]` — run a script against the saved project
  (`-process arm9.bin -noanalysis`; starts in seconds, do not re-import).
- `run-index.bat` — regenerate `function-index.txt`.

### Overlay programs (ov000–ov011) — separate programs, not blocks

The overlays **overlap in RAM** (ids 0/1 share `0x021A23E0`, 2/3 share `0x021E3560`,
4/5/8/9/10/11 share `0x02204C20`, 6/7 share `0x022176E0`), so a single memory block
cannot hold them. Each is imported as **its own program** at its own base, from
`oga-work-fe11/extracted/arm9_overlays/overlays.yaml`:

| overlay | base | code size | bss | notes |
|---|---|---|---|---|
| `ov000` | `0x021A23E0` | 266,048 | 576 | **where `gMapStateManager`, `gActionSt`, `gMapRenderState` live** |
| `ov001` | `0x021A23E0` | 166,208 | 9,920 | shares ov000's base |
| `ov002` / `ov003` | `0x021E3560` | 96,736 / 136,192 | 416 / 704 | |
| `ov004`/`ov005`/`ov008`/`ov009`/`ov010`/`ov011` | `0x02204C20` | 81,312 / 76,096 / 16,416 / 13,824 / 22,176 / 186,016 | | |
| `ov006` / `ov007` | `0x022176E0` | 74,304 / 22,272 | | |

Import with `run-import-ov000.bat` (ov000) and `run-import-overlays.bat` (ov001–ov011).
All twelve are imported (project programs `arm9.bin` + `ov000.bin`…`ov011.bin`).

**Function counts, measured** (`run-index-overlays.bat` → `function-index-ovNNN.txt`):

| overlay | fns | overlay | fns | overlay | fns |
|---|---|---|---|---|---|
| `ov000` | **564** | `ov004` | 181 | `ov008` | 42 |
| `ov001` | 972 | `ov005` | 150 | `ov009` | 22 |
| `ov002` | 354 | `ov006` | 185 | `ov010` | 108 |
| `ov003` | 245 | `ov007` | 53 | `ov011` | 697 |

Overlay total **3,573** functions, on top of `arm9.bin`'s **4,056** — so a query that only ever
looked at `arm9.bin` sees less than half the code.

⛔ **Querying `arm9.bin` for an overlay-resident global answers nothing** — which is why
`gMapStateManager` reads `0` outside a map. Query the **overlay program** (`-process ov000.bin`),
not `arm9.bin`.

⛔ **Arguments for these wrappers MUST live inside the `.bat`.** The project path contains a
space (`Devin Prater`); passing it through `bash -> cmd.exe` splits at the space and Ghidra
reports `Bad argument: <project name>` — blaming the name, not the truncated path. Likewise
`%20` in a passed path is treated as a variable expansion by cmd.

---

## 3. Function index — by system

Format: `symbol  entry  size  callers`. Only the highest-value named functions are listed;
the full 4,056-row inventory is `function-index.txt`.

### 3.1 Global objects (BSS) — confirmed

| symbol | address | evidence |
|---|---|---|
| `gFE11Database` | `0x02197254` | dsd symbol; live read gives a valid pointer |
| `gUnitList` | `0x021974D8` | **confirmed** — live: 1-based array, stride `0xA8` |
| `gForces` | `0x021974DC` | **confirmed** — live: 6 `Force` structs, `id == index` |
| `gMapStateManager` | `0x021E3328` | **confirmed** — valid only while **ov000** is mapped |
| `gActionSt` | `0x021E3344` | dsd `overlays/ov000/symbols.txt`; `unk_34` = acting unit id, `xDecision`/`yDecision` |
| `gMapRenderState` | `0x021E34FC` | dsd `overlays/ov000/symbols.txt` |

⛔ `gUnitList` (`0x021974D8`) and `gForces` (`0x021974DC`) are 4 bytes apart — the unit list's
second word is a tail pointer the force array begins immediately after. Do not read one as the
other.

### 3.2 Unit / Force / Job accessors (named)

| symbol | entry | size | callers | meaning |
|---|---|---|---|---|
| `Force::Get(long)` | `02040c98` | 20 | 20 | force lookup by faction; `Force::Get(faction+2)` |
| `Unit::GetMaxHp()` | `0203c454` | 44 | 13 | max HP |
| `Unit::GetMov()` | `0203c77c` | 20 | 6 | movement stat |
| `Unit::CheckAttribute(uint)` | `0203c810` | 36 | 16 | attribute bit test |
| `Unit::CanEquip(ItemData*, long)` | `0203c834` | 816 | 8 | equip check |
| `Unit::CanEquip(long,long)` | `0203cb6c` | 44 | 12 | equip check (id form) |
| `Unit::GetEquippedWeaponSlot()` | `0203cb98` | 44 | 12 | weapon slot |
| `Unit::GetWeaponLevel(uint)` | `0203c7ac` | 56 | 6 | weapon level |
| `Unit::EquipItem(long)` | `0203cd30` | 192 | 6 | equip |
| `Unit::MoveToForce(long,long)` | `0203bd34` | 156 | 8 | change faction |
| `Unit::Copy(Unit*)` | `0203aa4c` | 512 | 6 | copy unit record |
| `Item::GetData()` | `0203df8c` | 28 | 68 | item data pointer |
| `ItemData::GetEffectiveJob(Unit*)` | `02038fe4` | 148 | 8 | effective job |
| `ItemData::IsUsableBy(Unit*)` | `02038384` | 880 | 7 | usability |
| `GetJobDBIndex` | `02037f98` | 40 | 11 | job → DB index |
| `GetItemDBIndex` | `02037ffc` | 40 | 17 | item → DB index |
| `SetSpriteDirectoryForJob` | `0203e220` | 72 | 8 | sprite dir per job |

### 3.3 Text / hash / DB lookup (named)

| symbol | entry | size | callers | meaning |
|---|---|---|---|---|
| `GetText` | `02039e10` | 56 | 43 | the text getter |
| `HashTable::Get1(char*)` | `020377e8` | 16 | 16 | hash lookup 1 |
| `HashTable::Get2(char*)` | `02037800` | 16 | 33 | hash lookup 2 |
| `HashTable::Put(char*,void*)` | `02037818` | 24 | 7 | hash insert |
| `FlagManager::FindIdByName(char*)` | `020492f4` | 92 | 7 | flag id by name |
| `FlagManager::SetById(uint)` | `0204939c` | 28 | 8 | set flag |

### 3.4 Proc / event / flow (named)

| symbol | entry | size | callers | meaning |
|---|---|---|---|---|
| `Proc_Find` | `02018cfc` | 60 | 20 | find a process |
| `Proc_Goto` | `02018ee8` | 80 | 14 | state-machine transition |
| `Proc_SetMark` | `02018ee0` | 8 | 9 | set mark |
| `Proc_EndEach` | `02019020` | 12 | 12 | end each |
| `GameCtrl_GotoLabel` | `02022f28` | 32 | 18 | script goto |
| `Event::GetRunningEvent()` | `020477d4` | 12 | 13 | running event |
| `Event::StartEventByInfo(EventInfo*,void*)` | `020475cc` | 200 | 6 | start event |
| `EventCaller::CheckEventTrigger(...)` | `020479e8` | 88 | 6 | trigger check |
| `EventCaller::CheckEventFlag(char*)` | `02047a7c` | 108 | 6 | flag check |
| `EventSkip::IsSkipState4()` | `0204787c` | 28 | 57 | skip state |
| `GetEventStr` | `02035c6c` | 16 | 7 | event string |

### 3.5 Menu / UI (named)

| symbol | entry | size | callers | meaning |
|---|---|---|---|---|
| `StartMenu` | `0202fa70` | 192 | 6 | menu entry |
| `Menu::Menu()` | `0202d4c4` | 172 | 10 | menu constructor |
| `Menu::~Menu()` | `0202d5e4` | 44 | 37 | menu destructor |
| `Button::SetPosition(long,long)` | `02035348` | 72 | 6 | button placement |
| `Dialog::Dialog()` | `020302e0` | 72 | 7 | dialog ctor |
| `CursorPut` | `020460b0` | — | — | cursor placement |
| `CursorDelete` | `020460c8` | — | — | cursor removal |

### 3.6 Rendering / memory / misc (named)

| symbol | entry | size | callers | meaning |
|---|---|---|---|---|
| `Interpolate` | `02020a2c` | 292 | 30 | interpolation |
| `Decompress` | `02020bc0` | 72 | 6 | decompression |
| `LoadUncompressedFile` | `02011854` | 200 | 12 | file load |
| `LoadFileWithOffset` | `020116a0` | 220 | 9 | file load w/ offset |
| `LoadFileAndCache` | `020379e0` | 36 | 6 | cached load |
| `Heap::SizeOf(void*)` | `020114dc` | 96 | 16 | heap size query |
| `IntSys_Div` | `02020838` | 52 | 63 | integer divide |
| `IntSys_Mod` | `02020874` | 52 | 38 | integer mod |
| `BGMPlay` / `BGMStop` / `BGMPausePlay` | `02042ac0`/`02042bcc`/`02042c8c` | — | — | audio |
| `StartBlockingFadeInFromBlack` | `0201d7c8` | 68 | 6 | fade |
| `IsFadeActive` | `0201dae8` | 48 | 6 | fade state |
| `DisposPlayArea` / `DisposMoment` | `02043388`/`020433b8` | — | — | play-area disposal |
| `DangerAllSet` | `02046128` | — | — | danger overlay |
| `CountConvoyItems` | `02041110` | 72 | 6 | convoy count |
| `AddChapter` | `0204347c` | — | — | chapter add |

---

## 4. Structures and memory map — confirmed

### `struct Unit` (stride `0xA8`, base `gUnitList` `0x021974D8`, **1-based**: `GetUnit(id) = base + (id-1)`)
| offset | field | evidence |
|---|---|---|
| `+0x3C` | `next` list link | confirmed |
| `+0x40` | `pPersonData` | confirmed — deref gives `pid` string (`"PID_MARS"`) |
| `+0x44` | `pJobData` | confirmed — deref gives `jid` string (`"JID_LORD"`) |
| `+0x4C` | `force` pointer | confirmed — its `+0x08` reads the faction |
| `+0x6A` | level | confirmed |
| `+0x6C` | `hp` | **confirmed** — 18 for Marth at Prologue start |
| `+0x6D` | `mov` | confirmed (0 until deployed; use `JobData.mov`) |
| `+0x6E` / `+0x6F` | `xPos` / `yPos` | **confirmed** |
| `+0x70` | `items[5]` | confirmed |
| `+0x98` | `state1` | ⛔ **meaning NOT established** — filtering on its alleged acted/dead bits gave "0 live units" on a full map |

### `struct Force` (array of 6 at `gForces`)
`+0x00 head` · `+0x04 tail` · `+0x08 id` (faction; `id == index` confirmed).
⛔ **`sizeof` is not `0x0C`** — live entries sit `0xA8` apart. Do not compute a 7th entry from a `0x0C` stride.

Faction ids: `0` Player · `1` Enemy · `2` Player (scenario) · `3` Enemy (scenario) · `4` Unassigned
reserve (~60 roster slots, no HP, position `(0,0)`) · `5` Other (empty).
⛔ **Faction 4 holding ~60 units is normal.** A reader that tests "has HP field" reports 60 phantom
enemies. Test position *and* force id.

### `struct JobData`
| offset | field | evidence |
|---|---|---|
| `+0x28` | movement type (cost-matrix row) | **confirmed** — `u8`; Lord 0, Soldier 23, Fighter 7 |
| `+0x29` | `mov` | **confirmed** — Lord 7, Soldier 6, Fighter 6 |

⛔ `unk_28` is a **byte**, not a pointer. A 32-bit read gives `50464512` (`0x03025C00`) — looks like an address, is the byte plus neighbours.

### `gMapStateManager` (`0x021E3328`) → `+0x010` cursor
| offset | field | evidence |
|---|---|---|
| `+0x10` | pointer to cursor struct | confirmed |
| cursor `+0x08`/`+0x09` | `xTile`/`yTile` | **confirmed** — tracks movement |
| cursor `+0x0A`/`+0x0B` | `xDisplay`/`yDisplay` (pixels) | confirmed |
| `+0x24`..`+0x27` | map bounds `x0,x1,y0,y1` | **confirmed** — Prologue: x 1..15, y 1..22 |
| `+0x028` | occupying unit id per tile (32×32) | **confirmed** |
| `+0x828` | **pointer** to tile-id array (32×32, `0x400`) | **confirmed** |
| `+0x82C` | impassable flags per tile (`& 0x80`) | **confirmed** |
| `+0x830` | terrain category per tile (32×32) | **confirmed** |
| `+0xD30` | reachability bitmap, **inline `u8[0x80]`** | ⛔ **inline, not a pointer** (a 32-bit load returns `FFFFFFFF`) |

⛔ **The blue movement overlay is NOT resident.** All five candidate buffers (`unk_c30`, `unk_cb0`,
`unk_d30`, `unk_db0`, `unk_e30`) were sampled across 1500 frames and never held a range-like value.
It exists only while a move preview is on screen. **Compute range** from the cost matrix +
`JobData.mov` + categories + occupancy; do not depend on reading the overlay.

### Terrain cost matrix
Indexed `[movementType][terrainCategory]`, reached at `gFE11Database->unk_28`. The game's own test
(from `src/ov000/map_sequence.cpp:2741`) is `func_0203826c(pTerrain[...].unk_08, pUnitA->pJobData->unk_28) < 0`
(`<0` == impassable). Verified rows:
```
Marth   Lord    mov=7  row  0 : -1 1 1 1 2 2 2 2 5 4 -1 1 -1 1 -1 2
Soldier         mov=6  row 23 : -1 1 1 1 2 2 2 2 -1 -1 -1 1 -1 1 -1 2
Fighter         mov=6  row  7 : -1 1 1 1 2 2 2 2 -1 3 -1 1 -1 1 -1 2
```
`-1` impassable · `1` open ground · `2` forest/hill · `3` mountain · `4–5` higher peaks.
⛔ **There is NO category→name table.** `db.unk_24[category]` is null; `pTerrain[tile].unk_04` holds
**background art names** (`"BBG01"`). Any human-readable terrain word is **ours** and stays `tentative`.

### `struct MapData` (table at `gFE11Database->unk_18`, `sizeof` observed `0x1C`)
`+0x00` = `"bmap###"`/`"arena###"` (map id) · `+0x04` = `"MCT###"` (chapter text id).
`GetMapDBIndex = (pMap - table) / sizeof(MapData)`; `func_0203812c(pMap)` returns
`HashTable::Get1(pMap->unk_04)` falling back to `unk_00` — the game's own map-name getter.
⛔ **Which entry is the current map was NOT located** (open, §6).

---

## 5. Text / dialogue — UNKNOWN (honest negative)

**Dialogue has not been located, in RAM or in the ROM.** Found in RAM: message-table **IDs** and
asset filenames, not prose:
```
table @0x0221824C   gop_001  gop_002  gop_003  ill_203
table @0x022667C4   MG_ARITEA  MG_AKANEIA  MG_MACEDONIA  MG_GRUNIA   (nation names)
table @0x0226D564   MTUTH_00  MTUTH_05B  MTUTH_05T  MTUTH_06  MTUTH_08B
```
and assets `startup.cmb`, `panel.cl`, `unitselect.sc`, `touchcursor.tpl`.

The ROM scan found **no prose**: `strings -n 32` yields only leftover Nintendo Wi-Fi/SSL banners
(`"The connection has already been disconnected."`, VeriSign/Thawte certs). `MTUTH` does not appear
in the ROM at all, so the table is built at runtime or the text is compressed/encoded.
**Open problem.**

---

## 6. Open RE targets (so work does not re-tread)

1. **Current-map identity** — which `MapStateManager` field points into the `MapData` table
   (`GetMapDBIndex` is exactly `(pMap - table) / stride`).
2. **Dialogue text** — not located anywhere yet (§5).
3. **`Unit.state1` bit meanings** — the acted/dead reading is falsified.
4. **Terrain category names** — the game has no such table; ours are `tentative` until checked
   against what the game draws.

---

## 7. Decompilation headers that were WRONG (do not take at face value)

| header claim | reality | symptom |
|---|---|---|
| `TerrainCostData.unk_04` is `s8*` | flexible array at `+4`; `size` at `+0` is the row **width** | matrix looked like garbage |
| `JobData.unk_28` read as a word | it is a `u8` | got `0x03025C00`, "an address" |
| `MapStateManager.unk_d30` "a pointer" | inline `u8[0x80]` | got `FFFFFFFF` |
| `Unit.state1` acted/dead bits | meaning unestablished | "0 live units" on a full map |

The dsd decompilation is invaluable for **where to look**, but its struct headers have been wrong
in these places — treat its field types as hints to test, not facts to copy.

### Method notes worth keeping

- **Suspect a modal dialog before suspecting the memory map.** Enemies never appeared for tens of
  thousands of frames during the Prologue because a tutorial popup was swallowing every button.
  The popup's own text held the fix ("press B … to cancel the move").
- **Read the value where it lives, in the frame it is live.** `gMapStateManager` is ov000 BSS,
  mapped only while ov000 is loaded; a read after the frame loop returns `0`.
- **`poke_set_button`, not `poke_set_hotkey`** — the latter sends the script's letter keys, not DS
  buttons; a plan driven with it never moved anything and looked like a memory-map failure.

---

## 8. Source-of-truth documents

| document | contents |
|---|---|
| `~/oga-work/docs/reverse-engineering/fe11.md` | the human-readable record (351 lines) with confidence levels |
| `~/oga-work/reverse-engineering/fe11/symbols.json` | machine-readable findings for Open Game Access |
| `~/oga-work/reverse-engineering/fe11/static-analysis.json` | static analysis data |
| `...\oga-ghidra\proj\function-index.txt` | **generated** full function inventory (4,056 rows) |

*Regenerate the inventory:* `cmd.exe /c "...\oga-ghidra\proj\run-index.bat"`.

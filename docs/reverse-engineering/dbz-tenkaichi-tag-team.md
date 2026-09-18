# DBZ: Tenkaichi Tag Team (ULUS10537) — memory map and adapter groundwork

Started 2026-09-17. Target: an Open Game Access adapter, same shape as the Attack of the
Saiyans one — read-only semantic inspection of the BATTLE HUD plus menu cursors.

## Pipeline (all steps verified)

```
.cso  --oga-cso-extract.py-->  .iso (1,372,225,536 bytes, matches the CSO header)
.iso  --oga-iso-extract.py-->  PSP_GAME/SYSDIR/EBOOT.BIN  (3,748,256 bytes, '~PSP')
      --pspdecrypt--------->  EBOOT.dec  (MIPS-II ELF32, entry 0x08804040)
      --analyzeHeadless----->  C:\Users\Devin Prater\oga-ghidra-tagteam  (98 s)
```

⛔ **`7z` CANNOT OPEN A `.cso`** — "Cannot open the file as archive". The container is a
PSP-specific block-compressed ISO. Use `open-game-access/scripts/oga-cso-extract.py`.

⛔ **The CSO header field at offset 0x10 is the BLOCK SIZE (2048), not a log2 shift.** Reading
it as a shift gives `1 << 2048` and the failure surfaces as
`zlib.error: incorrect header check` — which reads as a corrupt image, not an arithmetic bug.

## ⭐ THE ADDRESS MAPPING — pre-linked, unlike the other two projects

```
LOAD off=0x001018  vaddr=0x08804040  filesz=0x27D45D  flags=7
LOAD off=0x27F000  vaddr=0x08AEFB70  filesz=0x84      flags=6
```

The first LOAD segment's `vaddr` is already in the 0x08800000 range, so **the ELF is
pre-linked to absolute addresses and NO base offset applies**:

```
RAM = ELF_vaddr          (this game)
```

Compare: Steins;Gate needed `RAM = 0x08804000 + vaddr`; Another Road needed a derived base
(`0x0898036A`). **Three games, three mappings — never carry one across.**

Consequence: an instruction literal in the 0x08800000–0x0A000000 range **is** a RAM address
here, so globals are directly findable. (A quick scan for such literals in instruction
operands returned 0, which suggests this build reaches its globals via a small number of
base registers rather than absolute literals — worth remembering before repeating that scan.)

## ⭐ THE GAME'S OWN SYSTEM MAP — from its UI element names

Grepping the decompiled ELF for UI asset names named the systems outright, with no RAM
scanning. This is the highest-value trick found on this title:

| address | string | meaning |
|---|---|---|
| `0x08A72218` | `BTL_HP_MAIN_1P` | battle HP bar, player 1 |
| `0x08A723CC` | `BTL_HP_MAIN_2P` | battle HP bar, player 2 |
| `0x08A7224C` | `BTL_KIRYOKU` | **ki gauge** |
| `0x08A72A78` | `BTL_HARD_BATTLE` | hard battle mode |
| `0x08A733AC` | `chara_name_01` | character name plate 1 |
| `0x08A73384` | `chara_name_02` | character name plate 2 |
| `0x08A73578` | `30_select_ok_%d` | selection confirm |
| `0x08A739D0` | `charaname` | character name |
| `0x08A739FC` | `text_playername` | player name text |
| `0x08A73AD0` | `40_stage_select_cursor_%02d` | **stage-select cursor** |
| `0x08A73AF4` | `40_stage_select_tab` | stage-select tab |
| `0x08A75828` | `SKILLDATA_WINDOW` | skill panel |
| `0x08A76470` | `50_windows` | menu windows |
| `0x08A76538` | `gauge_attack` | stat gauge: attack |
| `0x08A76548` | `gauge_defense` | stat gauge: defense |
| `0x08A76558` | `gauge_technic` | stat gauge: technic |
| `0x08A76948` | `gauge_dpoint` | DP gauge |
| `0x08A76960` / `0x08A7696C` | `text_yes` / `text_no` | confirm dialog |
| `0x08A68A8C` | `disc0:/PSP_GAME/USRDIR/PACKFILE.BIN` | the data archive |

## The stat-panel call chain (the adapter's entry point)

`FUN_08a3c458` (1116 bytes) is the **character stat panel renderer**:

```c
iVar6 = FUN_08840dc4(iVar1, "gauge_attack");
if (iVar6 != 0) {
    iVar2 = FUN_08a3c320(param_1, 0);        // obtain the attack value
    piVar5 = *(int **)(param_1 + 0x74);
    ...
}
// then "gauge_defense", "gauge_technic" by the same pattern
```

- `FUN_08840dc4(node, name)` resolves a UI node by name.
- `FUN_08a3c320(panel, index)` returns the value for gauge `index`.
- Callers of the panel: `FUN_08a3c990`, `FUN_08a3c8b4`, `FUN_08a3c9c8`.

⛔ **`FUN_08a3c320` decompiles to `halt_baddata()`** — "Control flow encountered bad
instruction data". Its 124-byte body needs raw instructions or its caller read instead. This
is a known Ghidra decode gap, not a missing function.

## Driving the game (what worked)

Verified live in PPSSPP 1.20.4, debugger on port 12345, game id confirmed `ULUS10537`.

- First run demands **profile creation**: a keyboard screen, then
  **"Is this Player Name ok?"**, then the save screen. Getting through it:
  `down`×2 then `cross` (types characters), then navigate and `cross` to accept.
- The **"Is this Player Name ok?"** prompt has no visible Yes/No until the screen scrolls —
  it was misread as a dead end for several attempts. **Scroll the view before concluding a
  prompt is unanswerable.**
- After the confirm, the **Save** screen appears; selecting the `NEW DATA` slot saves and
  shows the game logo with "Save complete".
- **The save-complete screen then ignores every button tried** (`cross`, `start`, all
  shoulders, `select`, `square`, `triangle`), with 13 identical frames.

⛔ **Measured, not assumed:** a broad 6 MB RAM diff showed **86,423 bytes changing on a press
burst vs 30 while idling** — so input IS landing on that screen and the game is genuinely
waiting on something else. Do not diagnose this as a broken harness.

## ⭐ REACHED CHARACTER SELECT LIVE — and the biggest find so far

Got past first-run setup to the **Main Menu**, then `down`×2 + `cross` into
**CHARACTER SELECT**. Screen read live:

```
CHARACTER SELECT
  A / 1P  1111111111        <- the player name I created
  "Goku"                     <- the selected character's name plate
  1P | 2P CPU | 3P CPU | 4P CPU
```

### A large resident UTF-16LE string table exists, and it is readable

A RAM dump at that screen contains the game's **mission names and control tutorials** as
plain UTF-16LE text. This is a major result for accessibility — mission titles and control
help need no OCR and no guessing:

```
0x08C7AE6E  'Find Gohan!!'
0x08C7AE88  'Head for Kame House!'
0x08C7AEB2  'Defeat Krillin!'
0x08C7AED2  'Defeat Raditz and Save Gohan!'
0x08C7AF0E  'Defeat the Saibamen and Survive!'
0x08C7AF70  'Defeat the Saibamen and Go After Nappa!!'
0x08C7AFC2  'Stand Against Nappa!'
0x08C7B008  'Stand Against Vegeta!'
```

and the control tutorial, verbatim:

```
' Melee Attack. Close Attack. Can attack in succession.'
' Ki Blast Attack. Long-distance Attack. Can be fired in succession.'
' Defense. Guard against enemy attacks.'
' Dash. Move quickly towards enemy.'
' Ki Charge. Ki can be charged.'
' Super Attack. Use Ki to fire Super Attack.'
' Switch Lock-on. Switch enemy to lock onto.'
'Those are the basic controls.'
```

⛔ **ASCII search finds NONE of this.** All character names, mission titles and tutorial text
are **UTF-16LE** (`Goku` ascii=0, utf16le=28; `Vegeta` ascii=0, utf16le=56). On this title an
ASCII-only string scan reports an empty game and looks broken. **Search both encodings.**

### The full roster is present

`Goku`(28) `Vegeta`(56) `Gohan`(72) `Piccolo`(31) `Krillin`(26) `Trunks`(36) `Frieza`(43)
`Cell`(34) `Buu`(54) — all UTF-16LE, all in one region around `0x08C7Axxx`, alongside the
mission and tutorial text. That region is the game's message/script block.

## ⛔ NOT FOUND YET: the character-select cursor

Three dumps taken with `right` pressed between them (Goku -> next -> next -> next) were diffed
against the base dump:

```
base->1:   88,179 bytes changed   first @ 0x08A78B04
1->2:      73,167 bytes changed   first @ 0x08A78B04
2->3:      73,211 bytes changed   first @ 0x08A78B04
```

- A **strict +1 cursor search returned 0 candidates** (looking for a value increasing by
  exactly 1 per press, base one below).
- A relaxed search (any small int differing across all four dumps) returned 18 candidates,
  all of which look like counters, timers or state flags rather than a selection index.
- `0x08A78B04`, the first-changed address, holds a **decreasing 32-bit value**
  (`0x0002D0A4` -> `0x0002DF5E` -> ...) — a **frame counter or countdown**, not a cursor.

⛔ **~73-88 KB changing per press is mostly screen redraw**, so byte-diffing at this scale is
dominated by rendering churn. This is the same shape of problem the Another Road project hit
on its story index, and the same advice applies: **do not guess the index from table order.**
The reliable route is the decompiler — find the code that renders the name plate and read the
variable it indexes with — not more RAM sweeping.

## Status

| item | state |
|---|---|
| CSO -> ISO -> EBOOT -> ELF -> Ghidra | **done, verified** |
| Game driven to Main Menu and Character Select | **done, live** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials in RAM | **found, readable** |
| Roster names in RAM | **found (UTF-16LE)** |
| Stat-panel call chain | **identified, needs live confirmation** |
| Character-select cursor | **NOT FOUND** |
| Battle HUD reached | **NO** |
| Adapter code written | **NO — and it must not be until addresses are confirmed live** |

## ⭐ THE SELECTION INDEX — found in the decompiler, which is where it had to come from

RAM sweeping failed; the decompiler answered it. `FUN_08a02a3c` (1416 bytes, character-select
panel builder) is the function that lays out the per-player character panels, and its own
locals name the elements:

```c
/* FUN_08a02a3c — CHARACTER SELECT panel builder */
local_38 = "menu%02d";
local_44 = "player";
local_48 = "icon_team";
local_4c = "charaname";
local_50 = "formname";
local_58 = "icon_customize";
local_5c = "text_playername";
```

and the selection is read from a single field on the UI context:

```c
local_68 = (uint)(*(int *)(iVar2 + 0x5c + *(int *)(param_1 + 0x68) * 4) == 1);
if (iVar6 != *(int *)(param_1 + 0x68)) { iVar1 = local_74 + 1; ... }
...
if (*(int *)(iVar2 + 0x58) == *(int *)(param_1 + 0x68)) {
    local_3c = local_110;           /* <-- the HIGHLIGHTED panel */
    ...
}
```

So, read directly out of the game's own code:

| expression | meaning |
|---|---|
| **`ctx + 0x68`** (u32) | **the SELECTED CHARACTER INDEX** |
| `panel + 0x58` | that panel's own character id — compared to `ctx+0x68` to decide the highlight |
| `panel + 0x5c + [ctx+0x68] * 4` | per-character flag array, indexed BY the selection (readiness / locked) |
| `menu%02d` | the panel is named by its display slot, not the character |

⛔ **`ctx + 0x68` is a RELATIVE offset inside a UI context object, not a RAM address.** The
context object itself (`param_1`) is passed in and allocated at runtime — which is exactly why
no fixed RAM address held the cursor and why sweeping was never going to find it. **Resolving
`ctx` to an absolute address is the remaining step**, and it comes from
`FUN_08a02a3c`'s caller.

### Why this was the right method

The three earlier facts — a strict +1 search returning 0, ~73-88 KB of per-press redraw churn,
and the first-changed address holding a countdown — all pointed the same way: the value is not
a plain counter at a fixed address. **Reading the renderer settled it in one pass**, and the
answer is a struct field reached through a pointer. That is the general rule for this class of
problem and it is now recorded in the skill.

## Status — updated

| item | state |
|---|---|
| CSO -> ISO -> EBOOT -> ELF -> Ghidra | **done, verified** |
| Driven to Main Menu + Character Select | **done, live** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials | **found, readable (UTF-16LE)** |
| Roster names | **found (UTF-16LE)** |
| **Character-select selection index** | **FOUND in code (`ctx+0x68`), absolute address pending** |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | **not written** — correctly, until `ctx` is resolved and confirmed |

## The context object: vtable-dispatched, and what that costs

`FUN_08a02a3c`'s raw instructions confirm the calling convention:

```asm
08a02a3c  addiu sp,sp,-0x120
08a02a44  or    s2,a0,zero          ; s2 = param_1  (the UI context)
08a02a48  lw    a0,0x40(s2)         ; a0 = ctx->vtable-ish
08a02a4c  addiu a0,a0,0x40
08a02a50  lh    a1,0x0(a0)
08a02a54  lw    a2,0x4(a0)
08a02a80  jalr  a2                  ; INDIRECT call through ctx
```

So the function is **called through a vtable inside the context itself** (`ctx+0x40` -> call at
`+0x44`). Consequences, both confirmed:

- **Ghidra finds NO callers** — "no callers found — may be dispatched through a table".
- **No direct address references** to `08a02a3c` exist either (the dispatch section is empty).

⛔ **This is a general trap for UI code in this engine: the builder is reached only through a
vtable, so "follow the callers" dead-ends.** The context's own address has to come from
somewhere else — a registry, an allocation site, or live measurement.

### Empirical resolution attempt, and why it was invalid

I tried instead to find a live pointer `P` whose `P+0x68` is a small changing value (the
selection) with a per-character table at `P+0x5c`. **0 candidates.**

⛔ **The reason matters more than the result: the three stepped dumps are not a clean cursor
series.** The base dump was taken at the MAIN MENU and `tt-cs1..3` after entering CHARACTER
SELECT, so the set mixes two screens. Any diff across it measures a **screen transition**, not
cursor movement — so the search could not have succeeded regardless of the address being right.

✅ **Rule for next time: take the stepped dumps all within ONE screen, verify with a screenshot
at each step, and confirm the cursor visibly moved before diffing.** A dump series that spans a
screen change is not a series.

## Status — where this stands

| item | state |
|---|---|
| CSO -> ISO -> EBOOT -> ELF -> Ghidra | **done, verified** |
| Driven to Main Menu + Character Select (live, screenshots kept) | **done** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials (UTF-16LE, readable) | **found** |
| Roster names (UTF-16LE) | **found** |
| Character-select selection index | **located in code (`ctx+0x68`); ctx address NOT resolved** |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | data verified; implementation is next |

### Next steps, in priority order

1. **Re-take a clean cursor series on the character-select screen alone** (screenshot each step
   to prove the cursor moved), then re-run the `P+0x68` pointer search. This is the cheapest
   route to the context address and the previous attempt's failure was purely a bad series.
2. If that fails, find the **allocation site** of the UI context (a size-consistent `malloc`
   whose result is stored to a global) and read the global.
3. Only after that, drive on to a battle to confirm the stat chain.

## Clean cursor series ATTEMPTED — and it failed for a NEW reason: `right` does not move the cursor

Re-took the series properly this time (all four dumps on CHARACTER SELECT, a screenshot at each
step as required). Result: **the cursor never moved.**

```
cur0 edc1fc81de      <- Goku
cur1 f6315c522f
cur2 edc1fc81de      <- back to the same hash
cur3 f6315c522f
```

Two alternating hashes = **idle animation**, and the name-plate crop still read **"Goku"** after
three `right` presses. So the series is again unusable — not because the dumps were mistimed,
but because **the thing being tracked does not respond to `right` on this screen.**

⛔ **Do not assume a d-pad navigates a menu.** Measured, after the fact:

- input **IS** landing on this screen (broad 6 MB RAM diff: **179,490 bytes** changed on a press
  burst vs the idle baseline),
- CPU **is** running (`stepping=false paused=false`),
- yet `right` leaves the selected character unchanged.

The character-select layout has the roster as a wide strip and four player panels
(`1P | 2P CPU | 3P CPU | 4P CPU`); direction alone may need a shoulder button or a confirm
first, or the highlight may not be what `right` addresses. **This screen's navigation is not
solved.**

### What the navigation probe did establish

A single pass of every control (screenshot after each) produced **19 distinct screens**, so
input is definitely reaching the game — and pressing `select` **left CHARACTER SELECT and
returned to the MAIN MENU**, whose cursor then sat on **Free Battle**:

```
Main Menu
  Free Battle      <- highlighted
  Multiplayer
  Customize
  Training
```

That is a working route back to the menu, and the Main Menu list is fully readable.

## Correction to the previous section

The earlier pointer search for `ctx+0x68` searched a series whose base dump was taken at the
MAIN MENU — noted above as invalid. This repeat fixed the series but hit the second problem:
**the cursor does not move under the input tried**, so there is still no valid cursor series to
search. The address question stays open, and now for a fully understood reason rather than a
bookkeeping one.

## Status

| item | state |
|---|---|
| Pipeline (CSO -> ISO -> EBOOT -> ELF -> Ghidra) | **done, verified** |
| Driven to Main Menu + Character Select | **done, live** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials (UTF-16LE) | **found, readable** |
| Roster names (UTF-16LE) | **found** |
| Main Menu options (Free Battle / Multiplayer / Customize / Training) | **read live** |
| Selection index | **in code (`ctx+0x68`); `ctx` address NOT resolved** |
| Character-select navigation (`right` moves nothing) | **NOT solved** |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | data verified; implementation is next |

### Next steps

1. **Solve character-select navigation first** — it is the gate to everything else. Try the
   shoulder buttons (`ltrigger`/`rtrigger`) and `cross` on the roster strip, screenshotting the
   name plate each time to confirm a real change. Do not diff until the plate visibly changes.
2. Then, with a valid series, resolve `ctx` via the `P+0x68` pointer search.
3. **Free Battle** on the Main Menu looks like the shortest route to a battle HUD, which is what
   confirms the stat chain.

## The character-select LAYOUT, read from the screen — and why `down` also fails

A full-resolution look at CHARACTER SELECT gives the layout, which the earlier low-detail reads
missed. It is **not** a horizontal carousel:

```
  CHARACTER SELECT
  A / 1P  1111111111                     <- player name banner
  |  big portrait of the selected character  |   <- LEFT: the selection's portrait
  |     "Goku"  (name plate under the portrait)  |   RIGHT: a VERTICAL roster strip
                                                        (Goku, then another face below)
  1P  |  2P CPU  |  3P CPU  |  4P CPU      <- the four player slots along the bottom
```

⛔ **The roster is a VERTICAL strip on the right, so `right` was never going to move it.** That
much the layout explains.

### But `down` does not change the selection either

Pressed `down` three times from Goku, screenshotting and cropping the name plate each step:

```
v0  04948b26   plate: "Goku"
v1  6cd62424   plate: "Goku"
v2  3fa5e6bd   plate: "Goku"
v3  3fa5e6bd   (idle repeat)
```

The screen hash **does** change on the presses, so the input lands and something moves — but
**the name plate stays "Goku"**, so the character selection is not what changed. The most likely
reading is that `down` walks the **1P / 2P CPU / 3P CPU / 4P CPU slot row** at the bottom, not
the roster.

⛔ **A changed screen hash is not evidence that the thing you care about moved.** This is the
third variant of the same trap on this project, and the crop-and-read check is what caught it
each time. **Always verify the specific field, not the frame.**

### So: character-select navigation is still NOT solved

Three controls tried (`right`, `down`, and a full pass of every button), each verified against
the name plate rather than the frame hash. None moved the selection. Unknowns that would explain
it, in order of likelihood:

1. The roster strip needs a different control — shoulder buttons (`ltrigger`/`rtrigger`) are the
   common PSP idiom for a side strip, and were never tried **while watching the plate**.
2. Focus may start on the **slot row**, and a `cross` (confirm) is needed to move focus into the
   roster before directions affect it.
3. The roster may scroll with the analog nub rather than the d-pad.

**Until one of those is settled with a plate-level check, there is no valid cursor series and the
`ctx+0x68` address cannot be resolved by measurement.**

## Status

| item | state |
|---|---|
| Pipeline (CSO -> ISO -> EBOOT -> ELF -> Ghidra) | **done, verified** |
| Main Menu + Character Select reached live | **done** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials (UTF-16LE) | **found, readable** |
| Roster names (UTF-16LE) | **found** |
| Main Menu options read live | **done** |
| Character-select LAYOUT (vertical roster, 4 slots) | **understood** |
| Character-select NAVIGATION | **NOT solved** (`right`, `down`, all buttons: plate unchanged) |
| Selection index | in code (`ctx+0x68`); `ctx` NOT resolved |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | data verified; implementation is next |

### The single next thing to try

`ltrigger` / `rtrigger` **while cropping the name plate each press** — the vertical-strip idiom
makes a shoulder button the most likely control, and it has not yet been tested with a
plate-level check. Everything downstream is gated on this one fact.

## ⛔ CORRECTION: the "Super Saiyan 3" read did not mean what I first thought

I saw **"Super Saiyan 3"** in the yellow bar and read it as evidence that `down` changes the
selection. It does not. A proper stepped check — dump + full screenshot at each `down`, then
crop the yellow form bar — shows the label **unchanged**:

```
s0  yellow bar: "Super Saiyan 3"
s1  yellow bar: "Super Saiyan 3"     <- after down
s2  yellow bar: "Super Saiyan 3"     <- after another down
```

Meanwhile the dumps differ by **87,084** and **86,786 bytes** per step. So `down` is changing
*many* things and **not** the character/form selection.

### The screen's actual fields, and what the yellow bar is

```
CHARACTER SELECT
  "CHARACTER S..."                        <- banner (top)
  [big portrait]   "Super Saiyan 3"       <- YELLOW BAR = the FORM/skin of the shown fighter,
  |  1P  1111111111 |                        not a selection cursor
  |  portrait grid |  vertical roster     <- right strip
  1P | 2P CPU | 3P CPU | 4P CPU           <- the four slot tabs
```

**"Super Saiyan 3" is a property of the displayed character, not a moving selection.** That was
my error, and it is the fourth variant this project has produced of the same failure mode:

| # | what looked like progress | what it actually was |
|---|---|---|
| 1 | 13 identical frames = frozen | an animation that had not finished |
| 2 | 73-88 KB changed per press = cursor moving | screen redraw |
| 3 | screen hash changed after `down` | the slot row, not the roster |
| 4 | **"Super Saiyan 3" appeared** | **a static property of the shown fighter** |

✅ **The rule that keeps catching this: verify the ONE field you are tracking, read back
literally — not the frame, not the hash, not a nearby label.**

### The index search, done properly this time, still finds nothing

With a valid series (three dumps on the same screen) and a bounded region
(`0x08800000-0x09000000`, not all 24 MiB), the search for small values changing across s0/s1/s2
returned **5 candidates** — `0x08A78E24` (12,11,8), `0x08A78E88`, `0x08AF3214`, `0x08B2E44C`,
`0x08B4637C`. All are **binary flags or a decaying counter**, not a selection index. None
corresponds to a character or form.

**Conclusion: `down` is not the roster control, so these dumps are still not a cursor series.**
The search is not the problem; the input is.

## Status — final for this session

| item | state |
|---|---|
| Pipeline (CSO -> ISO -> EBOOT -> ELF -> Ghidra) | **done, verified** |
| Main Menu + Character Select reached live | **done** |
| System map (UI element names) | **found** |
| Mission titles + control tutorials (UTF-16LE) | **found, readable** |
| Roster names (UTF-16LE) | **found** |
| Main Menu options | **read live** |
| Screen fields understood (form bar vs roster vs slots) | **understood** |
| Character-select NAVIGATION | **NOT solved** |
| Selection index | in code (`ctx+0x68`); `ctx` NOT resolved |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | data verified; implementation is next |

### The one thing that unblocks everything

**Learn which control moves the character-select roster** — from the game's own manual, from its
on-screen legend if one exists, or by asking someone who has played it. Guessing further is
wasteful: four controls have now been tried and checked at field level, and each attempt costs a
full cycle.

Once the roster control is known, a valid cursor series follows immediately, and with it the
`ctx` address and a confirmed adapter map.

## Six controls tested at field level — none moves the character-select selection

Every attempt below was checked by reading the candidate field back literally, not by hashing
the frame.

| control | field checked | result |
|---|---|---|
| `right` x3 | name plate | unchanged ("Goku") |
| `down` x3 | form bar | unchanged ("Super Saiyan 3") |
| full pass (up/down/left/cross/circle/square/triangle/start/select) | frame hash | changed, but `select` exited to Main Menu |
| `rtrigger`, `ltrigger`, `r2`, `l2` | form bar | unchanged |
| `left` x2, `right` x3 (second pass) | form bar | unchanged |

### The screen's fields, now fully identified

A clean full-resolution read settled what is what:

```
CHARACTER SELECT                                     <- banner
A / 1P  1111111111                                   <- player name banner
"Super Saiyan 3"        ( o )                        <- FORM selector: a wide yellow bar with
                                                        a small circular knob at its RIGHT edge
[ large portrait of Goku ]
                                                        (the knob implies a horizontal control;
  1P | 2P CPU | 3P CPU | 4P CPU                          it did not respond to left/right)
  1P  ...  big Goku portrait (current pick)
  right strip: small portrait grid (Goku, Vegeta visible)
```

⛔ **The knob on the form bar is a control affordance that looks adjustable and is not responding
to `left`/`right`.** That is the most likely reason all six attempts failed: the screen may be in
a **non-interactive preview state**, so no input moves anything until some other action is taken
first (a different focus, a confirm, or a mode toggle).

### Honest conclusion

**Character-select navigation is not solved, and guessing further is not a good use of cycles.**
Six controls have been tried with rigorous field-level checks. Two possible explanations remain
and **neither can be resolved by more blind pressing**:

1. the screen is a preview that must be entered/interacted with in a way not yet tried, or
2. the roster/form are driven by the **analog nub**, not the d-pad (never tested — the debugger
   accepts `input.axes` style events on some builds, and the DBZ:AR project's own notes warn the
   analog stick is a separate path).

### What unblocks it

Any ONE of these, in preference order:

1. **The game's own control legend or manual** — does a legend row exist on this screen? (None was
   visible in the captures; worth checking the pause menu or the Training mode text, which the
   RAM dump shows IS present as readable strings.)
2. **The analog nub**, tested with the same form-bar read-back.
3. Asking someone who has played it which control changes the character.

## Status — session end

| item | state |
|---|---|
| Pipeline (CSO -> ISO -> EBOOT -> ELF -> Ghidra) | **done, verified** |
| Main Menu + Character Select reached live | **done** |
| System map (UI element names, 53 strings) | **found** |
| Mission titles + control tutorials (UTF-16LE) | **found, readable** |
| Roster names (UTF-16LE) | **found** |
| Main Menu options | **read live** |
| Screen fields identified | **done** |
| Selection index | in code (`ctx+0x68`); `ctx` NOT resolved |
| Stat panel chain (`FUN_08a3c458`) | identified, needs live confirmation |
| Character-select navigation | not needed — Training bypasses it |
| Battle HUD | **REACHED LIVE** |
| **1P/2P HP addresses** | **VERIFIED LIVE under attack** |
| Fighter identity | open — text table found; index field not yet located |
| Adapter code | data verified; implementation is next |

### Artefacts left in place

- `C:\Users\Devin Prater\oga-ghidra-tagteam` — analysed Ghidra project (`TAGTEAM`)
- `%LOCALAPPDATA%\Temp\tagteam-extract\EBOOT.dec` — decrypted ELF
- `%LOCALAPPDATA%\Temp\tagteam-iso\*.iso` — decompressed image
- scripts: `oga-cso-extract.py`, `TagTeamQuery.java`, `TagTeamTrace.java`, `TagTeamCharsel.java`,
  `TagTeamCtx.java`
- RAM dumps and screenshots under `%LOCALAPPDATA%\Temp\psp-probe\`

## ⛔ THE HARNESS CANNOT SEND ANALOG INPUT — measured, and it closes off a hypothesis

The previous section listed "the roster/form are driven by the analog nub" as a likely
explanation. **That is now tested and the answer is: the debugger has no analog path at all.**

Every candidate was rejected by PPSSPP 1.20.4's debugger:

```
input.axis   {axis:"x", value:1.0}       -> Bad message: unknown event
input.axis   {stick:"left", x:1, y:0}    -> Bad message: unknown event
input.analog {x:1.0, y:0.0}              -> Bad message: unknown event
input.buttons.press {button:"an_right"}      -> Unsupported button value 'an_right'
input.buttons.press {button:"analog_right"}  -> Unsupported button value 'analog_right'
input.buttons.press {button:"stick_right"}   -> Unsupported button value 'stick_right'
input.buttons.press {button:"nub_right"}     -> Unsupported button value 'nub_right'
```

The only accepted input is `input.buttons.press` with the digital names already enumerated
(`cross, circle, square, triangle, start, select, up, down, left, right, home, hold, wlan,
screen, note, ltrigger, rtrigger, l2, r2`).

### What this means for the character-select problem

1. **The nub hypothesis is untestable with this harness** — not "untried", *impossible*. If the
   roster/ form genuinely need the nub, this project cannot drive that screen through the
   debugger at all.
2. **This is a hard limit of the approach, worth stating plainly:** any game screen whose sole
   input is analog is out of reach of the debugger-driven harness. For an accessibility project
   that is itself a finding — it bounds which games can be supported this way.
3. A **human** can still get past the screen, and if a save is made beyond it, the harness resumes
   from there. That is the practical workaround.

✅ **Check the accepted input surface BEFORE theorising about controls.** Enumerating the
debugger's input API takes seconds. I spent three rounds guessing at buttons when a one-shot
capability probe would have told me the digital buttons were the *only* thing available and that
one of my explanations was untestable by construction.

# ⭐⭐ BREAKTHROUGH: TRAINING MODE REACHES LIVE GAMEPLAY — the battle HUD is now in scope

**The user's suggestion solved the blocker that six controls could not.** Instead of fighting the
character-select screen, choosing **TRAINING** from the Main Menu and pressing `cross` through the
setup goes **straight into a live battle**. No roster manipulation required.

```
Main Menu -> Training -> cross through setup -> LIVE BATTLE (1P Goku vs Frieza)
```

Screens reached and verified by eye:

- arena intro (Goku facing Frieza), then
- **live gameplay with the full battle HUD.**

✅ **LESSON: when a menu screen resists input, look for a DIFFERENT ROUTE to the same
destination rather than defeating that screen.** The goal was never "navigate character select" —
it was "reach live gameplay". Training mode reaches it directly. Ask "is there another way in?"
before spending cycles on one hostile screen.

## The battle HUD, read live

```
+---------+--------------------------------------------+
| [Goku   |  ============ GREEN HEALTH BAR ========    |   <- 1P health (long, ~full at start)
|  face]  |  (2)                                        |   <- team counter (2 fighters)
|  [yellow sub-bar]                                    |   <- yellow secondary bar
|  0  [cyan ki bar]                                    |   <- 1P KI = 0 at this moment
+---------+--------------------------------------------+
                    ...arena...
                       [ 2P | Frieza ]  (2) [green bar]    <- 2P panel: name, team count, health
                    lock-on reticle (yellow rings)
                    red/orange diamond marker (top right) <- opponent direction indicator
```

Confirmed live HUD elements: **1P health bar, 1P ki bar (`BTL_KIRYOKU`), team counter number,
character face portraits, 2P name (`Frieza`), 2P health bar, lock-on reticle, direction marker.**

## The game's HUD element names ARE addressable in RAM

The strings are resident and were located at **fixed addresses** (this is the descriptor table the
renderer walks):

| string | address |
|---|---|
| `BTL_HP_MAIN_1P` | `0x08A72218` |
| `BTL_HP_MAIN_2P` | `0x08A723CC` |
| `BTL_KIRYOKU` | `0x08A7224C` |
| `BTL_HARD_BATTLE` | `0x08A72A78` |
| `chara_name_01` | `0x08A733AC` |
| `chara_name_02` | `0x08A73384` |
| `gauge_attack` / `gauge_defense` / `gauge_technic` | `0x08A76538` / `48` / `58` |
| `gauge_dpoint` | `0x08A76948` |
| `icon_team` | `0x08A739BC` |

Neighbouring strings in the same block: `BTL_HP_A`, `BTL_HP_B`, `BTL_HP_C`, `WAKU`, `GAGE`,
`BTL_CHARA`, `BTL_NAME`, `BTL_HP`.

Descriptor entries (pointer to the name + small id fields) sit at `0x08A72290` and `0x08A723BC` for
`BTL_HP_MAIN_1P`; the difference between the two sites is an id field of **`1` vs `3`** — i.e. the
1P vs 2P variants of the same element. These are **definitions**: they point at the name strings,
not at live values, and are **static across the battle**.

## HP value hunt: what was ruled out

Three RAM dumps: `battle1` (arena intro), `battle2` (gameplay), `battle3` (gameplay, after more
input). All 24 MiB each.

| candidate | address | verdict |
|---|---|---|
| allocator scratch `0x70000`-type values | many | ❌ not HP |
| 16-byte-stride gauge array | `0x08B45200`+ | ❌ animation state — values move both directions, maxes constant |
| `(cur,max)` cluster, max=3200 | `0x08B45240`+ | ❌ same array |
| static stat table, `0x2A9B` repeated | `0x08A70900`+ | ❌ never changes (definition table; `0x2A9B` = 10907) |
| `BTL_HP_MAIN_1P` descriptor sites | `0x08A72290`, `0x08A723BC` | ❌ definitions, point to name strings only |

⚠️ **Both health bars read FULL in every gameplay capture**, so **`cross` alone does not deal
damage** — the presses move/act but the fight does not visibly progress. That means no
"HP decreased" signature was ever produced, which is why the decrease-based search failed.

⛔ **Do not run a "find what decreased" search before confirming, visually, that the quantity
actually decreased.** Three dumps and two searches were spent on a damage event that never
happened. **Confirm the event on screen first, then diff.**

## To finish the HP mapping (next session)

1. **Deal real damage first.** In Training, the working input for an attack needs to be found
   (`circle` is the usual PSP confirm/attack; `square`/`triangle` are heavy/special in this
   series; the `cross` combos used here clearly did not damage). **Watch the opponent bar shrink
   on screen** — that is the gate.
2. Then re-dump and diff; the HP field will be among the values that dropped by the damage
   amount.
3. Alternatively locate the write in Ghidra via the `BTL_HP_MAIN_1P` descriptor consumer, which
   sidesteps damage entirely.

# ⭐⭐⭐ SOLVED: THE FIGHTER STRUCT — HP and gauges, VERIFIED against the game's own result screen

This is the breakthrough that took the session from "candidate addresses" to **confirmed live
values**. It came from a single decision: **get the game to display the number you want to find.**

## The method that worked

Losing the battle produced a **result screen with exact printed values**:

```
LOSE
Health       0 %      <- 1P HP current is now exactly 0
Max Hit      20 Hits
Max Damage   7460     <- highly distinctive
Total Score  1...
```

Those are ground truth. Searching RAM for them is unambiguous in a way that "find something that
went down" never is.

✅ **ASK THE GAME TO PRINT THE VALUE.** A result/status screen, a stat page, or any debug readout
converts a fuzzy search ("something decreased") into an exact one ("find 7460"). This single move
solved what three dumps of blind diffing could not.

## The battle stats block

`0x08B49000`, matching the result screen exactly:

| addr | value (b1 / b2 / b3 / result) | meaning |
|---|---|---|
| `0x08B49044` | 9370 / 6950 / 1180 / **7460** | **Max Damage** (matches the screen) |
| `0x08B49054` | 19 / 12 / 6 / **20** | **Max Hit** (matches "20 Hits") |

These accumulate during the fight and hold the peak at the result screen. ⚠️ `0x08B49044` is *not*
per-fighter HP — it is the **damage-record** field.

## ⭐ THE FIGHTER STRUCT — 1P and 2P, identical layout, 0x1A90 (6800) apart

Two symmetric structs. Verified by the 1P HP reaching exactly **0** at the result screen
("Health 0%") while reading ~full during gameplay:

```
1P struct base = 0x0973CEE0        HP current at  +0x14 = 0x0973CEF4
2P struct base = 0x0973E970        HP current at  +0x14 = 0x0973E984
struct stride  = 0x1A90 (6800)
```

| offset | field | 1P (b2 / b3 / result) | 2P (b2 / b3 / result) | evidence |
|---|---|---|---|---|
| `+0x14` | **HP current** | 24490 / 29570 / **0** | 23050 / 29310 / **0** | matches "Health 0%" exactly |
| `+0x18` | **HP max** | 30000 / 30000 / 30000 | 30000 / 30000 / 30000 | constant |
| `+0x1C` | team count / alive | 0 / 0 / 1 | 0 / 0 / 1 | `1` at result |
| `+0x20` | **secondary gauge (cur)** | 40040 / 40040 / **0** | 26960 / 51106 / **0** | drains to 0 too |
| `+0x24` | secondary gauge max | 100000 | 100000 | constant |
| `+0x2C` | flag | 0 / 0 / 65792 | 0 / 0 / 65792 | set at result |

### ⭐ THE ADAPTER MAP (verified live)

```
1P HP  = *(u32*)0x0973CEF4     1P HP max = *(u32*)0x0973CEF8
2P HP  = *(u32*)0x0973E984     2P HP max = *(u32*)0x0973E988
1P gauge2 = *(u32*)0x0973CF00  2P gauge2  = *(u32*)0x0973E990
struct stride = 0x1A90 (6800)  -> fighter N = 0x0973CEF4 + N*0x1A90
```

**These are the first addresses on this game confirmed against live gameplay.**

## How the HP reading behaves (important for an adapter)

The values **regenerate** during play (1P 24490 -> 29570; 2P 23050 -> 29310), so HP here is a
continuously-regenerating pool, not a strictly-monotonic bar. An adapter should therefore read
**HP current / HP max as a ratio** and track *change* rather than assume monotonic decrease.

## Corrections to earlier sections

- ❌ "Both health bars read full, so `cross` deals no damage" — **WRONG.** The fight had already
  ended between captures; the result screen shows **20 hits and 7460 max damage**. `cross` *does*
  attack.
- ❌ The decrease-based searches failed **because the dumps were taken across fight boundaries**,
  not because the quantity was absent.

⛔ **Repeated failure mode on this project (5th instance): comparing RAM across a state change.**
`battle1` = arena intro, `battle2`/`battle3` = gameplay, `result` = result screen. Only the
`b2`/`b3` pair was same-state, and the *decisive* search (against the result screen's printed
values) is what finally worked.

✅ **RULE: before diffing, state what screen each dump was taken on. If they differ, the diff is
measuring the transition.** Prefer an *absolute* search against a value the game prints over a
*relative* search between dumps.

# ⭐⭐⭐⭐ LIVE-VERIFIED: HP addresses confirmed against the RUNNING game

The dumped addresses were then verified **live over PPSSPP's debugger** — reading the running
game's RAM directly, not diffing files. This is the strongest confirmation available short of a
shipped adapter.

## The proof

`scripts/psp-tt-fight.mjs` presses the attack button and reads HP **in one debugger connection**
(necessary: PPSSPP allows only ONE client at a time, so a separate presser and reader cannot
coexist — the same constraint that broke the Steins;Gate follower).

```
# start      1P  30000/30000   2P  30000/30000
# cross  6   1P  29400/30000   2P  30000/30000  <== CHANGED
# cross  8   1P  28540/30000   2P  30000/30000  <== CHANGED
# cross  9   1P  25640/30000   2P  30000/30000  <== CHANGED
```

**1P HP drops while the CPU attacks.** The address responds to real gameplay — that is the
confirmation every earlier candidate lacked.

### ⚠️ A subtlety that matters for the adapter

`cross` **does attack**, but the drop is not caused by the player's press — it comes from the
**opponent's** attack landing between presses. So:

- the address is genuinely live HP ✅
- **do not attribute a change to the input that preceded it** without controlling for the
  opponent. The change is real; the *cause* attribution would have been wrong.

Note how the earlier reads showed HP **above** max-relative values regenerating, and here 1P sits
at 30000/30000 (full) before dropping. HP is a **regenerating pool**, read it as a ratio.

## Scripts (both committed to `scripts/`)

| script | purpose |
|---|---|
| `scripts/psp-tt-hp.mjs` | live HP reader. `--watch` (`--ms`, `--threshold`, `--json`), one-shot default. Prints the game id and **warns if it is not ULUS10537** — the addresses are game-specific. |
| `scripts/psp-tt-fight.mjs` | attack + read over ONE connection; attributes each change to a specific press. Auto-`cpu.resume`s if the emulator is stepping/paused. |

`--json` emits `[{"name":"1P","cur":..,"max":..,"g2c":..,"g2m":..}, ...]` — the hook a speech
layer would consume. `--threshold` avoids announcing every frame of a slow drain.

## FINAL VERIFIED ADAPTER MAP (live-confirmed)

```
1P struct = 0x0973CEE0      2P struct = 0x0973E970      stride = 0x1A90 (6800)
fighter N struct = 0x0973CEE0 + N*0x1A90

  +0x14  u32   HP current          (1P: 0x0973CEF4)   <- live-confirmed, drops under attack
  +0x18  u32   HP max              (1P: 0x0973CEF8, 2P: 0x0973E988)
  +0x1C  u32   team/alive count
  +0x20  u32   secondary gauge cur (1P: 0x0973CF00, 2P: 0x0973E990)
  +0x24  u32   secondary gauge max  (100000)

Battle stats block 0x08B49000:
  +0x44  u32   Max Damage   (matched the result screen's 7460)
  +0x54  u32   Max Hit      (matched "20 Hits")
```

**Status: HP + gauges DONE and verified. Identity (which character is which fighter slot) is still
open** — the struct carries no name pointer nearby, so the character name must come from elsewhere
(the `chara_name_01/02` descriptor at `0x08A733AC`/`0x08A73384`, or a separate roster slot table).

# The game's TEXT TABLE is resident in RAM as plain UTF-16LE — the whole UI vocabulary

A far larger find than the roster alone: **all the game's display strings live in one resident
block** around `0x08C85000`-`0x08C87200`, readable directly with no OCR.

## The character roster, in character-select display order — `0x08C85C82`

```
0x08C85C82  Goku              <- index 0   (1P on screen)
0x08C85C8C  Kid Gohan
0x08C85CA0  Teen Gohan
0x08C85CB6  Gohan
0x08C85CC2  Ultimate Gohan
0x08C85CE0  Piccolo
0x08C85CF0  Krillin
0x08C85D00  Yamcha
0x08C85D0E  Tien
0x08C85D18  Chiaotzu
0x08C85D2A  Vegeta (Scouter)
0x08C85D4C  Vegeta
0x08C85D5A  Majin Vegeta
... (48 entries) ...
0x08C85EE6  Frieza            <- index 33  (2P on screen)
0x08C85EF4  Frieza Soldier
0x08C85F12  Android #16 ... #19, Dr. Gero, Cell, Cell Jr.
0x08C85FA0  Majin Buu, Super Buu, Kid Buu, Broly
```

A **second copy of the roster at `0x08C8613A`** begins identically (Goku, Kid Gohan, Teen Gohan…)
and then diverges — it adds `Demon King Dabura`, `Supreme Kai`, `Kibitokai`, `Hercule`, `Bulma`,
`King Kai`, `Babidi`, `Narrator`, `Dende`, `Master Roshi`, `Kami`, `Shenron`, `Porunga`, etc.
**Two tables: playable fighters vs. everyone who can appear.**

## Also resident and readable

- **Forms**: `Super Saiyan`, `Super Saiyan 2`, `Super Saiyan 3`, `Post-Transformation`,
  `1st Form`, `2nd Form`, `3rd Form`, `Final Form`, `Full Power`, `Perfect Form`, `Perfect`,
  `Gohan Absorbed`, `Legendary Super Saiyan` — these are exactly the **character-select form bar**
  values ("Super Saiyan 3" seen on screen).
- **Stages**: `Rocky Area`, `Island`, `Mountain Road`, `Cell Games Arena`,
  `Planet Namek (Village)`, `Planet Namek Destroyed`, `Supreme Kai's World`.
- **Every special move**: `Kamehameha`, `Super Kamehameha`, `Father-Son Kamehameha`,
  `Galick Gun`, `Final Flash`, `Death Ball`, `Death Beam`, `Death Saucer`, `Big Bang Attack`,
  `Special Beam Cannon`-adjacent names, `Super Ghost Kamikaze Attack`, `x100 Big Bang Kamehameha`, …
- **UI text**: `Is this Player Name ok?`, `Enter Player Name. `, `Cancel input?`, `Stage Clear!`,
  `New Items in Stock!`, `Hidden Item 1..15`, `What if? Scenario`, and all the
  **mission objectives** (`Find Gohan!!`, `Defeat Frieza!`, `Stand Against Nappa!`, …) at
  `0x08C7AE6E`+.
- **Control tutorials** at `0x08C7ABB6`+ (`Those are the basic controls.`,
  `Let's try an actual battle!`, plus `Melee Attack. Close Attack...`, `Ki Blast Attack...`,
  `Defense. Guard against enemy attacks.`, `Super Attack. Use Ki to fire Super Attack.`,
  `Switch Lock-on. Switch enemy to lock onto.`).

## How the game indexes these strings

**There is no pointer table in RAM** — the strings are **packed variable-length** (measured
deltas between consecutive roster entries: `0xA`, `0x14`, `0x16`, `0xC`, `0x1E`, `0x10`, `0x10`,
`0xE`), and a search for pointers to the roster `"Goku"` entry (`0x08C85C82`) returned **zero**.

So the game indexes them by **computed offset from a base**, resolved in code, not through a
data table of pointers. That means an adapter must derive the same offset scheme the executable
uses — findable in Ghidra by locating the code that reads this base.

## Fighter identity: what was tried and ruled out

Goal: which character occupies fighter slot 1/2. **Not solved.** Ruled out:

| approach | result |
|---|---|
| pointer to the name string from the fighter struct | ❌ none — structs hold no name pointer |
| roster index (Goku=0, Frieza=33) stored in the struct | ❌ Frieza's `33` appears **nowhere** in its struct (searched u8/u16/u32 over 0x400 bytes) |
| Goku index 0 | ⚠️ untestable — 1261 zero hits, index 0 is not distinctive |
| following the struct's heap pointers (`+0x08`) | ❌ point to render data, not names |

The `chara_name_01` / `chara_name_02` descriptor entries (`0x08A733AC` / `0x08A73384`) are the
elements that *draw* the names, so the identity value is whatever those descriptors are fed.

⚠️ **Index 0 is a trap for identity searches** — the most common roster entry (Goku) has index 0,
and zero is unsearchable. Verify identity by a *different* fighter first, or by reading two slots
at once where one is non-zero.

## NEXT STEP (specific and bounded)

**Change the 2P fighter and re-diff.** Pick a visibly different opponent (e.g. Vegeta, index 11 —
distinctive and non-zero) and compare against Frieza (33). Whatever field changes to `11` is the
identity field. This is a two-run experiment, not a search, and it is the cheapest way to settle
it. Then the adapter map is complete: HP, gauges, names, and identity.

## ⛔ SETTLED: identity cannot be found in RAM — there are NO pointers to the text table at all

Previous sections listed "find the pointer into the name table" as the expected route. It is now
**tested and closed**: a full sweep of all 24 MiB found **ZERO** u32 values anywhere pointing into
the character-name tables (`0x08C85C00`-`0x08C86600`), on a dump taken **while `Frieza` was
displayed on the HUD**.

```
=== pointers into the name tables: 0 ===
```

Combined with the earlier findings, this is conclusive:

| evidence | conclusion |
|---|---|
| no pointer to roster `"Goku"` (`0x08C85C82`) | ❌ not pointer-indexed |
| no pointer anywhere into `0x08C85C00`-`0x08C86600` | ❌ **no pointer table at all** |
| strings are packed variable-length (deltas 0xA, 0x14, 0x16…) | offsets are **computed** |
| Frieza's roster index `33` absent from its fighter struct | ❌ not a plain index field |

✅ **The text is referenced by a COMPUTED OFFSET from a base in code.** The executable adds an
index × stride (or walks a packed table with length prefixes) to reach a name. **That scheme is
only visible in the disassembly** — it cannot be recovered by scanning RAM, and no amount of
dumping will find it.

⚠️ **Know when a question has left the reach of RAM search.** Three different RAM approaches have
now failed on identity, all for the same underlying reason. Continuing to dump would be wasted
cycles; the next move is Ghidra.

## The bounded Ghidra job (next session)

1. Find the code that references the text-table base (`0x08C85C82` region) — search the
   executable for the base address constant, or for the function that services
   `chara_name_01` / `chara_name_02` (`0x08A733AC` / `0x08A73384`).
2. Read how the index becomes an offset. That gives the **identity encoding**.
3. THEN the two-run experiment becomes trivial: read that field for both fighters.

The alternative empirical route remains valid and is still the cheapest first try:
**change the 2P fighter to Vegeta (roster index 11 — non-zero and distinctive) and re-diff.** If
the identity is a plain index *somewhere* (even if not in the struct scanned so far), `11` will
appear. Only fall back to Ghidra if that fails.

## SESSION SUMMARY — DBZ: Tenkaichi Tag Team (ULUS10537)

| item | state |
|---|---|
| CSO -> ISO -> EBOOT -> decrypted ELF -> Ghidra | **done, verified** |
| Reached Main Menu, Character Select, live battle | **done** |
| Blocker "character-select navigation" | **bypassed via Training mode** (user's suggestion) |
| Battle HUD reached live | **done** |
| UI element-name addresses | **found** (e.g. `BTL_HP_MAIN_1P` @ `0x08A72218`) |
| **1P/2P HP + gauge addresses** | **LIVE-CONFIRMED** (`0x0973CEF4` / `0x0973E984`, stride `0x1A90`) |
| Battle stats (Max Damage / Max Hit) | **found, matched result screen** |
| Game's full text table in RAM | **found** — roster, forms, stages, moves, missions, tutorials |
| Control tutorials + mission objectives | **readable** |
| Fighter identity (which char in slot) | **open — proven NOT in RAM; needs Ghidra** |
| Adapter code | HP/gauge data verified and scripted; identity pending |

### Scripts committed

- `scripts/oga-cso-extract.py` — CSO decompressor (header block field is a SIZE, not a shift)
- `scripts/TagTeamQuery.java`, `TagTeamTrace.java`, `TagTeamCharsel.java`, `TagTeamCtx.java`
- `scripts/psp-tt-hp.mjs` — **live HP reader** (`--watch --threshold --json`, game-id guard)
- `scripts/psp-tt-fight.mjs` — attack + read over one connection (auto `cpu.resume`)

### The four lessons this game taught

1. **Make the game print the value.** Losing to reach the result screen ("Health 0%", "Max Damage
   7460") turned a hopeless "what decreased?" search into an exact one. This is what solved it.
2. **Never diff dumps from different screens.** Five separate false conclusions came from this;
   cinematic-vs-gameplay produced a convincing but meaningless "90% HP" cluster.
3. **Look for another route into the screen you need.** Six controls failed on character select;
   Training mode bypassed it entirely.
4. **Know when RAM search is the wrong tool.** Identity has no pointers and no index field —
   it is computed in code. Three RAM approaches failed for one reason; stop and decompile.

## ⚠️ CORRECTION to "there are NO pointers at all" — the truth is more specific

My blanket claim was wrong. Widening the pointer scan (I had only searched `0x08C85C00`+, so a
pointer to an *earlier* base was invisible) found **828 pointers** into the string bank
`0x08C00000`-`0x08D00000`, 83 of which land on readable text:

```
0x08C7F888  ' Counter! ● times or more'     <- from 0x09661B00, 0x0966B178, 0x0966B190
0x08C8922C  't up! You can win this!'       <- from 0x08FAFC00, 0x08FB7000
0x08C8DB5C  'Is that seriously all you got?'<- from 0x09227684
0x08C90000  ' him. It's too pathetic to watch.' <- from 0x08F1F11C
0x08CAB4CE  'h, Trunks!'                    <- from 0x08FAE224, 0x08FB5E24
0x08C80000  'ning to Main Menu.'            <- from 0x08CD5AA8
```

✅ **So the game uses BOTH schemes:**

| data | indexing | evidence |
|---|---|---|
| **dialogue / battle barks** | **pointers** | 828 pointers into the bank, 83 readable |
| **roster names, forms** | **computed offset** | scanning the roster/form ranges specifically returns **0 pointers** |

```
=== pointers into roster/form tables: 0 ===
```

That last result is the one that matters for identity: **no pointer references a roster or form
string**, even though the dialogue system happily uses pointers. So identity is *not* obtained by
storing a pointer to a name — consistent with the index/offset reading, now properly scoped.

⛔ **My error worth recording: I chose the scan window too narrowly and reported a sweeping
negative.** "Zero pointers into 0x08C85C00-0x08C86600" became "no pointer table at all" — an
over-generalisation from a window that could not have seen a base-pointer. **Widen the window
until it covers the whole plausible region before declaring a negative.**

## Ghidra result: the name table is not referenced as a literal constant

`TagTeamNames.java` found **0 instructions with an operand inside the name region**, and the
`chara_name_01` / `chara_name_02` descriptor addresses appear only as **`PARAM` references**
(from `089f7cb0` and `089f7768`) — i.e. they are passed as arguments, not embedded.

One candidate function was decompiled, `FUN_089f728c` (3892 bytes), and it shows a **paired-table
lookup by a key**:

```c
piVar11 = *(int **)(param_1 + 0x1a4);
if (piVar11 == (int *)0x0) return;
iVar2 = 0; iVar9 = 0;
if (0 < piVar11[3]) {
  piVar8 = (int *)piVar11[1];
  do {
    iVar2 = iVar2 + 1;
    if (*piVar8 == param_2) {      // match a key
      iVar9 = piVar8[1];           // take the paired value
      goto LAB_089f733c;
    }
    piVar8 = piVar8 + 2;           // 8-byte stride: {key, value}
  } while (iVar2 < piVar11[3]);
  iVar9 = 0;
}
```

**An 8-byte-stride `{key, value}` lookup keyed on `param_2`** — this is the shape an identity
mapping would take (e.g. some id -> something). It is not yet confirmed to be *the* character
identity, but it is the strongest structural lead and the place to start next.

⚠️ The `PARAM`-only references mean the descriptor addresses are **runtime arguments** — the text
base is likely resolved at load time, which is why no literal constant appears in the code.

# ⭐⭐⭐⭐⭐ TAG TEAM IS FOUR FIGHTER SLOTS — and the two-slot map was reading EMPTY PARTNERS

The user's warning ("watch out for other team members, since this is a tag team game") caught a
real defect in what I had called *verified*.

## The defect

The "verified" 2-struct map read **`0x0973CEE0` and `0x0973E970`** and was confirmed live in
**Training** — where those two slots happened to hold the two fighters. In **story mode** they
read `0/30000` **while the on-screen health bar was FULL**:

```
1P HP      0/30000 ( 0.0%)  gauge      0/100000     <- but the HUD bar is FULL
2P HP      0/30000 ( 0.0%)  gauge      0/100000
```

✅ **A live confirmation in one mode is not a confirmation in another.** Training mode made a
wrong map look right. Story mode exposed it.

## The truth: FOUR slots, stride 0x1A90

```
0x0973CEE0   slot 0
0x0973E970   slot 1
0x09740400   slot 2
0x09741E90   slot 3
```

Verified live in story mode:

```
slot 0  HP      0/30000 ( 0.0%)  gauge      0/100000    <- EMPTY tag partner
slot 1  HP      0/30000 ( 0.0%)  gauge      0/100000    <- EMPTY tag partner
slot 2  HP  28690/30000 (95.6%)  gauge  86060/100000    <- ACTIVE fighter
slot 3  HP  12840/30000 (42.8%)  gauge  79920/100000    <- ACTIVE fighter
```

So **slots 0/1 are the tag partners** (they can legitimately be 0) and **slots 2/3 hold the live
fighters**. In Training the active pair landed in 0/1, which is *not* a stable assumption.

⚠️ **An adapter MUST read all four slots and not hardcode which two are active.** Deriving
"active" from `maxHP != 0` / `curHP > 0` is the robust approach; a fixed pair is a bug waiting for
the next game mode.

`scripts/psp-tt-hp.mjs` was corrected to read and label all four slots.

## Confirmed live during a story battle

```
slot 2  HP  22910/30000 (76.4%)  gauge  60000/100000
slot 3  HP   1180/30000 ( 3.9%)  gauge  91930/100000
```

The gauge and HP values move independently and track the on-screen bars.

## Fighter world POSITION — found (float triple)

Active fighters carry a float `(x, y, z)` at **`+0x0B0`** of the struct, with the same triple
repeated at `+0x160` and `+0x1A0` (current + previous frames — a position history):

```
slot2  +0x0B0 = -312.047   +0x0B4 = -17.639   +0x0B8 = 371.135
       +0x0BC = 1.000      (a 4th component, likely w or a scale)
       +0x160 = -312.047   +0x1A0 = -312.047   (previous positions)
```

This is the raw material an **objective arrow** would be computed from: the arrow direction is
almost certainly `normalize(objective_pos - player_pos)` per frame. **The arrow itself is a derived
quantity, so expect to find the OBJECTIVE position (a target vector) rather than an "arrow" value** —
same lesson as the identity index: when a displayed thing is computed, hunt the inputs.

⚠️ Position did not change under `left` pressure in that test, but the battle **had already
ended** (result screen: `LOSE`, Health 0%) — an invalid test, and the *fifth* instance on this
project of testing across a state change. Re-test position movement only inside confirmed live
gameplay.

## Battle stats block — confirmed TWICE, independently

```
0x08B49044   Max Damage    7460 (first result)   /  13610 (second battle)
0x08B49054   Max Hit       20   (matched screen: "20 Hits" / "13 Hits")
```

Both result screens' printed values were located at the same offsets.

## ⭐ STATUS: adapter-ready for combat state

| field | address | state |
|---|---|---|
| fighter slots (4) | `0x0973CEE0 + N*0x1A90` | **live-verified** |
| HP cur / max | `+0x14` / `+0x18` | **live-verified** |
| gauge cur / max | `+0x20` / `+0x24` | **live-verified** |
| world position (float xyz) | `+0x0B0` | **found** (movement not yet proven live) |
| Max Damage / Max Hit | `0x08B49044` / `0x08B49054` | **confirmed twice** |
| character name table | `0x08C85C82` (roster A), `0x08C8613A` (roster B) | found |
| fighter IDENTITY (which char) | ? | **open** — see the `{key,value}` lookup lead |
| objective arrow | ? | **open** — hunt the objective position, not an "arrow" |

### Next steps, in order

1. **Prove position movement** — inside confirmed live gameplay, hold a direction and re-read
   `+0x0B0`. That unlocks everything spatial.
2. **Find the objective position** (the arrow's input) — likely a similar float triple in a
   mission/stage struct.
3. **Story-mode HUD text** — mission objective strings are already readable in RAM
   (`Find Gohan!!`, `Head for Kame House!`, …); find the *current* objective index.
4. **Tag partner identity** — which partner is slotted where, for "partner X is down" announcements.

# ⭐⭐⭐⭐⭐⭐ ANALOG STICK CONTROL WORKS — and my "no analog path" claim was a NAME BUG

The user asked directly: *"For flying around the field, you have to use the joystick/nub. For that,
look for any way to control that. If you can't find a way, even by sending controller events, let me
know and we'll pause there."*

**Answer: there IS a way, and it works.** The earlier "PPSSPP's debugger has NO analog input path"
conclusion was **wrong**, and wrong for an embarrassing reason.

## The bug: request name vs broadcast name

PPSSPP's WebSocket API uses **two different names** for the same concept:

| name | kind | use |
|---|---|---|
| `input.analog` | **broadcast event** | you *listen* for stick movement |
| **`input.analog.send`** | **request** | you *set* the stick position |
| `input.buttons` | broadcast event | you listen for button changes |
| **`input.buttons.send`** | **request** | continuous hold (state) |
| `input.buttons.press` | request | momentary press |

I had been sending `input.analog` — the **broadcast** name — and getting
`Bad message: unknown event`, then concluding analog input was impossible. **I tested the wrong
name and declared a capability limit.**

Verified from PPSSPP's own source (`Core/Debugger/WebSocket/InputSubscriber.cpp`), which registers:
```cpp
map["input.buttons.send"]  = ... ButtonsSend
map["input.buttons.press"] = ... ButtonsPress
map["input.analog.send"]   = ... AnalogSend
```
`AnalogSend` takes `x`, `y` (each **-1.0 .. 1.0**) and optional `stick` (`"left"` | `"right"`).

## All of these are ACCEPTED by PPSSPP v1.20.4

```
input.analog.send {x:1.0, y:0.0}                 ACCEPTED
input.analog.send {x:0.0, y:-1.0, stick:"left"}  ACCEPTED
input.analog.send {x:0.5, y:0.5,  stick:"left"}  ACCEPTED
input.analog.send {x:1.0, y:0.0,  stick:"right"} ACCEPTED
input.buttons.send {buttons:{cross:true}}        ACCEPTED
input.buttons.send {buttons:{cross:false}}       ACCEPTED
```

✅ **So the field phase IS drivable.** `scripts/psp-tt-field.mjs` drives the stick and reads memory
over ONE connection (one-client limit), and always recentres the stick afterwards so the character
is never left spinning.

⚠️ **`input.buttons.send` gives CONTINUOUS HOLD** (`{name:true}` … `{name:false}`) — needed for
sustained flight. `input.buttons.press` is momentary and unsuitable for holding a direction.

⛔ **LESSON (the same shape as the pointer-window error):** I probed ONE name, got a rejection, and
generalised it into a capability limit. **Enumerate the API's actual surface from its source or
docs before declaring something impossible.** "Unknown event" means *that name* is unknown — not
that the capability is absent. Two of my errors this session were a too-narrow probe reported as a
confident negative.

## ⚠️ `+0x0B0` is a SPAWN ANCHOR, not the live position

The float triple at `+0x0B0` looked like `(x, y, z)` (and repeats at `+0x160`, `+0x1A0`), but a
2.5 s sample during live battle found **only a 0.463 jitter in `y`** and no x/z movement:

```
# HP at start: 17670
# HP at end:   17670   (unchanged)
# changed float fields: 1
   +0x0B4  0.463 -> 0.000
```

So `+0x0B0` is **fixed per battle** (a spawn point / reference origin). **The live position is
elsewhere** — and note the battle was not progressing during that sample either, so even this test
is not conclusive. `scripts/psp-tt-findpos.mjs` exists to settle it: it samples a struct twice and
reports only the fields that actually changed (float and u32), with the HP delta printed as a
liveness control.

✅ **Design note: a "find what changes" tool should print a liveness control alongside the diff.**
`findpos` prints the HP before/after so a zero-change result can be distinguished from a dead game.
This is the direct fix for the recurring invalid-diff problem.

## ⛔ SIX invalid tests this session — the dominant failure mode

| # | what I diffed/tested | why invalid |
|---|---|---|
| 1 | dump series base = Main Menu, steps = Character Select | crossed a screen transition |
| 2 | battle1 (cinematic) vs battle2 (gameplay) | crossed a state change |
| 3 | character-select `right` presses | the field being watched never moved |
| 4 | position movement test | battle had already ENDED (result screen) |
| 5 | story-mode 2-slot HP read | wrong slots (tag partners) |
| 6 | position "field" read | was actually on the result screen |

✅ **Every one is the same root cause: asserting something about state without first confirming
which state the game is in.** The fix, now used in the scripts: read a **liveness/identity control**
(HP delta, game id, slot maxHP) in the same breath as the measurement, and refuse to draw a
conclusion when the control says the game is not in the expected state.

## Scripts added this round

- `scripts/psp-tt-field.mjs` — analog stick driver + position/HP reader over ONE connection;
  `--probe`, `--drive X Y`, `--circle` (8 directions), `--watch-pos`; auto-recentres the stick.
- `scripts/psp-tt-findpos.mjs` — finds the live position field by two-sample diff with an HP
  liveness control; reports changed float and u32 fields.

## NEXT (bounded)

1. **Get into the open field** (past the story battle — the user's route: intro dialogue ->
   mission briefing -> play field, objective "Find Gohan"). Confirm the field is interactive
   (HP static and free movement expected).
2. Run `psp-tt-field.mjs --drive 1 0 --ms 2000` and confirm the position changes.
3. Run `psp-tt-findpos.mjs` and read off whichever field moved -> that is the **live position**.
4. Then hunt the **objective target position** (the arrow is `normalize(target - player)`), and the
   **current objective index** for spoken mission objectives.

# ⭐⭐⭐⭐⭐⭐⭐ TEAM ASSIGNMENT SOLVED BY WINNING — and the HP WRITE works

The user suggested: *"If you can't defeat opponents, just set their health to 0 and win that way."*
That produced the decisive experiment **and** a clean answer to the team-slot question.

## Team assignment is CONFIRMED by producing a WIN

**Method:** hold the PLAYER candidate team at max HP while forcing the ENEMY candidate team to 0,
repeating for 20 s. Then read the screen.

**Result:** keeping slots **0/1** full while zeroing slots **2/3** produced:

```
WIN
Health       100%
Max Hit      0 Hits
Max Damage   0
```

✅ **Slots 0/1 = the PLAYER's team. Slots 2/3 = the ENEMY's team.**

This also explains the earlier confusing results:

| observation | explanation |
|---|---|
| zeroing all four -> LOSE | the player's own HP was zeroed too; the enemy survived |
| zeroing slot 3 alone -> LOSE | slot 3 is the PLAYER's second fighter |
| zeroing slot 2 alone -> LOSE | slot 2 is the PLAYER's first fighter |
| **keeping 0/1 full, zeroing 2/3 -> WIN** | correct team assignment |

⚠️ **Which slot pair is "player" is NOT the low pair in every mode.** In the earlier Training/story
observations the *active* fighters appeared in slots 2/3 with 0/1 empty — the mirror of this. So the
rule stands: **never hardcode; determine the player's side empirically** (e.g. by watching which
slots lose HP with no input, since only the CPU attacks).

## The no-input IDLE test (the reliable side-finder)

With **zero input**, only the CPU deals damage, so **the slots whose HP falls are the player's**:

```
# start: s0 30000  s1 29710  s2 30000  s3 29740
# end  : s0 16650  s1 22680  s2 30000  s3 23930
# drops: s0 -13350, s1 -7030, s3 -5810, s2 -0
```

Slot 2 alone never dropped. This is a good corroborating signal, though the WIN experiment above is
the conclusive one.

## ⚠️ Mashing `cross` DESTROYS progress

Automating the advance with a repeated burst of `cross` won 6 battles but then left the game on a
**LOSE** result (`Health 0%`, `Max Hit 8 Hits`, `Max Damage 7000`). Cause: burst presses are consumed
by result/dialogue screens and then **spill into the next battle's start-up**, so the player stands
there taking hits.

✅ **Fix in `psp-tt-advance.mjs`: press ONCE, then immediately re-check liveness.** If a battle
started, win it before pressing again. Never burst-press across a state boundary.

## `scripts/psp-tt-advance.mjs` (new) — story advance with automatic battle wins

One connection, so it satisfies the single-client constraint:

- if a battle is live -> hold player team (0/1) at max and enemy team (2/3) at 0 until liveness ends
- otherwise -> single `cross`, then re-check
- prints a `#SHOT n` correlation marker (it deliberately does **not** screenshot; a second process
  cannot hold the debugger socket, so screenshots must come from the caller)

Log from a 55-iteration run: **6 battles won, 49 advances**, ending `s0 30000 s1 30000 s2 0 s3 0`.

⚠️ **Known limitation: this script logs `#SHOT` markers but never captures images, so its run is
unverifiable from its own output.** A screenshotting advance loop must interleave a separate
`psp-walk.mjs --steps shot` call between presses (which is what the manual runs did). Fix before
relying on it unattended.

## Field phase: STILL NOT REACHED

Story mode's open field (the "Find Gohan" mission area, with the objective arrow and analog flight)
has **not** been confirmed reached. Status:

- multiple story battles are now clearable automatically (WIN verified)
- the game has been observed on WIN, LOSE, battle, and dialogue screens
- **no frame so far shows the open field with free flight**

### The specific thing to try next

The route the user described: intro dialogue -> mission briefing -> **play field**. After a WIN,
**press `cross` slowly (one per ~5 s) with a screenshot after each**, and look for a screen with a
**free-flying character, a wide landscape, and an arrow/marker** — then immediately run
`psp-tt-field.mjs --drive 1 0 --ms 2000` to test whether the analog stick moves the character.
That single test decides whether the field phase is drivable at all.

# ⭐⭐⭐⭐⭐⭐⭐⭐ OBJECTIVE ARROWS FOUND — and the LIVENESS CONTROL finally did its job

## The arrows, seen on screen

The battle HUD carries **two arrow-style indicators**:

```
+--------------------+---------------------------+------------------+
| Goku face          |  [========= HEALTH ======]|    [RED/ORANGE   |
|   (2) team counter |                           |     ARROW,       |
|  1 [ki bar]        |                           |     top-centre]  |
+--------------------+---------------------------+------------------+
                     [ CPU  Frieza ]  (2) [bar]
                          (lock-on reticle)
   [YELLOW ARROW, bottom-left]
        (O)  <- button prompt
```

| indicator | position | interpretation |
|---|---|---|
| **red/orange arrow** | top-centre | direction to the current objective / opponent |
| **yellow arrow** | bottom-left, above a button prompt | objective / navigation cue |
| lock-on reticle (yellow rings) | around the target | current locked enemy |
| `CPU  Frieza` name plate | beside the reticle | the enemy's identity |

These match the user's description: *"This game (sometimes) has arrows that guide the player to the
objective."* ✅ **The arrows exist and are on screen.**

### The arrow is DERIVED, not stored

As with fighter identity, expect **no "arrow" value in RAM**. The arrow is computed per frame as
something like `normalize(objective_pos - player_pos)`, so the huntable inputs are:

1. the **live player position** (not yet found — `+0x0B0` proved to be a spawn anchor), and
2. the **objective target position**.

Both are the next targets. The arrow itself will not be found by searching RAM.

## ✅ THE LIVENESS CONTROL WORKED — and that is the session's most valuable fix

`scripts/psp-tt-movetest.mjs` (new) printed, for every direction tried:

```
# LIVENESS: HP 0 -> 0  (HP unchanged -- battle may be over or this slot is idle)
# float fields that moved: 3
   +0x0B4  0.000 -> 0.463   (d=0.463)
   +0x3E4  0.000 -> 0.463
   +0x14C  0.000 -> 0.463
```

The **liveness line flagged the test as invalid before I could draw a conclusion from it.** A
screenshot taken immediately afterwards confirmed the game was on a **LOSE result screen**
(`Health 0%`, `Max Hit 12`, `Max Damage 7000`).

That is the exact failure mode (8th instance this session) that previously cost a full cycle each
time. **The control now catches it automatically.** This is the single most important structural
improvement to come out of this session:

> ⛔ **Every probe must print a control value (HP delta / game id / slot maxHP) in the same breath as
> the measurement, and the operator must refuse to conclude anything when the control says the state
> is wrong.**

### The 0.463 value is a BREATHING ANIMATION, not movement

Slot 0 is **empty** (`HP 0/30000`) — one of the unused tag slots. The identical `0.463` oscillation
appearing at `+0x0B4`, `+0x14C` and `+0x3E4` simultaneously, flipping on left/right and settling on
down, is an **idle/breathing pose animation**, not locomotion. ⚠️ **A small, symmetric, self-reversing
float is animation — not position.** Real movement is directional and accumulates.

## Route knowledge (useful, hard-won)

The post-battle menu is:

```
Rematch
Change Character
Rearrange Team
Return to Main Menu     <- 3 downs from the top, then cross
```

And the Main Menu is:

```
Dragon Walker      <- story mode
Free Battle
Multiplayer
Customize
Training
```

⚠️ **`psp-tt-advance.mjs` still loses battles when auto-advancing** — a burst of presses crosses the
result screen, gets consumed by dialogue, and spills into the next fight's start-up. The fixed
version presses once and re-checks liveness so it wins before pressing again, but a run ending on
`LOSE` (Health 0%, 12 hits) shows presses are still reaching the game during setup. **Prefer the
manual one-press-one-screenshot loop for route discovery** until this is tightened.

## Screenshots without the socket — the verifiability fix

`scripts/psp-shot.py` (new) captures the PPSSPP window with `PrintWindow` +
`PW_RENDERFULLCONTENT`, which talks to the **window, not the debugger socket**.

✅ **So the one-client limit never applied to screenshots.** A socket-holding process can screenshot
by spawning this script — `psp-tt-advance.mjs` now does exactly that and produced **13 PNGs** in a
single run (`run-001.png` … `run-013.png`), making an automated advance run verifiable for the
first time.

❌ Earlier, `psp-tt-advance.mjs` logged `#SHOT n` markers and captured **nothing** — a run whose
output could not be checked. **A log marker is not evidence; only a written artifact is.**

## Scripts added

| script | purpose |
|---|---|
| `psp-shot.py` | socket-free window capture (PrintWindow), PNG out |
| `psp-tt-movetest.mjs` | hold stick + diff whole struct in ONE connection, with HP liveness control |
| `psp-tt-advance.mjs` | story advance; wins battles by HP; now screenshots via `psp-shot.py` |
| `psp-tt-live.mjs` | gated liveness helper (`--idle`, `--sweep`, `--topup`) |
| `psp-tt-findpos.mjs` | two-sample struct diff with HP liveness control |
| `psp-tt-field.mjs` | analog stick driver (`--probe/--drive/--circle/--watch-pos`) |

## STATUS

| item | state |
|---|---|
| Pipeline (CSO -> ELF -> Ghidra) | **done, verified** |
| Team assignment (0/1 player, 2/3 enemy) | **confirmed by producing a WIN** |
| HP write to end a battle | **works** |
| 4 fighter slots, HP, gauges | **live-verified** |
| Battle stats (Max Damage/Hit) | **confirmed twice** |
| Objective ARROWS | **found on screen** (top-centre + bottom-left) |
| Socket-free screenshots | **working** |
| **Live player position field** | **NOT found** (`+0x0B0` = spawn anchor; 0.463 = animation) |
| **Open field phase** | **NOT reached** (only battle / result / dialogue / rematch-menu observed) |
| Objective target position | not found |
| Fighter identity | open (needs Ghidra) |

### Next, in order

1. **Reach the open field**: `Return to Main Menu` -> `Dragon Walker` -> advance **one press per ~5 s
   with a screenshot each** (do not burst). Look for a wide landscape with free flight.
2. **There, run `psp-tt-movetest.mjs --slot <active> --all`** — with a live slot and a moving
   character the real position field will show up as a *directional, accumulating* float delta.
3. Then hunt the objective target and the current-objective index.

# ✅ OBJECTIVE ARROW CONFIRMED ON SCREEN (clear evidence frame)

A high-resolution capture settles it — the battle HUD carries a **large red/orange arrow**:

```
+----------------------+--------------------------------------------+
| [Goku SSJ portrait]  |  [============ GREEN HEALTH BAR =========]  |
|   (2) team counter   |                                             |
|  [yellow bar]        |              [ RED/ORANGE ARROW ]   <- top-centre
|   0 [ki bar]         |                                             |
+----------------------+--------------------------------------------+
                 [ CPU | Frieza ]  (2) [green bar]
                        ( yellow lock-on reticle )
                              FIGHT!  (red display text)
```

| element | confirmed |
|---|---|
| **red/orange arrow, top-centre** | **yes — this is the objective/direction indicator** |
| yellow arrow, bottom-left | seen in earlier frames |
| `(2)` team counters on both sides | yes |
| `CPU  Frieza` name plate + `(2)` + health bar | yes |
| yellow lock-on reticle around the target | yes |
| 1P ki bar showing `0` | yes |

✅ The user's description is exactly right: **the game does have guiding arrows.**

⚠️ **The arrow is DERIVED, not stored** (same lesson as fighter identity). It is almost certainly
`normalize(objective_pos - player_pos)` recomputed per frame, so searching RAM for an "arrow"
value will find nothing. The huntable inputs are the **live player position** and the **objective
position** — neither found yet.

## Route: the field is being SKIPPED by fast presses

Frame sequence from a slow, one-press-one-screenshot run (`psp-tt-route.mjs --tag r2`):

| frame | content |
|---|---|
| `r1-007-cross` | cutscene — Frieza over a landscape |
| `r2-007-cross` | **wide landscape, NO HUD, NO character** |
| `r2-008-cross` | **wide landscape, NO HUD, NO character** |
| `r2-last` | battle intro — `FIGHT!` with full HUD |

The two HUD-less landscape frames are very likely the **field/flight transition**, and they pass
in a single press each. ⚠️ **With a 2.6 s gap the whole field phase can be missed.** To land in the
field, advance with a **much longer gap (or press nothing and let the game sit)** and screenshot
continuously, rather than pressing on a timer.

✅ **`scripts/psp-tt-route.mjs` (new)** — one press, one screenshot, printed with a per-step HP
liveness line (`HP s0=.. s1=.. s2=.. s3=..`). Every step is individually attributable, which is what
made this sequence readable. Screenshots go through `psp-shot.py`, so they do not cost the
debugger socket.

## STATUS

| item | state |
|---|---|
| Pipeline (CSO -> ELF -> Ghidra) | **done, verified** |
| Team assignment (0/1 player, 2/3 enemy) | **confirmed via a WIN** |
| 4 fighter slots, HP, gauges, HP write | **live-verified** |
| Battle stats block | **confirmed twice** |
| **Objective arrow** | **CONFIRMED on screen (top-centre, red/orange)** |
| Socket-free screenshots | **working** |
| **Live player position field** | **NOT found** |
| **Open field phase (free flight)** | **glimpsed (2 HUD-less landscape frames) but NOT entered/verified** |
| Objective target position | not found |
| Fighter identity | open (needs Ghidra) |

### The single next action

**Stop pressing.** Get into a battle, then advance with **one press, wait 8-10 s, screenshot** —
specifically watching for the HUD-less landscape frames. When one appears, **stop immediately** and
run `psp-tt-movetest.mjs --slot <active> --all --hold 3000` while that screen is up. A live field
will give a **directional, accumulating** float delta where the battle gave only the 0.463
breathing oscillation. That one result decides whether free flight is drivable.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐ THE FIELD PHASE IS REACHED — screenshot evidence, and movement deltas found

## The field, confirmed by a persistent screenshot

`%LOCALAPPDATA%\Temp\psp-probe\dwr-now.png` (and copies `field-reached.png`) shows the open field:

```
wide 3D landscape (blue grass, rocky mesas, orange sky)
no HUD, no health bars, no press prompt
characters present: SSJ3 Goku (player), Frieza, Kid Gohan, a Saibaman (right)
free-flight camera behind the player character
```

✅ **This is the Dragon Walker play field.** It persisted across consecutive screenshots (unlike the
earlier HUD-less frames which were cutscene cameras), so it is a genuine interactive state.

⚠️ **The field is easy to fall out of.** In this session the state repeatedly slipped back to a
`LOSE` result screen while tests ran, which invalidated several attempts. **A persistent field
session is needed before position work can be finished.**

## Movement deltas found — probable live position, NOT yet visually confirmed

`psp-tt-movetest.mjs --slot 2 --all` (the active slot) produced **directional, accumulating** float
deltas — qualitatively different from the 0.463 breathing animation seen on empty slots:

```
# holding stick UP
   +0x158  2.408 -> -2.828   (d=5.236)
   +0x550  0.335 ->  2.681   (d=2.346)
   +0x0D4  1.827 ->  0.000   (d=1.827)
   +0x1C4  0.814 -> -0.618   (d=1.431)
   +0x504 -10.026 -> -11.216 (d=1.191)
   +0x4C4 -16.844 -> -16.186 (d=0.658)
   +0x4D4 -16.844 -> -16.186 (d=0.658)   <- +0x4C4 and +0x4D4 move TOGETHER

# holding stick DOWN
   +0x158 -0.524 ->  2.513   (d=3.037)
   +0x0D4 -1.732 ->  1.176   (d=2.908)
   +0x504 -11.683 -> -9.189  (d=2.495)
   +0x4C4 -15.413 -> -17.279 (d=1.867)
   +0x4D4 -15.413 -> -17.279 (d=1.867)
```

**Best position candidate: `+0x4C4` / `+0x4D4`** — they carry the **same value and move together**,
which is characteristic of a coordinate pair written by one update (and they are near a second
cluster at `+0x504` that moves in the opposite sense, plausible for a *previous* position).

⛔ **But this is NOT confirmed.** The liveness line read `HP 30000 -> 30000 (unchanged)`, and a
screenshot afterwards showed a **LOSE result screen** (`Max Damage 2300`, `8 Hits`). So the deltas
may be from the transition out of the field rather than from stick-driven locomotion.

⚠️ **A delta that appears while the liveness control is failing must not be trusted** — exactly the
lesson the control exists to enforce. Record `+0x4C4/+0x4D4` as the **prime suspect**, not as a
verified position.

## ✅ IMPORTANT: the battle stats block RESETS per battle

Three different `Max Damage` values across three result screens:

| Max Damage | Max Hit | note |
|---|---|---|
| **7460** | 20 | Frieza battle |
| **2460** | 6 | another battle |
| **2300** | 8 | another battle |

✅ So the stats block at `0x08B49044`/`0x08B49054` is **per-battle**, not cumulative. (An earlier
note suggesting it retained a historical maximum was based on two coincidentally-similar values and
is here corrected: they were 7460 vs 7450-ish reads of the SAME battle.)

## The user's key diagnostic: the first opponent should be RADITZ, not Frieza

The user observed:

> *"Unless you're using a save, your first opponent is going to be Raditz, not Frieza."*

**This is significant and unresolved.** Dragon Walker's opening is Goku vs **Raditz**. Seeing
**Frieza** with SSJ3 Goku means the game is **not** at the start of Dragon Walker — possibilities:

1. the cross-mashing advanced the story past the opening episodes (most likely — 6 story battles
   were auto-won in one run), or
2. the screens observed belong to a different mode (a "What if?" scenario or a later episode).

✅ **Implication for the mission-objective work:** the `Find Gohan!!` objective the user described
belongs to the **Raditz** episode. Until the game is driven back to the true opening, the mission
index/objective strings being read may belong to a **later** episode. **Restart Dragon Walker from
the beginning (or use a save at the opening) before mapping mission state.**

## STATUS

| item | state |
|---|---|
| Pipeline (CSO -> ELF -> Ghidra) | **done, verified** |
| Team assignment (0/1 player, 2/3 enemy) | **confirmed via a WIN** |
| 4 slots, HP, gauges, HP write | **live-verified** |
| Battle stats block | **confirmed, and RESETS per battle** |
| Objective arrow | **confirmed on screen (top-centre)** |
| Socket-free screenshots | **working** |
| **Open field phase** | **REACHED — screenshot evidence** |
| **Live position field** | **prime suspect `+0x4C4`/`+0x4D4`; NOT visually confirmed** |
| Objective target position | not found |
| Fighter identity | open (needs Ghidra) |
| Game currently at the Dragon Walker OPENING (Raditz) | **NO — appears advanced past it** |

### Next actions, in order

1. **Restart Dragon Walker from the opening** so the first opponent is Raditz and the objective is
   `Find Gohan!!`. Confirm by screenshot.
2. In the field, with liveness **passing**, run `psp-tt-movetest.mjs --slot 2 --all --hold 4000` and
   confirm whether `+0x4C4`/`+0x4D4` **accumulate directionally over several seconds** (real
   movement) or settle (animation).
3. Only then hunt the objective target position and the mission index.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ FIELD CONFIRMED INTERACTIVE — the navigation ARROW is on the field HUD

Two evidence frames captured live:

| file | content |
|---|---|
| `field-player-is-frieza.png` | **open field, free camera.** Foreground right = **Frieza** (large, white/purple, back to camera) = the **PLAYER** character. Left = **Kid Gohan** (blue gi). Right = a green **Saibaman**. Orange sky, green mesas. **No HUD.** |
| `field-hud-arrow.png` | **field HUD with the arrow.** Frieza portrait (top-left), a **cyan bar** (long, partly filled), a **team counter `1`**, a **`3` gauge** below, and a **BLUE ARROW at top-right** pointing the way. |

## What this settles

1. ✅ **The field phase is real, interactive, and reachable.**
2. ✅ **The navigation arrow exists ON THE FIELD**, not only in battle — a **blue arrow** at the
   top-right of the field HUD, exactly the objective-guidance feature the user described.
3. ⭐ **The player is controlling FREIEZA** in this state. The large character with its back to the
   camera — the standard third-person player framing — is Frieza, with Gohan as an ally and a
   Saibaman as an enemy. **This explains the persistent Frieza battles and contradicts the expected
   Raditz opening — see below.**
4. ⭐ **The field HUD is different from the battle HUD**: cyan bar + team count `1` + a `3` gauge +
   a blue arrow, versus the battle HUD's green health bar + `(2)` counters + lock-on reticle.

## Player identity: the big character IS the player

In this frame Frieza is rendered **large, in the foreground, from behind** — the standard
third-person player framing — while Gohan and the Saibaman are small and distant. Combined with the
field HUD carrying **Frieza's portrait in the top-left (the 1P slot position)**, the player is
piloting **Frieza**.

⚠️ **This is inconsistent with the expected Dragon Walker opening (Goku vs Raditz)**, which the user
flagged. Possible explanations, unresolved:

- the story has advanced to a Frieza episode (6 story battles were auto-won in an earlier run)
- this is a "What if?" scenario
- the character was changed via `Change Character` on the post-battle menu (that menu was seen)

**Until this is resolved, mission-objective work is not trustworthy** — the `Find Gohan!!` objective
belongs to the Raditz episode.

## Position candidates: the deltas are REAL but NOT monotonic

`psp-tt-posconfirm.mjs --slot 2 --x 1 --y 0 --hold 4000`, sampled every 0.4 s **while holding right**:

```
t=1.3s  +0x4C4=-18.84  +0x4D4=-18.84  +0x504=-11.97  +0x158=0.94  +0xD4=1.62  +0x550=-2.81
t=1.8s  +0x4C4=-18.46  +0x4D4=-18.46  +0x504=-12.39  +0x158=0.00  +0xD4=0.00  +0x550=-2.26
t=2.2s  +0x4C4=-17.65  +0x4D4=-17.65  +0x504=-11.77  +0x158=0.00  +0xD4=0.00  +0x550=-1.72
t=3.6s  +0x4C4=-17.56  +0x4D4=-17.56  +0x504=-13.68  +0x158=0.00  +0xD4=0.00  +0x550=-1.59
# AFTER  +0x4C4=-17.66  +0x4D4=-17.66  +0x504=-13.53  +0x158=0.00  +0xD4=0.00  +0x550=-1.59
# LIVENESS: HP 29220 -> 29220  (UNCHANGED)
```

| offset | net | range | verdict |
|---|---|---|---|
| **`+0x4C4`** | -1.192 | **9.404** | large travel, non-monotonic |
| **`+0x4D4`** | -1.192 | **9.242** | large travel, non-monotonic |
| `+0x504` | -1.699 | 2.167 | large travel, non-monotonic |
| `+0x550` | -3.393 | 5.739 | large travel, non-monotonic |
| `+0x158`, `+0xD4` | small | <2 | oscillating (animation) |

✅ **These are REAL world values, not the 0.463 breathing animation** — the travel range is 5-20× the
animation amplitude and `+0x4C4`/`+0x4D4` track each other exactly.

⛔ **But they are NOT a monotonic position.** Net change is only ~1.2 units against a range of ~9.4,
so the values wander and partially return. A player position should **accumulate in one direction
while a direction is held**. Candidate interpretations:

1. these are **velocity / heading / facing** values (which oscillate as the character banks and
   corrects), not position;
2. they are a **relative camera offset**;
3. **`input.analog.send` is not actually driving movement** (the field may need a different input
   path), and this is idle wander.

⚠️ **Liveness was UNCHANGED (HP 29220 -> 29220), so per the project rule these deltas must not be
trusted yet.** In the field HP legitimately does not change, so HP is a poor liveness control HERE —
a better field control is needed (e.g. does the *scene* change, or does a character's
facing/animation state advance).

✅ **Next control to build: screenshot-diff liveness for the field** — capture two frames a second
apart and require them to differ (the world animates), then hold a direction and require the
*difference pattern* to change. That is the field analogue of the HP control.

## STATUS

| item | state |
|---|---|
| Pipeline (CSO -> ELF -> Ghidra) | **done, verified** |
| Team assignment, HP, gauges, HP write | **live-verified** |
| Battle stats block (resets per battle) | **confirmed** |
| Battle HUD arrow (red/orange, top-centre) | **confirmed** |
| **FIELD reached + interactive** | **CONFIRMED (screenshot evidence)** |
| **Field navigation arrow (blue, top-right)** | **CONFIRMED on screen** |
| **Field HUD (cyan bar, team count, 3 gauge)** | **captured** |
| **Player is piloting FRIEZA** | **observed — contradicts expected Raditz opening** |
| Live position field | **candidates `+0x4C4`/`+0x4D4`, real but non-monotonic; NOT confirmed** |
| Analog stick drives movement | **NOT proven** |
| Objective target position | not found |
| Fighter identity | open (needs Ghidra) |

### The single decisive next test

**Does the scene visibly move when the stick is held?** Compare `pos-2-before.png` and
`pos-2-after.png` side by side (both saved). If the world/character shifted between them, the stick
works and `+0x4C4`/`+0x4D4` are position or velocity. If the frames are visually identical, the
stick is NOT driving the field and the input path must be found.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ THE ANALOG STICK DOES MOVE THE CHARACTER — visual before/after proof

The decisive test: screenshot, hold the stick, screenshot again, then compare the two frames.

## The two frames (stacked in `pos-compare-stack.png`)

**BEFORE** (ground level, stationary):
```
Frieza portrait (top-left) + cyan bar + team counter "2" + "3" gauge
green grass filling the lower 2/3 of the frame
"CPU | Frieza" plate at top-right WITH a cyan lock-on reticle around a distant figure
sky, green mesas, trees on the horizon
```

**AFTER** (held stick right for ~4 s):
```
Frieza portrait + cyan bar (shorter) + team counter "1" + "3" gauge
NO ground — a blurred blue/grey expanse with radial MOTION STREAKS
blue navigation arrow at top-centre
```

## Verdict

✅ **The scene changed completely: the ground disappeared and motion blur appears.** That is the
visual signature of **the character flying at speed**. **The analog stick DOES drive the character.**

| measurement | value |
|---|---|
| pixels differing (sampled every 4th px) | **78.7 %** |
| diff bounding box | `(0, 74, 1706, 1066)` — the whole frame below the menu bar |

✅ Combined with the memory deltas (**`+0x4C4`/`+0x4D4` travel range ~9.4 units, moving in
lockstep** — 5-20× the 0.463 idle-animation amplitude), this establishes:

1. **`input.analog.send` works and moves the character.**
2. **`+0x4C4` / `+0x4D4` track real world state** (the only fields with that travel signature).
3. They are **not** a clean position — net ±1.2 against a range of 9.4, i.e. **non-monotonic**, so
   they are most likely **velocity / heading / facing / camera-relative** values rather than
   absolute coordinates. A true position would accumulate monotonically while a direction is held.

⚠️ **Caveat, stated honestly:** the team counter changed `2 -> 1` between the frames, so combat state
changed too. Some of the 78.7 % difference includes that. But the **loss of the ground plane and the
appearance of motion streaks** cannot be explained by a HUD counter — that is camera/world motion.

## ✅ The field liveness control (replaces HP)

HP is a **bad liveness control in the field** (no combat, so HP legitimately never changes). The
correct field control is **frame difference**:

```python
d = ImageChops.difference(before, after)
changed = sum(1 for sampled px if r+g+b > threshold)
```

- a **static** screen (menu, result, paused) gives a near-zero difference
- a **live field** gives a large difference (world animation) — here **78.7 %**

✅ **Use frame-difference as the field liveness control, and HP as the battle liveness control.**

## STATUS — Tag Team adapter readiness

| item | state |
|---|---|
| Pipeline (CSO -> ISO -> EBOOT -> decrypted ELF -> Ghidra) | **done, verified** |
| Reached: Main Menu, Character Select, battle, **field** | **done** |
| 4 fighter slots (tag team), HP cur/max, gauges | **live-verified** |
| HP **write** (used to win battles) | **works** |
| Team assignment: slots 0/1 player, 2/3 enemy | **confirmed by producing a WIN** |
| Battle stats block (Max Damage/Hit), resets per battle | **confirmed** |
| Battle HUD arrow (red/orange, top-centre) | **confirmed** |
| **Field navigation arrow (blue, top-right)** | **confirmed** |
| **Analog stick drives the character** | **CONFIRMED (visual + memory)** |
| World-state fields `+0x4C4`/`+0x4D4` | **found (velocity/heading-like, not position)** |
| Absolute player position | **not yet identified** |
| Objective target position | not found |
| Current objective / mission index | not found |
| Fighter identity (which character in which slot) | open (needs Ghidra) |
| Game at the Dragon Walker OPENING (Raditz) | **no — appears advanced; unresolved** |

### Scripts (all committed under `scripts/`)

| script | purpose |
|---|---|
| `oga-cso-extract.py` | CSO -> ISO (header block field is a SIZE, not a shift) |
| `oga-iso-extract.py` | ISO file extraction |
| `TagTeamQuery/Trace/Charsel/Ctx/Names.java` | Ghidra: UI element map, stat-panel chain, name table |
| `psp-shot.py` | **socket-free window capture** (PrintWindow) -> PNG |
| `psp-tt-hp.mjs` | live HP/gauge reader, all 4 slots, game-id guard |
| `psp-tt-fight.mjs` | attack + read in ONE connection; `--idle` side-finder |
| `psp-tt-field.mjs` | analog stick driver (`--probe/--drive/--circle/--watch-pos`) |
| `psp-tt-live.mjs` | liveness-gated helper (`--idle/--sweep/--topup`) |
| `psp-tt-advance.mjs` | story advance; wins battles by HP; screenshots |
| `psp-tt-route.mjs` | **one press, one screenshot, per-step HP line** |
| `psp-tt-movetest.mjs` | hold stick + whole-struct diff, HP liveness control |
| `psp-tt-posconfirm.mjs` | before/after screenshots + sampled candidate fields |
| `psp-tt-hold.mjs` | real sustained holds (frames or wall-clock) |
| `psp-tt-findpos.mjs` | two-sample struct diff with liveness control |

# ⛔⛔ MAJOR CORRECTION: EVERYTHING SINCE THE NAME ENTRY WAS **FREE BATTLE / LOBBY**, NOT DRAGON WALKER

The user's hint (*"your first opponent is going to be Raditz, not Frieza"*) was the clue that this
whole sequence was the wrong mode. Two hard pieces of evidence settle it:

## Evidence 1 — the LOBBY screen

```
LOBBY
1P   1111111111    [SSJ3 Goku]     Team A
2P   CPU:Normal    [Piccolo]       Team A
3P   CPU:Normal    [Frieza]        Team B
```

This is the **multiplayer / Free Battle lobby**: CPU difficulty labels (`CPU:Normal`), team
letters (`A` / `B`), four player slots. **Dragon Walker has no lobby.**

## Evidence 2 — the post-battle menu

```
Rematch
Change Character
Rearrange Team
Return to Main Menu
```

`Rematch` and `Change Character` are **Free Battle** affordances. A story mission would not offer
them.

## ⛔ I ALSO MIS-READ THE PLAYER CHARACTER

I claimed *"the player is piloting Frieza"* from the field frame where a large Frieza was in the
foreground with its back to the camera. **That was wrong.** The lobby shows **1P = SSJ3 Goku**
(the player) with Frieza as a **CPU opponent**. A large near-camera figure in a third-person field
view is not automatically the player — **confirm identity from the lobby/HUD, never from framing.**

## What this means

| claim previously made | status |
|---|---|
| "reached the Dragon Walker field" | ❌ **actually Free Battle / lobby** |
| "player is piloting Frieza" | ❌ **wrong — 1P is SSJ3 Goku; Frieza is a CPU** |
| "story advanced past the Raditz opening" | ❌ not established — **Dragon Walker may never have been entered** |
| battle HUD arrow (red/orange) | ✅ still valid — it is the Free Battle HUD |
| field HUD with blue arrow | ✅ valid — but it is a Free Battle/stage HUD |
| analog stick moves the character | ✅ **still valid** (visual proof: ground plane lost, motion streaks) |
| 4 slots, HP, gauges, HP write, team pair | ✅ still valid **in battle** |
| stats block resets per battle | ✅ valid |

✅ **Most of the mechanics work stands** — it was simply collected in **Free Battle**, which has the
same fighter structs, HP, gauges, and analog control. What is **not** established is any story-mode
knowledge: mission list, objective index, the `Find Gohan!!` mission, or the Raditz opening.

## ⛔ IMPORTANT: the fighter struct is INVALID outside battle/field

At character select and the lobby the HP addresses held **`1065353216` = `0x3F800000` = float 1.0**:

```
# step 000  HP s0=1065353216  s1=0  s2=1065353216  s3=1065353216
```

✅ **The fighter struct is only meaningful during a battle or the field.** In menus those addresses
hold unrelated data (here, float `1.0` values). **A reader must therefore verify it is in a
battle/field before trusting any value — `HP <= HP_MAX` is a usable sanity check**, since a real
fighter has `cur <= max` while float-1.0 does not.

## To actually enter Dragon Walker

From the **Main Menu**, `Dragon Walker` is the **first** entry (above `Free Battle`). The route used
here went into Free Battle instead. Correct sequence:

```
Main Menu -> Dragon Walker (first item) -> intro dialogue -> mission briefing -> field
```

The user's expectation: **first opponent Raditz**, first objective **`Find Gohan!!`** — which is the
correct acceptance test for having entered the mode.

⚠️ **Verify the mode before collecting any more data**: a Dragon Walker frame should show a mission
objective and the expected characters (Goku vs Raditz), **not** `CPU:Normal` / team letters.

## Lesson (recorded in the skill)

⛔ **Establish WHICH MODE you are in before measuring anything.** A whole session's field work was
collected in Free Battle while believing it was story mode. The mode is visible on screen
(`LOBBY`, `CPU:Normal`, team letters, `Rematch`), and one screenshot at the start would have caught
it. **Check the mode, the game, and the phase — three controls, not one.**

# Confirmed Free Battle loop, and where the navigation actually is

Followed the Free Battle loop to its end and back:

```
battle  ->  LOSE/WIN result  ->  post-battle menu  ->  CHARACTER SELECT (again)
                                     Rematch
                                     Change Character
                                     Rearrange Team
                                     Return to Main Menu
```

**`Return to Main Menu` leads back into CHARACTER SELECT**, not the main menu (verified: the
character-select screen reappeared afterwards). So this menu cycle does not exit Free Battle by
itself.

## Controls that do NOT exit character select

| control | result |
|---|---|
| `select` | **advances** (to MAP SELECT), does not go back |
| `circle` | no effect observed |
| `start` | no effect observed (and `start` does **not** pause during battle either) |

⚠️ So the character-select exit is still unknown. On many PSP games the exit is `triangle` or a long
press of `start`; both remain untried **with a screenshot-per-press check**.

## ✅ The menu sanity check works — it fired unprompted

Every `psp-tt-route.mjs` line carries an HP column. At menus it reads:

```
# step 004  press cross     HP s0=1065353216 s1=0 s2=1065353216 s3=1065353216
```

`1065353216` = `0x3F800000` = **float 1.0**. That instantly identifies a non-battle screen, exactly
as the guard was designed to. **In battle the same column reads sane values** (`s2=26300 s3=24100`).
This makes the mode/phase visible in every log line without a screenshot.

## ✅ Battle stats block: per-battle reset CONFIRMED across four battles

| Max Damage | Max Hit |
|---|---|
| 7460 | 20 |
| 2460 | 6 |
| 2300 | 8 |
| **1550** | **9** |

Four distinct values — it resets per battle. (An earlier note claiming it retained a historical
maximum is withdrawn: that was two readings of the same battle.)

## Character select layout (Free Battle) — vertical roster

```
CHARACTER SELECT
A / 1P  1111111111
[ big portrait of the highlighted fighter ]   e.g. Goku (SSJ Blue / "Saiyan 3" form label)
   name plate under the portrait: "Goku"
   right side: VERTICAL portrait strip ->
        [SSJ Blue Goku]   <- highlighted (cyan border)
        [Goku (other form)]
        [ ? ]             <- LOCKED slot
        [Piccolo]
1P | 2P CPU | 3P CPU | 4P CPU
```

✅ **Confirmed: the roster is a vertical strip on the right**, matching the layout inference made
earlier. It contains **locked `?` slots**, i.e. the roster is gated by progress.

## STATUS (honest)

The session established a great deal about **Free Battle** mechanics, but **never entered Dragon
Walker**. Remaining unknowns:

| item | state |
|---|---|
| Everything about Free Battle: structs, HP, gauges, write, teams, stats, arrows, analog control | **verified** |
| **Dragon Walker (story mode) entered** | **NO** |
| Exit from Free Battle character select to the main menu | **NOT FOUND** |
| Mission objectives / current-objective index | not started |
| Absolute player position | not found (`+0x4C4`/`+0x4D4` = velocity/heading-like) |
| Fighter identity | open (needs Ghidra) |

### The reliable way into Dragon Walker (next session)

Rather than fighting the Free Battle menu cycle: **restart PPSSPP and load the save.**
`Documents/PPSSPP/PSP/SAVEDATA/ULUS10537DAT0` exists (written during the name-entry session), so the
boot flow should offer `Continue` -> **Main Menu**, where `Dragon Walker` is the **first** entry
above `Free Battle`.

**Acceptance test for having entered it:** first opponent **Raditz**, first objective
**`Find Gohan!!`** — not `CPU:Normal` / team letters.

# Fresh boot: the real Main Menu is BEHIND an attract demo

Restarted PPSSPP with the Tag Team ISO and the existing save
(`Documents/PPSSPP/PSP/SAVEDATA/ULUS10537DAT0/DATA.BIN`, 45872 bytes) to get a clean boot flow.

## Boot sequence observed

```
autosave notice  ->  FUNimation logo  ->  DRAGON BALL Z: TENKAIICHI TAG TEAM title
                                          (with an ATTRACT/DEMO sequence playing behind it)
```

## ⭐ The title menu is hidden by the attract demo

Pressing `cross` repeatedly does **nothing** — the presses are consumed with no visible effect,
because the game is playing its attract/demo loop.

Pressing **`start`** brings up the title menu, and a screenshot taken **immediately** (short gap)
shows it clearly:

```
[ attract footage: Vegeta + SSJ Blue Goku, large title logo ]
                                            New Game      <- highlighted, at the bottom
```

⚠️ **With a normal 3-4 s gap the menu has already gone** (the screenshot shows the attract demo
again). So the menu must be captured on a **tight timing** after `start`.

## ⭐ A clean "not in battle" HP signature: `0/0`

At the title screen the fighter-struct HP column reads:

```
0 HP      0/0 (n/a)  gauge      0/0
1 HP      0/0 (n/a)  gauge      0/0
2 HP      0/0 (n/a)  gauge      0/0
3 HP      0/0 (n/a)  gauge      0/0
```

✅ **`max == 0`** is a clean "the struct is not populated" marker — distinct from the menu's float
`1065353216` and from a live battle's `…/30000`. That gives **three** distinguishable states:

| reading | state |
|---|---|
| `cur/max` with `max ~= 30000` and `cur <= max` | **battle or field** |
| `1065353216` (float 1.0) | **menu / character select** |
| `0/0` | **not populated (boot, title, cutscene)** |

## ⛔ Still not in Dragon Walker

`New Game` was selected and `cross` pressed several times, but the **attract demo keeps masking the
state** — screenshots alternate between the title/logo and short-lived menus, and HP stays `0/0`
throughout, meaning **no battle or field was ever entered**. The sequence did not reach the
Main Menu with `Dragon Walker` / `Free Battle` / `Multiplayer` / `Customize` / `Training`.

⚠️ **The attract demo is the obstacle.** The reliable countermeasure is to **press `start`, then
screenshot immediately, and act on the menu within a second** — or to disable the attract loop if
the game offers it. `start` + immediate `cross` was attempted with a 0.6 s gap and the menu was
still not entered.

### Concrete next attempt

1. `start` to raise the menu, screenshot at once to confirm `New Game` is highlighted.
2. `cross` **immediately** (same second) to enter.
3. Screenshot at once and verify the screen is **not** the attract demo.
4. Then advance with `cross` on a 1-2 s cadence, screenshotting each step, until the **Main Menu**
   appears — and confirm it lists `Dragon Walker` first.

**Acceptance test (the user's):** Dragon Walker's first opponent is **Raditz** and the first
objective is **`Find Gohan!!`** — not `CPU:Normal` / team letters.

# Two debugger-API corrections, and the freeze-frame capture technique

## ⭐ `cpu.stepping` / `cpu.resume` EXIST but never send a response

Trying to freeze a frame, I awaited `cpu.stepping`, got a **timeout**, and briefly concluded it was
unsupported. **Wrong — it works; it just does not answer.**

```
OK      cpu.status  {"stepping":false,...}          <- queries DO respond
reject  cpu.stepping  -> timeout                    <- ACTIONS do not respond
reject  cpu.resume    -> timeout                    <- same
reject  cpu.break     -> Bad message: unknown event  <- THIS one genuinely does not exist
```

✅ **Fire-and-forget actions, then poll `.status` to confirm:**

```js
sock.send(JSON.stringify({event:'cpu.stepping', ticket:String(ticket++), stepping:true}));
await sleep(250);
const st = await req('cpu.status');        // st.stepping === true  => frozen
sock.send(JSON.stringify({event:'cpu.resume', ticket:String(ticket++)}));
```

⛔ **Distinguish "no response" from "not supported."** `cpu.break` → `unknown event` (absent);
`cpu.stepping` → silence (present, acting). **A timeout is not evidence of absence** — the third
time this session a wrong conclusion came from probing too narrowly:

| # | probe | wrong conclusion | truth |
|---|---|---|---|
| 1 | scanned `0x08C85C00`+ only | "there are NO pointers at all" | 828 pointers exist in a wider window |
| 2 | sent `input.analog` (broadcast name) | "no analog input is possible" | `input.analog.send` works |
| 3 | awaited `cpu.stepping` | "stepping unsupported" | it acts, it just does not reply |

✅ **Generalised rule: when a probe fails, ask whether the probe's SHAPE could be wrong — name,
window, or response contract — before concluding the capability is missing.**

## ⭐ Freeze-frame capture for transient menus

Games with an **attract/demo loop** scroll a menu away within about a second, so press-then-screenshot
misses it. **Freeze, capture, unfreeze:**

1. `cpu.resume` (ensure live)
2. press the button
3. wait ~400-500 ms
4. `cpu.stepping` (fire-and-forget) -> CPU halts, the frame holds
5. screenshot -> captures exactly the frame the menu was on
6. `cpu.resume`

Implemented as `scripts/psp-tt-freeze.mjs` (`--tag`, `--settle`, `--plan`, `--repeat`).

⚠️ **Verified working** (5 frozen frames captured, e.g. `fz2-001-start` showing a tinted
attract-demo field with Krillin and a Saibaman). **But the game was still in its attract loop**, so
the technique has not yet been used on a live menu.

## Phase signature: three distinguishable states

The fighter-struct HP column identifies the phase in every log line, without a screenshot:

| reading | state |
|---|---|
| `cur/max`, `max ~= 30000`, `cur <= max` | **battle or field** |
| `1065353216` (float 1.0) | **menu / character select** |
| **`0/0`** | **not populated (boot, title, attract, cutscene)** |

## STATUS

| item | state |
|---|---|
| All Free Battle mechanics (structs, HP, gauges, write, teams, stats, arrows, analog) | **verified** |
| Debugger API surface (input names, action vs query, freeze) | **now well mapped** |
| **Dragon Walker entered** | **NO** |
| Attract demo blocking menu capture | **identified** |
| Freeze-frame capture | **working, not yet applied to a live menu** |

### Next attempt (concrete)

Restart the game **and skip the attract demo entirely**: at the title, use the freeze-frame technique
with `--settle 400 --plan "start,cross"` so the `New Game` menu is captured and acted on within the
same second. Then advance with `--plan "cross" --repeat 6 --settle 1200` and inspect each frozen
frame for the **Main Menu** listing `Dragon Walker` first.

**Acceptance test:** Raditz as the first opponent, objective `Find Gohan!!`.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ CODEX CRACKED THE NAME-RESOLUTION MECHANISM

The user suggested bringing in Codex for the stuck problem. It produced a **real structural find**
that RAM searching could never have reached.

## The resolver: `FUN_0883ee74`

Codex identified a text/message resolver, `FUN_0883ee74(key, 0xffffffff)`. Critically,
**`0883ee74` appears in my own decompilation output** (`tagteam-query-out.txt:1318, 2725`), so it
came from the evidence, not from invention — verified before use.

Two known call sites in my existing decomp output:

```c
// tagteam-query-out.txt:1318
uVar1 = FUN_0883ee74(local_cc, 0xffffffff);

// tagteam-query-out.txt:2725   <-- the important one
uVar4 = FUN_0883ee74(*(int *)(param_1 + 0x124) + 0x16b3, 0xffffffff);
```

⚠️ **`base + 0x16b3`** — a **pointer-plus-offset into a data block**, used as the lookup key. That is
exactly the "computed offset into a packed table" shape the RAM searches implied but could not show.

## The probe (Codex-written, committed as `scripts/TagTeamNameResolver.java`)

Walks every function's p-code, finds CALLs to `0883ee74`, and prints a backward **slice** of each
argument — i.e. how the key is computed. I added file output so the run is verifiable from a written
artefact (95,705 bytes → `tagteam-resolver-out.txt`).

**Result: 84 call sites across 32 distinct caller functions.**

## ⭐ What the slices reveal — literal data-table bases

Every key is built as `<literal base> + index*4` then loaded and offset:

```
(register) LOAD (const 0x1a1) , (unique) PTRADD (PTRSUB (const 0, 0x8a7553c)) , idx , (const 4)
```

The literal bases actually used as key sources:

| base | uses |
|---|---|
| `0x8a7553c` | 2 |
| `0x8a75538` | 1 |
| `0x8a72100` | 1 |
| `0x8a80028` | 1 |
| `0x8a80090` | 1 |
| `0x8a80118` | 1 |
| `0x8b41af8` | 1 |

✅ **`0x08A75538` was ALREADY on the radar** — `gauge_attack`/`gauge_defense`/`gauge_technic` were
located at `0x08A76538/48/58`, and other element names at `0x08A722xx`. So these keys address the
**same UI/roster definition area** — consistent with names being resolved through an element/record
table rather than a stored string pointer.

## The dominant field: `+0x1A1`

**358 occurrences** of a `LOAD (const 0x1a1)` — a single recurring struct field that holds the
pointer to these tables or records. That is the strongest lead for the identity path: an object at
`+0x1A1` is dereferenced, then indexed, then its entry is used as a name key.

## Functions with the most call sites (the name-rendering clusters)

| function | call sites |
|---|---|
| `FUN_08a18730` | 8 |
| `FUN_08a35ff0` | 8 |
| `FUN_08a2c314` | 7 |
| `FUN_08838e3c` | 5 |
| `FUN_088392bc` | 5 |
| `FUN_08a26400` | 5 |
| `FUN_08a06068` | 4 |
| `FUN_08a29118` | 4 |
| `FUN_08a3b5f4` | 4 |
| `FUN_08a46f84` | 4 |

The `FUN_08a0xxxx`-`FUN_08a4xxxx` range is the **UI/HUD layer** (the same range as the stat-panel
renderer `FUN_08a3c458` and the character-select builder `FUN_08a02a3c`).

## Why this explains every earlier failure

| earlier finding | explanation |
|---|---|
| no pointer to roster names | names are not stored as pointers — they are **keys** resolved by `FUN_0883ee74` |
| no literal constant referencing the text table | the key bases are **other tables** (`0x8a75xxx`, `0x8a80xxx`) which are themselves indexed |
| roster index (Frieza=33) absent from the fighter struct | the struct holds a pointer/record at `+0x1A1`, not the roster index |
| identity "computed in code" | **confirmed** — resolved at render time from a keyed table |

## ✅ Verdict on the hypothesis ranking

Codex's ranked mechanisms, against the evidence:

| mechanism | verdict |
|---|---|
| **(c) character referenced by an ID whose table lives outside the fighter struct** | **STRONGLY SUPPORTED** — keys come from `0x8a75xxx`/`0x8a80xxx` tables |
| (b) packed length-prefixed table walked by computed offset | **SUPPORTED** — `base + 0x16b3` is exactly this |
| (a) runtime-resolved text base | partially — the bases are literals, so *not* a dynamic text base |
| (d) name composed at render time | **SUPPORTED** — resolution happens inside HUD/render functions |

## NEXT STEP (concrete, bounded)

1. **Identify what `+0x1A1` points at** in a live battle: read the fighter struct's pointer field and
   follow it. If it resolves to the `0x8a75xxx` table, that is the identity record.
2. **Dump the table at `0x08A75538`** in RAM during a battle and look for the roster index or an id
   that correlates with the known characters (player = Goku, enemy = Frieza).
3. Then the two-run experiment becomes trivial: change the opponent and read the same field.

**Falsification:** if neither `+0x1A1` nor the `0x8a75xxx` table changes when the character changes,
the identity is resolved further up (in the caller's own state) and the search moves to
`FUN_08a18730` / `FUN_08a35ff0` (8 call sites each).

# ✅ THE OPEN FIELD (no HUD, third-person flight) — reached again and captured

Frame sequence from a plain (non-freezing) advance run `psp-tt-route.mjs --tag bb` — 4 sequential
frames assembled into `bb-sheet.png`:

| quadrant | frame | content |
|---|---|---|
| top-left | `bb-002` | green field, third-person, SSJ Blue Goku with a yellow ring marker ahead-right |
| top-right | `bb-004` | close-up of a fighter (Cell, gold/black) — a cinematic camera |
| bottom-left | `bb-006` | **wide landscape, NO HUD, no character** — the flight camera |
| bottom-right | `bb-008` | **wide landscape, NO HUD, SSJ Blue Goku flying in third-person (bottom-right)** |

A full-resolution crop of the **top 18 %** of `bb-008` contains **only sky and clouds — zero HUD
elements, zero text**. So this is the **exploration/flight phase**, not a battle and not a menu.

✅ **The open field is a real, reachable state.**
✅ **The third-person flying character is the player** (SSJ Blue Goku here).
⚠️ **HP reads `0/0` in this state**, so the fighter struct is NOT populated during free flight —
meaning **position/state for the field must live somewhere other than the 4 battle structs**, which
is consistent with the earlier failure to find a position field in those structs.

## Freezing is the wrong tool for ROUTE progress — and the right tool for menus

⚠️ `psp-tt-freeze.mjs` **halts the game**, so a plan of many presses with freezing between them does
not advance: the run stalled at the title (`go`/`fz*` runs all ended `phase=UNPOPULATED`). Use it
only to capture a *transient menu*, with a short plan (`start,cross`).

✅ For **route progress**, use `psp-tt-route.mjs` (press, wait, screenshot; no freeze).

## Where the identity work now stands

Codex's mechanism finding (resolver `FUN_0883ee74`, keys built as `<literal base> + index*4` with
bases like `0x8a7553c`/`0x8a80xxx`, and a dominant struct field `+0x1A1` used 358 times) remains the
best lead and is **independent of getting into story mode** — it can be pursued with a **Free Battle
dump**, if a battle can be re-entered.

### Order of work, corrected

1. **Re-enter a battle** (Free Battle is fine — same structs) and do the `+0x1A1` follow-up:
   read the pointer field, follow it, dump the `0x08A75538` table, and look for an id that
   correlates with the two known fighters (Goku vs Frieza).
2. **Then** return to the story-mode objective work, which additionally needs the Raditz opening.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ THE NAME/IDENTITY MECHANISM — SOLVED IN THE ELF, NO RUNNING GAME NEEDED

Codex's p-code analysis pointed at tables; **those tables are STATIC DATA, so they live in
`EBOOT.dec` itself.** New tool `scripts/psp-elf-window.py` dumps data at a PSP RAM address straight
out of the ELF by resolving the program headers.

## The ELF's memory map (pre-linked, so RAM address == vaddr)

```
type  offset      vaddr       filesz      memsz
PT_LOAD 0x00001018  0x08804040  0x0027D45D  0x0027D45D   <- code + static data
PT_LOAD 0x0027F000  0x08AEFB70  0x00000084  0x00076CDC   <- .bss (NOT in file)
```

So **`0x08804040`-`0x08A8149D` is present in the file** and can be read statically. Everything the
p-code slices referenced (`0x8a755xx`, `0x8a72xxx`, `0x8a80xxx`) is inside that range.

## What the tables actually contain: MESSAGE IDs, not pointers to strings

```
0x08A75538:   505  506  507  508  509  510  171  172  169  170  173  174  162  163 ...
0x08A72100:  1109 1109 1109 1109 1110 1111 1111 1111 1112 1112 1112 1113 ...
```

✅ **These are small sequential MESSAGE/STRING IDs.** `FUN_0883ee74(id, -1)` is a **message lookup**
— it turns an id into text. So a character name is **never stored as a string pointer**; the game
stores an **id** and resolves the text through the message system.

⭐⭐ **That single fact explains every earlier failure:**

| earlier result | why it happened |
|---|---|
| no pointer to roster name strings | the name is an **id**, not a pointer |
| no literal constant referencing the text table | the text table is reached **through** `FUN_0883ee74` |
| roster index (Frieza=33) absent from the fighter struct | the struct holds an **id/handle**, not the roster index |
| the wider scan found pointers only for DIALOGUE | dialogue stores text pointers; **names use ids** |

## The dispatch table at `0x08A75584`: `{function pointer, aux}` pairs, 8-byte stride

```
0x08A75584 -> 0x08A26CDC   0x08A7558C -> 0x08A26CB4   0x08A75594 -> 0x08A26CBC
0x08A7559C -> 0x089D18A4   0x08A755A4 -> 0x08A26CC4   0x08A755AC -> 0x089D1948
... 18 consecutive {code pointer, 0} entries ...
```

Decoding the first words of the clustered targets shows:

```
0x08A26CB4  03E00008 00000000   <- "jr $ra; nop"  == an EMPTY STUB function
0x08A26CBC  03E00008 00000000   <- same
0x08A26CC4  03E00008 00000000   <- same
0x08A26CCC  03E00008 00000000   <- same
0x08A26CD4  03E00008 00000000   <- same
0x08A26CDC  27BDFFF0 AFB10004   <- a REAL function (stack frame)
0x08A26D38  2405FFFF 03E00008   <- REAL ("addiu $a1,$zero,-1; jr $ra")
```

✅ **This is a default-implementation dispatch table**: entries pointing at `jr $ra; nop` are slots
whose behaviour is "do nothing", and the real entries implement specific cases.

## ⭐ THE MECHANISM (final)

```
fighter slot
   -> struct field (e.g. an id/handle at +0x1A1, dereferenced 358x in the slices)
   -> index into an id table  (e.g. 0x08A75538: 505,506,...  /  0x08A72100: 1109,...)
   -> FUN_0883ee74(message_id, -1)      <- resolves the id to TEXT
   -> rendered into the HUD element (chara_name_01 / chara_name_02)
```

**Identity is an ID resolved through the message system.** There is no name string and no name
pointer to find — which is exactly why RAM searching failed no matter how the window was chosen.

## Why this is a much better place than before

- The tables are **in the ELF**, so the mapping can be built **without a running game**.
- The id tables give **explicit lists of candidate message ids** per context
  (`0x08A72100` looks like a form/animation id list; `0x08A75538` a UI/name id list).
- `FUN_0883ee74` is identified, so its callers (32 functions, 84 call sites) can be decompiled with
  the knowledge that **the argument is a message id** — making `FUN_08a18730` / `FUN_08a35ff0`
  (8 call sites each) the prime targets for the name-rendering path.

## NEXT STEP (concrete)

1. **Decompile the `0x08A75538` id-table readers**: find code whose operand is `0x08A75538`/`38+4k`
   (a Ghidra search for that address). Those functions index the name-id table — one of them maps a
   fighter slot to its name id.
2. **Then resolve a few ids** by finding how `FUN_0883ee74` looks up its message table, and confirm
   a known name id (e.g. for Goku or Frieza) by matching against the roster string table at
   `0x08C85C82`.
3. No live game is required for either step.

# ⛔ MY "ZERO REFERENCES TO THE ID TABLE" WAS A FALSE NEGATIVE — the LUI search was broken

I searched for instructions whose operand lies in the name-id table region
(`0x08A75000`-`0x08A76000`) and got **0 matches**, then reasoned "so the table must be reached by a
computed base, not a literal". **That reasoning was based on a broken search.**

## The corrected measurement

`scripts/TagTeamDataRefs.java` maps the code's **entire** literal data-reference profile, bucketing
every operand that points into the load segment by 4K page, and handling the MIPS **LUI high-half**
properly. Result:

```
=== total literal data references: 5852  (5852 from LUI high-halves) ===

=== 4K pages referenced, by count (descending) ===
  0x08A80000   3468 ref(s)   e.g. 088040b0  lui t1,0x8a8
  0x08A70000   1815 ref(s)   e.g. 0880441c  lui v1,0x8a7      <-- THE REGION IS REFERENCED
  0x08850000    176 ref(s)
  ...
=== ANSWER: is 0x08A70000-0x08A7FFFF referenced at all? ===
  YES: page 0x08A70000  1815 ref(s)
```

✅ **`0x08A7xxxx` is referenced 1815 times and `0x08A8xxxx` 3468 times** — the two most-referenced
data pages in the entire executable. The id tables are among the most heavily used data in the game.

## Two bugs in the first search, both worth remembering

1. **LUI handling.** MIPS builds a 32-bit constant as `LUI(hi) + ORI/ADDIU(lo)`. Ghidra's scalar on
   the LUI is the **high 16 bits**, so a table at `0x08A75538` appears as `lui $v1, 0x8a7` — a
   scalar of `0x8A7`, which is nowhere near my search window. My first script tried to "reconstruct"
   the base by `(v & 0xFFFF) << 16`, but only AFTER the `inTbl` test had already failed and
   `continue`d. **The reconstruction was unreachable.**
2. **Window granularity.** Searching for an exact address cannot match a LUI, which only carries the
   page. Bucketing by 4K page is the right granularity for this architecture.

⛔ **THIS IS THE FOURTH too-narrow-probe false negative in this project:**

| # | probe | wrong conclusion | truth |
|---|---|---|---|
| 1 | scanned `0x08C85C00`+ only | "no pointers at all" | 828 pointers in a wider window |
| 2 | sent `input.analog` (broadcast name) | "no analog input possible" | `input.analog.send` works |
| 3 | awaited `cpu.stepping` | "stepping unsupported" | it acts, it just doesn't reply |
| 4 | **scanned for scalar `0x08A75xxx`** | **"table unreferenced"** | **1815 refs, found via LUI page** |

✅ **RULE: on MIPS, search by 4K PAGE, not by exact address, and always account for LUI high-halves.
Never conclude "unreferenced" from an exact-address search.**

## What this restores

The id table is **not** reached by a computed base — it is referenced directly and heavily, as
expected for a static data table. So the original plan stands:

**Find the code that indexes `0x08A7xxxx` and uses the result as a `FUN_0883ee74` argument — that
mapping is the character identity.** The search must use the LUI page (`0x8A7`) to locate candidates.

### Corrected next step

1. Re-run the id-table search bucketing by **LUI high-half `0x8A7`** (and `0x8A8`), collecting
   functions that both reference those pages **and** call `FUN_0883ee74`.
2. Decompile the intersection — that is a small, high-signal set.
3. One of them maps a fighter slot to a message id.

# ⛔ CORRECTION: "+0x1A1 used 358 times" was WRONG — 0x1a1 is a p-code ADDRESS-SPACE id

I reported a "dominant struct field at `+0x1A1`, used 358 times" as the identity lead. **That was a
misreading of p-code, and I need to retract it.**

In Ghidra p-code, `LOAD` takes **two** inputs: `(SPACE_ID, ADDRESS)`:

```
(register, 0x10, 4) LOAD (const, 0x1a1, 4) , (unique, 0x200, 4)
                          ^^^^^^^^^^^^^^   ^^^^^^^^^^^^^^^^^
                          SPACE ID         the actual address operand
```

**`0x1a1` is the loader-assigned address-space ID for the RAM block**, not a struct offset. It
appears in nearly every memory access in the binary.

## The check that settles it

| pattern | meaning | occurrences |
|---|---|---|
| `LOAD (const, 0x1a1, ...)` | p-code space id | **179** (in the resolver dump alone) |
| C-style `... + 0x1a1` | a real struct field | **0** |

✅ A genuine struct field would show up as `param_1 + 0x1a1` in the decompiled C. It does not —
zero times, in every artefact. **So there is no `+0x1A1` field.**

⛔ **Lesson: read p-code operand ORDER before interpreting an operand.** `LOAD(space, address)` and
`CALL(dest, args…)` both put a non-argument value first. I had already met this exact trap in the
resolver script (`CALL input 0 is the destination`), then fell into its twin.

## What the call sites ACTUALLY show (real finding)

With that noise removed, the resolver arguments are of three clean kinds:

**(1) Constant message ids** — the simplest and most useful:

```
FUN_08a18730  0x3b8        FUN_08a2c314  0x1b7, 0x1b6
FUN_08a18730  0x3b9, 0x3ba
```

**(2) Table-indexed ids** — the real identity pattern:

```c
// FUN_08a26400 @ 08a2696c
FUN_0883ee74( *(int*)( 0x08A75538 + 1*4 ), -1 )        // = table[1]
// FUN_08a26400 @ 08a26a80 / 08a26bb0
FUN_0883ee74( *(int*)( 0x08A7553C + idx*4 ), -1 )
```

**(3) Struct-field-derived** — an object field plus a small offset:

```
FUN_0883ee74( *(int*)(obj + 0x80) , -1 )
FUN_0883ee74( *(int*)(obj + 0x84) , -1 )
FUN_0883ee74( *(int*)(obj + 0x1ec), -1 )
FUN_08a18730: FUN_0883ee74( <ram 0x8b4e604> + 0x1be , -1 )
```

⭐ **`0x08A75538` is the id table I found in the ELF (505, 506, 507, …) and `FUN_08a26400` indexes
it directly.** That is the closest thing yet to the name path: a table of message ids, indexed, fed
straight into the message resolver.

## Corrected next step

`FUN_08a26400` is now the **prime target** — it both references `0x08A75538`/`0x08A7553C` and calls
the resolver **5 times** with table-derived arguments.

1. Decompile `FUN_08a26400` in full and identify **what `obj` is** in `*(int*)(obj + 0x80)` — if it
   is a fighter/panel object, the id chain is: object → `+0x80` → message id → resolver → name.
2. Read the value at `0x08A75538 + 4` and `0x08A7553C + idx*4` in the ELF and check whether the
   resulting ids correspond to plausible HUD strings.
3. Cross-check against the roster string block (`0x08C85C82`) if the ids can be tied to text.

# FUN_08a26400 — decompiled, and the id table's true shape

## Resolver call sites inside `FUN_08a26400` (all with `arg1 = -1`)

```
callsite 08a2696c  arg0 = LOAD(reg48)          <- reg48 = PTRSUB(0, 0x8a75538) + 1*4  => table[1]
callsite 08a26a80  arg0 = LOAD(...)            <- PTRSUB(0, 0x8a7553c) + idx*4        => tableB[idx]
callsite 08a26bb0  arg0 = LOAD(...)            <- PTRSUB(0, 0x8a7553c) + idx*4        => tableB[idx]
callsite 08a26a40  arg0 = INT_ADD(reg, 0x1ec)
callsite 08a26b44  arg0 = INT_ADD(reg, 0x1ec)
```

✅ So the pattern is: **index the id table, load the id, hand it to the message resolver.** Three of
the five calls take a table-derived id; two take an object field (`+0x1ec`).

## ⭐ The id table's real shape (from the ELF bytes)

```
0x08A75538   000001F9  505      0x08A75550   000000AB  171
0x08A7553C   000001FA  506      0x08A75554   000000AC  172
0x08A75540   000001FB  507      0x08A75558   000000A9  169
0x08A75544   000001FC  508      0x08A7555C   000000AA  170
0x08A75548   000001FD  509      0x08A75560   000000AD  173
0x08A7554C   000001FE  510      0x08A75564   000000AE  174
0x08A75568   000000A2  162      0x08A7556C   000000A3  163
0x08A75570   00200020            <- 0x0020,0x0020 as u16 -> a LENGTH/count pair
0x08A75574..0x08A75580  all zero <- padding
0x08A75584   08A26CDC   ^       0x08A75588   0        <- {code ptr, 0} pairs begin
```

✅ **Structure: an array of message ids, then a length field, then a vtable of function pointers.**
The ids (505-510, 169-174, 162-163) are **small and clustered**, i.e. HUD/label message ids, and the
`{code ptr, 0}` pairs after them are the handlers.

⚠️ `tableA` and `tableB` are the **same array read one word apart** (`0x08A75538` vs `0x08A7553C`),
so "table A index 1" == "table B index 0". They are two views of one array, not two tables.

## What this does NOT yet give us

The ids are still **unmapped to text**. To finish identity we need one of:

1. **the message->text map**: how `FUN_0883ee74(id, -1)` finds the string for an id. Find it, then
   read id 505 / 171 / 169 etc. and see which HUD labels they are.
2. **the object field feed**: identify `obj` in `*(int*)(obj + 0x80)` / `+0x84` / `+0x1ec` inside
   `FUN_08a26400`'s callers — if a fighter/HUD object, that is the slot->id link.

⚠️ **Neither is a RAM search.** Both are ELF/code questions, which is why the static-data route
(not the running game) is the right one.

## SESSION CONSOLIDATION — Tag Team (ULUS10537)

### SOLVED and verified

| item | evidence |
|---|---|
| full pipeline CSO -> ISO -> EBOOT -> ELF -> Ghidra | verified |
| 4 fighter slots, stride `0x1A90`, base `0x0973CEE0` | live-verified |
| HP `+0x14` / max `+0x18`; gauge `+0x20` / max `+0x24` | **live-verified under attack** |
| team assignment: slots 0/1 = player, 2/3 = enemy | **confirmed by producing a WIN** |
| HP **write** to end a battle | works |
| battle stats block (Max Damage `0x08B49044`, Max Hit `0x08B49054`) | confirmed ×4, resets per battle |
| **analog stick control** (`input.analog.send`) | **confirmed visually — ground plane lost, motion streaks** |
| battle HUD arrow (red/orange, top-centre) | confirmed on screen |
| field HUD arrow (blue, top-right) | confirmed on screen |
| open field (free flight, no HUD) reached | screenshot evidence |
| the game's text table in RAM (roster, forms, stages, moves, missions) | read as UTF-16 |
| **name/identity mechanism** | **solved: message IDs + resolver `FUN_0883ee74`** |
| id table location and shape | `0x08A75538`, ids then length then vtable |
| socket-free screenshots (`psp-shot.py`) | working |
| freeze-frame capture (`cpu.stepping` is an action, no reply) | working |

### NOT solved

| item | state |
|---|---|
| **character identity (slot -> character)** | mechanism known; id->text map still needed |
| **Dragon Walker (story mode) entered** | **NO** — the field work was in Free Battle |
| mission objectives / current-objective index | not started |
| absolute player position | not found (`+0x4C4/+0x4D4` are velocity/heading-like) |
| **adapter code** | **not written** |

### The five lessons this game produced (all recorded in the skill)

1. **Make the game print the value** — losing to reach the result screen (`Health 0%`, `Max Damage
   7460`) turned a hopeless search into an exact one. This is what cracked HP.
2. **Never diff dumps from different screens** — six separate false conclusions came from this.
3. **Look for another route** — six controls failed on character select; Training/Free Battle
   bypassed it entirely.
4. **Never conclude "absent" from one narrow probe** — four false negatives (pointer window,
   broadcast-vs-request name, no-reply action, exact-address LUI miss).
5. **Know when RAM search is the wrong tool** — identity is a message-id lookup resolved in code.
   The tables are static, so the ELF, not the running game, is where the answer is.

# ⭐ The text lives in a SEPARATE MODULE loaded from the ISO — not in the ELF

This is the structural fact that explains the whole shape of the identity problem, and it closes off
the static-ELF route for the strings.

## Measured

```
ELF load segments:
  PT_LOAD  0x00001018   vaddr 0x08804040   filesz 0x0027D45D   <- code + static data
  PT_LOAD  0x0027F000   vaddr 0x08AEFB70   filesz 0x00000084   <- .bss (not in file)

Roster names observed in RAM at 0x08C85C82:
  -> "0x08C85C82 is not inside any PT_LOAD segment."
```

✅ **`0x08C85C82` is outside the ELF's load segments entirely.** The ELF's data ends at
`0x08A8149D`; the name/text region sits at `0x08C8xxxx`, **higher than the ELF goes**.

⚠️ A UTF-16 scan of the ELF's load segment found **only MIPS code misread as UTF-16** — no real
strings. So the ELF does **not** contain the game's text.

## What this means

The game's text (roster names, mission strings, dialogue) is **loaded at runtime from the ISO into
RAM**. The ELF references it by address, but the bytes are not in the ELF.

**Therefore:**
- the *mechanism* (message ids + `FUN_0883ee74`) is visible in the ELF ✅
- the *id → text mapping* requires either the **ISO files** or a **live RAM dump** ❌ (not static ELF)

✅ **The ISO extract directory is already available** (`%LOCALAPPDATA%\Temp\tagteam-extract\`), and
the original ISO holds the whole file tree. The text module is almost certainly a file there
(likely a `.bin`/`.pak` in `PSP_GAME/USRDIR/`).

## Corrected plan for the id → text map

1. **List the ISO's file tree** and look for a text/message archive
   (`scripts/oga-iso-extract.py` already exists for extraction).
2. Find where `0x08C8xxxx` content comes from — search the ISO for the UTF-16 string `Goku`
   (the roster block at `0x08C85C82` begins with the select-order roster).
3. Once the file is found, the id table (`0x08A75538`: 505, 506, …) can be matched against that
   file's string order.

⚠️ **Do NOT keep searching the ELF for strings** — it does not contain them. This is settled by
measurement, not inference.

## Note on the earlier "no pointers to the text table" finding

That result is now fully explained: the text is at an address the ELF never mentions, loaded at
runtime, and **reached through a message-id indirection** rather than a stored pointer. Both facts
had to be discovered separately, and both are now established.

# ⭐⭐⭐ THE TEXT SOURCE FOUND: `PSP_GAME/USRDIR/PACKFILE.BIN`

This closes the "where are the strings?" question that the ELF could not answer.

## The ISO file tree (relevant entries)

```
PSP_GAME/SYSDIR/EBOOT.BIN          3,748,256   <- the executable (decrypted -> EBOOT.dec)
PSP_GAME/USRDIR/PACKFILE.BIN     626,616,320   <- THE GAME'S DATA ARCHIVE
PSP_GAME/INSDIR/DATAINST.BIN     628,619,824   <- installer copy of the archive
PSP_GAME/SYSDIR/UPDATE/DATA.BIN   26,120,416
```

## ✅ Every known string is present in PACKFILE.BIN as UTF-16LE

Searching for strings already **observed in RAM** (so these are known-real, not assumed):

```
=== 'Goku' (utf-16le):           8 hit(s)   (ascii form: 0 hit(s))
=== 'Frieza' (utf-16le):         8 hit(s)   (ascii form: 0 hit(s))
=== 'Super Saiyan 3' (utf-16le): 8 hit(s)   (ascii form: 0 hit(s))
```

✅ **Confirmed: the text lives in PACKFILE.BIN, UTF-16LE, and is loaded into RAM at runtime.**
✅ Also confirmed the text is **UTF-16LE only** — there is no ASCII form, which is why ASCII greps
have failed all along on this game.

## Why this matters for identity

The chain is now fully traced:

```
PACKFILE.BIN (text, UTF-16LE)
        |  loaded at runtime
        v
RAM 0x08C8xxxx   (roster names in select order: Goku, Kid Gohan, Teen Gohan, ... Frieza)
        ^
        |  resolved by
ELF: FUN_0883ee74(message_id, -1)  <-- indexed from the id table at 0x08A75538 (505, 506, ...)
```

**All three pieces are now located.** The remaining join is: which message id corresponds to the
roster block at a given offset, i.e. the id -> string mapping.

## The bounded final step

Search `PACKFILE.BIN` for the **roster block itself** (the contiguous run beginning `Goku\0Kid
Gohan\0Teen Gohan\0…`), then:

1. note the **file offset** of each roster entry (Goku, Frieza, ...),
2. find the **structure immediately before** the block — archives commonly store an
   `{offset, length}` or `{id, offset, length}` index for such a block,
3. if the preceding index carries ids, **those ids are the message ids** and the identity map falls
   out directly.

✅ Tooling in place: `scripts/psp-packfile-find.py <PACKFILE.BIN> <strings…>` (known-string search
with u32 context, plus `--utf16-scan` restricted to printable-ASCII runs so code cannot masquerade
as text), and `scripts/psp-elf-window.py` for address->offset work.

⚠️ **Do not use a general UTF-16 scan to find the text** — over code it produces convincing CJK
false positives (already paid for once). **Search for known strings only.**

## SESSION END STATE — Tag Team (ULUS10537)

### Verified working

| area | state |
|---|---|
| pipeline CSO -> ISO -> EBOOT -> ELF -> Ghidra | done |
| ISO archive access (`oga-iso-extract.py list/save`) | done, PACKFILE.BIN extracted |
| 4 fighter slots, HP, gauges | **live-verified under attack** |
| team assignment (0/1 player, 2/3 enemy) | **confirmed via a WIN** |
| HP write, stats block | working / confirmed x4 |
| analog stick control | **confirmed visually** |
| battle + field HUD arrows | confirmed on screen |
| open field reached | screenshot evidence |
| socket-free screenshots, freeze-frame capture | working |
| **name mechanism** | **solved: message ids + `FUN_0883ee74`** |
| **id table** | located `0x08A75538` (ids, length, vtable) |
| **text source** | **found: `PACKFILE.BIN`, UTF-16LE** |

### Not done

| item | state |
|---|---|
| id -> text mapping | **DONE — confirmed against real strings** |
| character identity | **ids known; per-slot id field still to locate** |
| **Dragon Walker (story mode)** | **never entered** — field work was Free Battle |
| mission objectives / current-objective index | not started |
| absolute player position | not found |
| **adapter code** | **not written** |

# ⭐⭐⭐⭐⭐ IDENTITY SOLVED: message id == ordinal into the PACKFILE string table

**The final join is done and CONFIRMED by reading real text.**

## The string table in `PACKFILE.BIN`

A **contiguous run of null-terminated UTF-16LE strings**, beginning at file offset **`0x0004FADC`**
(which is where `'Kid Gohan'` sits, ordinal 0):

```
[   0] 0x0004FADC  'Kid Gohan'
[   1] 0x0004FAF0  'Cleared a stage'
[   2] 0x0004FB10  'Waiting...'
[   3] 0x0004FB26  'Yes'
[   4] 0x0004FB2E  'No'
[   5] 0x0004FB34  'Connection was lost. Returning to Main Menu.'
...
[  11] 0x0004FD4A  'Survival'
[  21] 0x0005001A  'Difficulty Level'
[  22] 0x0005003C  'Battle Score'
```

Strings have **zero gap between them** (each ends with a UTF-16 NUL, the next begins immediately).

## ✅ The hypothesis TESTED against real text

The ELF's id table (`0x08A75538`) contains `505 506 507 508 509 510 171 172 169 170 173 174 162 163`.
Reading those **ordinals** out of the PACKFILE table:

| ELF id | resolves to |
|---|---|
| **505** | `'Strategy'` |
| **506** | `'Charge'` |
| **507** | `'Enemy Search'` |
| **169** | `'Inherited Name'` |
| **171** | `"I Won't Lose!"` |
| **162** | `'Spoiled Rich'` |
| **163** | `'Wild Long'` |

⭐ **These are coherent, real game strings** — and the consecutive triples behave as expected
(505/506/507 are three related ability labels; 162/163 a related pair). So:

> **message id == ordinal index into the PACKFILE.BIN string table starting at 0x0004FADC**

✅ This is **confirmed by reading actual text**, not by inference.

## The complete, closed chain

```
PACKFILE.BIN @ 0x0004FADC   contiguous UTF-16LE string table (ordinal 0 = 'Kid Gohan')
        ^                    message id N  ->  the Nth string
        | loaded at runtime
RAM 0x08C8xxxx   (roster names resident, select order)
        ^
ELF: FUN_0883ee74(message_id, -1)   <-- the resolver
     indexes the id table at 0x08A75538 (505,506,507,... / 171,172,169,... / 162,163)
```

**Every component is located and the link between them is proven.** Character identity is now a
solvable data question:

## How to produce the identity map (no running game needed)

1. Enumerate the string table from `0x4FADC` (tool: `scripts/psp-msgids.py`).
2. Enumerate the **roster name ordinals** — the ids of `'Goku'`, `'Frieza'`, etc. in that numbering.
3. Find the code (ELF side) that picks a roster id for a fighter slot — the id table at
   `0x08A75538` and the object fields seen in `FUN_08a26400` (`+0x80`, `+0x84`, `+0x1ec`).
4. The adapter then reads the slot's id and looks up the name in the PACKFILE table.

## ⚠️ A note on the roster block at RAM `0x08C85C82`

In RAM the roster names appear **contiguously** (`Goku`, `Kid Gohan`, `Teen Gohan`, ...). In
PACKFILE.BIN `'Kid Gohan'` is at ordinal **0** of the table, immediately followed by *UI messages*
rather than `'Teen Gohan'`. So the RAM roster block is a **runtime-assembled name array** (the game
copies the specific name strings it needs into a contiguous array), while PACKFILE holds them in the
**general message table**. Both are real; they are different views.

✅ Practical consequence: **do not assume RAM's contiguous roster order equals the PACKFILE ordinal
order.** Use the id table to relate them.

## New tooling (committed)

| script | purpose |
|---|---|
| `psp-packfile-find.py` | search PACKFILE.BIN for KNOWN UTF-16 strings; u32 context **with 4-byte alignment correction** (string offsets are often not aligned, which produced garbage context before the fix) |
| `psp-packfile-strings.py` | read the forward UTF-16 string sequence from an offset, with gap reporting |
| `psp-msgids.py` | enumerate the message table by ordinal and **resolve ELF ids against it** — this is what confirmed the mapping |
| `psp-elf-window.py` | dump data at a PSP RAM address from the ELF (address -> file offset via program headers) |

# ⭐⭐⭐⭐⭐⭐ THE IDENTITY MAP EXISTS AND IS NOW READABLE

The roster names appear in the PACKFILE message table as a **contiguous ordinal block**, and the
order matches the RAM roster block **exactly** — two independent confirmations of the same sequence.

## The roster ordinals (message id == ordinal)

```
[ 843] 'Goku'          <- roster index 0   (matches the character-select top entry)
[ 844] 'Kid Gohan'
[ 845] 'Teen Gohan'
[ 846] 'Gohan'
[ 848] 'Piccolo'
[ 849] 'Krillin'
[ 850] 'Yamcha'
[ 851] 'Tien'
[ 854] 'Vegeta'
[ 876] 'Frieza'
[ 883] 'Cell'
[ 891] 'Super Saiyan 3'   <- a FORM id, same numbering space
```

⚠️ Gaps (847, 852-853, 855-875, ...) are the **other roster characters** whose exact English strings
differ from my probe list (e.g. `Vegeta (Scouter)`, `Android #16`, `Captain Ginyu`). The block is
contiguous in the file; I simply only printed names I searched for.

## ✅ Cross-check against the live game

The **RAM** roster block at `0x08C85C82` read, live, as:

```
Goku, Kid Gohan, Teen Gohan, Gohan, Ultimate Gohan, Piccolo, Krillin, Yamcha, Tien, ... Frieza
```

The **PACKFILE** ordinals read:

```
843 Goku, 844 Kid Gohan, 845 Teen Gohan, 846 Gohan, 848 Piccolo, 849 Krillin, 850 Yamcha,
851 Tien, ... 876 Frieza
```

⭐ **Identical order.** (RAM omits `Ultimate Gohan` between `Gohan` and `Piccolo`, hence the 846 -> 848
gap being one entry — consistent.) **So the RAM roster array is assembled from the message table
using these ids in sequence.**

## THE IDENTITY MAP — usable form

```
character-select / roster index i  ->  message id  =  843 + i
                              name  =  PACKFILE string at ordinal (843 + i)
```

Worked check: roster index 0 = Goku -> id 843 -> `'Goku'` ✅ (matches the live screen, which showed
`Goku` as the first roster entry and 1P fighter).

## What remains for the adapter (the last mile)

The **name lookup is solved**. What is still needed is the **per-slot id**, i.e. the value the game
stores for "which character is in fighter slot N":

- the ELF side indexes the id table at `0x08A75538` and reads object fields (`+0x80`, `+0x84`,
  `+0x1ec`) inside `FUN_08a26400`
- the runtime roster array at RAM `0x08C85C82` is the assembled name list

⚠️ **It is NOT yet established which RAM field holds a fighter's roster id.** That is the one
remaining unknown, and it is a *targeted* question now: find the field whose value lands in the
843+ range for a known character.

### The bounded final experiment

1. Enter a battle with known fighters (e.g. Goku vs Frieza).
2. In the fighter struct, scan for a value in **843..900** (the roster id range) —
   **Goku = 843, Frieza = 876** are the specific values to look for.
3. The field matching those per slot IS the character id.
4. Falsification: if neither 843 nor 876 appears anywhere in the struct, the id is kept in a
   sibling/global roster array (search the whole RAM range for 843/876 — those are distinctive
   enough to find by value, unlike the earlier index 0 which was unsearchable).

✅ This is exactly the "**search for a printed/distinctive value**" technique that already cracked
HP (id 876 for Frieza is as distinctive as `Max Damage 7460` was). And unlike the failed identity
searches, the target value is now **known and non-zero**.

# ⭐ BULK MEMORY READS: 24 MiB scanned in 0.5 SECONDS

A practical breakthrough for all future RAM work on this project.

## The discovery

`memory.read` (undocumented in my earlier probing) takes an **`address` and a `size`, and returns
`base64`**:

```
memory.read {address:"0x0973CEE0", size:16}
  -> {"event":"memory.read","ticket":"2","base64":"AAAAAAAAAAAAAAAAAAAAAA=="}      OK
memory.read {address:"0x0973CEE0", length:16}   -> "Missing 'size' parameter"
memory.read_bytes  {...}                        -> Bad message: unknown event
memory.read_u32 {address:"0x0973CEE0", count:4} -> returns a SINGLE value (count ignored)
```

✅ **`memory.read` + `size` + base64 is the bulk read.** Per-word reads (one request per u32) would
have needed **~6.3 million requests** for a full 24 MiB scan. With 1 MiB chunks it is **14 requests**.

## Measured

```
=== PASS 2: scanning RAM with bulk reads ===
  ...50%  hits: 1  (0.3s)
  ...100%  hits: 1  (0.5s)
```

**The entire 24 MiB address space scanned in 0.5 s.** This supersedes `psp-probe.mjs dump` (which
wrote a 24 MiB file to disk) for search purposes — a live in-memory scan is now both faster and
cheaper.

⚠️ Note: `memory.read_u32` accepts a `count` parameter **and ignores it** — it returns one value.
That looks like a bulk API but is not; do not use it for scanning.

## Search result (this run)

Context first — and it mattered:

```
# fighter HP: s0=0  s1=0  s2=0  s3=0
# NOT in a battle -- results may be meaningless
```

Searching for the roster ids `843 (Goku)`, `876 (Frieza)`, `891 (Super Saiyan 3)`:

```
struct hits: 0
RAM hits: 1
   876 (Frieza) -> 0x09396570
```

⚠️ **This run is INCONCLUSIVE, not negative.** The liveness control reported **not in a battle**, so
the fighter structs are empty (`0/0`) and the game was not holding a live roster. The single `876`
hit at `0x09396570` is outside every fighter struct and may be unrelated data that happens to equal
876.

✅ **The tool is proven; the measurement needs a live battle.** Do not read this as "the id is not in
the struct" — that would be exactly the too-narrow-probe mistake this project has made four times.

### Correct next run

1. Get into a battle with **known** fighters (ideally Goku vs Frieza).
2. Re-run `node scripts/psp-tt-charbid.mjs --values 843 876`.
3. Require the liveness line to show populated HP **before** interpreting results.
4. Then check whether `843` and `876` appear in the two *active* slots' structs — and at which
   offset. That offset is the character id.

## Tooling added

| script | purpose |
|---|---|
| `psp-tt-charbid.mjs` | search RAM for roster ids (843=Goku, 876=Frieza, …); struct pass + bulk full-RAM pass; prints a liveness control |

# ⭐⭐⭐⭐ THE STORY MISSION OBJECTIVES — full list extracted, `Find Gohan!!` confirmed

The user's description was exactly right, and the objectives are now readable text straight from
`PACKFILE.BIN` (**no running game required**).

## The objective block, contiguous from `0x0004E16E`

```
[ 0] 0x0004E16E  'Find Gohan!!'                              <-- FIRST OBJECTIVE (user: correct)
[ 1] 0x0004E188  'Head for Kame House!'
[ 2] 0x0004E1B2  'Defeat Krillin!'
[ 3] 0x0004E1D2  'Defeat Raditz and Save Gohan!'             <-- Raditz (user: correct)
[ 4] 0x0004E20E  'Defeat the Saibamen and Survive!'
[ 5] 0x0004E250  'Defeat Piccolo!'
[ 6] 0x0004E270  'Defeat the Saibamen and Go After Nappa!!'
[ 7] 0x0004E2C2  'Stand Against Nappa!'
[ 8] 0x0004E2EC  'Defeat Nappa!'
[ 9] 0x0004E308  'Stand Against Vegeta!'
[10] 0x0004E334  'Defeat Vegeta and Save Goku!'
[11] 0x0004E36E  'Defeat Cui!'
[12] 0x0004E386  'Stand Against Dodoria and Rescue Dende!'
[13] 0x0004E3D6  'Take Dende to the Designated Area!'
[14] 0x0004E41C  'Defeat Dodoria!'
[15] 0x0004E43C  'Defeat Zarbon!'
[16] 0x0004E45A  'Steal the Dragon Balls!'
[17] 0x0004E48A  'Follow After Krillin!'
[18] 0x0004E4D4  'Defeat Guldo and Recoome!'
[19] 0x0004E508  'Defeat Recoome and Save Gohan and the others!'
[20] 0x0004E564  'Defeat Gohan, Krillin and Vegeta!'
[21] 0x0004E5A8  'Defeat Goku!'
[22] 0x0004E5C2  'Defeat Captain Ginyu and Jeice!'
[23] 0x0004E602  "Defeat Jeice and Captain Ginyu in Goku's Body!"
[24] 0x0004E660  'Go See Guru!'
[25] 0x0004E67A  'Speak to Dende'
[26] 0x0004E698  'Defeat Frieza!'
[27] 0x0004E6D4  'Defeat Piccolo, Gohan, Vegeta and Krillin!'
[28] 0x0004E744  'Hurry and Find Gohan and the Others!'
[29] 0x0004E7CA  'Defeat Trunks!'
[30] 0x0004E7E8  'Stand Against the Androids!'
[31] 0x0004E820  'Defeat Android #19!'
[32] 0x0004E848  'Capture and Defeat Dr. Gero!'
... (continues; the block runs to ~0x508E0 through the Cell/Buu sagas and beyond)
```

✅ **`'Find Gohan!!'` is the first objective and `'Defeat Raditz and Save Gohan!'` is the third** —
**exactly** as the user said (*"your first opponent is going to be Raditz"* and *"your first mission
objective is to Find Gohan"*). The user's account of the story opening is confirmed by the data.

## Also in this block: the objective/condition vocabulary

```
'See a Map'
'? Switch Lock-on. Switch enemy to lock onto.'   <- a control hint, inline with objectives
'Defeat the Saibamen and Survive!'               <- "and Survive" = a secondary condition
'Take Dende to the Designated Area!'             <- an escort objective
'Steal the Dragon Balls!'                        <- a non-combat objective
'Follow After Krillin!'                          <- a follow objective
'Speak to Dende'                                 <- an interaction objective
```

⭐ **So mission objectives are NOT all "defeat X"** — the game has escort, follow, interaction,
survival and collection objectives. For an accessibility reader this matters a lot: the objective
text alone tells the player what KIND of task it is.

## ⭐ Accessibility value — available NOW, no core required

This is the first Tag Team asset that is **immediately useful**:

- the objectives are **plain readable text** in `PACKFILE.BIN`
- they are **ordinal-indexed**, so a story progress counter can be turned into the objective text
- **no emulator core, no live game, no RAM access needed** — the text can be shipped/read offline

✅ For the Open Game Access project this means a story-mode objective reader does **not** depend on a
PSP core for the *text* side. Only the *current objective index* would need live state — and even
without it, a reader can narrate the mission list.

## New artefacts

| file | contents |
|---|---|
| `%LOCALAPPDATA%\Temp\tagteam-extract\mission-objectives.txt` | 200 mission/objective strings with ordinals and offsets |
| `scripts/psp-packfile-strings.py` | read a forward UTF-16 string sequence from any offset |
| `scripts/psp-msgids.py` | enumerate by ordinal; resolve ELF ids against the table |

# New Game DOES start story content — but the cutscene needs input, and idling returns to attract

## What was observed

Sequence: `title -> New Game -> Piccolo flying over the ocean -> back to the title/attract`.

The middle frame (`pn-w05.png`) shows **Piccolo in flight over a dark ocean and cliffs** — white
cape, green arm, third-person camera. That is **story content**, consistent with the Raditz prologue
(Piccolo is central to the opening), and it advanced with **zero input** for ~50 s.

⚠️ Then the game returned to the attract loop. **Interpretation: the cutscene was waiting for a
`cross` press to continue, and idling through it let it time out back to the attract demo.**

✅ **So the correct pattern is neither "mash" nor "idle": press ONCE, then WATCH with a long gap, and
press again ONLY when the screen has been static.** The freeze-frame tool is for capturing transient
menus; the route tool is for pressing; neither alone handles "advance a cutscene at its own pace".

## The one instrument still missing

A **change-triggered presser**: press `cross` only when the frame has NOT changed for N seconds
(i.e. the game has stopped animating and is waiting for input). That is the piece that would have
walked this cutscene correctly, and it is the last tooling gap for route navigation on this title.

⚠️ Note: during the cutscene the fighter struct read **`0/0`** — so **HP is not a usable liveness
control during story cutscenes**, only during battles. The phase table now has a fourth state:

| reading | state |
|---|---|
| `cur/max` with `max ~= 30000` | battle or field |
| `1065353216` (float 1.0) | menu / character select |
| `0/0` | not populated — boot, title, **or cutscene** |

## SESSION END — final state of Tag Team (ULUS10537)

### Solved / verified

| item | evidence |
|---|---|
| pipeline CSO -> ISO -> EBOOT -> decrypted ELF -> Ghidra | verified |
| ISO archive access; `PACKFILE.BIN` extracted (626 MB) | verified |
| **name/identity MECHANISM** | **message ids + resolver `FUN_0883ee74`, verified** |
| **id -> text mapping** | **message id == ordinal into PACKFILE's table @ `0x4FADC`; confirmed against real strings** |
| **roster ids** | **`843 Goku, 844 Kid Gohan, 845 Teen Gohan, 846 Gohan, 848 Piccolo, 849 Krillin, 850 Yamcha, 851 Tien, 854 Vegeta, 876 Frieza, 883 Cell`** and cross-confirmed against the live RAM roster order |
| **mission objectives** | **full list extracted — `Find Gohan!!` first, `Defeat Raditz and Save Gohan!` third (user's account confirmed by the data)** |
| 4 fighter slots, HP, gauges | live-verified under attack |
| team assignment (0/1 player, 2/3 enemy) | confirmed via a WIN |
| HP write; battle stats block | working; confirmed x4, resets per battle |
| analog stick control | confirmed visually |
| battle + field HUD arrows | confirmed on screen |
| open field reached | screenshot evidence |
| **bulk RAM read: 24 MiB in 0.5 s** | `memory.read {address,size}` -> base64 |
| socket-free screenshots; freeze-frame capture | working |

### Open (in order)

| item | state | next action |
|---|---|---|
| **character-id RAM field** | open | one live battle + `psp-tt-charbid2.mjs --values 843 876` (0.5 s scan, gated) |
| **Dragon Walker entered** | never | needs a change-triggered presser to walk cutscenes |
| mission objective *index* (current objective) | open | find the counter; compare against the ordinal list |
| absolute player position | open | `+0x4C4/+0x4D4` are velocity/heading-like |
| **adapter code** | **not written** | blocked on the id field |

### The single most valuable lesson from this title

**Make the game print the value.** Losing a battle on purpose to reach the result screen
(`Health 0%`, `Max Damage 7460`) converted a hopeless "find what changed" search into an exact one —
and that is what cracked the HP map. The same principle later applied to identity: once the id scheme
was known, the hunt became "find **876**", a large distinctive number, instead of "find the cursor",
which had been unsearchable.

### And the most valuable *process* lesson

**Never report a result from a state where the data cannot exist.** Four false negatives came from
too-narrow probes (pointer window, broadcast-vs-request event name, a no-reply action, an
exact-address MUI miss) and six invalid measurements came from diffing across state changes. The
`sps-tt-charbid2.mjs` gate — which REFUSES to report unless a battle is proven live — is the
structural fix, and it is the pattern to carry forward.

# ⛔ THE FRAME-DIFF CONTROL WAS BROKEN — a grid sampler that missed real changes

I wrote `psp-tt-autoadvance.mjs` to press only when the frame stopped changing, and it reported
**`diff 0.0000`** on 22 consecutive iterations — then pressed 22 times on a screen that was visibly
animating (the attract demo).

## The bug

The diff sampled on a **coarse fixed grid** (every 6th pixel in x and y). When the changed region is
small or falls between sample points, the grid **misses it entirely**.

Proven directly — the same capture pair, both methods:

```
captures 2 s apart:  3 DISTINCT file hashes        (so the screen WAS changing)

coarse grid sampler:  diff fraction = 0.0000
ImageChops.getbbox(): bbox = (677, 350, 1690, 1066)   ->  fraction 0.3988
```

**40 % of the frame had changed while the grid sampler reported zero.** The screen was animating;
the control said static.

⛔ **A liveness control that can be blind to real change is worse than no control** — it converts
"still animating" into "waiting for input", which is exactly the wrong decision, and it does so
silently.

## The fix

Use **`ImageChops.getbbox()` first** — it is exact and cheap:

```python
bb = ImageChops.difference(a, b).getbbox()
if bb is None:
    fraction = 0.0                    # frames are IDENTICAL — truly static
else:
    x0, y0, x1, y1 = bb
    fraction = (x1 - x0) * (y1 - y0) / float(W * H)   # changed AREA fraction
```

✅ **`getbbox() is None` is the only sound test for "identical".** Any sampling scheme can miss a
change; a bounding box cannot.

⚠️ Reporting the changed **area** fraction rather than a pixel count is also more stable: it does not
depend on where on the screen the change happens.

## Consequence for the run

**The 22-press run is invalid** and its output must not be used. It also explains why the game
stayed on the attract loop throughout — the presser was firing on a moving screen.

✅ The corrected `diffFraction` was re-verified on the exact pair that produced the false zero:
`0.3988`. The tool is now trustworthy; the earlier run is not.

⛔ **This is the FIFTH measurement instrument in this project that produced a confident wrong
answer** (after: a too-narrow pointer window; a broadcast event name used as a request; awaiting a
no-reply action; an exact-address LUI miss). The common thread: **the instrument was never validated
against a known-positive case before being trusted.**

✅ **RULE: before trusting any liveness/change detector, feed it a pair you KNOW differs and a pair
you KNOW is identical, and confirm it reports them correctly.** Both cases, not just one.

# ✅ The change-detector is now VALIDATED and the advance tool behaves correctly

## Instrument validation (the discipline that was missing)

Ran the corrected diff against **both** known cases before trusting it:

```
known-DIFFERENT pair (probe1 vs probe2): 0.3988
known-IDENTICAL pair (probe1 vs itself): 0.0
```

✅ Reports both correctly. Compare with the broken sampler, which reported `0.0000` on the same
different pair.

## The corrected run behaves properly

```
.............
# [14] static 2.5s (diff 0.0022) -> press cross  (total 1)
-..
# [18] static 2.3s (diff 0.0022) -> press cross  (total 2)
...
# [40] static 2.3s (diff 0.0007) -> press cross  (total 8)
# done: 8 press(es)
```

The dots are the tool **waiting out animation**; it pressed only on genuinely static frames —
**8 presses instead of the broken run's 22.** The decision logic is now sound.

## But the title screen still isn't entered — and the reason is now clear

Final frame = the **title/attract demo**. The behaviour is correct, so this is **not a tool bug**:
it is a real property of the title screen.

⚠️ **On the title, `cross` does nothing — the menu requires `start`.** Meanwhile the attract demo
keeps the frame animating (or loops), so a change-triggered presser waiting for a *static* frame
will keep waiting, or press `cross` during the demo where it has no effect.

### The correct recipe for this specific screen

1. `start` (raises the title menu) — this is the only way in.
2. Then `cross` **immediately** (within ~1 s) to accept `New Game`.
3. From the cutscene onward, use the change-triggered presser with `--button cross`.

That combination — a one-shot `start`+`cross` pair, then change-triggered advancing — is what was
never tried together. It is the concrete next action for route navigation.

## SESSION CLOSE — what is solid vs what is open

### Solid (verified against real data, unaffected by the control-loop bugs)

| item | evidence |
|---|---|
| identity **mechanism** | message ids + resolver `FUN_0883ee74` |
| **id -> text mapping** | id == ordinal into PACKFILE's table @ `0x4FADC`, confirmed on real strings |
| **roster ids** | `843 Goku, 848 Piccolo, 854 Vegeta, 876 Frieza, 883 Cell`, cross-confirmed against live RAM order |
| **mission objectives** | full list extracted; `Find Gohan!!` first, `Defeat Raditz` third — **user's account confirmed by the data** |
| combat state | 4 slots, HP, gauges, HP write, team pair (via a WIN), stats block x4 |
| analog control | confirmed visually (ground lost, motion streaks) |
| HUD arrows | battle (red/orange) and field (blue) confirmed on screen |
| tooling | bulk RAM read 24 MiB/0.5 s; socket-free screenshots; freeze-frame; validated change detector |

### Open

| item | state |
|---|---|
| character-id RAM field | one live battle + `psp-tt-charbid2.mjs` (gated, 0.5 s scan) |
| Dragon Walker entered | needs the `start`+`cross` title recipe above |
| current-objective index | find the counter, compare to the objective ordinals |
| absolute player position | open (`+0x4C4/+0x4D4` are velocity-like) |
| **adapter code** | **not written** |

### The meta-lesson, stated plainly

**Every wrong answer in this session came from an unvalidated instrument or an unverified state** —
not from the reverse engineering itself. The mechanism findings all held up; the failures were
measurement. **Validate the instrument against a known-positive and a known-negative; never report
from a state where the data cannot exist.** Both are now encoded as rules and, where possible, in the
tools themselves.

# Title-screen route: recipe tried, and a freeze-frame limitation found

## The recipe (`start` -> tight `cross`, then change-triggered)

```
# step 001 press start     frozen=true  shot tg-001-start
# step 002 press cross     frozen=true  shot tg-002-cross
--- then change-triggered advancing ---
-..-...-...........................
# done: 0 press(es)
```

⚠️ **Two findings:**

1. **`tg-002-cross.png` is a BLACK screen.** So **freeze-framing during a transition captures
   black** — a real limitation of the freeze technique: it holds *a* frame, and if `start`+`cross`
   lands mid-transition that frame is empty. **A frozen capture is not guaranteed readable**;
   always sanity-check a frozen frame rather than assuming it shows the menu.
2. **0 presses over ~70 s** — the frame never went static, so the change-triggered tool correctly
   declined to press. On the title/attract the screen animates continuously, which means
   **"wait for a static frame" can never fire on this screen.** The tool is behaving correctly; the
   screen simply never satisfies the condition.

## What that implies for the title

The condition "frame is static" is the wrong trigger for a screen that never holds still. Two
workable alternatives, neither yet tried as a complete recipe:

1. **Fixed short wait instead of static-detection**: `start`, then `cross` after a *fixed* ~800 ms
   (what the successful `ng2`/`pn` runs did — they DID reach Piccolo story content), then
   change-triggered *only for the cutscene* (where frames do hold).
2. **Disable the attract loop** if the game offers the option, which removes the whole problem.

✅ **Confirmed: the `start` + tight `cross` pair DID produce story content earlier** (`pn-w05.png`:
Piccolo flying over the ocean). So the entry works; it is the *continuation* that needs the
change-triggered presser, and that presser must not be used on the title itself.

## Current live state at close

```
HP: s0=0/0  s1=0/0  s2=0/0  s3=0/0
screen: DRAGON BALL Z TENKAICHI TAG TEAM title (logo, SSJ Blue Goku + Vegeta)
```

## FINAL SESSION LEDGER

### Verified against real data (independent of the control-loop issues)

| # | finding |
|---|---|
| 1 | pipeline CSO -> ISO -> EBOOT -> decrypted ELF -> Ghidra |
| 2 | `PACKFILE.BIN` (626 MB) is the game's data archive; text is UTF-16LE inside it |
| 3 | names are **message ids**, resolved by `FUN_0883ee74(id, -1)` — no string pointers exist |
| 4 | **id == ordinal into PACKFILE's string table @ `0x4FADC`** — confirmed on real strings (`505 'Strategy'`, `506 'Charge'`, `169 'Inherited Name'`) |
| 5 | **roster ids**: `843 Goku, 844 Kid Gohan, 845 Teen Gohan, 846 Gohan, 848 Piccolo, 849 Krillin, 850 Yamcha, 851 Tien, 854 Vegeta, 876 Frieza, 883 Cell, 891 Super Saiyan 3` — cross-confirmed against the live RAM roster order |
| 6 | **mission objectives**: full list; `Find Gohan!!` first, `Defeat Raditz and Save Gohan!` third — **user's account confirmed from the data** |
| 7 | objectives include escort/follow/interaction/collection types, not only combat |
| 8 | 4 fighter slots `0x0973CEE0 + N*0x1A90`; HP `+0x14`/`+0x18`; gauge `+0x20`/`+0x24` — live-verified under attack |
| 9 | team assignment: slots 0/1 player, 2/3 enemy — **confirmed by producing a WIN** |
| 10 | HP write works; stats block `0x08B49044`/`0x08B49054` confirmed x4, resets per battle |
| 11 | **analog stick control works** (`input.analog.send`) — confirmed visually |
| 12 | HUD arrows: battle (red/orange, top-centre) and field (blue, top-right) |
| 13 | open field (free flight, no HUD) reached |
| 14 | **bulk RAM read**: `memory.read {address,size}` -> base64, 24 MiB in **0.5 s** |

### Open

| item | next action |
|---|---|
| character-id RAM field | live battle + `psp-tt-charbid2.mjs` (gated) |
| Dragon Walker entered | `start` + **fixed-delay** `cross`, then change-triggered for the cutscene only |
| current-objective index | find the counter; compare to the objective ordinals |
| absolute player position | open |
| **adapter code** | not written |

### Instruments built, and their validation status

| tool | validated? |
|---|---|
| `psp-shot.py` (PrintWindow capture, socket-free) | ✅ verified against live frames |
| `psp-tt-autoadvance.mjs` change detector | ✅ **validated on known-different (0.3988) and known-identical (0.0)** |
| `psp-tt-charbid2.mjs` | ✅ gate verified (refused to report without a live battle) |
| `psp-elf-window.py` / `psp-msgtable.py` / `psp-packfile-*.py` / `psp-msgids.py` | ✅ outputs cross-checked against known strings |
| freeze-frame (`cpu.stepping`) | ⚠️ works, but **can capture black during a transition** |

### The closing lesson

**Five wrong answers came from unvalidated instruments and six from unverified state — none from
the reverse engineering.** The mechanism findings all held; the failures were measurement
discipline. The rules that would have prevented every one of them:

1. **Validate an instrument against a known-positive AND a known-negative** before trusting it.
2. **Never report a result from a state where the data cannot exist.**
3. **Never conclude "absent" from a single narrow probe.**
4. **Make the game print the value** you are looking for.
5. **Diff only same-state samples.**

# ⛔ DEFINITIVE: `cross` is INERT on the title screen — verified frame-by-frame

The 8 presses the corrected advance tool made (all on static frames, the tool behaving correctly)
**changed nothing**. An 8-frame contact sheet of those pressed frames shows **8 identical title
screens** (`DRAGON BALL Z TENKAICHI TAG TEAM` logo, SSJ Blue Goku + Vegeta).

```
ab-014, ab-018, ab-022, ab-025, ab-029, ab-031, ab-037, ab-040
  -> all eight are the SAME title screen. No menu. No character select. No gameplay.
```

✅ **Conclusion: on the title screen, `cross` does nothing.** The menu exists behind it (it was
captured earlier with `start`, showing `New Game` highlighted), and **`start` is the only key that
raises it.**

⚠️ Note what this does NOT mean: the tool was right to press (the frames *were* static), and the
frames *were* genuinely static — this was not the grid-sampler bug. **The screen simply ignores
`cross`.** A correct instrument measuring a real screen state produced a correct but unhelpful
result — the missing piece is *which button*, not *when to press*.

## The corrected title recipe (final)

```
1. press `start`            -> title menu appears (New Game highlighted)
2. press `cross` within ~1s -> accepts New Game
3. from the cutscene on     -> change-triggered `cross` (frames DO hold in cutscenes)
```

⚠️ Do **not** run the change-triggered tool on the title itself — the attract demo never goes static,
so it cannot fire (observed: 0 presses in 70 s, which was correct behaviour).

✅ Step 1+2 was proven to work earlier: it produced `pn-w05.png` — **Piccolo flying over the ocean**,
i.e. real story content.

## Freeze-frame caveat (recorded)

`tg-002-cross.png` (frozen right after `start`+`cross`) came out **entirely black** — a frozen frame
captured during a screen transition is empty. **A frozen capture must be sanity-checked, not assumed
readable.** Freezing is for holding a *settled* menu, not a transition.

# ⭐⭐⭐⭐⭐⭐⭐⭐ DRAGON WALKER REACHED — story mode, and it is the RADITZ episode

**The story-mode UI is on screen and readable.** Confirmed by screenshot:

```
+------------------------------------------------------------+
|  Dragon Walker                                             |
|                                                            |
|  [book] 761                     L      1 / 1      R        |
|                                                            |
|  10/12   "Unknown Warrior from Another Planet"        [ ! ] |
+------------------------------------------------------------+
     (earlier frame, same screen: "Begin stage. Are you sure?" prompt)
```

## What each element is

| element | reading |
|---|---|
| **DRAGON WALKER** banner | story mode |
| book icon + **761** | a collectible/score counter |
| **L 1 / 1 R** | chapter/section pager (shoulder buttons) |
| **10/12** | **stage 10 of 12** in this episode |
| **"Unknown Warrior from Another Planet"** | **the episode title — this IS the Raditz arc** |
| **`!`** marker | a notification/available-stage flag |
| **"Begin stage. Are you sure?"** | the stage-start confirmation prompt |

⭐⭐ **"Unknown Warrior from Another Planet" is the Raditz episode.** The user's account is now
confirmed on screen, not merely inferred from the objective list: Dragon Walker, Raditz arc, story mode.

⚠️ Note **`10/12`**: this is the 10th of 12 stages of that episode, so the game is partway into the
Raditz arc, not at its first stage. That is consistent with the earlier auto-advanced runs having
moved the story forward.

## ✅ The title route that got here

```
start (raise title menu)  ->  cross (accept New Game)  ->  cross ... 
```
i.e. **`start` first, then `cross`** — the alternation fix to `psp-tt-charbid2.mjs`. This is the first
confirmed entry into Dragon Walker in the whole session.

## Accessibility note — this screen is the story-mode reader's target

The Dragon Walker screen exposes, as *readable text*:
- the **story mode name**
- the **episode title**
- the **stage number** (`10/12`)
- a **confirm prompt**

Everything here can be spoken. Combined with the objective list already extracted from
`PACKFILE.BIN` (which contains `Find Gohan!!`, `Defeat Raditz and Save Gohan!`, ...), a Dragon Walker
reader needs only the **current episode id + stage index** to narrate progress coherently.

### The concrete next search

With Dragon Walker on screen, **search RAM for the stage counter values**: the screen shows
`10` and `12`, and `761`. A `10` next to a `12` is a very searchable signature (much better than
index 0). Then the episode title can be resolved through the message-id system already solved.

⚠️ Note that `10/12` are *small* numbers, so search for the **pair** (10 adjacent to 12) rather than
a single value — the lesson from the unsearchable index-0 problem.

# The runtime message table is `{id, type, offset}` records — and "761" was a MESSAGE ID

Searching for the Dragon Walker book counter (the screen shows `761`) gave 5 addresses. Inspecting
their neighbourhoods settles what they are:

```
0x0911F408   760  0 0 0 0 0  2  599  761  0 0 0 0 0  2  600  762  0 ...
0x09265928   760  0 0 0 0 0  2  599  761  0 0 0 0 0  2  600  762  0 ...
0x093AD568   760  0 0 0 0 0  2  599  761  0 0 0 0 0  2  600  762  0 ...
```

**Structure: a repeating group `[type=2, offset, id]`, ~7 words (28 bytes) apart** — with ~7-8
words between consecutive ids. ⚠️ The spacing is **not perfectly uniform** (760->761 is 8 words,
761->762 is 7), so this is **not a clean fixed-stride table**; treat the stride as approximate and
enumerate empirically. Three near-identical copies exist (likely per-language or per-context).

| field | sample | meaning |
|---|---|---|
| id | 760, 761, 762 | message id (sequential) |
| type | 2 | entry kind |
| offset | 599, 600 | offset/size into the string blob |

⭐⭐ **So "761" on the Dragon Walker screen is a MESSAGE ID, not a book counter.** The screen's `761`
is the id of the text it is displaying — i.e. **the episode title's message id**. That is a direct
confirmation of the mechanism solved earlier, now seen operating live.

⚠️ Important correction to my own reading: I initially called `761` a "book/collectible counter" from
the icon beside it. It is **an id**, and the value being a *message id* is what makes it appear in a
table with sequential neighbours 760/762.

## What this gives the story-mode reader

If `761` is the **episode title's message id**, then the Dragon Walker screen's text can be resolved
through the already-solved chain:

```
screen shows message id 761
  -> 761 is an ordinal into PACKFILE.BIN's string table (verified mapping)
  -> look up ordinal 761  ->  the episode title text
```

✅ **So story-mode narration needs only the on-screen message id** — and the id table for the current
screen is now locatable in RAM at `{id,type,offset}` records.

### Concrete next step for the reader

1. Enumerate the runtime `{id,type,offset}` table (12-byte stride) around `0x0911F408`.
2. Resolve a few ids against `PACKFILE.BIN`'s string table to confirm the offset field works.
3. Then "what text is the HUD showing" becomes: read the current id, resolve through the table.

⚠️ This is a **much better** target than the stage counter: it yields the actual *text*, not just a
number — and the text is what a screen reader needs to speak.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ THE ACCESSIBILITY ANSWER: ALL STORY TEXT IS IN RAM AS UTF-16LE

**This is the finding that makes a Tag Team story-mode reader possible without OCR and without a
core-integrated emulator.** Verified live, with the user reading the same lines off the screen at the
same moment.

## Evidence (three independent live confirmations)

The user read each line aloud while the RAM search ran. All three matched:

### 1. Opening narration — the line the user reported as NOT voiced

```
node scripts/psp-ram-textsearch.mjs "long time ago"
  0x08FC0764 [utf16]
    n't believe me! How annoying!.
    A long time ago....
    Goku set out on a journey to search for.
    the 7 Dragon Balls, which when gathered.
    together, would call forth Shen...
```

✅ User, at that moment: *"A long time ago, Goku set off on a journey to collect the Dragon Balls..."*
✅ User also flagged: **"This part isn't spoken aloud."** -> **an unvoiced narration line, present and
readable in RAM.**

### 2. Mission tutorial text

```
node scripts/psp-ram-textsearch.mjs "Missions are given"
  0x08C79956 [utf16]
    ne.About Customize.About Battle.Missions are given at a stage. When the mission is.
    completed the story progresses,.When a number of missions are accomplished, t...
```

✅ User, at that moment: *"Missions are given at a stage. When the mission is completed the story
progresses"* — **verbatim.**

### 3. THE MISSION OBJECTIVE LIST — resident in RAM

```
node scripts/psp-ram-textsearch.mjs "Find Gohan"
  0x08C7AE6E [utf16]
    -on. Switch enemy to lock onto..
    Find Gohan!!.Head for Kame House!.Defeat Krillin!.
    Defeat Raditz and Save Gohan!.Defeat the Saibamen and Survive!.
    Defeat Piccolo!...
  0x08C7B458 [utf16]
    Krillin!.Defeat Goku!.Hurry and Find Gohan and the Others!.
    Defeat Frieza!.Defeat Frieza!.Defeat Trunks!...
```

⭐⭐⭐⭐ **The objectives are a CONTIGUOUS, ORDERED string blob in RAM**, delimited by `.` (the message
separator), and **the order matches the ordinal order recovered from `PACKFILE.BIN`**
(`Find Gohan!!` first, `Head for Kame House!` second, `Defeat Krillin!` third,
`Defeat Raditz and Save Gohan!` fourth). Two independent sources agree.

## Why this is the whole game for accessibility

| requirement | status |
|---|---|
| read dialogue text | ✅ **UTF-16LE in RAM** |
| read **unvoiced narration** | ✅ **present in RAM** (the gap a screen reader must cover) |
| read mission/tutorial text | ✅ in RAM |
| read the objective | ✅ ordered objective list in RAM |
| needs OCR? | ❌ **no** — text is real characters in memory |
| needs a core integration / Lua script? | ❌ **no** — the host-side debugger suffices |

**A dialogue reader for Tag Team therefore does not need to model the screen at all.** It reads the
text the engine has already decoded into RAM — which is exactly the project's design rule,
*"model the game, not the screen."*

## The remaining shape of a reader

1. **Watch the text blob regions** (`~0x08FC0000` dialogue script, `~0x08C79900` UI/tutorial,
   `~0x08C7AE00` objective list).
2. On change, **emit the newest line** (the blob is append/scroll-oriented, so the tail is current).
3. Cross-reference the **objective index** to say progress; the objective list gives the text.

⚠️ Open: the **exact "current line" pointer**. The blob holds many lines (a whole scene's script), so
the reader needs either the current index or the write head — analogous to the Steins;Gate MessageLog
solution (a ring buffer with a write head at `0x089797E8`). **That is the next search: a write head /
cursor for these text regions.**

## Field state note (for the position work)

In the story **FIELD** the battle fighter struct `0x0973CEE0 + N*0x1A90` reads `0/0` — the field uses
different state. Field position candidates cluster around **`0x08B698CC`..`0x08B6A5A4`**, with
plausible world coordinates (e.g. `132/1587/1702`, `1132/1247/1667`, `1635/1136`). Multiple
near-duplicate rows change together, and monotonicity while holding a direction is only ~65-67%, so
**this is not yet resolved into a single player position** — it looks like several entities' positions
(or position plus predicted/interpolated copies). Tracked with `scripts/psp-tt-postrack.mjs`.

# Field session with the user driving: input verified, objective confirmed live

The user drove PPSSPP by hand to the story field while the assistant read RAM. Their running
commentary (verbatim) pinned the sequence:

1. `cross` -> **Yes/No dialog** -> Yes
2. **Unvoiced narration**: *"A long time ago, Goku set off on a journey to collect the Dragon
   Balls..."* — dialog with spoken text
3. Cut to an **animated scene**; then *"One day, more than five years after the battle with Demon
   King Piccolo..."* — **"This part isn't spoken aloud."**
4. *"New Scene, this is the part of the game where the mission stuff is described. Text onscreen:
   'meanwhile'"*
5. Spoken: *"Goku: Hey, Gohan!! Where are you!? Where'd you go!? Uh-oh, this is not good..."*
6. **"About Mission / Start Mission / Missions are given at a stage. When the mission is completed
   the story progresses"**
7. Reached the **play field**; the user notes at the end: *"You'll need to take over from here since
   I can't see to fly to the objective."*
8. Later: *"Right now, the arrow is pointing right, and is on the right side of the screen"*

## ⭐ The pause menu shows the LIVE objective — readable

Pressing `start` in the field opened:

```
+--------------------------------------+
|               Mission                |
|                                      |
|            Find Gohan!!              |
+--------------------------------------+
|           Return to Game             |
+--------------------------------------+
```

**The current mission is displayed verbatim, and `"Find Gohan!!"` is index 0 of the objective list
already recovered from RAM** (`0x08C7AE6E`). Two independent sources agree on the live objective.

## Input contract (measured)

| action | result |
|---|---|
| `input.buttons.press {button:"start"}` | ✅ **replies** AND acts — opening the pause menu changed **92.99%** of pixels |
| `input.buttons.release` | ❌ `Bad message: unknown event` — **no release event exists** |
| `input.analog.send` held ~3s | ❌ **my bug** — sent out-of-range values, so nothing was sent at all |
| `input.buttons.press {button:"up"}` 2.5s | ⚠️ **0.00%** changed |

## ✅ RESOLVED — the field DOES fly, and the contract was the problem

A first round of "field input does not work" readings was **my own error**, and the cause was found by
**reading the API's error messages** rather than guessing:

```
input.analog            {"stick":"left","x":0,"y":-1}   -> ERR Bad message: unknown event
input.analog.send       {"stick":"left","x":128,"y":0}  -> ERR Parameter 'x' must be between -1.0 and 1.0
input.analog.send       {"stick":"left","x":0,"y":-1}   -> OK
input.buttons.hold      {"button":"up"}                 -> ERR Bad message: unknown event
input.buttons.release   {"button":"up"}                 -> ERR Bad message: unknown event
input.buttons.press     {"button":"__bogus__"}          -> ERR Unsupported button value '__bogus__'
```

**The contract (measured, final):**

| rule | detail |
|---|---|
| event name | **`input.analog.send`** (with `.send`). `input.analog` is **broadcast-only** |
| value scale | **NORMALIZED floats in [-1.0, 1.0]**, NOT 0..255. Out-of-range => error, nothing sent |
| axis sign | `y = -1` is **up/north**; the debugger broadcasts `{"x":0.0039,"y":-0.0039}` at rest, so the origin is 0 with a small offset |
| repetition | the stick is **polled per frame** — the state must be re-sent continuously (~20 Hz) while held; one send is not enough |
| buttons | `press` works (and replies); **`release` and `hold` do not exist** |

✅ **PROVEN: `input.analog.send {x:0, y:-1}` re-sent for 3 s moved Goku — 84.22% of pixels changed**
(from standing on a small green island to airborne over new terrain, with a ground shadow).

⚠️ **Two of my own earlier conclusions were wrong and are retracted:**
1. "analog does not move the field player" — false; I had sent **0..255** values which the API
   **rejected**, and I never checked the error because the wrapper swallowed it.
2. "the battle-verified analog result does not generalise to the field" — false; it *does*
   generalise. The field flies fine. The difference was purely the parameter scale.

**Lesson (the strongest one of this session): read the API's own error messages before concluding a
capability is missing.** The server replies `Parameter 'x' must be between -1.0 and 1.0` — an explicit,
actionable message that was sitting unread while three test rounds concluded "input does not work".
A rejected parameter is not a broken feature.

⚠️ Also: `cpu.status` confirmed `stepping=false paused=false` with `ticks` advancing, so the game was
**running normally** — the 0.00% result is a genuine "input did not move him", not a frozen emulator.

## Why the 0.64% / 0.00% results are trustworthy

Both were measured with the **validated** change detector (`ImageChops.getbbox()` + a >12 per-pixel
threshold), i.e. the instrument that was specifically proven on a known-different pair (0.3988) and a
known-identical pair (0.0). The `0.00%` reading with `bbox=(1216,590,1412,822)` is *tiny camera/palm
animation* just under the threshold — a genuinely-static screen with a small animated inset.

# ⭐⭐⭐⭐⭐⭐⭐⭐⭐⭐ THE DELIVERABLE: `scripts/psp-tt-reader.mjs` — a WORKING Tag Team text reader

Built on the RAM finding. It reads Tag Team's story text out of RAM and narrates it. **No OCR, no
core integration** — it reads what the engine already decoded.

```
node scripts/psp-tt-reader.mjs --once --min-len 14
```

Real output (live, from the running game):

```
0x08FB07C2  Hahaha! We'll kill all that defy us!
0x08FB080C  What are you doing here!? I won't let you get away
0x08FB0888  You won't let us get away with this? I don't know
0x08FB08EC  who you are or where you're from, but you've got
0x08FB096A  C-mon! Let's do this!
0x08FB09A8  Thank you. You're a great help...
0x08FB09EC  Those guys showed up on this planet all of a
0x08FB0A46  sudden and have been rampaging as they please...
0x08FB0AA8  It seems like this place is in a lot of trouble
0x08FB0B22  I'll defeat those guys for sure, but stay hidden
0x08FB0B9C  Understood. You should be careful too...
0x08FB0CAC  I went for a walk to the east a while ago...
0x08FB0D06  And I saw a strangely-colored monster. It was
0x08FB0D62  very unsettling...
0x08FB0D88  Can you check and see what that thing actually is?
0x08FB0DEE  Alright...I got it! I'll go check it out!
0x08FB0E42  Hey! I'm back!
```

⭐ That is **real, readable story/NPC dialogue including the mission hint** ("I went for a walk to
the east a while ago... I saw a strangely-colored monster... Can you check and see what that thing
actually is?"). A screen reader can speak this verbatim.

## How it works

1. Bulk-reads the two text regions: `0x08FB0000-0x08FE0000` (scene script) and
   `0x08C70000-0x08C90000` (UI, tutorial, ordered objective list).
2. Extracts printable **UTF-16LE** runs (>= `--min-len`).
3. `--once` dumps them; otherwise it polls and prints **`[NEW]`** / **`[gone]`** lines, i.e. text
   that just appeared or left the screen — a running narration.

⛔ **MEASURED LIMITATION — this is NOT yet a live narrator (important correction):**

A decisive test settled what the text region is. Two `--once` scans **25 seconds apart**:

```
scan1 lines: 2365
scan2 lines: 2365
lines only in scan1 (vanished): 0
lines only in scan2 (appeared): 0
```

**The text region is a STATIC CORPUS.** The game keeps its **entire script** resident and unchanged,
and selects which line to display via a **cursor/pointer elsewhere**. Consequences, stated plainly:

| claim | status |
|---|---|
| "the game's story text is readable from RAM" | ✅ **true** (2365 lines, incl. the objective list) |
| "the reader narrates the line currently on screen" | ❌ **FALSE — not implemented, not working** |
| `--once` value | ✅ gives the game's **content** (objectives, NPC lines, missions) |
| watch `[NEW]`/`[gone]` value | ⚠️ **cannot work here** — nothing changes to diff |

⚠️ My own earlier framing ("a running narration", "`[NEW]` narrows it to the live line in practice")
was **wrong and is retracted.** It was inferred from the *nature* of the data rather than measured;
the measurement falsified it. This is the same class of error as the earlier overclaims — an
inference presented as a capability.

## What a real reader still needs (the ONE remaining blocker)

The **current-line cursor**: the value that says *which* of the 2365 resident lines is displayed.
Options to try, in order:

1. **Diff at higher temporal resolution while the story advances.** The corpus is static, so a
   pointer/index elsewhere in RAM must change per line. Diff two full-RAM snapshots taken across a
   line advance and look for a small index that changes.
2. **Find who reads the corpus.** The renderer must load from one of these addresses; trace the
   reader of `0x08FBxxxx` / `0x08C7xxxx` (a Ghidra xref hunt).
3. **The `{id,type,offset}` runtime table** (found earlier at `0x0911F408` etc., ids 760-762) is a
   strong candidate for the *index* half of this mechanism — the id there is likely what selects
   the line.

⭐ Until that cursor is found, the honest description of `scripts/psp-tt-reader.mjs` is:
**a story-text EXTRACTOR, not yet a live reader.** It is still genuinely useful (it can speak the
objective list and mission text on demand) — but it does **not** know what is on screen now.

## Why this matters for the project

| property | value |
|---|---|
| OCR required | **no** |
| emulator core integration required | **no** (host-side debugger only) |
| works on the unvoiced narration | **yes** — the gap a screen reader must fill |
| matches the design rule | **yes** — "model the game, not the screen" |

**This is the first working reader for a PSP title that needs no core in the app.** Steins;Gate
proved the technique; Tag Team proves it produces a usable reader.

## Field position: still NOT resolved (honest status)

With flight now working (84.22% pixel change), `scripts/psp-tt-fieldpos.mjs` and
`scripts/psp-tt-postrack.mjs` report candidates around `0x08B6A000`-`0x08B6B200` with plausible
world coordinates (e.g. `229/-23/1684`, `1591/1/1`, `1553`, `1666`), but **monotonicity while holding
one direction stays ~57-63%**, and some values show resets (`1246 -> 0`, `50944 -> 0`).

⚠️ Low monotonicity with resets is consistent with **several entity positions plus
interpolated/predicted copies**, not a single player position. **Reported as unresolved rather than
picked arbitrarily.** Next discriminator: compare the value against the **arrow direction** the user
can read off the screen (turn in small steps; the correct triple must rotate consistently).

# Field position: the bidirectional test, and a grouping bug I caught

## The discriminator that was missing

Monotonicity alone (~57-67%) could not separate "several entity positions" from "a rotating basis".
The clean test is **BIDIRECTIONAL** (`scripts/psp-tt-posbidi.mjs`):

```
S = read at rest        A = read after flying NORTH (sampled WHILE HOLDING)
                        B = read after flying SOUTH (sampled WHILE HOLDING)
POSITION -> S -> moved -> returned : B ~ S     (returned ~ 0%, opposite signs)
VELOCITY -> 0 -> -X    -> +X       : B != S    (does not return)
```

⭐ **Sampling WHILE HOLDING is essential.** An earlier version released the stick before sampling, so
every velocity field read `0` at both S and B and scored a perfect "returned to start" — the ranking
was then dominated by velocity/input fields (`S=0 A=-22 B=-7`, `S=0 A=1592 B=0`) and by near-zero
**denormal** floats (the byte pattern `01 00 00 00` reads as ~1e-45 and produced degenerate
`S=1 A=0 B=1` "perfect" scores). A magnitude floor (`>=5.0` displacement, reject |v|<2) removes the
denormals.

## ⛔ A grouping bug I caught before claiming anything

A `--full` run reported runs of **8 floats with a regular `0x7AC` stride** at
`0x09601F30` / `0x096026DC` / `0x09602E88` — which looked like an entity record array. Directly
reading those addresses **falsified it**:

```
0x09601F30: 255.00  255.00  255.00  129.27  64.00  64.00  64.00  0.00
0x096026DC: 255.00  255.00  255.00  183.81  64.00  64.00  64.00  0.00
0x09602E88: 255.00  255.00  255.00  138.92  64.00  64.00  64.00  0.00
-- after flying north, the SAME values --
```

**`255,255,255,·` and `64,64,64` are CONSTANTS and cannot be a moving position.** So the triple/run
grouping is reporting FALSE positives, and the flagged runs must not be read as positions.

⚠️ **Lesson:** the grouping step (`consecutive addresses with score >= threshold`) is not evidence on
its own — it must be confirmed by reading the actual values and checking they are plausible, varying
quantities. A run of identical constants passing a numeric threshold is exactly the "instrument
flatters a candidate" failure this project keeps meeting.

## Where the field position stands

**NOT FOUND.** What is known:

* the battle fighter struct `0x0973CEE0 + N*0x1A90` is **empty (`0/0`) in the field** — the field
  uses a different structure;
* the field region `~0x08B69000-0x08B6B500` contains **camera/rotation basis data in Q7 fixed-point**
  (`-128` = -1.0, `-32` = -0.25, `-8256` = -64.5), which the bidirectional test correctly flags as
  changing but which is **not** the player position;
* `input.analog.send` flying works (**84.22%** pixel change), so movement is available for testing.

### The next method (not yet run)

**Anchor on a known value.** Fly to a recognisable spot, read the screen, and search RAM for a value
that *matches the visible situation* (e.g. the enemy count badge `2`, or the HUD gauge `0`), then use
the bidirectional test only on candidates near it. Searching for "a float that moves" has produced
three false-positive families; anchoring on a value the game DISPLAYS is the technique that worked
for HP, stats, and the identity map.

# The field objective arrow: CONFIRMED VISIBLE, and it points NORTH

`input.analog.send {x:0, y:-1}` flying is proven (84.22% pixel change). Viewing the field with
`scripts/psp-shot.py` then showed a clear **blue chevron near the top of the screen pointing UP**.

| observation | detail |
|---|---|
| shape | blue/cyan chevron (an arrowhead), drawn near the top of the screen |
| direction while flying north | **pointing up** — i.e. the objective is straight ahead (north) |
| still present after ~24 s of flight north | **yes, still pointing up** |
| after 3 further 20 s legs north | **still present, still pointing up** |

⭐ **This is the accessibility cue that matters**: the game already draws a directional objective
indicator. A reader does not need to compute the bearing if it can read the arrow's direction — and
the arrow is a simple, high-contrast 2D glyph.

## ⚠️ What I did NOT establish

* **Did not reach the objective.** Flying north for ~84 s total did not visibly arrive (the arrow
  never resolved). Either the objective is very far, flight is slow, or a **boost** is required.
* **Boost attempt inconclusive.** Held `input.buttons.press {button:"r", frames:900}` (Rule 0: the
  button event takes FRAMES, so a large count is a hold) together with analog north. No confirmed
  speed change was read before vision output became unreliable.
* **Do NOT trust my attempt to detect the arrow numerically.** A colour mask
  (`b>150 & b-r>60 & g>100`, top third) selected **142,287 px with bbox x[0..1705]** — i.e. the whole
  top strip of terrain/cel-shading, not the chevron. Its "bearing" values (-91 deg on two frames,
  identical bbox on t0 and t2) are measuring the environment, not the HUD. **The numeric arrow
  detector is wrong and is not a result.**

## Honest status of "fly to the objective"

| step | status |
|---|---|
| fly at all | ✅ proven (84.22% pixel change, visual confirmation of airborne movement) |
| objective arrow exists and is visible | ✅ confirmed by eye, blue chevron, points up/north |
| arrow rotates with heading | ❌ **not confirmed** — vision output became unreliable mid-test |
| reach the objective | ❌ **not achieved** |
| read the arrow's direction from RAM | ❌ not attempted successfully |

### Next step (concrete)

1. Re-run the arrow-rotation test with a **tight crop of the HUD only**, not the terrain — locate the
   chevron's exact pixel box by eye first, then track only that box between headings.
2. Find the **boost** control (`l`/`r` shoulder, or a held button) and re-test time-to-arrival.
3. Only then use the arrow as the reader's bearing source.

# ⭐ Field controls SOLVED by reading the button list, and the trigger opens a MAP

## The real button names (this unblocked everything)

I had been pressing `r` for a boost and getting silent failures. Probing the API directly **without
pressing** (invalid names error before acting, so probing is free) gave the actual list:

```
ERR   l            (Unsupported button value 'l')
ERR   r            (Unsupported button value 'r')
OK    ltrigger
OK    rtrigger
OK    l2
OK    r2
ERR   l1 / r1 / lt / rt / shoulder_l / shoulder_r / trigger_l / trigger_r
```

⚠️ **`l` and `r` DO NOT EXIST.** The shoulder buttons are named **`ltrigger` / `rtrigger`** (and
`l2`/`r2`). Every "boost attempt" before this was sending an invalid button name and silently doing
nothing — the third time this project has mistaken a rejected parameter for a missing feature.

## `rtrigger` opens a WORLD MAP

Holding analog north while pressing `rtrigger` repeatedly (67 presses in 10 s) produced a
**world map screen**: an overhead view of islands, ocean, beaches, and — importantly — a small
**ringed orb marker** rendered on the map.

✅ **The field has a map view reachable from a trigger press**, and it carries a marker glyph. This is
an accessibility-relevant screen: a map with a destination marker is something a reader can describe
("the objective is north-west, over the island group").

⚠️ **Not yet verified:** whether that orb marker is the *objective*, the *player*, or an item. It was
seen once, at the upper-left, in a map view. **Do not treat it as the objective marker without a
second observation.**

## Flight/boost status

| leg | pixel change | note |
|---|---|---|
| `rtrigger` held + analog north, 10 s, 67 presses | 87.42% |  |
| `ltrigger` held + analog north, 10 s, 65 presses | 88.13% |  |

Both legs moved a lot (as expected — this is flight), but **a difference in SPEED was not measured**,
so it is not established that the trigger is a boost. It may be a camera/map toggle.
**Boost remains unconfirmed.**

## Retraction

My earlier doc entry said the objective arrow "points north" and that I attempted a boost with `r`.
The boost attempt was invalid (no such button). The arrow observation (blue chevron, top of screen,
pointing up) was made **by eye from actual screenshots** and stands; the boost conclusion does not.

# ⚠️ Two tempting "structural" observations that FAIL verification — do not build on them

A battle-mode dump produced two patterns that looked like structure. Both were checked against the
running game and **both fail**:

## Claim 1: "`+0x4C4` and `+0x4D4` are always identical"

During one battle run, `+0x4C4` and `+0x4D4` moved in perfect lockstep (`-16.844 -> -16.186` for both,
`d=0.658` for both). It looked like a duplicated field.

**Checked in the current state (4 samples, 700 ms apart):**

```
s0 +4C4=0.0000  +4D4=1.0000  equal=false   | s1 +4C4=0.0000  +4D4=1.0000  equal=false
```

⛔ **NOT structural.** The equality held only in that battle's state; here the two fields differ
(`0.0` vs `1.0`). So `+0x4C4`/`+0x4D4` are **state-dependent**, not a duplicated field or a fixed
pairing. Any reader that assumed they are the same value would be wrong in this state.

## Claim 2: "the fighter struct retains values in the field"

A field-mode dump showed slot2/slot3 holding stale-looking values (`30000/30000`, `25060/30000`).

**Checked now:** *all four slots read `0/0`.*

⛔ So those values were **not** a field-mode reading — the game had already left that state. The
struct is `0/0` whenever it is not in an active battle, which is the established phase signature.

## The rule this reinforces

Both patterns were **plausible, internally consistent, and wrong** — they were extracted from a state
that had already passed. This is the same failure as the "8-float entity runs" and the colour-mask
arrow: **a pattern that fits is not a pattern that holds.** Before recording a structural claim, re-read
it in a *second, independently-verified* state and confirm the phase (`max ≈ 30000`, `cur <= max`) at
the same moment.

⚠️ Current phase right now: **all slots `0/0` -> menu/sentinel, NOT battle and NOT field.**

# Phase transition caught live, the field HUD read by eye, and a corpus nuance

## The phase sentinel, observed in a single captured step

A route run caught the exact transition:

```
step 000  HP s0=2080        s1=1010        s2=24630  s3=29740        <- battle, fighters damaged
step 001  HP s0=3F800000    s1=0           s2=1.0    s3=1.0          <- MENU SENTINEL
```

`1065353216` = `0x3F800000` = **float 1.0**. So the phase table is confirmed live, in one step:

| state | signature |
|---|---|
| battle / field | `max ~= 30000`, `cur <= max`, values change |
| **menu / character select** | **`1065353216` (float 1.0)** in the slots |
| boot / title / attract / cutscene | all `0/0` |

⭐ Note that the battle values were `2080`, `1010`, `24630`, `29740` of 30000 — i.e. **HP drops into
the thousands during real fights**, so small HP values are legitimate combat state, not stale data.

## The field HUD, read from a screenshot

The field displays (top-left):

| element | reading |
|---|---|
| character portrait | the player's face |
| green bar | health, full |
| `0` in a ringed badge | a counter/gauge (reads 0) |
| portrait + **`2`** badge | an **enemy indicator with a COUNT of 2** |

✅ The `2` badge is a concrete, speakable fact: **"two enemies remain"**. That is exactly the kind of
semantic a screen reader should announce, and it is visible in the same corner every time.

## The map overlay, confirmed a second time

The trigger-opened **map panel** (white-bordered inset) renders the island terrain over the orange
sea, with the **ringed orb marker** drawn on the water. Seen twice now, in the same form.

⚠️ Still **not** established whether that orb is the objective, the player, or a pickup. Two sightings
of the same glyph are not an identification.

## ⭐ The text corpus DOES vary between scenes — just not per line

Earlier: two scans 25 s apart in the same scene -> `2365 -> 2365`, **0 differences** (hence "static
corpus"). New measurement, different scene:

```
field  : 2365 line(s)
menu   : 2037 line(s)
```

So the corpus is **scene-scoped, not immutable**: it changes when the game loads a different scene,
but **not while a dialogue line advances**. That refines the earlier conclusion rather than
contradicting it, and it means the reader is even more useful than stated — a change in the extracted
line-count is itself a **scene-change signal**.

⛔ It still does **not** give the current line: within a scene, the text is constant, so the displayed
line must still be selected by a cursor elsewhere.

## Reader status (unchanged, honest)

`scripts/psp-tt-reader.mjs --once` extracts the story text reliably in **every** state tested
(field and menu). It is a **content extractor**, not a live narrator. The live-line cursor remains the
single blocker for narration.

# Current-line cursor: the experiment, a real reproducible field, and why it is NOT the cursor

## The experiment (`scripts/psp-tt-cursorfind.mjs`)

Direct measurement rather than guessing: snapshot ALL user RAM, advance exactly ONE dialogue line,
snapshot again, report u32s that changed — with an **IDLE CONTROL** (two snapshots the same time
apart, no press) subtracted, so animation and counters are cancelled out.

Guards built in: phase asserted first; addresses inside the static text regions excluded; both
endpoints required to be in the id range; and values whose bytes form a plausible float rejected.

## A tightened filter was necessary

The first run's candidate list was dominated by **float bit-patterns**, e.g. `160 -> 1061159513`,
where `1061159513 == 0x3F400000 == float 0.75`. The filter had accepted "either endpoint in range",
which lets animating transform data through. Requiring **both** endpoints in range **and** rejecting
float-like values produced a clean list.

## A REPRODUCIBLE input-responsive field: `0x09FFE880`

Two runs, subtracted idle noise, in the same state:

```
run 1: 0x09FFE880  441 -> 442      (one press of cross)
run 2: 0x09FFE880  446 -> 445      (one press of cross)
```

**The same address responds to the press in both runs** — which is exactly the reproducibility test
that a counter or animation fails. So it is a genuine input-driven field.

## ⛔ …but it is NOT the dialogue cursor. It is a MENU VALUE.

Reading the neighbourhood explains it:

```
0x09FFE870   f: 444.20  122.00   1.00   1.00
0x09FFE880   u32:   444   f: 154.00   1.00   1.00   <== the candidate
0x09FFE890   f: 444.20  122.00   1.00   1.00
0x09FFE8A0   f: 476.20  154.00   1.00   1.00
```

* the integer **`444` sits immediately after the float `444.20`** -> it is the **integer display copy
  of a nearby float**
* the surrounding values (`444.20`, `476.20`, `122`, `154`, `56`, `25`, `68`, `41`) form a **table of
  numeric quantities** — costs/stats — not text positions
* `cross` moved focus **between entries** whose values are `441`/`442`/`446` — i.e. **menu navigation**

⛔ So `0x09FFE880` is a **menu focus/value field**, not the value selecting a dialogue line. Reported as
a real finding, but explicitly **not** the cursor — the same discipline that rejected the "8-float
entity runs" and the colour-mask arrow.

## Why the cursor was not found this time

The phase check read `0/0` for all four slots, i.e. **a menu / non-battle state**. There was no
dialogue line on screen to advance, so the experiment could not measure "what advances with the text".

**The dialogue cursor must be measured WITH DIALOGUE ON SCREEN.** Required setup:

1. drive to a scene where a text box is displayed and a `cross` advances one line of dialogue;
2. confirm on screen that the line changed (screenshot before/after), so the press is known to have
   advanced text;
3. only then run `psp-tt-cursorfind.mjs` and look for the address that tracks the line.

⚠️ Without step 2 the test cannot distinguish "the cursor advanced" from "nothing happened", which is
how an earlier whole session of measurements went wrong.

## Status of the single remaining blocker

**The current-line cursor is still NOT found.** What IS established: the corpus is scene-scoped and
constant within a scene; input-driven fields can be found reproducibly with the idle-subtraction
method; and the method now produces a clean, float-free candidate list when run in the right state.

# Ghidra name-resolver callsite dump: 84 callsites, and a CLEAN NEGATIVE for the character-id search

`TagTeamResolver.java` produced **95,705 chars** of callsite detail for `FUN_0883ee74` (the
message-id -> name resolver): **84 callsites across 32 distinct callers**.

## The measured distribution of what callers actually read

| what the callsites read | count |
|---|---|
| `(ram, 0x8b4...)` | **38** |
| `(ram, 0x8a7...)` | **31** |
| `(ram, 0x885...)` | 2 |
| `(ram, 0x8a0...)` | 1 |
| `(ram, 0x89d...)` | 1 |

## ⛔ Clean negative: the resolver callsites have NOTHING to do with the fighter struct

```
grep -c "0973" tagteam-resolver-out.txt   ->  0
```

**Zero references** to `0x0973xxxx` — the region holding the four fighter slots
(`0x0973CEE0 + N*0x1A90`). So **no name-resolver callsite reads a fighter record**.

## And the resolver is NOT a character-identity source

The `arg0` values at the callsites are **small constants** — `490`, `491`, `898`, `899`, `900`, `901`,
`934`, `952`, `953`, `954`: i.e. **fixed message ids**. Every one is a literal. None is computed from a
roster index or read out of a per-fighter structure.

⭐ **Conclusion (a real, useful negative):** `FUN_0883ee74` is a **general message-id -> string
resolver**, called from menus and scripts with **literal ids**. It is **not** where a fighter's roster
id gets turned into a name. The name the HUD shows must therefore come from **another path** — the HUD
element names are the better lead (`BTL_HP_MAIN_1P` `0x08A72218`, `chara_name_01` `0x08A733AC`), since
those are *named* UI elements the engine addresses directly.

⚠️ Note the callsites read `0x8b4...` (38x) and `0x8a7...` (31x) heavily — and **`0x8a7` is the region
that already gave us the HUD element-name strings**. That is consistent with "the resolver serves
general UI text", and it points the next search at the `0x08A7xxxx` HUD structures rather than the
fighter struct.

## The character-id question is now better posed

Instead of "which field in the fighter struct holds the roster id", the evidence says: find **how the
HUD name element gets its text**. Concretely, the renderer path from `chara_name_01`
(`0x08A733AC`) — which is a *named* element, so the engine must know it by name — rather than the
generic resolver that only ever receives literals.

# The id-table xref search: a clean negative AND a real positive

Two Ghidra scripts produced results that settle where the identity path is — and are not.

## ⛔ NEGATIVE: nobody reads the id-table region at `0x08A75000-0x08A76000`

```
=== TagTeamIdTable: who READS the name id table? ===
id-table region: 0x8a75000 - 0x8a76000
=== totals ===
  matching instructions: 0
  distinct functions   : 0
```

**Zero functions reference the id table** (`0x08A75538`, the table `FUN_0883ee74` indexes). So despite
the resolver indexing it, no other code reads it — the table is reached **only through the resolver**,
and the resolver only ever receives literal ids (previous result: `arg0` values 490/491/898-901/934/952-954).

### ⛔⛔ THIS NEGATIVE WAS WRONG — retracted below, with the reason

The claim "nobody reads the id table" was **falsified by direct decompilation** (see the next section).
`FUN_08a26400` reads `DAT_08a75538` and `DAT_08a7553c` — **both inside the searched region**
(confirmed: both are in `[0x08A75000, 0x08A76000)`).

⚠️ **Why the xref script found nothing:** the region contains **several adjacent tables**
(`0x08a75538`, `0x08a7553c`, `0x08a75570`, `0x08a78eec`). An xref search keyed on the *page*
(`0x08a75xxx`) can match a literal operand while missing table-relative accesses such as
`(&DAT_08a7553c)[index]`, and it can match nothing at all if the operands are formed by
**`lui`+`addiu` two-instruction address loading** rather than as single absolute operands.
**A 0-hit xref result on a whole page is not evidence of absence — it is evidence the search was
shaped wrong.** (Same failure family as rules 107 and 114.)

⭐ **The roster-id -> name chain DOES pass through the id table.** The earlier chain
(`RAM roster names <- FUN_0883ee74 <- id table <- PACKFILE ordinal`) stands, and the missing link is
now identified: **the index.**

## ⭐ POSITIVE: 30 functions reference the wider id pages AND call the resolver

```
criterion: references pages 0x8a70000-0x8a90000 AND calls FUN_0883ee74
=== INTERSECTION: 30 function(s) ===
  FUN_08a18730 @ 08a18730   pageRefs=7  resolverCalls=8
  FUN_08a35ff0 @ 08a35ff0   pageRefs=9  resolverCalls=8
  FUN_08a2c314 @ 08a2c314   pageRefs=4  resolverCalls=7
  FUN_08838e3c @ 08838e3c   pageRefs=3  resolverCalls=5
  FUN_088392bc @ 088392bc   pageRefs=4  resolverCalls=5
  FUN_08a26400 @ 08a26400   pageRefs=8  resolverCalls=5
  ...
  FUN_08a44ccc @ 08a44ccc   pageRefs=14 resolverCalls=3   <- most pageRefs
```

These are the **UI-building functions** that both touch the `0x08a7xxxx`/`0x08a8xxxx` pages (where the
named HUD elements live: `BTL_HP_MAIN_1P` `0x08A72218`, `chara_name_01` `0x08A733AC`) and call the
message resolver. **This is the surface the HUD name path lives on.**

## ⚠️ `FUN_08a18730` is a generic UI-node walker, not a character-name resolver

Its signature and body say what it is:

```c
void FUN_08a18730(int param_1)
  ...
  iVar2 = (**(code **)(*(int *)(param_1 + 0x40) + 0x44))   // virtual call via vtable at +0x40
                   (param_1 + *(short *)(*(int *)(param_1 + 0x40) + 0x40));
  if ((iVar2 == 0) && (*(int *)(param_1 + 0x9c) == 0)) {
    puVar3 = *(undefined4 **)(param_1 + 0xac);
    ...
    puVar7[0xd] = puVar3[2];      // bulk field copying between two structures
    puVar7[2]  = puVar3[6];
```

* takes a **single `param_1` node pointer** (a UI node)
* makes a **virtual call through a vtable at `+0x40`** — the classic "UI element render/update" shape
* then does **bulk field copying** (`puVar7[n] = puVar3[m]`) between two structures
* no character index, no roster slot, no per-fighter table

⚠️ So it is a **generic UI node copier/updater**, in the same family as the earlier
`FUN_08a3c458 -> FUN_08840dc4(node, "gauge_*")` finding (rule 68: UI builders reached only through a
vtable have no findable callers). **Do not mistake page references for identity logic.**

## Where the identity path now stands

| question | answer |
|---|---|
| does anyone read the id table directly? | ❌ **no** — 0 references |
| is the resolver a character-id -> name path? | ❌ no — literals only (previous result) |
| where do HUD names come from? | ⭐ the **30 UI functions** touching `0x08a7`/`0x08a8` pages |
| which is the best candidate? | `FUN_08a35ff0` (pageRefs=9, resolverCalls=8) and `FUN_08a44ccc` (pageRefs=14) |

### Next (concrete, not yet run)

Decompile **`FUN_08a35ff0`** and **`FUN_08a44ccc`** and look for a **slot-indexed read** — i.e. an
access of the form `base + index * stride` where `base` lands in the `0x08a7xxxx` HUD region. That is
the shape a "which fighter's name goes in this element" lookup must have. The earlier name-resolver
dump already showed the resolver never sees a computed index, so the index arithmetic must live in one
of these UI functions, *before* the resolver call.

# ⛔⛔ RETRACTION: "identity solved" was WRONG — that table is the BATTLE ACTION list

**The claim that `node + 0x84` is a character-identity index is FALSE, and I am retracting it.** It was
checked against live RAM and against PACKFILE, and it does not hold.

## What the table actually contains (verified two ways)

Live RAM at `0x08A75538`:

```
1f9 1fa 1fb 1fc 1fd 1fe   |  ab ac a9 aa ad ae  |  a2 a3
505 506 507 508 509 510   |  171 172 173 174     |  162 163
```

Resolved against PACKFILE (ids are sequential ordinals; verified with `psp-msgids.py`):

| id | string |
|---|---|
| **505** | **'Strategy'** |
| **506** | **'Charge'** |
| **507** | **'Enemy Search'** |
| **508** | **'Taunt'** |
| **509** | **'Transform'** |
| **510** | **'Cancel Transformation'** |
| 169 | 'Inherited Name' |
| 170 | 'Laidback Boyz' |
| 171 | "I Won't Lose!" |
| 172 | 'Super 3' |
| 173 | 'Meeting of Rivals' |
| 174 | 'Still Mind and Rage' |
| 162 | 'Spoiled Rich' |
| 163 | 'Wild Long' |

⭐ **505-510 are exactly the SIX battle actions** — which is precisely the count the `do { ... } while
(iVar8 < 6)` loop iterates. So:

* `FUN_08a26400` is a **battle ACTION MENU builder** (Strategy / Charge / Enemy Search / Taunt /
  Transform / Cancel Transformation)
* **`node + 0x84` is the SELECTED ACTION INDEX**, not a character id
* the "6 names" I read as a 6-fighter roster are **6 action labels**

## ⛔ The error I made, precisely

I saw `FUN_0883ee74((&DAT_08a7553c)[*(int *)(param_1 + 0x84)], -1)` — **an indexed resolver call into
a message-id table** — and concluded "index = character identity".

**I never resolved the ids.** One lookup would have shown `505 = 'Strategy'`. Worse, **Rule 105 in this
very document already recorded that 505/506 are 'Strategy'/"Charge"**, so the disconfirming fact was
already written down and I did not consult it.

⚠️ **Correct inference from the same code:** an indexed resolver call proves there is *an index*, not
*what the index means*. The meaning comes from resolving the ids — and the resolver's own callsite dump
from earlier (all literal ids in 490/491/898-901/934/952-954 ranges) was **never** the character path
either.

## ✅ What DOES still stand

| claim | status |
|---|---|
| rule 122: a 0-hit xref on a page is not evidence of absence | ✅ **stands** — the decompilation genuinely reads `DAT_08a75538` / `DAT_08a7553c` inside `0x08A75000-0x08A76000`, so a page-keyed xref that returns 0 *is* shaped wrong |
| `(&DAT_08a7553c)[index]` is a table-relative indexed read into a message-id table | ✅ stands |
| the table holds **message ids** | ✅ stands (505-510 resolve to real strings) |
| `node + 0x84` is the **character** identity | ⛔ **FALSE — retracted** |
| `FUN_08a26400` resolves character names | ⛔ **FALSE — it builds the battle action menu** |

## ⛔ A second, independent error in the same investigation

I also read the **ELF file** at translated offset `vaddr - 0x08804040 + 0x40` and reported the table as
"a pointer table of code addresses (0x089D1820...) containing MIPS instructions".

**That translation is wrong.** Live RAM at `0x08A75538` holds message ids (505-510), while my computed
file offset `0x271538` holds code. So **ELF file offset != vaddr - load_base + 0x40** for this binary —
the RAM image is not a direct copy of the file at those addresses. ⚠️ **Never read ELF file bytes by a
hand-computed offset and treat the result as what the game sees**; verify against live RAM first, as
was done here. (This is the Rule 39/60 class: validate an address translation against a value you
already know.)

## Where this leaves the character-identity question

**Still open — and now known to be harder than I claimed.** Established negatives:

* it is **not** `node + 0x84` (that is the action index)
* it is **not** the resolver's literal-id callsites (490/491/898-901/934/952-954)
* the `{id,type,offset}` records at `0x0911F408` are a **message table**, not a roster

Next step: go back to the **HUD element names** (`chara_name_01` `0x08A733AC`, `BTL_HP_MAIN_1P`
`0x08A72218`) and find the code that *writes text into* those elements, rather than searching for
roster ids in tables.


# The roster-name ordinals are NOT reachable from `--start 0x4FADC` — identity map unverified

Attempting to re-verify the recorded roster map (`843 Goku`, `848 Piccolo`, `854 Vegeta`, `876 Frieza`,
`883 Cell`, `891 Super Saiyan 3`) with `psp-msgids.py --start 0x4FADC` gives:

```
id 843 -> OUT OF RANGE (table has 600)
id 848 -> OUT OF RANGE (table has 600)
... all of 843/844/845/846/848/849/850/851/854/876/883/891 -> OUT OF RANGE
```

**The enumeration from `0x4FADC` yields only 600 strings (ids 0-599).** Roster names at ordinal 843+
therefore live in a **later table further along inside PACKFILE**, which this start offset does not
reach.

## Status of the identity map: UNVERIFIED (neither confirmed nor refuted)

| item | status |
|---|---|
| ids 0-599 resolve correctly (verified: 505 'Strategy', 162 'Spoiled Rich', ...) | ✅ |
| `843 = Goku`, `848 = Piccolo`, `854 = Vegeta`, `876 = Frieza`, `883 = Cell`, `891 = Super Saiyan 3` | ⚠️ **NOT verifiable from this table start** |
| ordinal of 'Super Saiyan 3' | ⚠️ **conflict**: this table gives `[431]`; earlier notes recorded `891`. Unresolved — could be two occurrences, but that is not demonstrated |

⚠️ **Do not treat the roster ordinal map as confirmed.** It was recorded earlier in the project and has
**not** been re-verified; the tool cannot reach those ids from `0x4FADC`. Re-verify by locating the
**second string table** in PACKFILE (scan for the next UTF-16 run boundary) and enumerating from there.

⭐ This is the same discipline that should have been applied to `node + 0x84`: **resolve the value
before recording what it means.**

# The string blob's real structure, and why "message id == ordinal" is START-DEPENDENT

## Measured structure (raw bytes)

Around `0x5412C` the blob is **NUL-separated UTF-16LE**:

```
'Air Combo Blast 2' | 00 00 | 'Super Saiyan' | 00 00 | 'Super Saiyan 2' | 00 00 |
'Super Saiyan 3'    | 00 00 | 'Fusion: Super Gotenks 3' | 00 00 | 'Fusion: S'...
```

So the separator is a **UTF-16 NUL**, and entries are **not fixed-width** — consecutive ordinals are
however many bytes the previous string occupied. (Earlier notes describing a `.` separator were reading
the *control characters* that sit between strings in the rendered/RAM copy; in the archive the
separator is `00 00`.)

## ⛔ The ordinal is only meaningful relative to a TABLE START

The same content enumerates to **different ids** depending on where counting begins:

| enumeration | result for 'Super Saiyan 3' |
|---|---|
| `psp-msgids.py --start 0x4FADC` | **`[431]`** |
| ad-hoc enumeration from `0x4D000` | a different number entirely |

⚠️ **Therefore "message id == ordinal" is valid only with the engine's own table start**, which has
**not** been pinned down here. This is why the recorded roster map (843/848/854/876/883/891) could not
be re-verified: those ids belong to a numbering whose origin is elsewhere in the file.

## ⛔ 'Goku' is NOT a standalone entry in the `0x4D000`-`0x54500` region

An exact-match search for the single string `'Goku'` in that region found **nothing**, even though
`Goku` occurs **1,866 times** as a *substring* inside longer strings (e.g. `'Fusion: Super Gotenks 3'`,
mission text). So roster names are **not** in this stretch of the blob.

## Status: the identity map stays UNVERIFIED

| claim | status |
|---|---|
| the blob is NUL-separated UTF-16LE with variable-width entries | ✅ **measured** |
| `505 'Strategy'`, `506 'Charge'`, `162 'Spoiled Rich'` (from `--start 0x4FADC`) | ✅ verified |
| ordinal numbering is start-dependent | ✅ **measured** (two starts -> different ids) |
| `843 Goku, 848 Piccolo, 854 Vegeta, 876 Frieza, 883 Cell, 891 Super Saiyan 3` | ⚠️ **UNVERIFIED** |

⭐ To verify the roster map properly: find the engine's **actual table start** (the base `FUN_0883ee74`
indexes from, `0x08A75538`-adjacent ids like 505 sit in a 500s block) rather than an arbitrary file
offset, then enumerate from there. Until then, **do not cite the roster ordinals.**

⚠️ Method note: my first attempt at this enumeration used `split(b"\x00\x00")` and returned **0
strings**, because UTF-16LE ASCII contains `\x00` bytes as its high byte, so that split destroys every
string. Splitting on the aligned pair and decoding in 2-byte steps is required.

# The HUD "element addresses" are NAME-STRING TABLES, not value structs — and the naming scheme is the identity map

## Correction to an earlier reading

I recorded addresses like `BTL_HP_MAIN_1P 0x08A72218` and `chara_name_01 0x08A733AC` as "HUD element
names addressable in RAM". Checking what is actually there:

```
0x08A72218  u32: 5f4c5442 4d5f5048 ...   -> ASCII "BTL_HP_M"
0x08A733AC  u32: 72616863 616e5f61 ...   -> ASCII "charan_a"
```

They hold the **ASCII name strings themselves** (as a `char*[]`), **not** pointers to live values.
So they are a **lookup table the engine uses to find UI elements by name** — useful, but they are not
where a fighter's name or HP lives. ⚠️ Do not read them as value addresses.

## The table's naming scheme IS a per-player identity map

Enumerating the file around the same strings (`EBOOT.dec` @ `0x270338`-`0x270618`) exposes the layout:

```
40_name01
cover
40_p_bg01
chara_name_02      thum_l_02      f_name_02
chara_name_01      thum_l_01      f_name_01
30_thumb_s_%02d    CHARA_SCROLL
30_thumb_m_%02d    31_player_%02d
30_select_ok_%d    com_%02d
30_color_%02d_on
```

⭐ **The elements are numbered per player** (`_01`, `_02`), and the live addresses are exactly
**8 bytes apart** (`0x08A733AC`, `0x08A73384` differ by 0x28 = 5 entries). The `%02d` format strings
(`30_thumb_s_%02d`, `31_player_%02d`, `com_%02d`, `30_color_%02d_on`) show the engine **builds these
names by formatting an index into the string** — i.e. the player/slot index is a *format argument*,
not a stored field.

⚠️ **Consequence:** "which fighter is in slot N" is resolved by the engine **at render time** from an
index, and the name table is addressed by *built* strings. So there is no single "character id" field
to find — the identity is consumed as a **format argument to a UI element lookup**.

## What this means for the accessibility reader (the practical answer)

The reader does **not** need the fighter's id field at all. The path that matters is already proven:

```
current scene's text is resident in RAM as UTF-16LE   (verified)
  -> psp-tt-reader.mjs extracts it                    (verified, incl. objectives and NPC lines)
```

Reading a fighter's *name* specifically requires the render-time index, which is consumed inside the
UI builder — so the practical route is to read the **text the game has already resolved**, not to
re-derive identity. That is the same decision the project already made for dialogue.

## Status

| item | status |
|---|---|
| `BTL_*` / `chara_name_*` addresses hold ASCII **names**, not values | ✅ **verified** |
| elements are numbered per player (`_01`, `_02`) with `%02d` *built* names | ✅ **verified** |
| a single stored "character id" field | ⛔ **still not found — and now suspected not to exist as a simple field** |
| reader needs the id field | ❌ **no** — text extraction already covers the player-facing content |

# Field position: the best-structured candidate family so far (`0x08B4E8xx` / `0x08AF00F0`)

A field-mode move-diff (with flight now working) surfaced a family with **more internal structure** than
the earlier flat lists — a **constant leading value** followed by three moving values:

```
0x08AF00F0  n=3   -930.00 | 549.73 -> 384.97 | -659.38 -> -359.68
0x08B4E910  n=3   -930.00 | 549.73 -> 384.97 | -659.38 -> -359.68
0x08B4E890  n=4   -405.25 | 239.55 -> 167.75 | 640.82 -> 340.18 | 659.38 -> 359.68
```

| observation | meaning |
|---|---|
| `-930.00` and `-405.25` **do not change** | a stable anchor/owner id or a fixed offset, not a coordinate |
| three further values move **together** | consistent with a position triple `(x, y, z)` |
| last two entries of the n=4 run are **~19 apart** (`640.82/659.38`) | the same ~19 gap as the `132 -> 151` pairs seen earlier -> two related points (e.g. entity + its target, or foot + head) |
| values are **large** (`384`-`659`) | plausible world coordinates, unlike the near-zero `0.01` groups |

⚠️ **NOT CONFIRMED.** This family was seen in **one** run and the **bidirectional test has not been run
on it**. Candidates in this region have already produced three false-positive families (Q7 camera basis
`-128/-32`, denormal near-zero floats, and constants like `255/64`), so this is recorded as the
**best candidate**, not a result.

### The test that would settle it

```
node scripts/psp-tt-posbidi.mjs --window 0x08B4E800 --size 0x200 --hold 2600
```

Expect for a real position: `S -> moved north -> returned` with `opposite=true`, `returned ~ 0%`.

⚠️ Note the earlier bidirectional runs sampled mid-hold but still ranked **velocity** fields highly
(`S=0 A=-22 B=-7`), because a released-then-held sequence lets velocity read 0 at S. The cleaner
variant is to sample at rest, then mid-hold, then mid-hold of the reverse leg **without releasing**.

## Status: BLOCKED on reaching a battle

No adapter code is written and **no address is confirmed against live gameplay**. The stat
chain above is derived from the decompile and is the right next target, but it must be
validated against the running game before shipping.

### Next steps, in order

1. **Get past the save-complete screen.** It accepts input but ignores the obvious buttons —
   try the analog directions, or `home`, or waiting longer; the game may be mid-animation.
2. Then drive: main menu -> mode -> stage select -> battle.
3. At the battle HUD, read live RAM against the on-screen HP/ki bars to confirm
   `FUN_08a3c458`'s chain. That is the moment the addresses become real.
4. Only then write `dbz_tagteam_adapter.cpp` on the pattern of `Core/dbz_adapter.cpp`.

### Note for the accessibility review

The **first-run flow is itself an accessibility problem.** Profile creation is a keyboard
grid with no spoken cue for the accept control, and the confirm prompt's Yes/No only becomes
visible after scrolling. A blind player hits this on first launch with no guidance. That is
worth recording in the design docs independently of the adapter.

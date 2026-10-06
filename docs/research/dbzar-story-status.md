# DBZ Another Road — story mode: FINAL STATE (parked here deliberately)

This is the one-page handoff for the Another Road (story mode) work. It states what is **solved**,
what is **open**, and exactly where to start if it is picked up again. Read this first; the
per-topic documents are linked at the bottom.

## Solved — verified against the running game

### The message text is readable (MSG-ID → text)

`FUN_000da040` is the loader, and its decompile **is** the format spec:

```
byte 0     '#' = not loaded, '!' = loaded      (measured: '#' on disk, '!' in RAM)
+0x01      "MSG"
+0x12 u16  COUNT
+0x14 u32  RELATIVE offset -> array[COUNT] of u32, relative to base  = message NAMES (ASCII)
+0x18 u32  RELATIVE offset -> array[COUNT] of u32, relative to base  = message TEXT (UTF-16LE)
```

`text(id) = base + rel( array_at(base+0x18)[id] )`.

⛔ **The text is UTF-16LE.** A plain NUL scan truncates every string to one character.

**Resolved: 7,485 messages** — 3 containers in `EBOOT.dec` (160) and **287 in `data_sys_us.afs`
(7,425)**. English, original line breaks.

Id structure:

| pattern | meaning |
|---|---|
| `MSG_AR_<chapter>_<scene>_<line>` | story cutscene line |
| `MSG_AR_CLEAR_<nn>` | the 24 chapter clear conditions ("Defeat Dabura!", "Gather 7 Dragon Balls…") |
| `MSG_AR_CITY_<nn>` | the 24 city names (East Village, Dende's Village, …) |
| `MSG_AR_FIELDPLAY_<nnn>` | in-level strings (Senzu prompts, Dragon Ball finds, "Clear Condition") |
| `MSG_AR_AFBT_<n>` | 528 battle quips |
| `MSG_MN_ZT_CH_<n>` | Z Trial conditions |
| `MSG_DR_<nnn>_<A\|B>_<nnn>` | the drama script — **still JAPANESE in the US build** |

### Story-mode state

| what | ELF vaddr | RAM | note |
|---|---|---|---|
| Another Road mode flag | `0x1B11D4` | **`0x89B51D4`** | measured **0 → 1** on entering |
| chapter index | `0x1B11D5` | `0x89B51D5` | does **not** move when browsing |
| chapter table | `0x1B11D8` | `0x89B51D8` | 7 entries `0x601C5, 0x601C5, 0x601C3, 0x601C4, 0x601C8, 0x601C6, 0x601C7`, then 0 |
| chapter records | `DAT_001b0b18` | — | 320-byte (`0x140`) records, via `FUN_00031484` |
| pointer to the active story container | — | **`0x8AB7C0C`** | equals the container base; advanced `0x70` on a scene change |

The whole `[AR]` state machine is decompiled and tabulated (FIELD EVENT, BATTLE MODE, BTL START,
GAMECLEAR, GAMEOVER, BACK TO MENU, FIELD RESULT, CITY UPDATE/DRAW, PAUSE, ESCP MSG). AR state
struct base = `0x6b90` (`FUN_000213c4` returns it; 33 callers).

### The story reader

`psp-ar-story-reader.mjs` — reads the live story text off the running game. Modes: `--probe`,
`--list`, `--speak`, `--json`, and a follow mode.

**Verified against the screen four times:** on-screen "Goku suffered and died of a hea…" ↔
`MSG_AR_000_00_001`; "It is a world overcome with despair…" ↔ `_002`; "This world's lone surviving
war…" ↔ `_003`; "In the ensuing battle with the androids…" ↔ `_004`.

**Mechanism:** story containers load **on demand** — at the menu only the 3 system containers are
resident; a cutscene loads an extra `#MSG` container from `data_sys_us.afs`. The reader finds it and
parses it directly.

## Open — and this is where it was parked

### 1. "Which line is showing right now" (auto-tracking)

Not solved. The reader reports **which scene's lines are loaded and their full text**, not "line 4
of 7". Everything tested came back negative:

| test | result |
|---|---|
| any RAM word equal to a line's TEXT pointer | none outside the container's own arrays |
| any RAM word equal to a NAME pointer | none |
| any RAM word equal to an **array-entry** address (`p14 + k*4` / `p18 + k*4`) | only the container's own header words; nothing advances |
| small counter beside the container pointer (`0x8AB7C10`) | no change over 8 guarded advances → **dead** |
| hooking `FUN_000d9bdc` (the `text[id]` lookup) | logging execution breakpoint armed, **never fired** |

**Why the hook target was wrong:** `FUN_000d9bdc`'s only callers are `FUN_000cf9d8` and
`FUN_000a5860`, and the latter is the **font/texture preload** (`[FIX] WAIT FONT %dP`). It is the
font-loading lookup, not the line path.

**Reasoned conclusion:** the narration is driven by the game's **script VM** (`ev_*.spx` scripts
paired with `MSG_AR_*.msg`, per the 22 `#MG` configs). An interpreter holds a **bytecode PC**, not an
index into a message list — which explains every negative at once.

**Where to start next, cheapest first:**

1. ⭐ **A READ data breakpoint on the container's text-pointer array** (`container + 0x18`). Data
   breakpoints demonstrably work (the data-vs-execution distinction is measured and recorded) and
   they fire on the *fetch* that matters, with no CPU control needed. The reader code for finding
   the live container and its `+0x18` array already exists in `psp-ar-story-reader.mjs`.
2. Then the script VM: find the VM state (script pointer + PC).

⛔ Do **not** hook `FUN_000d9bdc`. Do **not** chase `0x8AB7C10`.

### 2. Chapter Select's browse cursor

**Answered structurally, not as an address.** `FUN_00008590` is a **map/graph renderer**: it walks
32 ten-byte entries from the 320-byte chapter record, each holding a grid position in nibbles (high
= X, low = Y) plus up to 3 links to other nodes, drawing links when a node is inside the camera
window (`DAT_001ac2a1`/`DAT_001ac2a2`). So Chapter Select is a **map, not a list** — which is why
every list-shaped hunt returned nothing. A map has no linear cursor to find.

A story reader does not need it: entering a chapter sets `DAT_001b11d5`, which is readable.

## ⛔ The recurring trap on this game (now written into the skill)

**A RAM diff is worthless unless the screen is first CONFIRMED SETTLED.** Cutscene text animates
continuously and scenes transition; three separate diffs were saturated this way (50,301 / 58,433 /
101,917 words changed, when a cursor move is a handful). Verify two screenshots are byte-identical
before trusting any diff, and advance exactly one line between dumps.

## Project conventions worth keeping

- `RAM = 0x08804000 + ELF vaddr` for **this build** (derived 5 ways). Derive per build — do not inherit.
- ⛔ The ELF has a uniform **`+0x74` file-offset ↔ vaddr skew**. Searching the file by vaddr finds
  nothing; add `0x74`.
- ⛔ `iRam`/`cRam` addresses from Ghidra are **not** trustworthy here (`FUN_000213c4` "returns
  0x6b90"; `cRam00099dac` lands in `.text`). Trust only `.data` addresses a live probe confirms.
- `FUN_00008590`'s chapter records are **graph nodes**, not a list (above).

## Topic documents

| document | contents |
|---|---|
| `dbzar-msg-resolver.md` | the `#MSG` format, the resolver, all 7,485 messages |
| `dbzar-story-reader.md` | the working reader + how it was verified |
| `dbzar-vm-hook.md` | **the PPSSPP breakpoint/register/stepping API map** (fully measured) |
| `dbzar-line-index-positive…` → `dbzar-line-index-negative.md` | the `0x8AB7C10` dead end |
| `dbzar-chapter-select-map.md` | Chapter Select is a map |
| `dbzar-story-mode.md` | the `[AR]` state machine and chapter data |
| `dbzar-charsel-reader.md` | the character-select reader (16 selectable / 8 locked) |

## Tooling (repo `scripts/` and `scripts/dbzar/`)

`psp-ar-story-reader.mjs` · `msg_resolve_elf.py` · `msg_resolve_afs.py` · `psp-ar-containers.mjs` ·
`psp-ar-currentline.mjs` · `psp-ar-arrayptr.mjs` · `psp-ar-loghook.mjs` · `psp-ar-vmhook*.mjs` ·
`psp-ar-currentmsg.mjs` · `psp-ar-counter.mjs` · `psp-ar-story.mjs` · `pps-ar-anchor…` · plus the
Ghidra scripts `DisStory*.java`, `DisText.java`, `DisMsgApi.java`, `DisLookup.java`, `DisChSel.java`.

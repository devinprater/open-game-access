# DBZ Another Road — STORY READER (working) + what is still unpinned

## What works, verified live

`psp-ar-story-reader.mjs` reads the **story text off the running game**, with no external lookup
file and no guessing. It was verified against the screen repeatedly.

**The mechanism — story containers load on demand.** At the menu only the 3 EBOOT system
containers are in RAM. When a cutscene plays, the game loads an extra `#MSG` container from
`data_sys_us.afs` whose message names are `MSG_AR_<chapter>_<scene>_<line>`. Measured live:

| step | screen actually showed | container `0x8ba1b10` (7 lines) |
|---|---|---|
| 4 | "Goku suffered and died of a hea… Earth's strongest defende…" | `MSG_AR_000_00_001` ✅ |
| 5 | "It is a world overcome with despair…" | `MSG_AR_000_00_002` ✅ |
| 6 | "This world's lone surviving war… returned to his past" | `MSG_AR_000_00_003` ✅ |
| later | "In the ensuing battle with the androids and C…" | `MSG_AR_000_00_004` ✅ |

Those are four independent matches between the on-screen line and the container entry at the
same index. The container's name list therefore identifies the *scene's* message set, and the
entries correspond one-to-one with the lines the game plays, in order.

### Verified container layout (live)

The game's own loader `FUN_000da040` fixes the container up in place, so at runtime:

```
byte 0     '!'  = loaded           (measured: '#' in the file, '!' in RAM)
+0x12 u16  COUNT
+0x14 ptr  -> array of pointers to message NAMES (ASCII)
+0x18 ptr  -> array of pointers to message TEXT  (UTF-16LE)
```

Measured on the live story container `0x8BA1F20`: count 15, names at `+0x1C`, texts at `+0x58`,
and every text pointer inside it decodes to a real story line. The reader parses this directly.

### The active-container pointer — pinned

| what | address | evidence |
|---|---|---|
| pointer to the **active** story container | `0x8AB7C0C` | it equals the current container base, and its value **advanced by 0x70** when the scene changed (146414352 → 146415392) |

Its neighbour `0x8AB7C10` went **32768 → 32769 (+1)** on one advance — a promising candidate for a
line counter but **observed only once, so not claimed**.

## Modes

| command | what it does |
|---|---|
| `node psp-ar-story-reader.mjs` | follow the active container (text) |
| `--speak` | one speakable line per change |
| `--json` | machine readable (container, count, every line name) |
| `--probe` | report once |
| `--list` | dump every line in the active container with its full text |

`--list` output, run live:

```
story container @0x8BA1B10  7 lines
   [0] MSG_AR_000_00_000  "In another future…"
   [1] MSG_AR_000_00_001  "Goku suffered and died of a heart condition,\nand Earth's strongest def…"
   [2] MSG_AR_000_00_002  "It is a world overcome\nwith despair…"
story container @0x8C00520  3 lines
   [0] MSG_AR_CHPTSEL_000  "TOTAL"
   ...
```

## ⛔ What is NOT pinned: the exact current-line index

The reader reports **which scene's lines are loaded and their full text**, but it does not yet
report "line 4 of 7 is on screen". The word that advances is the *container pointer*, not a line
counter, and the line index has resisted:

| attempt | result |
|---|---|
| words equal to a container TEXT pointer | only the container's own arrays — the engine does not hold them |
| words equal to a container NAME pointer | same |
| words equal to the container base | found the holder `0x8AB7C0C` — which is the container pointer itself |
| small (0..32) words, diffed across one advance | 17,661 hits — a scene transition saturated the diff (101,917 words changed) |

⛔ **The recurring trap, for the third time on this game:** a RAM diff is worthless unless the
screen is *confirmed settled*. Every wide diff has been swamped because cutscene text animates and
scenes transition. Any further attempt must (a) verify two screenshots are byte-identical before
trusting a diff, and (b) advance exactly one line between dumps.

**Best lead for next time:** `0x8AB7C10`, the word immediately after the active-container pointer —
it moved +1 on an advance. Confirm it over several advances.

## Is this enough for a story reader?

**Yes, for most of what matters.** A reader can:

- say the scene's lines (the container's full text) — and for a blind player, being given the
  lines of the current narration is the substance of the feature;
- announce when the text set changes (a new scene);
- name the chapter from `DAT_001b11d5` and the clear condition from `MSG_AR_CLEAR_<nn>`
  (`dbzar-msg-resolver.md`).

What it cannot yet do is auto-track which line is being displayed without the player stepping.
That is a polish item, not a blocker.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-story-reader.mjs` | **the story reader** (probe / list / follow / speak / json) |
| `psp-ar-containers.mjs` | finds every loaded `#MSG` container in RAM |
| `psp-ar-currentline.mjs` | hunts the current-line pointer (records the negative results) |
| `psp-ar-linetrack.mjs` | watches the container-pointer neighbourhood across an advance |
| `psp-ar-lineptr.mjs` | dumps the active container's record structure |
| `psp-ar-lineindex.mjs` | small-word diff (records the transition-saturation failure) |

# DBZ Another Road — character select, corrected (2026-10-06)

## Correction: the "roster table" is a SLICE of a general string table

An earlier note called `0x08A243F0` "the roster pointer table". That is only half right, and the
correction matters.

Measured: the pointer run is **longer than the roster**. It starts well before "Goku":

```
0x08A243AC -> 0x08A25116
0x08A243B0 -> 0x08A25150
...
0x08A243E8 -> 0x08A2551A   "Profile Card ..."
0x08A243EC -> 0x08A2555E   "Waiting for opponent."
0x08A243F0 -> 0x08A2558A   "Goku"          <- roster starts here
...
0x08A2444C -> 0x08A25736   "Future Trunks" <- roster ends here
```

So it is a **general string-pointer table** in which the 24 character names happen to form a
contiguous run. **There is no dedicated per-character record structure at that address**, and no
flag array beside it (which is why the earlier parallel-array hunt failed — there was nothing to
find there).

Consequence: the unlock state is NOT stored with the names. It lives in whatever per-character
data structure the game keeps elsewhere, or only in the save.

## What IS confirmed about character select

- **Reached it.** Main menu -> `down` x4 -> `cross` lands on it. The screen title is
  "Select Characters".
- **Layout**: a highlighted character with a form label (Goku, "Normal") and a **RANDOM** option.
- **Only Goku was selectable**, and `left`/`right` changed nothing. Per the project's own rule,
  a screen that does not scroll cannot reveal a cursor — so this is **not** a case of the cursor
  hiding; with a single available character there is nothing to move between.
- Buttons that DO change this screen: `start`, `cross`, `circle`, `square`, `triangle`, `up`,
  `down`. Buttons that do NOT: `left`, `right`, `ltrigger`, `rtrigger`.

That last point is the load-bearing one for this task: **a locked-vs-unlocked RAM diff needs at
least two selectable characters**, and this save has one. The diff cannot be run on this data.

## The lock icon, found on a different screen

Driving deeper from character select reached a **Customize** screen (stat list: Health, Ki,
Defense, Rush, Smash, Energy Blast, Chasing ATK) which shows an explicit **lock icon** in the
corner — a direct visual unlock indicator, and a better lead than a diff, because it is on
screen while the state is live.

That is the place to hunt the flag next: dump RAM while the lock icon is shown, find a value
that matches "locked", then compare against a character whose icon is not locked.

## Corrections to earlier notes, so they do not mislead

| earlier claim | corrected |
|---|---|
| "roster pointer table at `0x08A243F0`" | it is a slice of a **general string table**; no per-character records there |
| "the text pool is resident" | partly: on this screen the menu text pages (`0x08BF0000`, `0x08C00000`) were **empty**, so the pool does vary with screen after all |
| "find the unlock flag by a locked-vs-unlocked diff on character select" | **blocked on this save**: only one character is selectable, so there is nothing to diff |

## Status of task 3

| item | state |
|---|---|
| character roster (24 names + addresses) | **found** |
| 52-entry title table | **found** |
| the game's unlock wording (`"%s has become available!"`) | **found** |
| per-character unlock FLAG | **not found** |
| blocked routes | save encryption; single-character save makes the diff impossible; no per-character record beside the names |

**Recommended next step:** hunt the **lock icon's** backing field on the Customize screen, or
decrypt the save. Both are larger jobs than they look; neither was completed here.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-cshunt.mjs` | ordinal hunt + roster-structure scan on character select |
| `psp-ar-lock.mjs` | captures the lock-icon screen's text and the pointer table |
| `psp-ar-charsel.mjs` | drives toward character select |
| `psp-ar-shotsweep.mjs` | per-button screen change (settles what a screen responds to) |

# DBZ Another Road — the current-line INDEX: chased and NOT found (negative result)

This records a **negative result** deliberately, so the next session does not re-chase the same
lead. The story reader works (see `dbzar-story-reader.md`); this is the one piece it lacks.

## What was tested, and what happened

### Test 1 — the settled-screen rule was followed

A run navigated into Another Road, then for each step took **two screenshots ~1.3 s apart** and
only treated the screen as settled when the hashes matched. It sampled a small, cheap window
around the active-container pointer (`0x8AB7BFC .. 0x8AB7C34`), pressed `cross` once, waited
~4.2 s, and re-read.

Result across **8 advances**:

```
step 0  +0x10=0          +0x14=0        (narration not started)
step 2  +0x10=0x8BA1B10  +0x14=32768    (container loaded)
steps 2..8  IDENTICAL — +0x10=0x8BA1B10, +0x14=32768, everything else unchanged
```

So **nothing in the container-pointer neighbourhood advances per line**. `+0x14` sat at `32768`
(`0x8000`) for every step; the single `+1` observed in an earlier session (`32768 → 32769`) did
**not** reproduce and is therefore **not a line counter**.

### The lead is dead

⛔ **`0x8AB7C10` / `0x8AB7C14` is NOT the line counter.** Do not chase it again.

| what | address | verdict |
|---|---|---|
| pointer to the ACTIVE story container | `0x8AB7C0C` | ✅ real and stable (equals the container base) |
| word right after it | `0x8AB7C10` = `0x8AB7C14` in the earlier dump | ❌ **not** a line counter — no change over 8 advances |

(`0x8AB7C10` and `0x8AB7C14` are the same word in differently-based dumps; the earlier note used
a −0x70 base, this dump used `0x8AB7BFC`.)

## The reasoned conclusion: there may be no line index to find

The narration is almost certainly driven by the game's **script VM**, not by a message index.
Evidence from the project's own notes (`DECOMP_INDEX.md §5`):

> 22 `#MG` configs map story content: `ev_004_XX.spx` scripts paired with `MSG_AR_004_XX.msg`.

So the pairing is **script ↔ message file**. A script interpreter executing `ev_*.spx` holds a
**bytecode program counter**, and the displayed line follows from what the script does, not from
"index N into the message list". That would explain every negative result here:

- no word in RAM equals a line's text pointer (the engine does not hold the text pointer),
- no word in RAM equals the container base beyond the single container pointer,
- no small counter sits beside the container pointer,
- a wide "small word that changed" diff gives tens of thousands of candidates because script
  state and animation churn everywhere.

**Therefore the right question is not "which index" but "which script and PC".** The productive
next step is the `.spx` script VM: find the VM state (script pointer + program counter) and read
the line from the message id the current bytecode references — or, far simpler, hook the message
lookup the engine already performs.

## Simpler alternative that avoids the problem entirely

The engine must call its own "get text for id" routine to draw each line. Hooking **that** (or
finding the pointer it returns into) gives the current line directly, with no index to find. The
loader is `FUN_000da040` and the id→text path is documented in `dbzar-msg-resolver.md`; the
draw-side caller is the place to watch.

## What this means for the reader as it stands

Unchanged and still true: the reader **speaks the scene's actual lines** and names the container.
Only auto-tracking "which line is showing *right now*" is missing, and this document exists so
that gap is not mistaken for a solved problem, nor re-attempted with the dead lead.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-counter.mjs` | the settled-screen-guarded probe used for this negative result |

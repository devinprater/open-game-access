# DBZ Another Road — the unlock flag: both routes tried (2026-10-06)

Task: find the per-character unlock state. Two routes attempted; **both are blocked, and both
blockers are recorded** so the next session does not repeat them.

## Route 1 — read the save file: BLOCKED (the save is encrypted)

The save is `Documents/PPSSPP/PSP/SAVEDATA/ULUS102340000/DATA.BIN`, **22,256 bytes**.

Measured:

| test | result |
|---|---|
| entropy per 512-byte window (all 44 windows) | **7.53 – 7.66 bits/byte**, uniform across the file |
| runs of ≥16 small bytes (`<= 0x20`) | **none** |
| zlib / gzip streams | **none** |
| printable ASCII runs ≥ 8 chars | only 6 short noise fragments (`8pxU#H53`, …) |

A uniform ~7.6 bits/byte with no structure anywhere is **encryption** (not plaintext, and not
a plain zlib stream). PSP saves are commonly encrypted and/or MAC'd by the game.

**Consequence:** the unlock state is in there, but reading it needs the game's save crypto —
its own reverse-engineering job, comparable to the Dissidia text-codec dead end. Not a quick
win. Do not re-run a plaintext scan of `DATA.BIN` expecting a small flag array; it is not there.

## Route 2 — find the runtime builder by write watchpoint: API works, target wrong

The table is built at runtime (0 static ELF references — see `dbzar-roster.md`), so a write
watchpoint is the correct instrument. The debugger's breakpoint API is **available**:

```
memory.breakpoint.list -> { address, size, enabled, log, read, write, change, hits, condition, logFormat, symbol }
memory.breakpoint.add { address, size: 4, type: "write" } -> OK
memory.breakpoint.add { address, type: "write" }          -> Missing 'size' parameter
```

⛔ `size` is **required** on `memory.breakpoint.add` — omitting it fails with
`Missing 'size' parameter`, which reads like a wrong event name rather than a missing field.

The roster table (`0x08A243EC`, 28 pointers) is **resident and unchanged** by navigating the
menus (same address, same count before and after driving main menu -> Training). So the table
is not rewritten on menu navigation, and a watchpoint on it will not catch an unlock.

⛔ A breakpoint was already armed at `0x08A241F0` with **`hits: 0`** — presumably a prior
session's attempt at this same question. It had not fired, which is consistent with "the table
is resident, not rewritten".

## What is established, and what the next route is

**Established:** the 24-character roster table (address + all names), the 52-title table, and
the game's own unlock wording (`"%s has become available!"`).

**The flag is not:** in a static ELF reference, in a simple parallel array beside the table, in
plaintext in the save, or written to the table on menu navigation.

**Next route (recommended): a live locked-vs-unlocked diff on the character-select screen.**
A locked roster row is drawn differently, so the flag can be isolated by diffing RAM with a
known-locked character on screen versus a known-unlocked one. The prerequisite is reliably
reaching character select: from the main menu, `down` x4 lands on Training, then `cross`
enters it.

Alternatively, decrypt the save (route 1's blocker) — a larger, separate job.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-watch.mjs` | finds the roster table live, probes/arms write breakpoints, drives and reports hits |
| `psp-ar-roster*.mjs` | roster table location and dump |
| `DisRoster.java` | ELF reference search (result: 0 sites) |

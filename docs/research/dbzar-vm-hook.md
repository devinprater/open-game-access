# DBZ Another Road — runtime hook attempt: the API map, and why the target was wrong

I was asked to hook the VM to track story lines. **The hook instrument works now; the target was
wrong.** Both halves are recorded here — the API map is reusable, and the wrong turn is worth
not repeating.

## ✅ The PPSSPP debugger API map (measured, this build)

This is the valuable part. All measured against the running emulator, not guessed.

### Breakpoints — two DIFFERENT things that look alike

| call | what it actually is |
|---|---|
| `memory.breakpoint.add {address, size, type:"read"/"write"/"change", log}` | a **DATA** breakpoint. ⛔ `type:"execute"` is **accepted but is still a data breakpoint** — the list reports `read/write/change` flags and it never fires on execution. Arming it on a code address gives 0 hits and reads like "the function is never called". **That is a false conclusion** — do not draw it. |
| `cpu.breakpoint.add {address, log, logFormat}` | the **EXECUTION** breakpoint. The list echoes the disassembled instruction (`"addiu sp,sp,-0x20"`), which is how you confirm the address is right. |

`cpu.breakpoint.list` / `memory.breakpoint.list` / `cpu.breakpoint.remove {address}` /
`memory.breakpoint.remove {address}` all work. `core.breakpoint.*` and `debugger.breakpoint.*`
do **not** exist ("unknown event").

### Registers and control

| call | result |
|---|---|
| `cpu.getAllRegs` | `{categories:[{id, name, registerNames[35], uintValues[35], floatValues[35]}]}` — GPR names include `zero…ra, pc, hi, lo`. **Index by name**, not position: `uintValues[registerNames.indexOf("a0")]` |
| `cpu.getReg {name:"a0"}` | works, but ⛔ returns **`0xdeadbeef` (3735928559)** when the register is unavailable — that sentinel means "no value", never a real value |
| `cpu.status` | `{stepping, paused, pc, ticks}` — the cheapest way to tell "is the CPU running at all" |
| `cpu.stepping` | resume. It **does not reply until the CPU halts again**, so it looks like a timeout on a running game |
| `cpu.resume`, `core.resume`, `debugger.resume` | do not exist ("unknown event"). `cpu.stepping` is the resume call |

### Unsolicited pushes

The debugger pushes state without being asked — chiefly `input.buttons` (the full button
bitfield every change) and `input.analog`. A client must handle messages with no matching
`ticket`, or it will mistake input traffic for breakpoint hits.

### Two real gotchas

- ⛔ **Arming before the game boots fails** with `CPU not started` and `pc=0`. Wait for
  `cpu.status.pc !== 0` before arming anything.
- ⛔ **A halted CPU makes ordinary input calls time out** (`!! input.buttons.press timed out`).
  That timeout is a *symptom of a halt*, and can look like a broken input API when it is
  actually the breakpoint working.

## ❌ Why the target was wrong

The hook was aimed at `FUN_000d9bdc` at RAM `0x88DDBDC`, on the theory that

```c
FUN_000d9bdc(id, container)  ->  *(u32*)(container->texts + (id & 0xffff)*4)
```

was "give me the text for message id". It is a real lookup — but **not the story line path**:

- its **only two callers** are `FUN_000cf9d8` and `FUN_000a5860`, and `FUN_000a5860` is the
  font/texture preload (`"[FIX] WAIT FONT %dP"`, loading 4 entries into `0xa0ab0`);
- a **logging** execution breakpoint on it armed successfully and **never fired** while the
  story narration advanced — no `cpu.breakpoint` hit was pushed, and the only pushes were
  `input.*`.

So the function is called during **font loading**, not during story text drawing. Conclusion:
**⛔ `FUN_000d9bdc` is the font-preload lookup, not the line path. Do not hook it for line
tracking.**

## The other, independent negatives (still true)

| test | result |
|---|---|
| any RAM word equal to a line's TEXT pointer | none (outside the container's own arrays) |
| any RAM word equal to a NAME pointer | none |
| any RAM word equal to an **array-entry** address (`p14 + k*4` / `p18 + k*4`) | only the container's own header words — nothing advances |
| a small counter beside the container pointer | no change over 8 guarded advances |
| `0x8AB7C10` as a line counter | dead (see `dbzar-line-index-negative.md`) |

## Where the line tracking actually has to come from

Not from a message-list index and not from `FUN_000d9bdc`. The two remaining honest routes:

1. **Find the text DRAW path.** Something eventually consumes a text pointer and renders it; its
   caller holds whichever line is current. The container's text pointers are the inputs, so a
   **read** breakpoint on the container's text-pointer array is the cheapest next instrument
   (data breakpoints DO work) — it fires when the engine fetches a line.
2. **The script VM.** `ev_*.spx` scripts drive the narration, so the line follows a bytecode PC.
   Tracking the PC gives the scene position, though not cleanly a "line" number.

## Status

The story reader (`psp-ar-story-reader.mjs`) is unaffected and still works: it reports the active
container and every line's full text. Only "which line is showing right now" is open, and this
document exists so the next attempt starts from the API map above instead of rediscovering it.

## Tooling (all in the scripts dir)

| script | purpose |
|---|---|
| `psp-ar-vmhook.mjs` | first probe: verified the address, discovered the breakpoint API |
| `psp-ar-vmhook2.mjs` | data-vs-exec breakpoint proof; armed both |
| `psp-ar-vmhook3.mjs` | register/status/stepping API discovery |
| `psp-ar-vmhook4.mjs` | hook loop (failed: armed before boot) |
| `psp-ar-vmhook5.mjs` | corrected hook loop (registers unavailable at the halt) |
| `psp-ar-loghook.mjs` | **the logging breakpoint test that produced the negative** |
| `psp-ar-arrayptr.mjs` | array-entry pointer scan (negative) |
| `DisLookup.java` | found the lookup's only callers → `lookup.txt` |

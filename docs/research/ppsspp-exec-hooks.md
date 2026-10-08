# PPSSPP exec hooks — measured, and what they would unlock

Written after reading PPSSPP's source in this tree (`~/src/ppsspp`, the pinned `f293b10`) while
looking for the PSP analogue of the GBA exec-hook work. **Everything here is read from the pinned
source, not run** -- it is a capability survey with citations, not a proven integration.

## Why this came up

The GBA reader needed the CPU at an exact instruction (see `gba-host-proof.md`): its text and menu
hooks read `memory.getregister(...)`, mGBA's Lua API has no exec hook, and approximating them by
polling player movement made the reader speak unrelated data. The fix was mGBA's real breakpoints.

The PSP reader is different in kind, and the difference matters:

    Dissidia's reader is NATIVE (Core/dissidia_adapter.cpp), not Lua.
    PPSSPP has no Lua at all, so the reader was rewritten against the Native* layer.
    A native reader CAN ask the emulator for registers directly at any moment it is called.

So the PSP does **not** have the GBA's problem today. What it has instead is a set of things a
native reader cannot currently observe, and exec hooks are what would make them observable.

## ✅ PPSSPP HAS REAL EXEC HOOKS, AND OUR INTERPRETER PATH KEEPS THEM

The IR interpreter (which is what this app runs -- no JIT on a sideloaded iOS build) has a
complete breakpoint path:

    Core/MIPS/IR/IRFrontend.cpp:362   IRFrontend::CheckBreakpoint(addr)
                                        -> FlushAll(), then ir.Write(IROp::Breakpoint, addr)
    Core/MIPS/IR/IRInterpreter.cpp:55 IRRunBreakpoint(pc) -> g_breakpoints.ExecBreakPoint(pc)
    Core/MIPS/IR/IRInterpreter.cpp:1195  dispatched from the IR switch

The frontend is explicit that block caching must be invalidated when breakpoints change
(`FlushAll()` inside `CheckBreakpoint`), and it skips the optimiser passes for any block that
contained one (`if (!js.hadBreakpoints) { ...passes... }`). That is the whole correctness story for
a cached IR block, and it is already written.

⛔ **THE CLASSIC INTERPRETER AND THE IR INTERPRETER DIFFER, AND THIS PROJECT ALREADY KNOWS.**
`Core/psp_core.cpp:127-129` says the classic interpreter is used "so debugger MemChecks fire (the
IR interpreter's memory ops bypass them)". That is true of MEMORY breakpoints. It is **not** true
of EXEC breakpoints: `IRRunBreakpoint` is a first-class IR op, so an exec hook works on the fast IR
path and does not need the slow classic interpreter. That distinction is the useful part.

## What that would unlock for the PSP reader

Today the PSP reader is frame-driven: `OnFrame` samples RAM every frame and announces changes. That
is correct for state that persists across a frame, and blind to anything that happens *within* one.
Three concrete things a native reader cannot see now:

  1. **A routine that runs and returns inside one frame.** The GBA footstep problem exactly: an
     effect with no persistent state to poll. An exec hook at the routine's address fires every
     time, including twice in a frame.
  2. **What called what.** The reader can see *that* a menu changed but not *which* code wrote it.
     An exec hook sees the code path, which is how the Dissidia board-tooltip decomp (FUN_001cfe78
     and friends) could be confirmed live rather than trusted from the decomp.
  3. **Ordering within a frame.** Several writes to one address in a frame collapse to the last
     value when polled; a hook sees each one. That is the difference between "the HP is now X" and
     "the HP went A -> B -> X".

⛔ **AND THE HONEST COUNTERWEIGHT.** Every one of these is *observability*, not speech. A hook that
fires does not by itself tell the player anything; it has to be turned into an announcement, and
the announcement queue's rate limits (`docs/design/announcement-queue.md`) exist because per-frame
events are far too frequent to speak. So exec hooks are a way to make a READ more correct, not a
new channel -- and any hook added for speech has to pass the same criteria the queue already
enforces.

## Traps, read from the source rather than discovered the hard way

  * **`g_breakpoints.ExecBreakPoint` sets `coreState`.** `IRRunBreakpoint` returns 1 and stops the
    block when `coreState != CORE_RUNNING_CPU`. A hook that does not restore the running state will
    park the emulator on the first hit -- the same class of hang as mGBA's `isPaused` trap
    (see `gba-host-proof.md`). Treat "the run stopped dead with no error" as this, first.
  * **Breakpoints are global (`g_breakpoints`).** They are process-wide in PPSSPP, so a hook
    installed for one game state persists until cleared; `HasBreakPoints()` is checked on every
    instruction dispatch, so a leaked breakpoint is also a permanent speed cost.
  * **`CheckSkipFirst`** exists to let the debugger step past a breakpoint it is sitting on. A
    scripted hook that wants to fire every time must not inherit that skip.
  * **The optimiser is skipped for blocks with breakpoints** (`js.hadBreakpoints`), so installing
    many hooks does slow the affected blocks down -- measured cost is the thing to check before
    relying on them for anything hot.

## Why this is parked, not started

The PSP reader is native and works without any of this: Dissidia reads, announces and is host-
proven at 900/900 frames. Exec hooks would make three specific reads *more* precise. That is
worth doing when one of those three is the thing blocking a feature -- for example when the
board-tooltip work wants live confirmation of the decomp's call path -- and not before. There is
no player-visible bug that this fixes today.

⚠ Everything above is source reading on the pinned revision. No hook has been installed or
measured in this project; the first step if it is ever started is the same spike the GBA work used
(`tools/gba-debug-hook-spike.c` is the shape: prove the primitive, measure the cost, then wire it).

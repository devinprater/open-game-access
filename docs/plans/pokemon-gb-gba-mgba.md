# Plan: Pokémon Access (GB/GBC/GBA) → mGBA → Open Game Access

Status: **planning**. Nothing in this document has been executed yet. Every claim about
what exists was verified by inspecting the files (see "What already exists").

---

## The goal

Run the original Pokémon Access reader scripts for the Game Boy family inside mGBA, and
surface their output through Open Game Access's speech layer — without the readers
knowing anything about Open Game Access, and without OGA knowing anything about Pokémon.

That separation is the whole design, and it is already half-built.

---

## What already exists (verified)

This is not a from-scratch build. Three pieces are already in place:

**1. The reader scripts — 166 Lua files.** At `%LOCALAPPDATA%/Temp/pokemon-access-gb/`,
including per-game and per-language trees:

```
gb.lua      18,595 bytes    GB/GBC entry point
gba.lua     70,333 bytes    GBA entry point
a-star.lua   5,348 bytes    pathfinding helper
serpent.lua  8,310 bytes    serialisation helper
game/common/  rby.lua, gsc.lua, rse.lua, frlg.lua     per-generation logic
game/{red_blue,gold,silver,crystal,yellow}/{en,es,fr,de,it,pt-br}/
game/expansion/  firered/cfru.lua  (ROM-hack support)
```

**2. A working mGBA bridge, in C++.** The Android app already ships
`app/src/main/cpp/MGBAScriptJNI.cpp` (11,519 bytes) plus `MGBACore.{cpp,h}`. It exposes a
`MGBARunner` class that loads a ROM, creates a script host, and wires callbacks:

```cpp
script->setSpeechCallback([](const char* text, bool interrupt, void*) { … });
script->setLogCallback(…);
script->setSoundCallback([](const char* path, int pan, int volume, void*) { … });
```

**3. A speech bridge contract that already works.** The JNI entry point
`setGbSpeechBridge` reflects into a Java object and resolves:

```
speak(String, boolean)      required
stop()                      required
playSound(String, int, int) optional — a bridge without it still works, silently
```

Note the optional-sound handling: that is the right pattern for OGA too.

### ⛔ The one real gap

The readers target **BizHawk's** Lua API (`emu.`, `joypad.`, `memory.`), and grep found
**no** mGBA-specific API usage (`mgba.`, `emu:read`) anywhere in the 166 files. So a
compatibility shim is required between the readers and mGBA's Lua host — the same shape
as `bizhawk_compat.lua`, which already exists in OGA for the NDS readers.

That shim is the single load-bearing new component.

---

## The architecture

```
  reader scripts (gb.lua / gba.lua / game/**)      <- unchanged, BizHawk API
            ↓
  bizhawk_compat shim  →  mGBA Lua host            <- NEW, the load-bearing piece
            ↓
  speech / log / sound callbacks                   <- already the pattern in MGBAScriptJNI
            ↓
  OGA acquisition layer  (tools/re/runtime)        <- console-independent
            ↓
  OGA presentation       ("Enemy directly ahead")  <- accessibility output
```

The rule this preserves: **a reader must never contain an address, and OGA must never
contain a Pokémon fact.** The reader speaks; OGA carries speech. Neither knows the other.

---

## Phases

### Phase A — Get one reader running in mGBA on Windows
Smallest possible loop: Pokémon Crystal (GBC) is the best first target because `gb.lua`
is 18 KB (vs `gba.lua` at 70 KB) and GBC emulation is simpler than GBA.

1. Install mGBA for Windows via Scoop (user's standing instruction: use Scoop for Windows
   tools). Check what Scoop actually has before writing any install command.
2. Determine mGBA's Lua surface: which globals it provides, whether `emu:read` style calls
   exist, and whether it supports the *file loading* the reader set needs (`dofile`, or
   require-with-path). This is the crux — if mGBA's Lua cannot load sibling files, the
   reader's multi-file structure becomes a packaging problem.
3. Write the shim mapping `emu.*` / `memory.*` / `joypad.*` onto whatever mGBA exposes.
4. Prove speech: run Crystal, get the reader to emit its first line.

**Exit criterion:** a real spoken line from a real save, in mGBA, on this machine.
Not "it loads" — a line of output.

### Phase B — Generalise the shim across the family
Only after Phase A produces speech.

- GB/GBC generations: Red/Blue, Yellow, Gold, Silver, Crystal.
- GBA generations: Ruby/Sapphire/Emerald, FireRed/LeafGreen.
- Verify the ROM-hack path (`game/expansion/firered/cfru.lua`) is not broken by the shim.

**Exit criterion:** the same shim, unmodified per game, serves all generations. A shim
that needs per-game special-casing is the wrong shim.

### Phase C — Hook into Open Game Access
- Add a runtime adapter beside the existing melonDS one, so OGA has one interface and two
  consoles. The melonDS path is the template: `poke_*` entry points plus a script host.
- Define the semantic boundary: readers emit *what the game is doing*; OGA renders *what
  the player should hear*. Do not let a reader format its own prose into OGA's style.
- Reuse the `speak/stop/playSound` contract shape rather than inventing a second one.

**Exit criterion:** the same OGA speech layer narrates both an NDS Fire Emblem session and
a GBA Pokémon session, with no game-specific code in the OGA layer.

### Phase D — iOS
The NDS work already reaches iOS via xtool. Once Phase C is stable, the GBA path follows
the same route. Deliberately last: no point porting a design that is still moving.

---

## Risks, stated plainly

| Risk | Why it matters | Mitigation |
|---|---|---|
| **mGBA's Lua API may not support multi-file loading** | the 166-file reader set is structured as modules | Investigate first (Phase A step 2). If unsupported, pre-concatenate into one bundle at load time — a packaging step, not a rewrite |
| **Reader API surface is large** | `gba.lua` is 70 KB; a shim must cover whatever it touches | Start with Crystal's 18 KB surface; grep the actual API calls the readers make rather than guessing |
| **BizHawk and mGBA differ in more than memory reads** | savestate handling, input injection, frame timing | Determine empirically in Phase A; do not assume |
| **166 files include translations** | the `de/es/fr/it/pt-br` trees are per-language, so a shim must not assume English | Language is a reader concern, not a shim concern — keep the shim language-blind |
| **Licensing** | the reader set is the user's own prior work | Attribute in README as already done; confirm before publishing any of it |

---

## What this plan deliberately does NOT do

- **No decompilation yet.** The reader set already reads live RAM, which is what
  accessibility needs. Decompiling a Pokémon ROM is a much larger job and would only be
  justified if a reader needs a structure we cannot locate by runtime inspection. If that
  point is reached, escalate per the user's instruction.
- **No new reader logic.** These scripts already work in BizHawk; the task is to host
  them, not to rewrite them.
- **No OGA changes until Phase C.** Keeping the layers separate means Phases A–B can
  succeed or fail without touching the working NDS path.

---

## Tracking

Board: `docs/plans/gb-gba-mgba-board.md` (Kanban). Todos mirror the phases above.

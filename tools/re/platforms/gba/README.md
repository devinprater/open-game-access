# mGBA host-API shim — design

Companion to `docs/plans/pokemon-gb-gba-mgba.md`. This is the load-bearing component: it
lets the 166-file Pokémon Access reader set run inside mGBA.

## ⛔ THE HARD FINDING: the readers are LuaJIT + FFI + Tolk, not pure Lua

Discovered while building the load path. The reader set does:

```lua
local ffi  = require "ffi"
local tolk = ffi.load("tolk")
tolk.Tolk_Output(encoding.to_utf16(s), false)
```

That is **BizHawk's LuaJIT**. mGBA embeds **stock Lua 5.4**, which has no `ffi` module and
no way to load a DLL without writing a C module. This is not a configuration difference —
it is a different Lua runtime, and it was not visible from the call-surface enumeration
(which looks at `emu.*`/`memory.*`, not at `require`).

**Why it is still tractable:** the surface is tiny and bounded. Of **166 Lua files, exactly
four** touch FFI:

| file | purpose | disposition |
|---|---|---|
| `tolk.lua` | speech output | stubbed → routes text to the shim's speech sink |
| `win-controls.lua` | file dialogs | only for interactive prompts; unused headless |
| `crc32.lua` | game identification | **reimplemented in pure Lua** (`oga_pure.lua`), tested against published CRC-32 vectors |
| `encoding.lua` | UTF-16 for Tolk | identity functions — safe because speech no longer goes through Tolk's C API |

The core readers — `gb.lua`, `gba.lua`, `game/common/*.lua` and all per-language files —
are pure Lua once the shim is installed. So the FFI layer is replaced rather than 688 KB of
reader code being ported.

`crc32` is **reimplemented rather than stubbed**, because a stub would return nil and the
reader would report "this game is not supported" for a supported game — a silent wrong
answer. The pure-Lua version accepts both a string and a table of byte values, matching the
original, since `memory.readbyterange` returns a table. Verified against the canonical
vector `crc32("123456789") == 0xCBF43926` plus four more, in `test-oga-pure.lua`.

## The measured problem

| | |
|---|---|
| Reader files | **166 Lua files, 688,474 bytes** |
| Host API calls they use | **16 distinct functions** |
| mGBA-specific calls in those files | **0** — every call is BizHawk-style |

A 16-function surface for 688 KB of script is a very favourable ratio. The shim is small;
the readers stay untouched.

## The exact surface to implement

```
emu.         frameadvance (2)   platform (1)

memory.      readbyte (161)          readword (80)         readdword (76)
             getregister (72)        gbromreadbyte (48)    readbyteunsigned (6)
             registerexec (6)        readbyterange (6)     gbromreadword (2)
             readbytesigned (2)      registerwrite (1)     readdwordsigned (1)

input.       read (1)
```

Counts are call sites; the order matters, because the top four (`readbyte`, `readword`,
`readdword`, `getregister`) carry most of the load and are the ones to get exactly right.

## Mapping onto mGBA's API

mGBA's scripting API is object-oriented and documented at <https://mgba.io/docs/scripting.html>:

| Reader expects | mGBA provides | Notes |
|---|---|---|
| `emu.frameadvance()` | `emu:runFrame()` | direct |
| `emu.platform()` | `emu:platform()` → 0=GBA, 1=GB | direct |
| `memory.readbyte(a)` | `emu:read8(a)` | direct |
| `memory.readword(a)` | `emu:read16(a)` | direct |
| `memory.readdword(a)` | `emu:read32(a)` | direct |
| `memory.readbyteunsigned(a)` | `emu:read8(a)` | same; BizHawk's signed/unsigned split is a no-op here |
| `memory.readbytesigned(a)` | sign-extend `emu:read8(a)` | shim must convert |
| `memory.readwordsigned(a)` | sign-extend `emu:read16(a)` | shim must convert |
| `memory.readbyterange(a,n)` | `emu:readRange(a,n)` | returns a **string**; may need unpacking |
| `memory.getregister(r)` | `emu:readRegister(name)` | **name mapping required** — see below |
| `input.read()` | no direct equivalent | see below |
| `memory.registerexec(a,f)` | **no equivalent** | see below |
| `memory.registerwrite(a,f)` | **no equivalent** | see below |
| `memory.gbromreadbyte(a)` | `emu:read8` via the `cart0` domain | GB ROM banking — domain-aware read |
| `memory.gbromreadword(a)` | same, 16-bit | |
| `memory.lua` | not an API | false positive; likely `memory.lua` as a filename in a path string |

## The three real mismatches

### 1. `registerexec` — an exec hook mGBA does not have

Used **6 times**, and it is not decorative: it is how the readers detect **footsteps**.

```lua
memory.registerexec((ROM_FOOTSTEP_FUNCTION % 0x4000) + 0x4000, function()
  if memory.readbyte(HRAM_ROM_BANK) == math.floor(ROM_FOOTSTEP_FUNCTION / 0x4000) then
    -- a step happened: recompute position and announce the tile
```

That is the core of "walk around and hear what is in front of you", so it cannot be
dropped. mGBA has no exec hook, but it does have `callbacks:add("frame", cb)`. The shim
can implement `registerexec` as a **per-frame poll of the same predicate** — the callback
becomes a frame callback that checks the PC against the registered address.

Caveat to verify, not assume: a frame-level poll may **miss** a routine that runs and
returns within a single frame. If footsteps are missed, the fallback is to poll the
observable *effect* (the ROM bank byte plus the player's position changing) rather than
the PC. Establish this empirically — do not build the fallback pre-emptively.

### 2. `registerwrite` — a write hook, used once

```lua
memory.registerwrite(0x40000dc, function()   -- GBA DMA control
  local dest = memory.readdword(0x40000d8)
  -- checks whether DMA is writing into VRAM
```

Used once, for GBA DMA-into-VRAM detection. Same treatment as `registerexec`: poll the
watched address per frame and fire on change. For a DMA control register this is a
reasonable approximation because the value persists for the duration of the transfer.

### 3. `getregister` — register-name mapping (72 call sites)

BizHawk names GB registers `A`, `B`, `C`, `D`, `E`, `H`, `L`, `F`, `PC`, `SP`; mGBA's
docs list the same set but lowercase, plus combined pairs (`bc`, `de`, `hl`, `af`). The
shim therefore needs a **case-normalising lookup table**, including the combined pairs
the readers may ask for.

## What the shim must NOT do

- **Not interpret game semantics.** It converts host calls; it knows nothing about
  Pokémon. Any game knowledge in the shim would be in the wrong layer.
- **Not reformat speech.** Readers emit text; the shim forwards it. Presentation belongs
  to Open Game Access (Phase C).
- **Not assume English.** 166 files include `de/es/fr/it/pt-br` trees. The shim is
  language-blind.
- **Not patch the readers.** If a reader needs a change, that is a finding about the
  shim's incompleteness, not a licence to edit the scripts.

## Verification plan

The shim is correct when, with **no reader edits**:

1. Pokémon Crystal loads and the reader emits its first real line.
2. Walking produces footstep announcements (this specifically exercises the `registerexec`
   → frame-poll substitution, the riskiest mapping).
3. A map transition announces the new area.
4. The same shim, unchanged, serves a GBA generation (Emerald or FireRed).

Step 2 is the one that matters. A shim that loads and speaks but misses footsteps would
look like success while breaking the primary accessibility feature.

## Open risk

mGBA's `readRange` returns a **string**, not a table. Six call sites use
`readbyterange`. Whether the readers index the result as a table or as a string must be
checked against the actual call sites before implementing — a mismatch here would be a
silent wrong-value bug rather than an error.

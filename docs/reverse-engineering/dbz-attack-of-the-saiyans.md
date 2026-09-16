# Dragon Ball Z: Attack of the Saiyans — status

**ROM:** `Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds`
**Game code:** `BRPE` — but see the trap below, the internal title says something else.
**Internal title (bytes 0x00–0x0B):** `DB KAI RPG` — this is a **Dragon Ball KAI** game internally.

## Where the work stands

| Step | Status |
|---|---|
| ROM identified | Done — `BRPE`, 128 MB |
| Public memory documentation found | Done — Action Replay lists (USA **and** Europe) + DeSmuME wiki |
| Host probe written | Done — `fe/dbz_probe.cpp` |
| Probe builds and runs | Done |
| Console boots and reaches gameplay | **Verified** — see the screenshot |
| Published addresses verified | **NOT verified yet** — all read 0 so far |
| Adapter written | Not started |

## The measured results

### Run 1 — no input, 6000 frames

Every published address read exactly `0`. A control address (`0x02000010`) read
real data, which proves the probe itself works.

### Run 2 — with an input plan, 20,000 frames

Still `0` at every published address. But the **window population** measurement
separates the two possible explanations:

```
party block (USA 0x020CD000-0x020CE000)  nonzero   157 /  4096 bytes
item block  (USA 0x020CC700-0x020CC900)  nonzero    14 /   512 bytes
item block  (EUR 0x020CC300-0x020CC500)  nonzero     0 /   512 bytes
control     (0x02000000-0x02001000)      nonzero  3703 /  4096 bytes   <- busy
```

The control window is **90% populated**, so the probe is reading live memory
correctly. The EUR window is entirely zero. The USA party block is sparse — and
critically, **the identical numbers appear at 24,000 frames** (run 3), so that
157/4096 is a static floor, not a structure filling in.

### The screenshots settle what the numbers meant

- `docs/evidence/dbz-20000-frames-intro-scene.png` — intro dialogue with Krillin,
  world map and party visible.
- `docs/evidence/dbz-24000-frames-house-interior.png` — an isometric house
  interior, **with a red HP bar rendered at the bottom of the screen**.

The second one is decisive. **The game is in play, on the bottom screen's HP bar
display, and the published addresses still read zero.** So:

- ✅ The emulator boots this ROM.
- ✅ Input driving works — the plan reaches gameplay.
- ✅ The probe reads live memory.
- ❌ **The published Action Replay addresses are wrong for this ROM.**

⛔ That last conclusion could only be reached *because* of the HP bar in the
screenshot. A zero reading on its own is ambiguous between "not allocated" and
"wrong address"; a zero reading **while the game draws the value** is not.

## The structure scan, and why it did not settle it

Using the one structural fact the code lists give — a record stride of `0x24C`
(from Europe's `DC000000 0000024C`) — the probe scans `0x020C0000..0x020E0000`
for an address whose value is plausible *and different* at `base`, `base+stride`
and `base+2*stride`.

It returned **348 candidates**, which is useless in itself:

```
0x020C7DDC  5489 / 7240 / 4279     <- graphics data, not RPG stats
0x020C7F74  3185 / 3782 / 3570
0x020C99B2  513 / 515 / 516        <- small, but uniform
0x020C99B4  1 / 5 / 5
```

⛔ **This is the project's own documented trap: a memory scan is a confirmation
tool, not a discovery tool.** Hundreds of plausible triples is what a permissive
filter over graphics memory looks like. The scan needs a *hypothesis* to test, not
a wider net.

## What the honest next step is

The addresses are wrong, so stop refining them. Two better routes, in order:

1. **Get the real memory map from the ROM, not from a code list.** A randomizer
   repo documents ARM9 RE and the `.narc`/BDAT formats for this game. A
   decompilation or disassembly would give named addresses directly — the same
   move that made Fire Emblem tractable (`Eebit/fe11-us` ships `symbols.txt`).
   **Search for a decomp before scanning again.**
2. **If scanning, make it a controlled experiment.** Change exactly one value
   in-game (take damage so the HP bar visibly drops), snapshot before and after,
   and diff. That is a hypothesis with a known expected result, and it narrows to
   a handful of addresses instead of 348.

⛔ **Do not trust the Europe list's offsets either.** Europe's item block is
entirely zero across the whole run while the USA one has 14 bytes, which is
consistent with neither list matching this ROM. The internal title says
`DB KAI RPG`, hinting this release shares lineage with the Japanese `DB Kai`
game — so the code lists may simply be for a different build.

## The probe itself

`fe/dbz_probe.cpp`, built against the host object set:

```bash
export PA_SHIM="$PWD/Sources/OpenGameAccess/Resources/bizhawk_compat.lua"
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$HOME/src/melonds-lua/src" -std=c++17 \
  -o Vendor/dbz_probe fe/dbz_probe.cpp Vendor/hostobj/*.o -lpthread -lm -ldl
DBZ_SHOT=/path/out.ppm ./Vendor/dbz_probe <rom> 20000 fe/plans/dbz-boot.txt
```

It reports, per address: first/last value, change count, distinct values — plus
window population and a screenshot.

### Three traps this probe hit, all now encoded in it

- ⛔ **`poke_start` refuses without a script** ("No accessibility script was
  bundled"), and that check runs before any frame. A native probe needs an idle
  looping script, and the shim must load first or the script dies on
  `attempt to index a nil value (global 'emu')`.
- ⛔ **`PA_SHIM` must be exported INSIDE the script**, not from a parent shell —
  otherwise the shim is silently absent and the error looks like a broken script.
- ⛔ **`poke_framebuffer` must be called BEFORE `poke_framebuffer_ptr`.** The
  pointer helper is stateful and returns null unless the screen was selected
  first — and you still get valid width/height, so it reads as a broken
  framebuffer rather than a call-order mistake.

## The published code lists (claims to verify)

USA (`0x020CC770` zenny, etc.) and Europe (`0x020CC370` zenny) differ by `0x400`.
Europe's party records use a stride of `0x24C` (`DC000000 0000024C`), which is
strong evidence of the record layout and is worth testing directly at
`base + n * 0x24C`.

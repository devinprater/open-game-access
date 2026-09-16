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
correctly. The EUR window is entirely zero. The USA party block is sparse.

### The screenshot settles what the numbers meant

`docs/evidence/dbz-20000-frames-intro-scene.png` — the game is **past the title
and in an intro dialogue scene** with Krillin, on the world map with the party
visible. So:

- ✅ The emulator boots this ROM.
- ✅ Input driving works (the plan got the game into play).
- ✅ The probe reads live memory.
- ❌ **No battle has happened yet**, so the party stat block is not populated.

⛔ **This is the third time in this project that a "the reader is broken" symptom
turned out to be "the game is not where you think it is".** The memory was
consistent with a half-built structure AND with a wrong address; only the picture
distinguished them. Bracket RAM claims with a screenshot — every time.

## Why the addresses may still be wrong (two live hypotheses)

1. **The game has not entered a battle.** The AR lists name these addresses in
   battle context (HP/Ki/AP per character), so they may only be written once
   combat starts. **This is the leading hypothesis** and matches the screenshot.
2. **The region offset is wrong.** The USA and Europe lists disagree, and the
   internal title (`DB KAI RPG`) hints this release may share lineage with the
   Japanese `DB Kai` game, so the code list's region may not match this ROM.

Both are testable: drive further into the game until a battle, and re-measure.

## ⛔ Next step, in order

1. **Extend the input plan to reach a battle** — advance dialogue, then trigger a
   random encounter. The plan currently stops in the intro scene.
2. **Re-measure the same table.** If the party block populates, hypothesis 1 is
   confirmed and the addresses are good.
3. **If it is still zero, rescan nearby** — the population measurement then tells
   us whether we are in the right region at all.

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

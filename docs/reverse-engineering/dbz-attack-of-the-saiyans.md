# Dragon Ball Z: Attack of the Saiyans — party structure FOUND

**Status: the character array is located and its layout is measured.** The
published Action Replay addresses are wrong, and the reason is now known exactly.

## ROM

| | |
|---|---|
| file | `Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds` |
| size | 134,217,728 bytes (128 MB) |
| internal title | **`DB KAI RPG`** — not "Attack of the Saiyans" |
| game code | **`BRPE`** |
| maker | `AF` (Namco Bandai) |
| revision | 0 |

⚠️ GameTDB lists this game as **`BRPP`**, but this ROM is **`BRPE`**. The internal
title (`DB KAI RPG`) suggests a shared lineage with the Japanese *DB Kai* title.
Both are evidence that published code lists may target a different build.

## Why the published addresses read zero

They read zero because **the base address in the code lists is wrong**, not because
the structure was absent. The party exists and is populated from the very start.

## ✅ The verified layout

Four consecutive character records, **stride `0x24C`** — exactly the stride the
Europe code list claims via `DC000000 0000024C`:

| character | name address (measured) | name offset in record |
|---|---|---|
| Goku | `0x020CD774` | +0x20 |
| Gohan | `0x020CD9C0` | +0x20 |
| Piccolo | `0x020CDC0C` | +0x20 |
| Krillin | `0x020CDE58` | +0x20 |

The gaps are `0x24C, 0x24C, 0x24C` — **identical, three times over**. That
repetition is what makes this a measured structure rather than four coincidences.

Derived array base: **`0x020CD754`** (name address − 0x20).

⛔ The code list's base is `0x020CD300`. It is **0x454 too low**, so every offset
taken from it lands in empty memory. That single error explains every zero reading
observed across all previous runs.

## ✅ Per-character stat fields (candidates — naming still needs in-game confirmation)

Read as u32. Each value appears **three times consecutively** (current / max / a
display copy — the normal shape for a DS RPG), then the next stat follows the same
pattern 0x10 later:

| character | `name+0x1D8` (+1DC, +1E0) | `name+0x1E8` (+1EC, +1F0) |
|---|---|---|
| Goku | 290 | 95 |
| Gohan | 660 | 225 |
| Piccolo | 300 | 105 |
| Krillin | 320 | 110 |

These are plausibly **HP** and **Ki** — they vary per character in exactly the way
party stats should, and the tripling is structural rather than accidental.

⛔ **What is confirmed is the LAYOUT, not the field names.** "290 is Goku's HP" is a
hypothesis. To confirm it, change exactly one value in-game (take damage so the HP
bar visibly drops) and re-read — never accept a plausible-looking number.

## ⛔ The method that worked, and the one that wasted a run

**What failed:** scanning 128 KB for an address whose value was "plausible AND
different" at `base`, `+stride`, `+2*stride`. It returned **348 candidates** — the
top hits (`5489 / 7240 / 4279`) were graphics data. Plausibility is not evidence,
and a permissive filter over graphics memory finds hundreds of matches.

**What worked: scanning for ASCII STRINGS.**

```
=== string scan: character names locate the record layout ===
  0x020CD774  "Goku"     (len 4)
  0x020CD9C0  "Gohan"    (len 5)
  0x020CDC0C  "Piccolo"  (len 7)
  0x020CDE58  "Krillin"  (len 7)
  -- 4 string(s); distance between them is the record stride
```

**Character names are the best structure oracle in an RPG.** They are long,
self-identifying, and sit at a fixed offset inside each record — so the distance
between one name and the next *is* the stride, measured rather than assumed. Four
hits replaced 348 useless candidates and produced the base, the stride, and an
identifiable anchor for every scalar field.

**Generalisable rule: when hunting an unknown structure, look for the strings
first.** A name gives you a known-content anchor; a "plausible integer" gives you
nothing to check against.

## ⛔ The zero-reading trap, and the screenshot that broke it

A probe reported every published address as `0` while a control address read real
data. That is consistent with **both** "the structure is not allocated yet" **and**
"the address is wrong" — and the two demand opposite next actions.

The screenshot on the bottom screen showed a **status gauge while the addresses read
zero**. That converts the ambiguity into a finding: the stats demonstrably exist (the
game is drawing one), so the addresses are wrong.

**Generalised: "reads zero" is ambiguous; "reads zero while the game draws the value"
is not.** Bracket every RAM claim with a screenshot at the same moment.

## Evidence

- `docs/evidence/dbz-20000-frames-intro-scene.png` — intro dialogue, past the title
- `docs/evidence/dbz-24000-frames-house-interior.png` — house interior **with a
  status gauge rendered**, the image that proved the addresses wrong

## How to reproduce

```bash
export PA_SHIM=<repo>/tools/re/platforms/gba/mgba_compat.lua   # ⛔ A FILE, not a dir
bash scripts/build-host.sh
g++ -O1 -g -fPIC -fwrapv -fno-strict-aliasing -DHAVE_PTHREADS=1 -DPOKE_HOST=1 \
    -Wno-everything -I$PWD/Core -I$PWD/Sources/CPokeCore/include \
    -I$HOME/src/melonds-lua/src -I$HOME/src/lua-5.4.7/src -std=c++17 \
    -o /tmp/dbz_probe fe/dbz_probe.cpp Vendor/hostobj/*.o -lpthread -ldl -lm
DBZ_SHOT=shot.ppm /tmp/dbz_probe "<rom>.nds" 12000 fe/plans/dbz-battle.txt
```

⛔ **`PA_SHIM` is a FILE, not a directory.** Pointing it at the shim's directory makes
`fopen` fail silently in effect: the probe falls through to a bare
`emu.frameadvance()` with no shim loaded and dies on
`attempt to index a nil value (global 'emu')`. The `emu` global comes from the shim.

⛔ **Do not link `-lSDL2`** — the `POKE_HOST` platform layer uses
`clock_gettime`/`usleep`, not SDL.

## Next steps

1. **Confirm the field names** — take damage in-game, re-read `name+0x1D8`, and see
   whether it drops. That converts the candidate stat block into verified HP/Ki.
2. **Locate the level field** — it should be within the same record and change on
   level-up, which is a cheap in-game experiment.
3. **Then build the AotS adapter** on the measured layout — `Core/adapter.h` keyed
   on game code `BRPE`, reporting party name/HP/Ki via the same `Host` callbacks the
   GBA and Fire Emblem adapters use.

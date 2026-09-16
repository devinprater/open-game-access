# oga-re — Open Game Access reverse-engineering toolbox

`oga-re` is the reusable reverse-engineering layer behind Open Game Access. Its job is
to turn *"where is this value in memory"* into *"what does this mean about the game"* —
for many consoles, without rebuilding the research workflow per platform.

Fire Emblem: Shadow Dragon (FE11, Nintendo DS) is the first proving ground. It is not
the design target. Everything here is shaped so the second target costs a platform
adapter rather than a fresh methodology.

## The separation this exists to enforce

```
  game semantics      "GetUnitAtPosition(x, y)"           ← what the game means
        ↓
  runtime data        unit array + occupancy lookup       ← where it lives
        ↓
  acquisition         melonDS memory read / PPSSPP API    ← how we get it
        ↓
  presentation        "Enemy Cavalier, 4 east, 17 HP"     ← what a player hears
```

⛔ These layers must not bleed into each other. An accessibility string must never
contain an address, and a platform loader must never contain a game rule. When those
mix, the knowledge stops being portable and every new game pays the full cost again.

## Layout

```
tools/re/
  README.md              this file
  manifests/
    tools.json           exact installed versions (reproducibility)
    platforms.json       per-platform capability matrix
  common/
    runtime-diff/        snapshot/diff — console-independent
    symbols/             symbol import/export adapters
    schemas/             shared JSON schemas (address model, findings)
  ghidra/
    scripts/             headless Ghidra queries (game-agnostic)
    extensions/          third-party Ghidra extensions (tracked, not vendored)
    headless/            driver scripts
  platforms/
    nds/ gb/ gbc/ gba/ nes/ snes/ genesis/ n64/ ps1/ ps2/ psp/
```

Directories are created **as the tooling becomes real**, not pre-filled with
placeholders. An empty directory claiming a capability is worse than an absent one,
because it reads as done.

## The common address model

Different consoles have genuinely different memory models, and flattening them into one
integer loses information that matters. A SNES address is meaningless without its bank;
an NDS address is meaningless without knowing whether it is a runtime RAM address or an
overlay-relative one. See `common/schemas/address.json`.

The rule: **never conflate ROM address, runtime RAM address, file offset, module,
and bank.** A discovery that moves between an emulator, a save state, Ghidra, and the
runtime reader will cross all five representations, and a lossy conversion at any step
silently produces a wrong answer later.

## Discipline

Facts carry a confidence level — `confirmed`, `strongly inferred`, `tentative`, or
`unknown` — and evidence. A decompiler's suggestion is not evidence; a live read that
matches a predicted value is. Guesses are never encoded as facts, and a function is not
renamed to a descriptive name until something independent agrees with the guess.

Findings live in `docs/reverse-engineering/<game>.md` (human-readable, with evidence)
and `reverse-engineering/<game>/` (machine-readable JSON for Open Game Access to
consume). Neither the ROM nor any extracted Nintendo asset is ever committed.

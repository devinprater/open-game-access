# open-game-access — status

Superseded name: `pokemon-access-mobile` / `pokemon-access-ios`.

## Done this session

1. **Reviewed the existing architecture** → `docs/current-architecture.md`.
   Answers the seven questions asked, including whether a second adapter can load
   without destabilising Pokémon (**yes**, and why).
2. **Fire Emblem: Shadow Dragon memory map** → `docs/fire-emblem-shadow-dragon-memory.md`.
   Cursor confirmed two independent ways; the unit array and its stride measured;
   character identity read from the game's own identifiers.
3. **Adapter seam** → `Core/adapter.h`, `Core/adapters.cpp`.
4. **Fire Emblem adapter (milestone 1)** → `Core/fe_access.cpp`.
5. **Debug dump** that can be attached to a bug report (`fe-access.sh`, below).
6. **README** reframing the project as Open Game Access.

## Milestone 1 — reached

```
Where am I?  -> Cursor 1, 20. Terrain: unknown (tile id not yet verified).
                Unit here: Marth, 18 HP, unacted.
Next ally    -> Marth, 18 HP, position 1, 20, unacted, 0.0 tiles away.
Next enemy   -> No enemies found.              (chapter 1 has only your own units)
```

Confirmed live values:

```
gMapStateManager 0x0226ED68 -> cursor 0x0226FD20  x=1 y=20 visible=1
gUnitList        0x0227527C  stride 0xA8  slot 1 @ 0x02275328
  level 1  HP 18  at (1,20)
  PersonData -> pid 'PID_MARS'      JobData -> jid 'JID_LORD'
```

Names come from the game's identifier strings, so `Marth` is read rather than
hard-coded; unknown characters fall back to the raw `pid`.

## How to run it

```bash
wsl.sh fe-access units 5000     # boot to a map, print state + the commands
wsl.sh fe-run   units 5000      # the same run with the raw structure dump
wsl.sh fe-dump  cursor 6200     # the cursor experiment (7 snapshots)
wsl.sh black-check              # Pokémon regression — must still narrate
```

All scripts mirror the Windows source tree into WSL first, so a script can never
test a stale copy.

## What is NOT done, stated plainly

- **Terrain under the cursor.** The map buffers are located (three 0x400-byte
  candidates = 32×32, exactly the tile grid) but which is the terrain layer is not
  verified. The adapter says *"Terrain: unknown"* rather than guess — a wrong
  terrain name is worse than an admitted gap.
- **Allegiance.** Grouping works via the `Force*` pointer. The faction *number* is
  not located.
- **"Has acted".** `state1` bit 0 is documented as `US_ACTED`, but the live lead
  unit's `state1` is `0x04001002` — bit 12 ("not present") set on a unit that is
  plainly present. **So the state bits do not mean what the decompilation guesses**
  and the adapter must not claim acted/unacted until a before/after-acting
  experiment settles it. It currently prints the bit and does not rely on it.
- **Enemies.** Not exercised: the scripted run reaches the Prologue/first map where
  only player units exist. Cycling enemies needs a chapter with enemies in it.
- **Movement / attack range, objectives, pathing.** Not started; read the game's
  own highlight grids before reimplementing FE's rules.
- **Not wired into the app.** The adapter runs as a host binary. Wiring it to the
  SwiftUI shell through the C ABI is the next step after the state is reliable.
- **No RAM writes anywhere.** Read-only, by design.

## Next session, in order

1. **Terrain**: walk the cursor over visibly different tiles (plain/forest/fort)
   with a screenshot each, and diff the three 0x400-byte map buffers. Do not guess
   from a single sample.
2. **Acted state**: snapshot a unit before and after acting, across a turn
   boundary, and find which bit flips and resets.
3. **Reach an enemy chapter** so ally/enemy cycling is exercised rather than
   assumed — a plan that plays through the Prologue into chapter 1.
4. **Movement range**: select a unit (A) and watch for the highlighted-tile grid,
   rather than computing FE's movement rules ourselves.
5. Then wire `fe_access` to the app behind the adapter registry.

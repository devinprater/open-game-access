# How other games solved accessibility — transferable findings

Research compiled from how landmark accessible titles and existing accessibility
mods actually work, to inform which techniques Open Game Access should use and
which games are worth attempting.

Two source reports are preserved alongside this file:
- `open-game-access-research.md` — Mortal Kombat 1, The Last of Us Part I/II
- `game-access-mod-research.md` — Persona 4 Golden, Pokémon, BT2, DBZ, plus a
  cost-ordered ladder of reading techniques

## The two opposite answers to the reaction-time problem

**This is the most important finding in the whole report**, because it decides
whether an action game is approachable at all.

**Mortal Kombat 1 — make state a categorical audible token.** Do not slow the
game down. Instead turn every dimension of opponent state into a discrete sound
the player learns: a click track that speeds up and gets *quieter* as the
fighters close ("Distance between Fighters"), separate cues for health at
75/50/25/critical, end-of-line, ducking, blocking, and — the key one — **unique
sounds per hit level (high/mid/low/overhead)**. A high/low mixup, which is
normally a *visual* read with a few frames of window, becomes a named category
you can hear. On by default at EVO 2023.

**The Last of Us Part II — remove the reaction requirement in layers.** Auto-target
that locks onto offscreen enemies, aim-assist strength 1–10, slow motion,
enhanced dodge, enemies-don't-flank, invisible-while-prone (which doubles as
portable cover while the player re-orients), and Navigation Assistance that turns
the camera toward the golden path. Difficulty and access are kept as separate
axes.

**Rule for us:** an action game is tractable if its state can be **categorised
into sounds** (MK1's route) or if its timing can be **assisted away** (TLOU's
route). A game that is neither — requiring continuous visual tracking with no
categorical state — is a wall. This matches the "type D" wall already recorded in
the project's screening criteria.

## What was designed-in vs bolted on — and why it matters to us

| Game | Status | Evidence |
|---|---|---|
| The Last of Us II | **Designed-in** | TTS, high contrast, listen mode and navigation assistance prototyped by end of 2017, ~2.5 years before ship; first accessibility playtests summer 2018 |
| Mortal Kombat 1 | **Lineage** | Sound design cues go back to MK9 (2011); MK11 shipped the first in-game reader; MK1 layered the full suite |
| BT2 mod | **Bolted on, community** | Reads PS2 RAM over PINE; no official support |
| Pokémon Access | **Bolted on, community** | Lua RAM reads plus code hooks in an emulator |

⛔ **Navigation assistance and high contrast needed "a studio-wide effort across
every level"** (per Naughty Dog). Those are **per-level content annotation**, not
systems — which is exactly why a post-hoc reader cannot produce them. Open Game
Access is a post-hoc reader by construction, so it must choose mechanisms that
derive state from RAM rather than from hand-annotated level data. That is a real
constraint, not a limitation to wish away.

## The cost-ordered ladder of reading techniques

Cheapest and most reliable first. **Always try to read state at the highest rung
available** rather than scanning memory.

1. **Engine's own focus/UI tree** — e.g. Sparking Zero (UE Slate focus polling),
   Kakarot (UE4SS). Semantically correct by construction.
2. **A text pointer the game itself uses** — BT2's story text has a single pointer
   at `0x008C6244` to the string being drawn. **One address replaced ten hardcoded
   ones and reached four otherwise-unmapped screens.** Look for this before
   building a table.
3. **Code hooks** — Pokémon Access registers ~35 `memory.registerexec` hooks
   (`ROM_RENDER_TEXT`, `ROM_DRAW_MENU_CURSOR`, `ROM_BATTLE_YESNO`). Hooks fire
   exactly when the game draws, which is when the player needs to hear it.
4. **Offsets + snapshot diff** — Persona 4 Golden's fallback when Ghidra failed
   (Arxan hides xrefs; data in transient heap arenas): snapshot ~0.5 GB, make one
   input, re-snapshot, diff for a field stepping 0,1,2,3.
5. **HUD read from captured video** — BT2's minimap-dot objective tracking (blob
   tracking + Jacobian solve). Expensive, last resort, but it worked.
6. **Pre-rendered menu artwork** — BT2's menu labels are images with no text
   anywhere in the exe or `.pak`s, so they needed hand-authored tables and are
   **untranslatable**. Avoid; this is the worst case.

### Two techniques worth stealing outright

- **An in-mod hardware watchpoint.** P4G used DR0/DR7 with a vectored exception
  handler to catch what writes a value. This is the native-app equivalent of the
  mGBA exec-hook problem already documented in this project — and it is the
  proven answer to "a frame poll cannot observe execution".
- **Resolve, don't scan.** Kakarot's mod measured `FindAllOf` at ~115 ms and
  concluded resolution must be cached, not repeated per frame. A reader that
  scans every frame will stutter the game it is trying to make playable.

## Decompilations ARE the RAM map

Pokémon Crystal's constants were verified against `pret/pokecrystal`'s `.sym`
file — the decompilation symbols *are* the memory documentation. This is the same
finding that made Fire Emblem tractable (`Eebit/fe11-us` ships `symbols.txt` with
thousands of named addresses).

**Check for a decomp/disassembly before any scanning.** For DBZ specifically, a
randomizer repo documents ARM9 RE and the `.narc`/BDAT formats.

## What was never solved — worth knowing so we do not repeat it

- **MK1:** no adjustable speech rate; no traditional fighting-game notation;
  store/currency/rewards unnarrated; the Invasions sound language is unexplained.
- **TLOU Part II:** no cinematic audio description. **Part I has no captions for
  non-speech world audio**, which undercuts its own Clicker-warning cues.
- **TLOU Navigation Assistance silently refuses in some areas** — the player gets
  no feedback that the assist is unavailable.
- **BT2's menu text is pre-rendered artwork**, so menus are unreachable and
  untranslatable. A second BT2 mod skipped menus entirely as too expensive.

⛔ **A silent refusal is worse than a spoken one.** If a control cannot work in the
current state, say so — this project already applies that rule (the adapter
refuses rather than narrating uninitialised memory), and these failures confirm it.

## Principles to carry into Open Game Access

1. **Categorise state into sounds; do not slow the game.** Discrete audible tokens
   beat continuous narration for anything time-critical.
2. **Assist the timing when you cannot categorise the state.**
3. **Prefer reading the engine's own notion of state** over re-deriving it from
   pixels — the project's existing "model the game, not the screen" rule, now with
   external precedent.
4. **One good pointer beats ten hardcoded addresses** (BT2's story-text pointer).
5. **Hook the draw, do not poll for it** — hooks fire when the player needs them.
6. **Resolve once, reuse** — never scan per frame.
7. **Say when something is unavailable** rather than failing silently.
8. **Consult disabled players**, and treat their reports as findings: MK1's
   stage-interactable cues were added because a blind pro player asked.
9. **Do not promise what post-hoc reading cannot deliver** — navigation
   assistance and high contrast need per-level annotation a reader cannot supply.

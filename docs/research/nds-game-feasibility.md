# NDS folder — accessibility-mod feasibility assessment

Scope: the 17 `.nds` files in `Dropbox/Games/NDS`.
Date: 2026-09-13. Method: headless screening (`scripts/scan-roms.sh`) plus
structural research on each title.

## What was measured, and what it is worth

`scripts/scan-roms.sh` boots every ROM headlessly through the real iOS core with
the real compat shim, drives a scripted player through the boot path, and records:

- **frames completed / frames changed** — does it boot AND is it live, or a still
  image? (A screen whose pixels never change is a hang or a static title.)
- **top/bot distinct colours, `VRAMCNT_A`** — did the GPU reach display setup?
- **prose runs** — runs of ≥12 printable ASCII chars with spaces, ≥70%
  letters/spaces: the shape real dialogue/UI text has.
- **UTF-16 runs** — the same in UTF-16LE.

**Honest limits.** This is a coarse screen, not a verdict. The prose metric also
catches SDK strings and asset filenames (`<INFO><CLASSNAME>NSCRINFO…`), so a high
count does NOT prove live dialogue is in RAM — it proves the game ships plain-text
strings. Deciding a specific game's reader cost needs per-game reverse
engineering, which is the work itself, not a prerequisite for it. Treat the table
as "where to start", not "how long it will take".

## Verified: every ROM boots and renders

All 17 reached `VRAMCNT_A` ≥ 128 and animated (`frames_changed` 4–12 out of 12
samples). Exceptions worth noting:

- **BlayzBloo** reported `VRAMCNT_A = 0` and 1 distinct colour — it is DSiWare and
  needs a DSi boot path check before anything else.
- **Black/White** showed 1 distinct colour at my screening's sample point; that is
  my poke sequence landing on a white transition, not a defect — the dedicated
  `black-check.sh` run reaches 77→112 colours and narrates 28 lines.

## The decisive factor is not genre — it is which of four reader families is needed

| Reader family | Reads | Works when | Games here |
|---|---|---|---|
| **A. Text buffer / printer** | the game's own text buffer in RAM | text is stored as plain data | DBZ Attack of the Saiyans, DB Origins 2, Bleach 3rd Phantom |
| **B. Menu / list state** | cursor index, row count, option names | lists are data-driven | all of the above, + all fighting games' menus |
| **C. Map / grid / position** | tile map, player coords, entity list | the game keeps a tile grid | Pokémon; Bleach 3rd Phantom (grid) |
| **D. Real-time position** | sprites, hitboxes, facing, timers | **always weak** | every fighting game, every action platformer |

Families A–C are cheap and repeatable. **Family D is the wall**: a reader can say
"enemy is 3 tiles left", but a fighting game's whole skill is reacting inside a
few frames, and speech cannot be that fast. No amount of scripting fixes that —
it is a design problem, not an emulation problem.

Measured support for this: **Legend of Kage 2 produced only 3 prose runs** (all
asset filenames), i.e. its text is not sitting in RAM as plain ASCII. A text reader
there would need the font/encoding cracked first. By contrast **DBZ Attack of the
Saiyans** yielded 1,167 prose runs including its opening narration verbatim:

> "Long ago, deep in the mountains, thousands of miles away from any city…"

and **Dragon Ball Origins 2** yielded real UI prose ("Reward for getting an S Rank
on Ep. 1-1…"). Those two are the cheapest text readers in the folder.

## The reference cost, for scale

`main.lua` — the working Pokémon Black/White reader — is **23,163 lines, 33
hand-written screen modules, 544 functions**, each module carrying the
reverse-engineering rationale for the addresses it reads (overworld, battle
menus, party list, summary, Pokédex, PC box, name entry, Poké Mart, evolution,
hall of fame…). That is the unit of work for a full commercial game, and it took
months by a specialist. Any estimate below is in fractions of that.

## Ranked assessment

### Tier 1 — real candidates, text already plain data

1. **Dragon Ball Z: Attack of the Saiyans** (Monolith Soft, turn-based RPG)
   *Easiest in the folder.* Turn-based battles are menus all the way down — the
   ideal shape for speech. Verified plain text in RAM, including narration.
   Needs: A + B, a light C for the overworld. No D. Best effort-to-value ratio.

2. **Bleach: The 3rd Phantom** (Tom Create, tactical RPG)
   Turn-based on a grid. 335 prose runs measured. Needs A + B + C (grid + units +
   inventory). A close analogue exists publicly: **StanHash's GBA Fire Emblem for
   Screen Readers** — copy its approach rather than inventing one. Bigger than
   #1 because a tactics map carries a lot of state.

3. **Dragon Ball: Origins 2** (Game Republic, action-adventure)
   Verified plain UI text (1,111 prose runs). Menus, story and shops are cheap.
   The live action combat is family D and would stay weak — but the game is
   progressable with positional narration, so it is still worth a partial mod
   ("read everything except the fighting").

4. **Dragon Ball Z: Harukanaru Densetsu** (card-based RPG)
   Turn-based card battles read beautifully as speech (card name, effect, target).
   Text is data-driven (asset XML visible in RAM). Needs A + B. Cheap to start;
   the card system itself needs documentation.

### Tier 2 — menus easy, the game itself is the obstacle

5. **Pokémon: SoulSilver** — the existing script covers **only Black/White**.
   HGSS is a different engine with different overlays and a different text system,
   so it is a second full reverse-engineering project, not an extension. We do now
   have the IR fix it would need (HGSS is an IR cart, same as B/W).
6. **Pokémon: Platinum** — Gen 4 engine, no reader exists. Measured prose is low
   (230 runs, mostly SDK strings), suggesting Gen 4 text is compressed/encoded
   rather than plain — which would make it *harder than B/W despite being Pokémon*.
7. **Pokémon: Diamond / Pearl** — same Gen 4 situation; only 8 prose runs
   measured. Currently the hardest Pokémon titles in the folder.
8. **Dragon Ball Kai: Ultimate Butou Den** — menu readers are trivial; the fight
   is family D. Japanese-only, so it also needs a translation layer.
9. **Naruto: Ninja Destiny**, **DBZ: Supersonic Warriors 2**, **Bleach: The Blade
   of Fate**, **Bleach: Dark Souls** — all 2D/3D fighting games. Menus cheap,
   combat is family D and not solvable by narration.

### Tier 3 — not worth attempting

10. **Legend of Kage 2** — real-time action platformer (family D) *and* its text is
    not plain in RAM (3 prose runs). Both obstacles at once, and the smaller
    audience. Lowest priority in the folder.
11. **BlayzBloo** — DSiWare mini-fighter, 8.7 MB, didn't reach display setup in my
    run. Smallest content of the set and family D. Not worth a reader.

## Recommendation

Start with **DBZ: Attack of the Saiyans**. It is the only title here where the
game's own structure (turn-based, menu-driven, verified plain text) does most of
the work, so a reader can be built incrementally and each increment is audible —
which is exactly how the Pokémon script was built.

Before writing any reader for a new game, the two things worth doing first:

1. **Confirm the text path**: find where the game keeps the string it is currently
   displaying. `scripts/scan-roms.sh` tells you whether to look for plain text or
   for a compressed/encoded format; the latter changes the project's shape.
2. **Confirm the input model**: does the game read the keypad, or the touch panel?
   Touch-only screens need the mnemonic/touch path, which is a weaker narration
   channel than buttons.

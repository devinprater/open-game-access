# DBZ Another Road — naming the menu screens (2026-10-06)

Answers task 1: how a screen's name and its items are obtained. **The mechanism is found and
verified on the main menu.**

## The mechanism: the engine decodes the current screen's text into a RAM pool

The message tables on disc hold symbolic ids (`MSG_AR_CITY_00`), not display text, and the
real labels live inside `.AMT`/`.MGB` containers. But the game must **decode** that text to
draw it, and the decoded form lands in RAM as **UTF-16LE**.

Measured: on the main menu, the page immediately below the menu struct (`0x08BF0000`; struct
was at `0x08BFC758`) holds the screen's own label text **in item order**:

| pool order | text (verbatim from RAM) | item |
|---|---|---|
| 0 | `An original story that takes place after "Trunks Another Story."` | Another Road |
| 1 | `In this mode, you fight CPU opponents one after the other at the World Tournament.` | Arcade |
| 2 | `Participate in battles with various conditions and test your limits.` | Z Trial |
| 3 | `Battle other players in ad hoc mode. (Maximum 2 players).` | Network Battle |
| 4 | `Select an opponent and practice.` | Training |
| 5 | `Manage profile cards or view battle data.` | Profile Card |
| 6 | `Edit various settings and Save/Load.` | Options |

The order matches the menu order exactly, which is what makes it usable: **item index N's
description is pool entry N**. These are the game's own words, not ours.

The same page also holds `Nickname` / `Power Level` / `Money` (the info panel) and the
per-mode blurbs (`Set Arcade Mode difficulty.`, `Set the time for Arcade Mode.`, …), i.e. the
descriptions of the *submenu* screens too.

## Where the text lives (measured)

UTF-16LE strings ≥6 chars, filtered to those NOT present in the ELF (so they are runtime
output, not static constants):

| page | strings | contents |
|---|---|---|
| `0x08C00000` | 189 | mission objectives ("Dodge 4 times!", "Win with a Perfect!"), character titles ("Elite Warrior", "Prince of Destruction") |
| `0x08BF0000` | 188 | **the menu descriptions above** |
| `0x08A20000` | 133 | system messages ("Memory Stick Duo", "Cancel,", "Return to Main Menu.", "Keep game data?") |
| `0x09AC0000` | 82 | further UI text |

## Method that works

1. Locate the active-menu struct by its unique signature (see `dbzar-menu-routes.md`).
2. Read `menuId` (+0x04) and the screen id (+0x70).
3. Read the UTF-16LE pool in the page below the struct; entries are the screen's own labels
   in item order.
4. Filter out strings that also exist in the ELF — those are static data, not screen output.

## Honest limits

- **Entering a game mode leaves the menu system**: the struct disappears, so there is no
  menuId to name. That is expected (the mode has its own UI), but it means screen naming
  applies to menu screens, not to in-game screens.
- The pool's exact start offset varies with the screen; read the whole 64 KB page and take
  the runs, do not assume a fixed address.
- Character titles are present in the pool (`0x08C01D74` onwards) but have NOT been tied to
  per-character records yet — that is the character-select/unlockables task.

## Tooling

| script | purpose |
|---|---|
| `psp-ar-text.mjs` | dumps every UTF-16LE/ASCII run not present in the ELF |
| `psp-ar-pool.mjs` | groups those runs by 64 KB page, to find the UI pool |
| `psp-ar-page.mjs` | prints one page's decoded strings |
| `psp-ar-name.mjs` | read the struct + the pool for the current screen |
| `psp-ar-reader.mjs` | the reader; now carries the main menu's descriptions |

# DBZ Another Road — submenu item lists (task 2, 2026-10-06)

The submenus' items are now identified **from the game's own text**, captured out of the
decoded UI pool. Full dump saved at `dbzar-ui-strings.txt` (876 strings).

## How it was found

Two corrections to earlier assumptions, both measured:

1. **Submenus DO keep the menu struct.** Entering Options gave `menuId=6` (main menu is
   `menuId=1`), so the struct anchor works inside submenus too — better than the first
   estimate suggested.
2. **The text pool is RESIDENT, not per-screen.** The main menu and Options showed the
   *same* 174 strings. So the pool holds text for many screens at once, and a screen's items
   must be looked up in it, not assumed to be the whole pool. (Same trap as Dissidia's
   resident string pool.)

## Options (menuId 6)

| pool order | item | the game's description (verbatim) |
|---|---|---|
| 68 | Assign Buttons | `Change controls in a battle as desired.` |
| 70 | Sound | `Adjust the volume of music and sound.` |
| 72 | Save/Load | `Save/Load game data.` |
| 73 | Connection Style | `Select whether games can be interrupted by challengers.` |
| 75 | Voice Select | `Switch voices.` |
| 76 | Credits | `View credits.` |
| 77 | Screen Display | `Set the display for the Health and Ki Gauges.` |

⛔ **Pool order is the data order, not necessarily the display order.** An earlier live
session observed the first six on screen as: Assign, Sound, Save/Load, Connection Style,
**Screen Display, Voice Select** — so the last items differ from the pool order. The item
*vocabulary* is settled; the exact display order of the final entries still needs a live pass.

## Other submenus captured (item text, in pool order)

**Save/Load** — `Save game data.` · `Load game data.` · `Erase game data.` · `Enable/disable Auto-Save.`

**Sound** — `Adjust BGM volume.` · `Adjust SE and voice volume.` · `Return volume to default settings.` ·
`You can select Dragon Click.` · `You can listen to the music used in the game.`

**Language / Voice Select** — `Select Language` · `Voice Select` · (Japanese, English, Italian,
German, French, Spanish, Korean) · `Sound Test` · `Music Test` · `Switch languages.`

**Profile Card / records** — `Player Battle (Normal)` · `Player Battle (Custom)` · `COM Battle` ·
`Card Sharing` · `Buy materials for creating Profile Cards.` · `Edit your profile card for network battles.` ·
`View your battle records.` · `View the results of your network battles.` ·
`View the profile cards of your friends and rivals.` · `You can send and receive profile cards with others.`

**Battle stats page** — `Play Time` · `Success Ratio` · `Max Power Level` · `Current Title` ·
`# of Battles` · `# of Victories` · `Victory Ratio` · `Consecutive Wins` · `Max Consecutive` ·
`# Cleared` · `Challenge`

**Button names** — `Rush Attack` · `Smash Attack` · `Energy Attack` · `Guard` · `Gather Ki` ·
`Aura Burst` · `Default Settings`

**Arcade setup** — `Set Arcade Mode difficulty.` · `Set the time for Arcade Mode.` ·
`Set the rounds per match for Arcade Mode.`

## Correction: `+0x74` is a TIMER, not the selection

In Options, `+0x74` counted steadily DOWN by ~90 each step and never read a small item index
(4294967120 -> 4294967034 -> ...). Combined with the main-menu test (stayed 0), this settles it:
**`+0x74` is a countdown/timer field, not the selected item.** The selected item still needs the
ordinal hunt on a screen where the list is focused.

## Status

| need | state |
|---|---|
| screen identity | **solved** — `menuId` (1 main, 6 options), stable struct anchor |
| screen text / item vocabulary | **solved** — resident decoded pool, game's own wording |
| exact display order per submenu | **needs a live pass** (pool order is data order) |
| selected item inside a submenu | **needs the ordinal hunt** (the struct field is a timer) |

## Tooling

| script | purpose |
|---|---|
| `psp-ar-pool2.mjs` | dumps ALL UI strings not in the ELF -> `dbzar-ui-strings.txt` |
| `psp-ar-submenu.mjs` | enters a submenu and walks it, reporting struct state |
| `psp-ar-name.mjs` | read the struct + pool for the current screen |

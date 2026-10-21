# Proposal: an adapter game library in the OGA app

Status: proposal (Devin, 2026-10-18). The data half is implemented:
option (a) below shipped the same day — `coverage` and `notes` are
trailing members of `Core/adapter.h`'s Adapter struct, populated for
every registered adapter, with host-test checks that the two PSP
launch titles carry them (khbbs test, 34 checks). The app UI (library
list, game pages, verify-then-play flow) is not built yet. The flow
below is the app-level companion to the adapter registry: the
registry already knows which games have adapters; the app should
present that fact instead of opening on a bare ROM picker.

## The flow

1. **Library.** The app's home screen is a list of the games that have
   adapters, generated from the registry (`Core/adapters.cpp`): each
   entry shows the adapter's `display_name`, the exact supported
   `game_code`, and a one-line coverage summary (below). A generated
   list cannot drift from what actually ships.
2. **Load other game.** Below the list, one more entry: "Load other
   game (may be playable but not accessible)." It opens the existing
   ROM picker and runs the game with no adapter attached. The warning
   is part of the label, not a dialog to dismiss: a blind player
   should know before loading that there will be no speech or cues.
3. **Game page.** Tapping a library entry opens that game's page:
   - the per-game accessibility notes (below),
   - the supported version, named exactly (e.g. "USA retail v1.00,
     ULUS10505"),
   - a **Load Game** button, which opens the ROM picker filtered to
     that console's file types.
4. **Verify, then play.** After the user picks a file, the core
   identifies the game the way it already does (PSP: PARAM.SFO
   DISC_ID; the PPSSPP proof harness prints this as `gameid=`) and the
   app looks the code up with `find_by_game_code`:
   - **Match, and `attach()` accepts:** play begins automatically. No
     extra confirmation tap; the adapter's `ready()` then governs
     when speech starts, exactly as it does today.
   - **No match (wrong region/revision, or another game entirely):**
     say so plainly — "This file is ULUS10505F; the adapter supports
     ULUS10505" — and offer two buttons: "Load anyway without
     accessibility" and "Choose a different file." Never fail
     silently, and never attach an adapter to a game it did not
     verify.

## Per-game notes

Each game page carries notes written for that game alone, because the
accessibility shape differs per game: MK Unchained speaks menus,
character select, fight state and Konquest dialogue; Birth by Sleep
speaks its title/difficulty menus and field state (health, selected
command, gauge). The notes should cover, in plain language:

- **What is spoken or cued** — the surfaces the adapter covers, in the
  order a player meets them.
- **Game-specific controls** — the things a generic manual cannot
  know: in BBS, the ✕ Attack row sits outside the Command Deck cycle
  and △ executes the highlighted command; in MK Unchained, △ is the
  front-end's back button.
- **What is NOT mapped yet, stated plainly.** MK Unchained's QUIT GAME
  dialog is unmapped (the adapter still reports the pause row under
  it); BBS's camp menu is not mapped yet. A blind player planning
  around a gap needs to hear about it from us first, not discover it
  mid-game.
- **The supported version line** (above), so a player with the wrong
  pressing knows before they load, not after.

## Where the notes live

Two options:

- **(a) In the adapter, as trailing struct members** — e.g. a
  `coverage` one-liner and a `notes` text added after `cue_snapshot`
  in `Core/adapter.h`, following that member's precedent (trailing,
  may be null, existing initializers keep compiling). The notes then
  version with the code they describe, are reviewed in the same diffs,
  and the game page renders the shipped adapter's own words. The
  per-game memory docs under `docs/reverse-engineering/` stay the
  deep record; the notes are the player-facing summary of them.
- **(b) App-side content keyed by adapter `id`.** No ABI change, but
  two sources of truth: the list can promise coverage the shipped
  adapter does not have.

Recommendation: (a). The registry is already the single source for
identity (`id`, `display_name`, `game_code`); coverage claims should
live with it.

## Accessibility of the library itself

The library, game pages and picker are the app's own UI, so they are
held to the app's standard: every row reachable and labelled under
VoiceOver/TalkBack, the coverage summary part of the row's label, and
the verify result (match / mismatch / loaded-without-accessibility)
announced, not just shown. The auto-begin in step 4 must not skip an
announcement: "Kingdom Hearts: Birth by Sleep. Adapter attached."
before the game takes the screen.

## Open questions

- Coverage vocabulary for the list's one-liner: free text per adapter
  (honest, uneven) or a small fixed set ("Menus", "Menus + field",
  "Menus + battles")? Free text reads better; a fixed set compares
  better.
- Should "Load other game" remember the last unadapted ROM the way
  the library remembers adapted ones? Probably yes, listed separately
  under the same warning label.
- Multi-code adapters (a game whose regional codes share one adapter,
  if that ever proves safe) — the registry currently maps one code to
  one adapter; the page's version line is where a second supported
  code would be named.

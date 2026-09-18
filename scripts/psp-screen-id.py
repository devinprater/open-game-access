#!/usr/bin/env python3
"""psp-screen-id.py -- SUPERSEDED / DOES NOT DISCRIMINATE. Kept only as a marked trap.

WHAT IT TRIED
    Identify the game's current screen by which UI strings are resident in RAM, to remove the
    dependence on OCR (which cannot read Dissidia's stylized title/mode font).

WHY IT FAILS -- measured, not assumed
    Run against the live game it reported EVERY screen as a match:
        title / mode menu        MATCH   Museum, Shop, Options, Story, Battle, Arcade Mode, Lobby
        story / region map       MATCH   Level Progression, Story Mode, storypoint, ...
        pause menu               MATCH   Return to Game, Retry, Quicksave, ...
        battle (tutorial/help)   MATCH   HP attacks, EX Mode, EX Burst
        save / load dialog       MATCH   GAME DATA, Memory Stick, autosave, ...
        result screen            MATCH   Continue to Next Battle, DP

    The cause is that the UI text pools are STATIC and COMPREHENSIVE: 0x09D16A68 onward contains the
    whole menu string set, and further pools at 0x09A3Fxxx / 0x09A40xxx / 0x09E58xxx hold more, all
    resident at once. So a string's presence carries no information about the current screen. This is
    the same trap as doc section 25's "region assets are resident so story mode is loaded" -- the
    FOURTH instance of treating a resident string as evidence of state.

    It also means the EBOOT-resident test alone is insufficient here: these strings are NOT in
    EBOOT.BIN.dec (they are loaded into RAM by the resource loader), yet they are still not per-screen.
    "Absent from the ELF" is necessary but NOT sufficient for "indicates current state".

WHAT WOULD ACTUALLY DISCRIMINATE
    * A screen INDEX field maintained by the screen manager (not yet located -- and doc sections 27-31
      show the manager has no static anchor).
    * The on-screen BANNER read by OCR, which does work for plain-font banners: OCR reliably read
      "PAUSED" on the pause menu, and that IS a positive screen identification. Use OCR for banners,
      not for mode lists.
    * Frame structure (a static two-frame diff plus saturated-colour percentage) to distinguish
      "menu" from "scene", which psp-reach-menu.py already does correctly.

USAGE
    Do not use for identification. See the module docstring.
"""
import sys

if __name__ == "__main__":
    print(__doc__)
    print("This script is SUPERSEDED: it does not discriminate between screens.")
    sys.exit(1)

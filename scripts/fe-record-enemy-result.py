#!/usr/bin/env python3
"""Verify and document the enemy-detection result, then update the docs.

Run on Windows (python 3 is available there now) or in WSL; uses only stdlib.
"""
import pathlib
import re
import sys

doc = pathlib.Path("/mnt/c/Users/Devin Prater/open-game-access/docs/fire-emblem-shadow-dragon-memory.md")
if not doc.exists():
    doc = pathlib.Path("C:/Users/Devin Prater/open-game-access/docs/fire-emblem-shadow-dragon-memory.md")

text = doc.read_text(encoding="utf-8")

old_start = "- **Enemy reading is VERIFIED; enemy NAVIGATION is not.**"
idx = text.find(old_start)
if idx < 0:
    print("!! anchor not found — nothing changed")
    sys.exit(1)

# Find the end of that bullet block (next top-level bullet or heading).
rest = text[idx:]
end = len(rest)
for marker in ("\n- **Chapter / map identifier.**", "\n## "):
    j = rest.find(marker)
    if j >= 0:
        end = min(end, j)
new_block = """- **Enemy detection — VERIFIED END TO END** (no save file needed).

  The blocker was never the reader. It was input: the Prologue's "Waiting" tutorial
  popup swallows map input, so a plan that never presses B re-reads the popup forever
  and the enemy phase is never reached — which is why enemy counts stayed at zero for
  tens of thousands of frames and looked like "this map has no enemies".

  The popup's own text gives the fix: *"You can also press B or touch the B icon at the
  top of the screen to cancel the move."* Adding a **B press to dismiss the popup**
  after each move unblocks it. Enemies then appear at ~frame 5900 of `fe/plans/tutorial2.txt`.

  Live output, `core/fe_access.cpp` against the USA ROM with no save loaded:

  ```
    slot addr       Lv HP Mov   X   Y  act dead fac name
      1    0x02275324  1 18   0  11  20   0    0    0 Marth               PID_MARS           JID_LORD
      2    0x022753CC  1 16   0   8   3   0    0    1 PID_P01_GRA_SLDR    PID_P01_GRA_SLDR    JID_SOLDIER
      3    0x02275474  2 17   0   9   7   0    0    1 PID_P01_GRA_SLDR_1  PID_P01_GRA_SLDR_1  JID_SOLDIER
      4    0x0227551C  1 14   0  11  11   0    0    1 PID_P01_GRA_SLDR_2  PID_P01_GRA_SLDR_2  JID_FIGHTER
      5    0x022755C4  1 14   0  10  13   0    0    1 PID_P01_GRA_SLDR_3  PID_P01_GRA_SLDR_3  JID_FIGHTER

    Next enemy   -> PID_P01_GRA_SLDR_3, 14 HP, position 10, 13, 7.1 tiles away.
    Where am I?  -> Cursor 11, 20. Terrain category 13 (tile 14, verified). Unit here: Marth, 18 HP, unacted.
  ```

  Confirms in one run: four enemy units read by faction (`fac 1`), by their class
  identifier (`JID_SOLDIER` / `JID_FIGHTER`), with live HP and positions; `Next enemy`
  selecting nearest-first by distance from the cursor; and that enemies and the player
  are distinguished by the game's own faction number rather than by pointer identity.

  The earlier save-file work still stands as a second, independent confirmation (it
  populated forces 2 and 3 with 9 player / 22 enemy units); the Prologue route is the
  reproducible one because it needs no external data. See `docs/save-files.md`.
"""
text = text[:idx] + new_block + rest[end:]
doc.write_text(text, encoding="utf-8")
print("updated", doc)

# Open Game Access v0.6.5 — Fire Emblem: the save screen, and what was really wrong with it

**The short version:** Continue in Fire Emblem: Shadow Dragon was never broken — the test save
was. The save screen's highlighted slot is now read out loud, and the game's own code is what
found it. Everything here is host-proven.

## What this fixes

### 1. Continue worked all along; one of the two test saves does not

Two save files for the same game and region behaved completely differently. One loads and
reaches a real chapter map. The other makes Continue return to the main menu, whatever you press.

Every earlier "Continue does not work" test had used the second save, so the reader was blamed for
a save the game itself refuses. Both saves were sitting in a scratch directory the whole time; the
tracker had recorded them as unavailable.

Measured with the working save, the save path is:

    Continue -> Chapter Saves (a slot list: Endgame / Epilogue / NO DATA, with a PLAY TIME footer)
             -> a slot with map savepoints leads on to a Map Savepoints list
             -> the chapter loads: the map manager is valid, 15 units, Marth under the cursor

The confirm button was also cleared: pressing A on the row the cursor already sits on advanced
the game and changed 45 percent of the screen, so the button was fine and the row was the problem.

### 2. The save file screen is read out loud

That screen used to say "Not on a map yet". It now says which slot is highlighted:

    "Save file screen. Slot 2 of 3 is highlighted. Choose a save slot, then its savepoint."

The row is read from the game's own object model, not from a guess. Fire Emblem's save screen is a
C++ object called `MainSaveMenu`; the decompilation names the class and its vtable, and the game's
own header names the fields, so the highlight is a named field with a known width.

- Slot 0 is Endgame, the top row.
- Slot 1 is Epilogue, the middle row, and it is where the cursor starts.
- Slot 2 is NO DATA, the bottom row.

It stays flat for five thousand frames with no input, steps correctly on the direction pad, and
clamps at both ends. Reading the screen at those same frames shows the same three names in the
same order. The object is found by scanning memory, never by a fixed address.

### 3. One screen-state value covered two different screens

The slot list and the savepoint list after it both report the same screen id. The reader now tells
them apart by whether the save menu object is present, and says "Save point screen" on the second.
Before this it would have announced a slot number on a screen that has no slot list.

## Also in this build

- **A test harness that waits on game state, not on a frame.** Fire Emblem's main menu appears at
  a different frame on every boot — measured anywhere from frame 2500 to 4600 on the same build —
  so every fixed-frame test plan was a coin flip. Plans can now wait until the game is really on
  the screen they expect. An impossible wait times out and the run finishes rather than hanging.
- **A build script gap fixed.** The Fire Emblem harness's link step piped the compiler through a
  filter that printed nothing on failure and left the old program in place, so a run could report
  old behaviour as if it were new.

## What is not proven

**No part of this build has been run on a phone.** Every finding above was measured on a host
harness driving the emulator, not on hardware. The iOS and Android builds are byte-checked for
platform, lack of signature, and reader contents, but they have not been launched on a device.

## What is still open

- The Map Savepoints list itself is named but its rows are not read.
- Battle preparations on the Hard route turned out not to exist — that route goes straight to the
  Chapter 1 map. A prep screen may exist on a mid-game Continue path.

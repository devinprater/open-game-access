# Open Game Access v0.6.8 — Dragon Ball Z: Another Road, the story mode where you fly and defend cities

**The short version:** Another Road's story mode is not a fighting game. You fly around a map and
keep your cities alive while enemies attack them, and until now none of that was readable. This
release adds a reader for that mode and a radar cue, so a blind player can tell which city is in
trouble, how many are left, and which way to turn.

## What this adds

### A reader for the field mode

The reader speaks the city in the worst shape first, how many cities are critical and hurt, how many
enemies are alive, the full city list worst-first, and the nearest city in plain distance bands. It
also announces when a city crosses one of the game's own damage thresholds, so you are told when
something changes rather than having to ask again.

### A radar cue you can steer by

Field mode has no targeting, so a centred beep is useless: the player has to fly. The cue therefore
carries a **bearing**. It is a single ring over enemies, then cities, then allies, and an empty kind
is skipped rather than dead-ending. Direction is carried by stereo placement, distance by how fast
the cue pulses, and identity by its tone, so an enemy, a city and an ally each sound different.

The game stores no facing anywhere, so the reader measures one from your own movement. When you have
not moved recently the heading is treated as stale and the cue centres instead of pointing you
confidently down a direction you have since left.

### Two bugs found by getting the game into the field

- **City presence was exactly inverted.** A city in play carries the id `0x0000` and an empty slot
  carries `0xFFFF`, so the obvious test, "id is not zero", hid the first city on every map. Presence
  is now capacity, which is what actually tells the two apart.
- **The reader used to narrate a dead mission over a live battle.** The mode flag stays set for the
  whole of Another Road, through battles, menus and result screens, so it cannot answer "is the
  field running". The reader now requires a second, stronger signal: a pointer that exists only
  while the field's own tasks are resident.

## Measured

On the host, against synthetic memory for the reader and against the live game for the addresses:

    adapter host tests (field reader, radar, liveness gate)  55 checks, 0 failed
    mutations of those checks                                 3 of 3 detected
    printed HUD percentage vs the reader, at full health      3 printed = 3 read = 100%
    printed gauge bar vs the reader, mid-scale                90.5% against 90.0%

The gauge measurement uses the game's own printed bar as the second opinion, which is how the two
disagreed loudly enough to be caught rather than believed.

## What is still not proven

**Nothing here has run on a phone.** Every result above is host-side. The reader has never been
attached to a PSP game on iOS or Android, and the radar cue has never been heard against live
steering on a device. The addresses it reads were measured live, but only through a desktop emulator.

The cue's sound was rendered over real gameplay audio on the host to check that the encoding is
audible against the game's own sound. That proves the encoding, not the phone's playback.

## Carried over from v0.6.7

The emulated-frame button press fix, the Game Boy sound fix and the Game Boy Color cartridge fix are
all included, and were unchanged by this release.

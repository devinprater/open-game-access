# Open Game Access v0.6.6 — Game Boy sound, and the numbers that should not have been there

**The short version:** On iOS, Game Boy, Game Boy Color and Game Boy Advance games were completely
silent, and the cause was one line that set the volume to zero. A Game Boy Color cartridge was also
being read as a Game Boy Advance cartridge, which is where the "bunch of numbers" came from. Both
are fixed, and both are measured on the host with real cartridges.

## What this fixes

### 1. Every Game Boy game was silent on iOS, and the audio path was never broken

The sound was reaching the app the whole time. mGBA multiplies each sample by a master volume on
the way out, and the host had left that volume at zero, so every sample became nothing:

    sampleLeft = (sampleLeft * audio->masterVolume * 6) >> 7;

The volume comes from the emulator options, which the host was building with `memset` and never
setting. The game was playing its music correctly underneath. The fix is one line that sets the
volume to the value mGBA itself uses by default.

Measured on two real cartridges, before and after:

    Pokémon Crystal (.gbc)   before: 1,649,944 frames read, 0 nonzero, peak 0
                             after:  1,649,944 frames read, 2,570,978 nonzero, peak 22,652
    Pokémon FireRed (.gba)   after:  1,653,518 frames read, 3,145,244 nonzero, peak 24,528

This applies to every Game Boy, Game Boy Color and Game Boy Advance title on iOS. The reader's
positional sound cues were a separate path and already played.

### 2. The "bunch of numbers" was a Game Boy Color cartridge read as a Game Boy Advance one

The native Game Boy Advance reader accepted a Game Boy Color cartridge, because such a cartridge
has no game code and the reader was written to accept that and let the reader identify itself. Its
addresses are Game Boy Advance addresses, so it read whatever happened to be at those places in a
Game Boy Color cartridge. That is the string of numbers.

The emulator already knows which console it loaded, so the fix is a console check before the reader
attaches. Measured: Crystal now refuses cleanly and logs the reason, and Pokémon FireRed still
attaches and reads normally.

## What the Android app is, and is not

The Android app was never affected by either bug. It has no native Game Boy Advance reader at all,
and it reads the console correctly through the Lua reader, so it never spoke those numbers. It also
has no path that plays the emulated console's own sound, so Android Game Boy games are still silent.
That is a missing feature there, not a regression, and this release does not change it.

## What is still not proven

Both fixes are measured on the host with real cartridges, not on your phone. The Game Boy sound in
particular is worth listening for on device, since that is the report that started this.

## How this was found

The first attempt fixed a genuinely broken thing — the audio reader slot was empty, so the samples
were never collected at all — and then proved it by showing that 2,048 audio frames came back. That
proof was worthless: a buffer full of zeros also returns 2,048 frames. The check built on it passed
while the app was still silent, and it passed again with the volume put back to zero.

The check now measures the sound itself, by peak volume and how many samples are not silence. It
was proved by putting each bug back and confirming the check fails.

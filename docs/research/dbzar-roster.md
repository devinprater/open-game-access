# DBZ Another Road — character roster and unlockables (task 3, 2026-10-06)

## The roster: 24 characters, FOUND

A pointer table at RAM **`0x08A243F0`**, stride 4, one pointer per character into the
contiguous name block at `0x08A2558A`. Read live and verified (each pointer was followed and
the string read back):

| # | character | # | character |
|---|---|---|---|
| 0 | Goku | 12 | Broly |
| 1 | Teen Gohan | 13 | Gotenks |
| 2 | Gohan | 14 | Gogeta |
| 3 | Vegeta | 15 | Vegito |
| 4 | Trunks | 16 | Pikkon |
| 5 | Krillin | 17 | Janemba |
| 6 | Piccolo | 18 | Future Gohan |
| 7 | Frieza | 19 | Majin Buu |
| 8 | Android #18 | 20 | Super Buu |
| 9 | Cell | 21 | Dabura |
| 10 | Kid Buu | 22 | Bardock |
| 11 | Cooler | 23 | Future Trunks |

Addresses: entry N is at `0x08A243F0 + N*4`; the name it points to is at `0x08A2558A` onward.
The table sits inside a larger pointer array (other UI strings are in the same run), so index
by the name block rather than assuming the table starts at the first string.

## The unlock mechanism exists, and it is the game's own wording

Immediately after the roster names, the same text block holds the unlock messages:

```
"%s has become available!"
"%s has reached a new transformation stage!"
"A new scenario is now available in Another Road."
```

So there is a per-character "became available" event. The `%s` is the character name, which
means the game announces unlocks by name — good news for a reader: the text to speak exists.

## The title table: 52 entries

`0x08C01D68` onwards, 52 titles in address order (character titles used on profile cards):

```
Elite Warrior, Low-Class Warrior, King of Darkness, Prince of Destruction, Prince, Majin,
Demon, Cooler's Armored Squad, Hanger-On, Warrior of Rage, Warrior Race, Elder, Great Elder,
Guardian, Supreme Kai, King Kai, Great Kai, Ogre, King, Strongest on Earth, Android,
Bio Warrior, Legendary Super Saiyan, Legendary Warrior, Perfect Form, Aloof Warrior,
Future Warrior, Z Fighter, Super-Genius, Messenger of HFIL, Champion, Crybaby, Great Ape,
First Form, Second Form, Third Form, Final Form, Draconian, God of Dreams, Saibaman Class,
Yamcha Class, Worthless Maggot, Tourist, Golden Warrior, Warrior of Justice, In Training,
Hooligan, Vulgar Army Corps, Great Warrior, Coward, Proud Warrior, Serves Frieza, …
```

## What is NOT found yet, stated plainly

**The per-character unlock FLAG.** Two things are ruled out:

- **No static reference in the ELF.** A search of the whole listing for an immediate near the
  table's ELF address (`0x1E03F0`) returned **0 sites**, and a search for the unlock message
  strings returned **no references either**. The table is therefore **built at runtime**, so
  the code that fills it cannot be found by following a static address.
- **No simple parallel flag array.** Scanning the 0x400 bytes on either side of the table for a
  24-entry run of small values found nothing (the only hits were at bogus out-of-range
  addresses from a faulty scan window — recorded so it is not mistaken for a result).

The remaining routes, in order of promise:

1. **Read the save file.** Unlocks must persist, so a byte array or bitfield per character is in
   the save data. That is a bounded, offline job — no emulator needed.
2. **Watch the roster rows on the character-select screen.** A locked row is drawn differently;
   diffing RAM across a locked/unlocked pair would isolate the flag. Needs the character-select
   screen reached and one known-locked character.
3. **Find the runtime builder.** Since the table is filled at runtime, find what allocates or
   writes it by watching for the write (a write watchpoint on `0x08A243F0`), rather than by
   static analysis.

## Also observed (useful for later)

- The main menu is reachable via `start` x3 then `cross` x2-3 (title -> Load screen -> main
  menu). Blind `cross`-mashing enters Another Road's story intro, from which there is no cheap
  exit — navigate deliberately with a screenshot after each press (`psp-ar-nav.mjs`).
- `start` does not pause inside Another Road; the pause confirm's options are
  "Cancel," / "Return to Main Menu."

## Tooling

| script | purpose |
|---|---|
| `psp-ar-chars.mjs` | finds title-like strings and the string neighbourhood |
| `psp-ar-roster.mjs` | finds the roster pointer table and reports the block |
| `psp-ar-roster2.mjs` | dumps the table + neighbourhood, resolves each pointer to a name |
| `psp-ar-charsel.mjs` | drives toward character select and hunts a parallel flag array |
| `DisRoster.java` | searches the ELF for references to the roster table and unlock strings |

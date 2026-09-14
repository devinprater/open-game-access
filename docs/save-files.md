# Save files for Fire Emblem: Shadow Dragon

## What is here

- `fe11-usa-finalboss.sav` — 262,144 bytes (256 KB), **USA** (`YFEE` at offset 0).
  GameFAQs save #26484 ("Before Final Boss", by Xenosagaxxx).
- `fe11-usa-ch10.sav` — 262,144 bytes, padded from a 245,760-byte extraction of
  GameFAQs #20257 ("Completed Chapter 10"). **Currently does not work** — see below.

Both were fetched from `gamefaqs.gamespot.com/ds/943695-fire-emblem-shadow-dragon/saves`.

## `fe11-usa-finalboss.sav` — loads, and proved enemy READING

This one is clean: the `.duc` container appends a DeSmuME footer that says, in plain
ASCII, *"Snip above here to create a raw sav by excluding this DeSmuME savedata
footer"*, so the raw save is simply the first 262,144 bytes. No tool needed.

Loading it populated a real enemy force for the first time:

```
[save] loaded fe11-usa-finalboss.sav
faction 2 (player, scenario)  units=9     <- Marth's army
faction 3 (enemy, scenario)   units=22    <- the enemy force
faction 4 (unassigned)        units=60    <- empty reserve slots
```

That confirms the faction ids inferred from the decompilation (`disposition.cpp` call
sites): **2 = player, 3 = enemy** for scenario forces, alongside 0/1 for live-map forces.

**But it cannot finish the verification.** Despite the title, it is *after* the final
boss: booting it plays the ending cutscene (screenshots: Nyna *"Well done, Marth..."*
→ Campaign Summary) and `gMapStateManager` is **never** valid — checked at
3000/3600/4200/4600/5000/5600/6200/7000/8000. No map is entered, so `Next enemy`
correctly answers "Not on a map yet".

## `fe11-usa-ch10.sav` — does NOT work; layout misunderstood

Downloaded as `fire-emblem-shadow-dragon.20257.duc`. It is an **ARD Savegame**
container (`ARDS000000000001`, "Converted Savegame") whose payload is **`YFEE` USA**,
not `YFEP`.

Both naive extractions were tried — `d[0:0x40000]` and `d[len-0x40000:]` — and each
produced a file that **loads without error and restores nothing**: the game boots to
the title as if no save were present. That silent, error-free failure is the trap.

What the container actually holds:

```
total 262,644 bytes = 0x401f4
  "ARDS000000000001" + Converted Savegame header ...... first 0x41f4 bytes
  then 240 blocks of exactly 1024 bytes, EACH starting "YFEE"
    block 0 @0x41f4, 1 @0x45f4, 2 @0x49f4, ...  (1024-byte spacing)
    all 240 distinct, each padded to its boundary with FF
```

240 distinct 1024-byte records is **not** a flat 256 KB battery save, and the game code
repeating in every record means these are per-record headers rather than one save
image. So `header + raw save` is the wrong model, which is exactly why padding to
256 KB restored nothing.

**Do not fix this by guessing another offset.** Either convert with a tool that
understands the ARD container, or use a save already in a raw format.

## The European attempt, kept as a lesson

`shadow-dragon_save.rar` from fireemblemwod.com fails for two independent reasons:

1. **Wrong region** — its embedded strings are `YFEP` (Europe); this ROM is `YFEE` (USA).
2. **Wrong container** — No$GBA `.dss` (magic `NocashGbaBackupMediaSavDataFile`), which
   wraps the SRAM rather than being raw. `poke_load_rom()` wants a raw `.sav`.

## Using a save

    bash scripts/fe-save-census.sh fe11-usa-finalboss.sav 6000 units

`poke_load_rom()` takes the save as its 3rd argument; the harnesses read `SAVE` from
the environment. Note a save still starts at the **title screen** — input is needed to
choose CONTINUE — so always pair it with a plan.

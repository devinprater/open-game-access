# Which of Devin's games already have a port

Checked 2026-10-12. **Nothing was built, installed or run.** Every "a port exists" line is a
repository's own claim, cited with its stars, licence, creation date and last push so its
age is visible. Progress percentages come from the curated list's own decomp.dev column.

## How this was checked, and its limits

- **The library came from the filesystem**: `C:\Users\Devin Prater\Dropbox\games`, 15 platform
  folders, about 6,030 files. Counts: NES **5,829** (a full GoodNES-style set, so effectively
  every NES game), NDS 32, GBA (with GB/GBC) 35, PSP 22, PS2 20, PS1 48 files (~15 games),
  SNES 12, N64 10, 3DS 10, GameCube/Wii 5, PS3 5, Dreamcast 4 games, Genesis 1, plus BIOS
  folders and one loose PS3 EBOOT.
- **Port status came from**, in this order: two curated lists
  (`CharlotteCross1998/awesome-game-decompilations`,
  `SubmitGame/awesome-decomp-games` — the latter carries a rating and a decomp progress
  column), a GitHub sweep of 225 repositories matching recompilation and decompilation
  queries, and then the README of each candidate that mattered.
- **Three different things get confused in the wild, so they are kept apart here:**
  1. **A recompilation port** — the game runs natively, no decompilation needed.
  2. **A decompilation project** — the source is being reconstructed; a *port may not exist*.
     A high decomp percentage is not a playable game. Paper Mario: The Thousand-Year Door is
     12% decompiled and has no port.
  3. **An existing port** — something a player can download.
- ⛔ **The lists mark many entries with ⚠️, and the scene's newest work is AI-assisted,
  single-author and days old.** Both KartPad and Wiicompiled disclose AI assistance; KartPad
  discloses it in its own README. Judge by running, never by stars or prose.

## The games Devin owns that run natively today

**On his phone, which is the rare part:**
- **Mario Kart Wii → KartPad** (`chrissotraidis/kartpad`, 655★, GPL-3.0, created 2026-08-28,
  pushed 2026-10-12). Native **Android ARM64/Vulkan, iOS, iPadOS, macOS**, tvOS preview.
  Apple builds via PadMint from his own disc; Android is a free download. Touch controls,
  motion steering, controllers, optional Retro Rewind content. Discloses substantial AI
  assistance.

**Desktop, immediately:**
- **The Legend of Zelda (NES) → `mstan/LegendOfZeldaNESRecomp`** (27★, "believed 100%
  playable", Windows x64 and a Linux AppImage, launcher with a Mods screen). **This is the
  exact game our Zelda 1 Access reader speaks.**
- **Pokémon Emerald, FireRed, LeafGreen, Ruby, Sapphire → the `gbarecomp` family**
  (`EmeraldRecomp` 30★, `FireRedLeafGreenRecomp` 20★, `RubySapphireRecomp` 15★, all created
  2026-06-22). "Boots through the BIOS intro to the title screen and into gameplay"; the GBA
  BIOS is recompiled and executed rather than stubbed; save states; `.gbamod` mod packages;
  the runtime supports Android (gyro tested on a Galaxy S22 Ultra).
- **Dragon Ball Z: Buu's Fury → recompiled** (23★); **Legacy of Goku II → recompiled** (5★).
- **Mario Kart: Super Circuit → recompiled** (65★, optional 60 FPS and adaptive widescreen).
- **Super Mario Advance 2 and 4 → recompiled** (10★, 7★).
- **Paper Mario (N64) → PaperBoat** (453★, Windows/Linux/macOS) and `Paper-Mario-ReCut`
  (306★, MIT); the decompilation behind them is essentially finished (1.6k★).
- **Mario Kart 64 → SpaghettiKart** (HarbourMasters, 2.4k★, Windows/Linux/macOS, needs the
  US `.z64`).
- **Super Mario 64 → `sm64ex` (1.9k★), Ghostship (817★), sm64coopdx (1.4k★)**, plus a
  "Super Mario 64 Recompiled" page.
- **Ocarina of Time → Ship of Harkinian** (`HarbourMasters/Shipwright`, 5.6k★, Windows,
  Linux, macOS; **no iOS build**). `Zelda64Recomp` (7.4k★) lists OoT as "soon", and
  `coccofresco/TriAevum` (474★) is an experimental runtime for the **3DS remaster**.
- **Dr. Mario 64 → `drmario64_recomp_plus`** (150★) and a 100% decompilation (80★).
- **Pokémon Stadium 1 and 2 → `mstan/PokemonStadiumRecomp`** (121★) plus the `pret`
  decompilations (210★, 57★).
- **Mario Party 3 → `Rainchus/MarioParty3-Recomp`** (18★); Mario Party 1 and 2 have active
  decompilations (81★, 51★); Mario Party 4's is complete.
- **Super Smash Bros. Melee → the decompilation is 100% complete** (2.4k★), and
  `Hero88go/melee-unlocked` (103★) is a source port for Windows.
- **Fire Emblem: The Sacred Stones → 100% decompiled** (225★ and a 19★ fork); no port yet.
- **Kingdom Hearts 358/2 Days (NDS) → 100% decompiled** plus `khdays-port` (64★, experimental
  native runtime).
- **Dragon Quest IX (NDS) → decompilation exists** (`DQIX/dqix-decomp`, 45★). **This is our
  own DQ9 adapter's game.**
- **Fire Emblem: Shadow Dragon (NDS) → decompilation 7.3%** (`Eebit/fe11-us`, 16★). Our FE11
  adapter's game, and too early to be useful.
- **Spyro the Dragon and Year of the Dragon → decompilations** (175★, 25★).
- **Chrono Cross → decompilation 12.5%** (71★).

## Nothing found

- **PSP (22 games)**: no port or decompilation for Dissidia, Dissidia 012, Crisis Core,
  Kingdom Hearts Birth by Sleep, Tekken Dark Resurrection, Tekken 6, Soulcalibur Broken
  Destiny, Danganronpa, Steins;Gate, Metal Slug Anthology, Samurai Shodown Anthology,
  Dragon Ball Z Shin Budokai / Another Road / Tenkaichi Tag Team, Final Fantasy Tactics War of
  the Lions, Soulcalibur, Spider-Man Friend or Foe, Unbound Saga. **The one exception is God
  of War: Ghost of Sparta, which he owns and which is one of the two titles
  `psp-web-recomp` already brings up in a browser** — the only recompiled PSP content that
  exists.
- **PS2 (20 games)**: nothing for Tekken 4/5, Soulcalibur III, Odin Sphere, Mortal Kombat
  Armageddon / Deadly Alliance / Shaolin Monks, Bloody Roar 4, DOA2 Hardcore, Capcom vs SNK 2,
  Super Dragon Ball Z, Urban Reign, X-Men Next Dimension, Space Channel 5, Infinite World,
  Budokai 1/2/3, Budokai Tenkaichi 2/4. The nearest relatives are Xbox 360 and PS2 projects
  for **other** Dragon Ball titles: `novapowers0/DBZ-Budokai-3-HD-Collection` (145★, MIT,
  ReXGlue — the Xbox 360 HD remaster, which is the port the **Budokai 3 Access** screen-reader
  mod hooks), `z3xox/BT3-Recomp` and `z3xox/Tenkaichi3Decomp` (Tenkaichi 3).
- **3DS (all ten games)**: nothing.
- **PS3 (Raging Blast 1/2, Injustice, Mortal Kombat, Under Night)**: only
  `WistfulHopes/RB2` (36★, Raging Blast 2, Xbox 360). PS3 recompilation is a stub repo.
- **Dreamcast (Power Stone 1/2, Plasma Sword, Psychic Force 2012)**, **Genesis (Ultimate MK
  Trilogy hack)**, and **SNES (Chrono Trigger, Final Fight 1/2/3, Killer Instinct, Knights of
  the Round, Power Instinct, Rushing Beat, Sonic Blast Man 1/2, Kunio)**: nothing, except a
  1★ SNES recompiler for Chrono Trigger (`ThisIsAkill/ct-recomp`, MIT, bring your own ROM) —
  a tool, not a port.

## The most interesting structural finding: the R.A.I.D. family

`mstan` maintains **one static recompiler framework per console**, each with per-game
repositories that ship ROM-free releases and take the player's own ROM:

- **`nesrecomp`** (111★) — Super Mario Bros., Duck Hunt, Dr. Mario, **The Legend of Zelda**,
  Metroid, Faxanadu, Yoshi, Yoshi's Cookie, Mega Man 3, Gumshoe. Mappers 0, 1, 4 and 66 are
  supported; UxROM, CNROM, AxROM and MMC2 are **not yet**.
- **`gbarecomp`** (47★) — DBZ Legacy of Goku I and II, Buu's Fury, Mario Kart Super Circuit,
  WarioWare: Twisted! (Windows and Android gyro), Super Mario Advance 2 and 4, FireRed /
  LeafGreen, Ruby / Sapphire, Emerald, Minish Cap, Mega Man Zero. Recompiles the GBA BIOS,
  supports `.gbamod` packages and save states, and has Android support in the runtime.
- **`snesrecomp`** — Super Mario World, Zelda: A Link to the Past (47★), Mega Man X (179★,
  Windows/macOS/Linux).
- **`psxrecomp`** — Tomba 1 and 2, Ape Escape, Mega Man X4/X5/X6, Xenogears (community).
- **`smsggrecomp`** — Sonic the Hedgehog (SMS), Sonic Blast (Game Gear).

Why this matters to us: adding a game is a `game.toml` plus a repository
(`docs/GAME_PROJECT_SETUP.md`), releases carry no game data, and there is a Mods system with
hooks. **That is the closest thing in this scene to our own model — one framework, many games,
readers as data** — and it is desktop-first, with Android appearing in places and **iOS
nowhere except KartPad and BlueWake**.

## Our own games, lined up

- **Zelda 1 (NES)** — we have a reader (Lua, under Mesen) *and* the scene has a recompiled
  port of the same game with a Mods screen. Nobody has put a reader into the port. **This is
  the cheapest real experiment in the whole survey.**
- **Dragon Warrior (NES)** — we have a reader; the scene has only a disassembly (88★). Dragon
  Warrior is MMC1, which `nesrecomp` supports, so a port is possible with the framework and
  the disassembly as the map.
- **Fire Emblem: Shadow Dragon (DS)** — our adapter; decomp at 7.3%, no port. Nothing to take.
- **Dragon Quest IX (DS)** — our adapter; decomp exists, no port.
- **Dissidia (PSP)**, **DBZ: Attack of the Saiyans (DS)**, **DBZ: Shin Budokai Another Road
  (PSP)** — our adapters; no port anywhere.
- **Fire Emblem: The Sacred Stones (GBA)** — 100% decompiled, no port.

## Verdict

1. **Today, on his phone**: KartPad (Mario Kart Wii) — iOS and Android, his own disc.
2. **The experiment worth running**: point the Zelda 1 reader work at
   `LegendOfZeldaNESRecomp`, the one game where we own the reader and the scene owns the port.
3. **The pattern to watch**: the R.A.I.D. family, because it is a framework with per-game
   repositories and hooks, which is the shape a reader plugs into.
4. **The gap**: nothing in his 3DS, PS2 or PSP library has a port, and almost nothing in this
   whole scene runs on iOS.

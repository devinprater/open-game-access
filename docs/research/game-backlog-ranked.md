# Game backlog — every system, ranked easy to hard

Complete inventory of `Dropbox\Games`. Supersedes the NDS-only first pass.

Ranking applies `docs/research/how-other-games-did-it.md` and the project's
screening criteria.

## ⛔ The core outranks the game

**A game we cannot boot is harder than any game we can.** An adapter cannot be
written or tested without a running core, so the system is the first gate and it
beats everything the game itself would suggest.

| System | Core status | Verdict |
|---|---|---|
| **NDS** | melonDS integrated, boots, Lua path verified | **workable now** |
| **GB / GBC / GBA** | mGBA verified (identification, hotkeys, footsteps) | **workable now** |
| **PS1** | **no PS1 core**; BIOS present (`scph5500/5501/5502.bin`) | blocked |
| **PS2** | **no PS2 core**; BIOS + DVD firmware present | blocked |
| **PSP** | no core | blocked |
| **PS3** | no core, and no realistic path | out |
| **SNES** | no core | blocked |
| **3DS** | no core | blocked |
| **GameCube / Wii** | no core (Dolphin not integrated) | blocked |
| **N64** | no core | blocked |
| **Dreamcast** | no core; `dc_boot.bin`/`dc_flash.bin` present | blocked |
| **Genesis** | no core | blocked |

So the honest shortlist remains **NDS and GB/GBC/GBA**. Everything else is gated
behind core work unrelated to accessibility — see the note at the end for why
that may be worth doing for PS1/PS2 specifically.

## Reader families

- **(A) Text buffer/printer** — dialogue is readable state. Easiest.
- **(B) Menu/list state** — cursors, selections, counts. Moderate.
- **(C) Map/grid/position** — needs a coordinate model. Moderate–hard.
- **(D) Real-time sprite/hitbox** — the player must react in frames. **A wall.**

⛔ No adapter fixes a type D game. MK1's answer (state as a categorical sound)
is a *design* solution a post-hoc reader can only approximate.

---

## Tier 1 — Easiest (turn-based, documented, core available)

| Game | System | Family | Notes |
|---|---|---|---|
| **Pokémon Black / White** | NDS | A+B+C | Done. Reference implementation. |
| **DBZ: Attack of the Saiyans** | NDS | A+B | Turn-based; party HP/Ki/AP addresses public in 3 places. Menus + BDAT text encoding unknown. |
| **Chrono Trigger** | NDS | A+B+C | Turn-based, exhaustively documented. |
| **Dragon Quest IX** | NDS | A+B | Turn-based, menu-driven, well documented. |
| **Final Fantasy Tactics A2** | NDS | A+B+C | Tactical but turn-based. |
| **Radiant Historia** | NDS | A+B | Turn-based, grid combat. |
| **Fire Emblem: Shadow Dragon** | NDS | A+B+C | Done — terrain, movement, enemies, chapter id. |
| **Pokémon Diamond / Pearl / Platinum** | NDS | A+B+C | Same family as B/W; gen-4 offsets differ but techniques transfer. |
| **Pokémon SoulSilver** | NDS | A+B+C | As above; GB/GBC ancestors already have readers. |
| **Mario & Luigi: Bowser's Inside Story** | NDS | A+B | Turn-based; timing minigames are the friction. |
| **Mario & Luigi: Partners in Time** | NDS | A+B | As above. |

## Tier 2 — Moderate (turn-based but undocumented, or mild real-time)

| Game | System | Friction |
|---|---|---|
| **Advance Wars: Dual Strike / Days of Ruin** | NDS | Turn-based tactics; needs a full map/cursor model. |
| **Fire Emblem: The Sacred Stones** | GBA | Same family as Shadow Dragon and the GBA core works — but the GBA readers are Pokémon-specific, so a new adapter is needed. |
| **Tactics Ogre: The Knight of Lodis** | GBA | Tactical, dense menus. |
| **Mario & Luigi: Superstar Saga** | GBA | Turn-based; dodging is a timed press. |
| **Final Fantasy Tactics: War of the Lions** | PSP | Easy on merits (turn-based tactics) — **blocked by no PSP core.** |
| **Dissidia / Dissidia 012** | PSP | Menu-heavy; combat is real-time. No core. |
| **Crisis Core: FFVII** | PSP | Action RPG. No core. |
| **Kingdom Hearts: Birth by Sleep** | PSP | Action RPG. No core. |
| **Bleach: The 3rd Phantom** | NDS | Tactical RPG. |
| **DBZ: Harukanaru Densetsu** | NDS | Card RPG — turn-based, but the cards are the whole reader job. |
| **Odin Sphere** | PS2 | Action RPG. No core. |
| **Steins;Gate** | PSP | Pure visual novel — *would be the easiest game here* if a PSP core existed. |
| **Chrono Cross** | PS1 | Turn-based JRPG, well documented. Blocked by no PS1 core. |
| **Mega Man Legends** | PS1 | Action-adventure. No core. |
| **Danganronpa** | PSP | Visual novel + trial minigames. No core. |
| **Mario Party 1/2/3** | N64 | Mostly menu/dice (tractable); minigames are not. No core. |
| **Paper Mario / TTYD** | N64/GC | Turn-based combat but action commands are timed; no core. |
| **Pokémon Stadium 1/2** | N64 | Turn-based battles, real-time minigames. No core. |

## Tier 3 — Hard (real-time but forgiving, or heavy modelling)

| Game | System | Why |
|---|---|---|
| **Mario Kart DS / Super Circuit / 64 / Wii** | multi | Racing. Rank/lap/item are categorical and speakable; driving is continuous. |
| **Super Mario 64 / New Super Mario Bros.** | N64/NDS | Platforming needs frame-accurate spatial awareness. |
| **Mario Party DS / Advance** | NDS/GBA | Dice+menu tractable; minigames not. |
| **Dr. Mario 64 / Mario vs. Donkey Kong** | N64/NDS | Puzzle-action; falling pieces are a real-time grid. |
| **DBZ: Buu's Fury / Legacy of Goku II / Advanced Adventure** | GBA | Action-adventure; overworld + real-time combat. |
| **DBZ: GT Transformation** | GBA | Beat-em-up. |
| **DBZ: Budokai 1 / 2 / 3 / Infinite World** | PS2 | Fighting games with heavy menus. No core. |
| **DBZ: Tenkaichi Tag Team / Shin Budokai 1 & 2** | PSP | Fighting. No core — but see the BT2 note: a BT2 mod exists for the PS2 sibling. |
| **DBZ: Raging Blast 1 & 2** | PS3 | Fighting. No core. |
| **Lord of the Rings: The Third Age** | GBA | Turn-based combat, little documentation. |
| **Rhythm Tengoku / Rhythm Heaven Fever** | GBA/Wii | Audio-led by design, but play is precisely-timed input — the least suited to spoken output. |
| **bit Generations: Soundvoyager** | GBA | Played entirely by ear; needs no accessibility layer at all. Worth noting rather than adapting. |
| **Space Channel 5 / Metal Slug / Samurai Shodown / Soulcalibur: Broken Destiny** | PS2/PSP | Action/rhythm/fighting. No cores. |
| **God of War: Ghost of Sparta / Spider-Man: Friend or Foe** | PSP | Action. No core. |
| **The Combatribes / Final Fight 1-3 / Final Fight Guy / Knights of the Round / Rushing Beat / Kunio-tachi no Banka / Power Instinct / Sonic Blast Man 1 & 2** | SNES | Beat-em-ups. No core. |
| **Dragonball: Evolution** | PSP | Action. No core. |

## Tier 4 — Wall (real-time reaction; no post-hoc adapter solves it)

| Game | System | Why |
|---|---|---|
| **DBZ: Supersonic Warriors 1 / 2** | GBA/NDS | Fighting. Hit-level and distance cues would help (MK1's approach) but the mixup window cannot be narrated. |
| **DBZ: Extreme Butoden** | 3DS | Fighting; no core. |
| **Super Smash Bros. Melee / 3DS** | GC/3DS | Platform fighter; no core. |
| **Guilty Gear X: Advance / Guilty Gear (PS1)** | GBA/PS1 | Fighting. |
| **King of Fighters EX 2** | GBA | Fighting. |
| **BlazBlue: Continuum Shift II / Code of Princess** | 3DS | Fighting / action; no core. |
| **Super Street Fighter IV: 3D / Dead or Alive: Dimensions** | 3DS | Fighting; no core. |
| **BlayzBloo / Bleach: Dark Souls / Blade of Fate** | NDS | Fighting. |
| **Mortal Kombat 4 / Mythologies: Sub-Zero / Trilogy** | PS1 | Fighting. **Note: MK4 and Trilogy are the games NetherRealm later made accessible by design.** |
| **Mortal Kombat: Deadly Alliance / Armageddon / Shaolin Monks** | PS2 | Fighting. |
| **Mortal Kombat: Unchained / Arcade Kollection / Komplete Edition** | PSP/PS3 | Fighting. |
| **Mortal Kombat - Arcade Kollection** | PS3 | Fighting. |
| **Ultimate Mortal Kombat Trilogy** | Genesis | Fighting, no core. |
| **Tekken 4 / 5 / Soulcalibur III / Bloody Roar 4 / Urban Reign / X-Men: Next Dimension / Evil Zone / DOA / DOA2 Hardcore / Marvel Super Heroes vs. Street Fighter / Capcom vs. SNK 2 / Super Dragon Ball Z** | PS1/PS2 | Fighting. No cores. |
| **Killer Instinct** | SNES | Fighting. No core. |
| **Legend of Kage 2 / Advance Guardian Heroes / Iridion II / Justice League Heroes: The Flash** | NDS/GBA | Action; reaction-driven. |
| **Plasma Sword / Power Stone 1 & 2 / Psychic Force 2012** | Dreamcast | Fighting; no core. |
| **Fire Emblem: Awakening / Fates** | 3DS | Turn-based and *easy on the merits* — blocked purely by no 3DS core. |
| **Pokémon Omega Ruby / Alpha Sapphire** | 3DS | Same: easy on merits, blocked by core. |
| **DBZ: Burst Limit** | PS3 | No core. |
| **DBZ: Budokai Tenkaichi 2 / SLUS 219.78** | PS2 | **A community accessibility mod already exists** (146 KB memory map, PINE-based). No PS2 core here, so it is out of reach regardless. |
| **Injustice: Gods Among Us** | PS3 | Fighting. No core. |
| **Under Night In-Birth** | PS3 | Fighting. No core. |
| **Fisherman's Bait 2** | PS1 | No core. |

---

## What the inventory says about priority

1. **Finish what is started.** Fire Emblem: Shadow Dragon is wired in and needs
   *verification*, not features.
2. **Attack of the Saiyans next** — turn-based, addresses already public, NDS core
   working. Genuinely the easiest new target.
3. **Then the Pokémon gen-4/5 family**, reusing the Black/White reader.
4. **Do not start a game on a system with no core.** Fire Emblem: Awakening,
   Pokémon ORAS, Final Fantasy Tactics: War of the Lions, Steins;Gate and Chrono
   Cross are all *easy games behind a hard wall*.

## A finding worth acting on: this collection is overwhelmingly fighting games

Counting the library: roughly **40 of ~130 titles are fighting games**, and
another ~20 are beat-em-ups or action. That is the single hardest genre, and it is
the majority of what is here. Two consequences:

- **A ranked list is not enough for this library** — most of it is Tier 3/4.
- **The MK1 findings matter disproportionately.** For this collection, the
  categorical-sound approach (distance/health/block/duck/hit-level) is the only
  thing that could ever make the fighting games playable, and it is a design
  project, not a RAM reader. Worth deciding deliberately whether that is in scope.

## The PS1/PS2 question

PS1 and PS2 BIOS/firmware are present, and **PS1/PS2 have two advantages the
Nintendo systems do not**: the DBZ Tenkaichi mod shows PS2 RAM is reachable over
**PINE** (PCSX2's socket IPC) with no base-pointer chasing, and PS1 emulators have
a mature memory-access story. If a PS1/PS2 core were integrated, a large block of
this library becomes reachable — including the fighting games, where the BT2 mod
is a working precedent to copy. That is a bigger project than an adapter, but it
unlocks far more of this collection than any single NDS game will.

## Verification status of the AotS addresses

The probe run (6000 frames, **no input sent**) read **exactly 0** at every
published Action Replay address, while a control address returned real non-zero
data. That is the documented "region entirely zero" signal — **the party structure
had not been allocated**, because with no input the game never left its title
screen. It is *not* evidence the addresses are wrong.

Next step: drive input past the title into a battle and re-measure. Until then the
AotS addresses are **claims, not findings**.

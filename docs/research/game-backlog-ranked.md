# Game backlog — ranked easy to hard for an accessibility adapter

Every game in `Dropbox\Games`, ordered by how much work an accessibility adapter
would be. The ranking applies the findings in
`docs/research/how-other-games-did-it.md` and the project's existing screening
criteria.

## Read this first: the core decides more than the game does

⛔ **A game we cannot boot is harder than any game we can.** An adapter cannot be
written or tested without a running core, so the system is the first gate, and it
outranks everything the game itself would tell you.

| System | Core status | Verdict |
|---|---|---|
| **NDS** | melonDS integrated, boots, Lua script path verified | workable now |
| **GB / GBC / GBA** | mGBA verified working (identification, hotkeys, footsteps) | workable now |
| **3DS** | no core | blocked — needs a whole new core integration |
| **GameCube / Wii** | no core (Dolphin not integrated) | blocked |
| **N64** | no core | blocked |
| **Dreamcast** | no core | blocked |
| **Genesis** | no core | blocked |

So the honest shortlist is **NDS and GB/GBC/GBA** — everything else is gated
behind core work that has nothing to do with accessibility.

## The four reader families (from the project's screening criteria)

- **(A) Text buffer / printer** — dialogue is readable state. Easiest.
- **(B) Menu / list state** — cursors, selections, counts. Moderate.
- **(C) Map / grid / position** — needs a coordinate model. Moderate–hard.
- **(D) Real-time sprite / hitbox** — the player must react in frames. **A wall.**

⛔ **No adapter fixes a type D game.** Speech cannot be faster than a fighting
game's mixup window. MK1's answer (make state a categorical sound) is a *design*
solution that a post-hoc reader can only approximate.

---

## Tier 1 — Easiest (turn-based, documented RAM, existing cores)

| Game | System | Family | Why it is easy |
|---|---|---|---|
| **Pokémon Black / White** | NDS | A+B+C | Already done. The reference implementation. |
| **DBZ: Attack of the Saiyans** | NDS | A+B | Turn-based; party HP/Ki/AP addresses are already public in three places (DeSmuME wiki, Action Replay lists, RetroAchievements). Menus and Monolith's BDAT text encoding are the unknowns. |
| **Chrono Trigger** | NDS | A+B+C | Turn-based; one of the most-documented games in existence, with a full disassembly community. |
| **Dragon Quest IX** | NDS | A+B | Turn-based, menu-driven, heavily documented. |
| **Final Fantasy Tactics A2** | NDS | A+B+C | Tactical but turn-based, so no reaction requirement. |
| **Radiant Historia** | NDS | A+B | Turn-based, grid-based combat. |
| **Fire Emblem: Shadow Dragon** | NDS | A+B+C | Done — terrain, movement, enemies, chapter id. |
| **Pokémon Diamond / Pearl / Platinum** | NDS | A+B+C | Same family as Black/White; the gen-4 layouts differ but the techniques transfer directly. |
| **Pokémon SoulSilver** | NDS | A+B+C | Same as above; GB/GBC ancestors already have working readers. |
| **Mario & Luigi: Bowser's Inside Story** | NDS | A+B | Turn-based with timing minigames — the timing is the only friction. |
| **Mario & Luigi: Partners in Time** | NDS | A+B | As above. |

## Tier 2 — Moderate (turn-based, but less documented or with real-time elements)

| Game | System | Family | Friction |
|---|---|---|---|
| **Advance Wars: Dual Strike** | NDS | A+B+C | Turn-based tactics; needs a full map/cursor model. |
| **Advance Wars: Days of Ruin** | NDS | A+B+C | As above. |
| **Fire Emblem: The Sacred Stones** | GBA | A+B+C | Same family as Shadow Dragon, and the GBA core already works — but the existing GBA readers are Pokémon-specific, so a new adapter is needed. |
| **Tactics Ogre: The Knight of Lodis** | GBA | A+B+C | Tactical, turn-based, dense menus. |
| **Mario & Luigi: Superstar Saga** | GBA | A+B | Turn-based; dodging is a timed press. |
| **Bleach: The 3rd Phantom** | NDS | A+B+C | Tactical RPG. |
| **Dragon Ball Z: Harukanaru Densetsu** | NDS | A+B | Card-based RPG — turn-based, but card text is the reader's whole job. |
| **Kingdom Hearts: 358/2 Days** | NDS | B | Menu-heavy; action combat is the problem. |
| **Naruto: Ninja Destiny** | NDS | D | **Fighting game — see Tier 4.** Listed here only because its menus are simple. |

## Tier 3 — Hard (real-time but forgiving, or heavy map modelling)

| Game | System | Family | Why hard |
|---|---|---|---|
| **Mario Kart DS / Super Circuit / 64 / Wii** | multi | D | Racing. Position is real-time; the *state* (rank, lap, item) is categorical and could be spoken, but driving is continuous. |
| **Super Mario 64 / New Super Mario Bros.** | N64/NDS | D | Platforming needs frame-accurate spatial awareness. |
| **Mario Party 1/2/3/DS/Advance** | multi | B+D | Mostly menu and dice-driven (tractable); the minigames are not. |
| **Dr. Mario 64 / Mario vs. Donkey Kong** | N64/NDS | D | Puzzle-action; falling pieces are a real-time grid. |
| **Paper Mario / TTYD** | N64/GC | A+B | Turn-based combat (easy) but action commands are timing-based, and the core is unavailable. |
| **Pokémon Stadium 1/2** | N64 | B+D | Turn-based battles, real-time minigames. No core. |
| **Dragon Ball Z: Buu's Fury / Legacy of Goku II / Advanced Adventure** | GBA | D | Action-adventure; overworld movement plus real-time combat. |
| **Dragon Ball GT: Transformation** | GBA | D | Beat-em-up. |
| **Lord of the Rings: The Third Age** | GBA | A+B | Turn-based combat, but little public documentation. |
| **Radiant Historia / Code of Princess** | NDS/3DS | — | 3DS has no core. |
| **Rhythm Tengoku / Rhythm Heaven Fever** | GBA/Wii | D | Audio-led by design, but the *play* is precisely-timed input. Ironically the least suited to spoken output. |
| **bit Generations: Soundvoyager** | GBA | D | A game played entirely by ear — but it needs no accessibility layer at all, which is worth noting rather than adapting. |

## Tier 4 — Wall (real-time reaction; no post-hoc adapter can solve these)

| Game | System | Why it is a wall |
|---|---|---|
| **DBZ: Supersonic Warriors 1 / 2** | GBA/NDS | Fighting games. Hit-level and distance cues would help (MK1's approach) but the mixup window cannot be narrated. |
| **DBZ: Extreme Butoden** | 3DS | Fighting game, and no core. |
| **Super Smash Bros. Melee / 3DS** | GC/3DS | Platform fighter; no core for either. |
| **Guilty Gear X: Advance Edition** | GBA | Fighting game. |
| **King of Fighters EX 2** | GBA | Fighting game. |
| **BlazBlue: Continuum Shift II** | 3DS | Fighting game, no core. |
| **Super Street Fighter IV: 3D Edition** | 3DS | Fighting game, no core. |
| **Dead or Alive: Dimensions** | 3DS | Fighting game, no core. |
| **BlayzBloo** | NDS | Fighting game. |
| **Bleach: Dark Souls / The Blade of Fate** | NDS | Fighting games. |
| **Ultimate Mortal Kombat Trilogy** | Genesis | Fighting game, no core — and the very genre MK1 solved by *design*, which a reader cannot retrofit. |
| **Legend of Kage 2 / Advance Guardian Heroes / Iridion II / Justice League Heroes: The Flash** | NDS/GBA | Action; reaction-driven. |
| **Plasma Sword / Power Stone 1 & 2 / Psychic Force 2012** | Dreamcast | Fighting games, no core. |
| **Fire Emblem: Awakening / Fates** | 3DS | Turn-based and would be *easy* on the merits — blocked purely by having no 3DS core. |
| **Pokémon Omega Ruby / Alpha Sapphire** | 3DS | Same: easy on the merits, blocked by core. |
| **Dragon Ball Z: Burst Limit** | PS3 | No core. |
| **Dragon Ball Origins 1 & 2, Kai: Ultimate Butou Den** | NDS | Action / fighting. |

---

## What this ranking implies about priority

1. **Finish what is started.** Fire Emblem: Shadow Dragon is wired in and needs
   verification, not more features.
2. **Attack of the Saiyans next** — turn-based, documented addresses, NDS core
   already working. It is genuinely the easiest new target.
3. **Then the Pokémon gen-4/5 family**, reusing the Black/White reader wholesale.
4. **Do not start a 3DS, GameCube, N64, Dreamcast or Genesis game** until that
   core is integrated. Fire Emblem: Awakening and Pokémon ORAS are easy games
   behind a hard wall.
5. **Treat every Tier 4 fighting game as out of scope** unless the goal is MK1's
   categorical-sound approach — which is a much larger design project than a
   RAM-reading adapter.

## Verification status of the AotS addresses

The first probe run (6000 frames, **no input sent**) read **exactly 0** at every
published Action Replay address, while a control address returned real non-zero
data. That is the documented "region entirely zero" signal: **the party structure
had not been allocated yet**, because with no input the game never left its title
screen. It is *not* evidence the addresses are wrong.

The next step is to drive input past the title into a battle and re-measure.
Until that is done, the AotS addresses are **claims, not findings**.

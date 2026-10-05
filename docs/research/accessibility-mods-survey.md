# How the wider accessibility-mod scene works — and what transfers to Open Game Access

Research compiled from the mods named for study, plus the three landmark commercial
implementations already documented in `how-other-games-did-it.md` (Mortal Kombat 1,
The Last of Us Part II) which are not repeated here.

Survey shape: **31 repositories**. Language census — **C# 14, C++ 4, Python 3, C 3,
Lua 2, Squirrel 1, PowerShell 1**. That census is itself the finding, and it is
explained in §1.

---

## 1. There are two different worlds here, and OGA is in the smaller one

The list looks like 31 variants of the same thing. It is not. It is two families, and
they differ by **where the mod runs relative to the game**:

**Family A — in-process .NET mods for NVDA on Windows (the majority: 14 of 31 C#).**
SF6Access, P4G Access, Undertale, Duel Links, Iron Lung, Dressmaker, Kingdom Access,
Graveyard Keeper, Wasteland 2, Rogue Trader, Among Us, FF1/FFXII Screen Readers,
Sleeptalker, Beholder, Vigil, Words of Power II, BDC.

They inject a DLL into the game's own process, hook the game's own functions with a
framework such as REFramework, read the game's own objects, and speak by calling
**NVDA's controller client (or Tolk) directly on the host**. The screen reader is the
host's, and the game is a Windows program that has already loaded.

**Family B — script or port readers driven by an emulator (the smaller one).**
Zelda1Access (BizHawk **Lua**), Zomboid Access (**Lua**, in the game's own mod loader),
blind-starship (a **decompilation port**), AccessXI (Lua), RetroArch (libretro, native
speech requests). The mod runs **under** the game, or beside it, and owns the game's
knowledge itself.

⛔ **Open Game Access is Family B, and every Family A technique is unavailable to it.**
An emulator cannot inject a DLL into a DS cartridge that has not existed since 2010, and
it cannot call NVDA on iOS. So the Family A pile — which is most of the list — is useful
for **what it decides to speak** (§3), not for **how it reaches the game**. Reading them
for mechanism would be wasted effort.

This is not a limitation to work around. It is the reason OGA's existing architecture
(a Lua reader plus native adapters behind `Core/adapter.h`) is the right shape: it is
the Family B shape, and Family B is where the emulated systems actually live.

---

## 2. The three transferable techniques, in cost order

### 2a. Hook the draw, do not poll for it — now confirmed twice more

`how-other-games-did-it.md` recorded this from Pokémon Access (35 `memory.registerexec`
hooks). The new material confirms it as the **dominant** technique rather than one
option among several.

- **SF6Access: 212 hook files, one per screen.** `SF6Access/Hooks/CharacterSelectHooks.cs`
  does not scan for the character-select cursor; it hooks
  `app.menu.SelectedFighterCtrl.SetFighterSetting` and fires when the game actually
  changes a fighter. The cursor position is never computed by the mod — the game hands
  it over. Its own header comment states the rule: *"Hooks SelectedFighterCtrl
  .SetFighterSetting to detect fighter changes per player. Buffers announcements within
  a frame so both players are read together."*
- **blind-starship** hooks the decompilation's own state rather than reading a memory
  image, for the same reason: the symbol exists, so nothing has to be inferred.

**For OGA:** this is the strongest available argument for the adapter API growing a
*notification* shape alongside the current *query* shape. Today an adapter is asked on
a frame (`on_frame`) and answers. A hook fires at the moment of change and pushes. The
project already knows this shape — `memory.registerexec` in the Lua layer is exactly
it — but the native adapter seam has no push path. That is a real gap, and it is
cheaper to close it now, with five adapters, than with twenty.

### 2b. Coalesce within a frame, or the reader talks over itself

`CharacterSelectHooks.cs` buffers announcements **within a frame** so that two players'
changes are spoken together as one line rather than two that interrupt each other.
blind-starship does the same at a larger scale: per-event announcements (hit bonuses,
ring streaks) are chatty, so they *share a single toggle*, and the README says so
explicitly.

**For OGA:** the announcement queue already exists (`AnnounceQueue` on `Host`). What
these mods add is the discipline of **deciding what is too chatty to say by default**,
and telling the player that the toggle exists. blind-starship's wording — "these share a
single toggle that is on by default" — is the honest phrasing to copy.

### 2c. True binaural audio beats panning for spatial information

blind-starship renders spatial cues as **real 3D binaural (HRTF) audio via Steam Audio**,
which gives genuine left/right *and* front/back through headphones, with **pitch for
above/below**. Plain stereo pan (what most mods use) can only do left/right.

This is markedly better than anything recorded in the existing research, and it matters
for exactly the genre that fills this user's library (racing, flying, platforming). It is
also the one technique here that is **not** available to OGA on iOS: Steam Audio is a
C++ library with a Windows/console story, and the app's audio path is the platform's.
Worth recording as a known ceiling rather than rediscovering.

Its obstacle-cue design is a clean, copyable **categorical** scheme, and it is honest
about its own gaps:

| Cue | Meaning | Encoding |
|---|---|---|
| Obstacle warning | solid thing ahead on your course | low buzz, straight ahead, beats faster as you close |
| Obstacle beside | would hit if you steered that way | mid chord, **stereo pan = side, distance = pan depth** |
| Obstacle above | would block a climb | high chord, louder as it nears |
| Obstacle below | hill you are flying over | low chord, same rule |

⛔ **And then it states what it does NOT cover**: ground, water, lava and bosses are
not warned about, shapes are approximated so a warning can arrive early or late, and
"these are very much a work in progress". That is the project's own "a silent refusal is
worse than a spoken one" rule, and this mod applies it in its player-facing README.

---

## 3. What the whole family decides to speak — a checklist worth copying

Reading all 31 for *content* (not mechanism), the announcements cluster into a set that
is remarkably consistent. This is the most directly reusable output of the survey.

| Category | Examples seen |
|---|---|
| **Menu position** | focused item + its value, on every screen |
| **Numbers with units** | "514,396 words", league points, play time, damage + hit count |
| **Currency / resources** | rupees, bombs, keys, hearts (Zelda); drive gauge, super art gauge (SF6) |
| **Mode and place** | on-rails vs all-range; current screen / dungeon / cave; "no reader, code X" |
| **Lists with counts** | "3 notifications"; items 1-of-N so position is never lost |
| **Progression events** | ring streak, streak broken, score bonus, rank-up |
| **State changes as categories** | health 75/50/25/critical; distance as a click track |
| **Refusals** | what is deliberately unreachable (Battle Hub, World Tour; ground/lava/bosses) |

⛔ **The refusal row is the most transferable and the most often skipped.** Both SF6Access
and blind-starship name what they do *not* cover. A blind player who is told "Battle Hub
is not accessible" stops hunting for the missing feature.

---

## 4. Per-repo findings for the requested systems

### Zelda 1 (NES) — `GADeuvall2000/Zelda1Access` — **directly relevant, and the answer is Lua**

**It is a BizHawk Lua script**, not a compiled mod. Its two text documents (fetched in
full) describe a drop-in script plus the accessibility BizHawk build.

Components:
- `Zelda1Access.lua` + supporting files, dropped into BizHawk's `Lua/` folder
- `nvdaControllerClient64.dll` and `Tolk.dll` in BizHawk's root — **how it speaks**
- `SoundBridge.bat`, a separate process that must stay running; it handles *both*
  screen-reader speech *and* synthesised game audio (footsteps, bumps, radar). If it
  closes, the mod goes silent.

Features: spoken menus and dialogue, spoken inventory with treasure cycling, pathfinding
across overworld/dungeons/caves/basements, **spatial enemy radar and item beacons**,
footstep and wall-bump cues, player-placed waypoints, an accessibility menu for toggles
and volumes, and a second-controller save-screen shortcut.

⛔ **What this means for OGA is concrete and good.** The Zelda adapter needs **no new C++
adapter at all**. This is a BizHawk Lua reader — the same class as the Pokémon
`gba-lua` set the app already hosts, using the same shim (`bizhawk_compat.lua`). Two real
obstacles stand in the way, and neither is the adapter:

1. **NES has no core in the app.** `Core/nes_adapter.cpp` exists as a seam that
   *refuses by design*, because Mesen is admitted (84/84 TUs compile) but not
   integrated — there is no `Core/nes_core.cpp`. No core, no console to read.
2. **The audio path.** Zelda1Access leans on a *separate process* for its spatial cues
   and footstep audio. OGA has no equivalent, and §2c is the same wall. Speech is fine;
   the non-speech audio layer is the missing piece, and it is app-level, not adapter-level.

So: **the Zelda adapter is a Lua-porting job gated on the NES core**, not an adapter to
write from scratch. That reorders the work: finish the core, then port the script.

### StarFox 64 — `ohylli/blind-starship` — **not portable, and worth saying so**

It is a fork of **HarbourMasters/Starship**, the StarFox 64 *decompilation port* — a
native PC re-implementation, not an emulated ROM. It speaks through **PRISM**
(`ethindp/prism`), a cross-platform screen-reader library. Current state: main menu,
sound options, pause menu and the F1 settings menu are accessible; gameplay has
*partial* accessibility, limited to training mode.

⛔ **This one cannot become an OGA adapter.** OGA runs emulated ROMs; Starship is the
game's own source compiled for the host. There is no memory image to read and no emulator
to host it. An N64 core in OGA would not make this mod work, and this mod working would
not help OGA.

What *does* transfer: its cue design (§2c) and its honesty about gaps (§3). If StarFox 64
is ever adapted for OGA it will be a **new N64 Lua reader written against an N64 core**,
with this mod as the design reference rather than the code.

### Street Fighter 6 — `Ali-Bueno/sf6Access` — the reference for *what to say*

212 C# hook files under `SF6Access/Hooks/`, one per screen, built on REFrameworkNET.
Reads menus, prompts, combo trials (the actual button sequence), training data, VS
screens, replay rows, key config with **real input names**, and fills button glyphs the
screen reader cannot see. It states its own gaps (Battle Hub, World Tour).

The transferable part is §2a and §3. The code is not portable to an emulator, and SF6
is not an emulated title.

---

## 5. Corrections to the existing research

- **`how-other-games-did-it.md` cites a Street Fighter 6 report; this survey clarifies
  the mechanism.** SF6 ships *combat* audio cues natively (attack hits, health, Drive
  Gauge, distance) but **no menu narration** — so the mod is what makes the game
  navigable, and the native cues are what make it playable. Two halves, two authors.
  Neither half is sufficient alone, which is the same "categorise state + assist
  navigation" split already recorded from MK1/TLOU2.

---

## 6. What to do with all of this

Ordered by cost, and honest about what is blocked:

1. **Close the adapter push-path gap (§2a).** Cheap now, expensive later, and the whole
   Family B evidence says push beats poll.
2. **Copy the announcement checklist (§3), including the refusal row.** No new hardware
   or core needed; it improves readers that already exist.
3. **Port Zelda1Access when the NES core lands.** It is a Lua job, not an adapter.
4. **Record the non-speech audio ceiling (§2c)** — binaural cues and a separate audio
   process are what the strongest spatial readers use, and OGA cannot do either today.
   Deciding whether to invest there is a product decision, not a research one.
5. **Do not spend effort on Family A mechanisms**, and do not promise StarFox.

---

## 7. Zelda 1 Access, measured: what the reader actually is

The repo ships no source, only `Zelda1Access v1.0.zip` (237,806 bytes). Fetched and
extracted, it contains the real reader: **10 Lua files, ~340 KB**, plus
`nvdaControllerClient64.dll`, `Tolk.dll` and a PowerShell sound bridge. The module
names are self-documenting, which is how the shape below is known:

| File | Module | Role |
|---|---|---|
| (main `.lua` inside the zip) | loader | bootstrap, file select / register mode constants |
| `Navigation.lua` (192 KB) | navigation engine | all announcements, hotkeys, timing |
| `Worldmap.lua` (38 KB) | overworld data | pure data, no logic |
| `Dungeons.lua` (15 KB) | dungeon data | names + room/location label formatting |
| `GameData.lua` (11 KB) | decoder | raw bytes → names, **and decodes ROM data structures** (NPC dialog text) |
| `Waypoints.lua` | user state | JSON on disk |
| `VisitedRooms.lua` | progress | `dungeon_visited.json` on disk |
| `Settings` + JSON | config | coordinate format, footsteps on/off |

### The emulator API surface is ONE function

Counted across every Lua file, the entire reader calls:

| API | Uses |
|---|---|
| `memory.read_u8` | 135 |
| `mainmemory` | 121 |
| `console.log` | 5 |
| `gui.text` | 4 |
| `emu.frameadvance` | 1 |
| `savestate` | 1 |

⛔ **No `memory.registerexec`. No write hooks. No register reads. No `joypad` calls.**
This reader is a **poll-and-decode** design, not a hook design — the opposite of the
Pokémon reader, which registers ~35 exec hooks. That makes it the *easier* class to
host, and it means the technique in §2a is not universal: a reader can be built either
way, and this one proves the cheap way still reaches a complete two-quest playthrough.

Its memory map is 27 distinct addresses read with `read_u8`, all in NES RAM
(`0x0010`–`0x06FF`, plus a few ROM reads at `0x6BB2`, `0x687E`, `0x68FE` for text).

### The shim already covers it

`Sources/OpenGameAccess/Resources/bizhawk_compat.lua` already defines
`mainmemory.read_u8`, `read_u16_le`, `read_u32_le`, `write_u8` and a range reader, with
`RAM_BASE = 0x02000000` (the DS mapping). So the Zelda reader's needs are met by the
BizHawk shim that already exists — **for a DS-shaped console**.

⛔ **The one real mismatch is `RAM_BASE`.** NES has no `0x02000000`; it is a flat
64 KB address space. The shim hardcodes the DS base, and `Sources/OpenGameAccess
/Resources/` contains **zero mentions of NES, FCEUX or Mesen**. So porting this reader
needs a per-console `RAM_BASE` (and a NES core underneath it), not a new shim.

### Audio, again

The reader pairs with a **separate PowerShell process** (`SoundBridge`) that stays
running and carries both speech and synthesised non-speech audio. `sound_bridge_command.txt`
is one byte — an IPC flag. This is the same conclusion as §2c from a completely
different codebase: the strongest spatial/footstep readers put their audio *outside*
the emulator, and OGA has no equivalent path today.

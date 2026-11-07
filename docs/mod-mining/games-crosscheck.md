# Games Cross-Check: Devin's Library vs. Accessibility Research

Written 2026-10-25. Sources actually read for this report:

1. **Dropbox `/Games`, listed recursively in full** (6,744 entries over 23
   paginated `list_folder` calls — the listing was NOT truncated).
   Game-file counts by shelf: PSP 26 game files, NDS 42 `.nds` files,
   GBA 51 game files, N64 18, SNES 19, NES 5,829 (a near-complete NES set),
   plus smaller PS1, PS2, PS3, 3DS, GameCube/Dolphin, Genesis and
   Dreamcast shelves. Only titles the listing actually returned are named
   below.
2. **The tricks catalog** — `~/workspace/oga-queue/accessibility-tricks.md`
   (12 source sections).
3. **Molitvan's list** — README of
   github.com/Molitvan/blind-accessible-games-list, read in full
   (all 632 lines of the rendered README).

Honesty rules applied: a match means the *same game*. Sequels, remakes,
ports to a different platform, and regional/version variants are flagged
as near-matches and never counted as matches.

---

## (a) Games he owns that already have a community accessibility mod

### Direct matches — same game, mod exists

1. **Pokémon mainline, Game Boy / Game Boy Color / Game Boy Advance**
   — Owned (GBA shelf): Red, Blue, Yellow, Gold, Silver, Crystal,
   Ruby, Sapphire, Emerald, FireRed, LeafGreen.
   **Mod: Pokémon Access** (nuiv) — Lua scripts run inside an emulator
   that read game memory and speak dialogue, menus, party/battle state
   and surroundings, with warp lists and pathfinding.
   Where it lives: the Pokémon Access script set (catalog §5);
   Molitvan's entry: "all GBC and GBA Pokemon games including some ROM
   hacks… with the Pokemon Access mod — mostly accessible."

2. **Pokémon Diamond, Pearl, Platinum** (DS) — Owned (NDS shelf):
   Diamond (Rev 5), Pearl (Rev 5), Platinum.
   **Mod: Pokémon 4th Gen Access** by arqmeister — screen-reader access
   to 4th-gen Pokémon games.
   Where it lives: github.com/arqmeister/pokemon-4th-gen-access
   (catalog §5).

3. **Pokémon Black, White** (DS, Generation 5) — Owned (NDS shelf):
   Black Version, White Version (both NDSi Enhanced).
   **Mod: a Generation 5 access project** hosted at
   accessibilitymods.com — Molitvan grade: work in progress
   (catalog §11.1).
   **Also in his own library:** `/Games/NDS/Lua/` contains a BizHawk/
   EmuHawk Lua speech-reader project for exactly these DS Pokémon
   games. Its `main.lua` header (read for this report) describes a
   loader that detects the running game and hands over to a per-game
   reader: Black and White (`games/bw.lua`) are marked "Done, both
   games first-class"; Platinum is the lead in-progress target;
   Diamond/Pearl and HeartGold/SoulSilver readers are on its roadmap.
   `core/speech.lua` (also read) is its shared speech engine. He also
   owns every ROM on that roadmap except HeartGold — he has SoulSilver.

4. **The Legend of Zelda** (NES, "Zelda 1") — Owned (NES shelf):
   `Legend of Zelda, The (USA).nes` plus Rev 1 / Virtual Console /
   GameCube Edition variants.
   **Mod: Zelda1Access** — Molitvan grade: **fully accessible**
   (catalog §11.1). Emulator-driven on PC, the same architecture
   family as OGA.

5. **Dragon Warrior** (NES) — Owned: `Dragon Warrior (USA).nes`
   (plus II, III and IV, for which no mod was found in either source).
   **Mod: Dragon Warrior Access** — Molitvan grade: work in progress
   (catalog §11.1). Emulator-driven classic-JRPG access; the catalog
   calls it "an existence proof for classic JRPG adapters."

6. **Fire Emblem: The Sacred Stones** (GBA) — Owned (GBA shelf):
   `Fire Emblem - The Sacred Stones (USA, Australia).gba`.
   **Mod: a Sacred Stones access mod** — Molitvan grade: partially
   accessible, development status unclear (catalog §11.1, which notes
   grid tactics is "the hard case for speech").

7. **Mario Kart 64** (N64) — Owned: `Mario Kart 64 (USA) (LodgeNet).z64`.
   **Mod: Super Blind Kart**, a fork of the Spaghetti Kart PC port —
   Molitvan grade: work in progress.
   *Platform caveat:* the mod is built on the PC port, not on the
   original N64 ROM running in an emulator — same game, different
   host than the file on his shelf.

8. **Mario Kart Wii** — Owned (Dolphin shelf):
   `Mario Kart Wii (USA) (En,Fr,Es).rvz`.
   **Mod: the MKWii Accessibility project** — Molitvan grade: work in
   progress, requires controller (catalog §11.1 cross-ref; list entry
   read directly).

9. **Rhythm Heaven Fever** (Wii) — Owned (Dolphin shelf):
   `Rhythm Heaven Fever (USA).rvz`.
   **Mod: RH Fever Access** (hosted at molitvan.me) — Molitvan grade:
   work in progress.

10. **RetroArch Accessibility project** (emulator-level, not one game)
    — Molitvan lists it as an emulator-wide screen-reading project,
    applied per core (work in progress). It is an alternative host for
    many ROMs already on his shelves rather than a mod for one title;
    catalog §11.1 calls it "the alternative architecture to OGA's
    per-game adapters… worth watching as a complement, not a rival."

### Near-matches — flagged, NOT counted

- **Dragon Ball Z: Budokai** and **Dragon Ball Z: Budokai 3** —
  Owned on the **PS2** shelf (`Dragon Ball Z - Budokai (USA).chd`,
  `Dragon Ball Z - Budokai 3 (USA).iso`; Budokai 2 also owned).
  **Budokai Access** (BlindChallengerX, catalog §1) is the headline
  mod of the research — but it targets the **PC HD ports** of Budokai
  1 and 3, not the PS2 versions on his shelf. Same games, wrong
  platform for the mod as shipped. Nothing in either source offers a
  PS2 version of it.
- **Final Fantasy I, II, IV, V, VI** — Owned in several original
  versions: FF1 (NES), "Final Fantasy 2" = FF IV and "Final Fantasy
  III" = FF VI (SNES), FF 4/5/6 Advance (GBA), FF I & II 20th
  Anniversary (PSP). The **FF1–FF6 Screen Reader mods** in the
  catalog (§9) and on Molitvan's list target the **PC Pixel Remaster**
  editions specifically. Same games, different editions — the mods
  don't run on his copies, though their design is directly reusable.
- **Tekken 3** — Tekken 3 Access (BlindChallengerX, catalog §10) is
  for the Tekken 3 PC port. His shelves hold Tekken: Dark Resurrection
  and Tekken 6 (PSP), Tekken 4 and 5 (PS2) — no Tekken 3. The only
  "Tekken 3" file in the listing is an unlicensed NES pirate port
  (`Games/NES/Tekken 3 (Asia) (En) (Pirate).nes`), which is a
  different game in everything but name. **Not a match.**

---

## (b) Games he owns that appear on Molitvan's list

The mod-driven entries above (§a items 1–9) are all also Molitvan
entries; their list grades are given there. Beyond those:

- **"Accidentally accessible" — most fighting games.** The list's
  accidental section leads with: "Most fighting games (e.g. older
  Mortal Kombat)" — playable by sound design and memorised structure
  without any mod or built-in feature. That describes a large slice
  of his shelves, verified from the listing: Mortal Kombat titles on
  N64 (MK4, MK Trilogy, MK Mythologies: Sub-Zero), PS1 (same three),
  PS2 (Armageddon, Deadly Alliance, Shaolin Monks), PSP (Unchained),
  GBA (Deadly Alliance, Tournament Edition) and PS3 (Komplete
  Edition, Arcade Kollection), plus Street Fighter, Tekken,
  Soulcalibur, Samurai Shodown, King of Fighters, Guilty Gear,
  ClayFighter and other fighters across SNES/N64/PS1/PS2/PSP/3DS.
  Category on the list: **accessible by sound design / accident**,
  not natively and not via a mod.
- **Mortal Kombat 1 (2023)** is on the list as "fully accessible
  w/audio description" — **he does not own it.** His newest MK is
  the PS3 Komplete Edition (MK9). Flagged so the list entry is not
  mistaken for coverage of his copies.
- **Dragon Ball Sparking! Zero** (Sparking Zero Access mod, WIP) and
  **Dragon Ball Z: Kakarot** (DBZ Kakarot Access, WIP) are the list's
  only Dragon Ball entries — **he owns neither game.** His Dragon
  Ball shelf is entirely older titles (Budokai 1/2/3, Budokai
  Tenkaichi 2, Infinite World, Super Dragon Ball Z on PS2;
  Raging Blast 1/2 on PS3; the DS/GBA/PSP titles in §c). Near-name
  warning: Sparking! Zero is the successor *series* to Budokai
  Tenkaichi, not the same game as his Tenkaichi titles.
- **Pokémon Generation 5** (Black/White) and the **GBC/GBA Pokémon**
  blanket entry — covered in §a.
- **Rhythm Heaven (3DS)** appears in the accidental section as
  "accidentally playable" — he owns the GBA original, **Rhythm
  Tengoku** (Japan), and the Wii **Fever** (mod, §a item 9), but not
  the 3DS game the entry describes. Flagged, not counted.

No other owned title was found on the list as read. In particular,
none of his Kingdom Hearts, Dissidia/Final Fantasy spin-off,
Castlevania, Chrono, Mario platform/RPG, or Steins;Gate copies
appear — the list's Final Fantasy entries are the Pixel Remaster
editions only (see §a near-matches).

---

## (c) Games he owns that already have OGA adapters — reference only

All verified present in the Dropbox listing:

| Adapter | His file |
|---|---|
| Kingdom Hearts: Birth by Sleep | `Games/PSP/Kingdom Hearts - Birth by Sleep (USA) (En,Fr,Es).chd` (and an `.iso` copy) |
| Mortal Kombat: Unchained | `Games/PSP/0744 - Mortal Kombat - Unchained (USA) (v1.03).cso` |
| Dissidia: Final Fantasy | `Games/PSP/1982 - Dissidia - Final Fantasy (USA).cso` |
| Dragon Ball Z: Shin Budokai — Another Road | `Games/PSP/Dragon Ball Z - Shin Budokai - Another Road (USA).iso` |
| Dragon Ball Z: Attack of the Saiyans | `Games/NDS/Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds` |
| Fire Emblem (fe11 = Shadow Dragon) | `Games/NDS/Fire Emblem - Shadow Dragon (USA).nds` |
| Dragon Quest IX | `Games/NDS/Dragon Quest IX - Sentinels of the Starry Skies (USA) (En,Fr,Es).nds` |
| The Legend of Zelda: A Link to the Past | `Games/SNES/Legend of Zelda, The - A Link to the Past (USA).sfc` |
| GB/GBA reader line | His whole GBA shelf (51 game files: Pokémon, Zelda, DBZ, Castlevania, Mario, Fire Emblem, Final Fantasy, etc.) |

Adjacent owned titles the adapters do **not** cover (flagged, not
matches): Dissidia 012 (the sequel), Dragon Ball Z: Harukanaru
Densetsu (queued next, no adapter yet), DBZ: Tenkaichi Tag Team
(PSP — research only, no adapter), Steins;Gate: Hiyoku Renri no
Darling (PSP — deep research doc, no adapter).

---

## (d) Shortlist — owned, in OGA's emulator scope, easiest first

Drawn only from §a/§b. One line each.

1. **Pokémon Diamond / Pearl / Platinum / SoulSilver / Black / White
   (DS)** — Turn-based, menu- and text-driven; a 4th-gen access mod,
   a Gen 5 project, AND a working Lua reader already sitting in his
   own NDS folder prove the memory-reading approach on these exact
   ROMs, which he owns in full.
2. **Pokémon Red→LeafGreen mainline (GB/GBC/GBA)** — Pokémon Access
   already speaks these games end-to-end, and OGA's GB/GBA reader
   line is the natural home for the same coverage.
3. **Dragon Warrior (NES)** — The simplest classic JRPG shape
   (menus, text, tile movement) with a Dragon Warrior Access mod
   already blazing the trail; he owns I–IV if it grows.
4. **The Legend of Zelda (NES)** — Zelda1Access is graded *fully*
   accessible, the strongest proof in this whole report that a
   top-down action-adventure can be completely spoken; tile-based
   world, no timing-critical combat.
5. **Fire Emblem: The Sacred Stones (GBA)** — A partial mod exists,
   and OGA already speaks Fire Emblem (Shadow Dragon DS), so series
   knowledge transfers — held back only because grid tactics is the
   hardest shape to narrate well.
6. **Final Fantasy VI (his SNES "Final Fantasy III" / GBA FF6
   Advance)** — No mod runs on his editions, but the Pixel Remaster
   screen-reader mod is a complete design blueprint (area scan with
   state flags, step directions) for a dialogue-first JRPG he owns
   twice over.
7. **Mario Kart 64 (N64)** — Super Blind Kart shows the community is
   already working this exact game; racing is real-time and
   cue-driven, so it's the hardest here — and OGA's N64 bench work
   (Pokémon Stadium) means the platform isn't new.

**Out of OGA's current scope, noted honestly:** Budokai 1/3 (PS2 —
no PS2 backend, and the mod needs the PC HD ports anyway), Mario
Kart Wii and Rhythm Heaven Fever (Wii/Dolphin — no Wii adapter path
in OGA today).

---

## Library notes (from the listing, for the record)

- `/Games/PSP` holds **26 game files**, matching the known count,
  plus `ccc-patched.cso` (unidentified from its filename alone — not
  counted as any title) and a `maxcso` tool folder.
- One PS2 file, `SLUS_219.78.DBZBT4B14REV2ENG.iso`, is identified
  only by that filename here; it was not counted as any catalogued
  title in this report.
- `/Games/NDS` contains a whole BizHawk/EmuHawk installation (the
  `dll/`, `gamedb/`, `Shaders/` trees and the Lua reader of §a
  item 3), which is why the recursive listing runs to 6,744 entries.
- The NES shelf (5,829 files) is effectively a complete set, so
  "owns the NES original" is true for most classic NES titles —
  only titles individually verified above are claimed.

---

## Revisit 2026-11-04 — what the new tricks changed

Appended 2026-11-04 at the user's ask ("go through the shelves
again and revisit games that would be hard or not possible
before"). The original cross-check above is unchanged; this
addendum summarises the re-judgements. Full report:
`shelf-revisit-2026-11-04.md` (same folder). New evidence since
2026-10-25: tricks catalog §26 (SSFV heap-sweep state-finding),
§27 (TWD Access in-engine Lua architecture), and the ARMSX2 /
ARMSX3 platform research (2026-11-03).

**Moved — hard/not-possible → plausible:**
- **The whole PS2 class (Android-only).** The premise "no PS2
  backend exists" is dead: ARMSX2 is a mature open-source Android
  PS2 emulator built core+shell, so an OGA backend is now a
  core-integration project, not a missing invention. Lifted with
  it: MK Armageddon and MK Shaolin Monks (roadmap stages 2–3),
  Budokai 1 + 3 (complete Budokai Access design already exists;
  community PCSX2-side access to the PS2 versions proven),
  Budokai 2, Infinite World, Budokai Tenkaichi 2, Soulcalibur
  III, DOA2: Hardcore, Odin Sphere, Urban Reign. Honest limit:
  nothing integrated or measured yet — the ARMSX2 core spike is
  the falsification test (revisit report, experiment #3).

**Still hard, blocker narrowed/changed:**
- **PS3 class** (MK9, Injustice, Raging Blast 1/2, Burst Limit,
  UNI-B): "no route" → ARMSX3 exists and its notes name an MK
  Komplete freeze fix; blockers are now performance headroom,
  no scripting layer / Cell RE, and an unscoped integration.
- **Steins;Gate (PSP):** the current-line-witness blocker is
  re-aimed by §27.4's doctrine — hunt where the script engine
  writes the line, not the rendered line. Still gated on the hunt.
- **Dawn of Sorrow (DS):** the touch-seal blocker narrowed — a
  fixed, discrete gesture is replayable by synthesized touch
  (§27.11 principle + §27.9 auto-execution), unlike continuous
  touch steering.
- **Tenkaichi Tag Team (PSP):** RE blocker unchanged in status;
  §26.1/§26.10 heap-sweep/marker-scan give the state-hunt a
  second proven method family.
- **UMK Trilogy (Genesis recomp):** re-checked against §26/§27 —
  nothing moves; the 4 MiB / SSF-paging framework gate stands.
- **Spyro pair (PS1):** finding narrowed (§27.5 ranked list +
  §14.1 scan); gliding + camera + PS1 core stand.
- **Metal Gear Solid (PS1):** guard facing reclassified as a
  data read (§27.12 principle); first-person aim and the PS1
  core stand.

**Still not possible (checked against §26/§27 by barrier):**
touch-only with no controller path (Phantom Hourglass, Spirit
Tracks, DB: Origins — §27.11 needs a controller path that exists);
platform fighters (Melee, Smash 3DS, BlayzBloo); free-camera
precision platforming (SM64, Castlevania 64 ×2); minigame
collections; speed/bullet-density titles; the real-time brawler
class; rhythm-only titles. Platform gates that did not move:
NES/SNES app link, N64 core, PS1 core, GC/Wii core check,
3DS/Genesis/Dreamcast link — their shape ratings stand as-is.

**Order/approach changes among the easy items:** Chrono Trigger
(DS) rises within Tier 1 — a blind completion of the DS version
is now documented (field report 2026-10-31) and its famous
mimic-game blocker has a built-in gold bypass. The menus-and-
story fighter class keeps its rating but its menu layer gets
§26's proven method. MK Mythologies' timed-mover design gains
§27.9/§27.10 vocabularies. Danganronpa's trials get a §27.4 +
§27.12 design pairing. Pokémon TCG unchanged (gate is queue
position, not technique).

**Recommended next 3 (roadmap-respecting — MK Unchained
Konquest stays the active line):** (1) scout MKU's script VM
for a TWD-style loadable-script/debug hook (§27.1); (2) the
Steins;Gate text-build hunt on the READY PSP backend; (3) the
ARMSX2 core spike — boot Budokai 1 from a controlled harness
and measure speed + in-process memory access.

---

## Addition 2026-11-05 — Pokémon Access Plus (Devin's ask: grab it, stage it, list it)

**Pokémon Access Plus** — pates's expanded version of Nuive's Pokémon
Access scriptset (AudioGames topic 60864, posted 2026-11-05; download:
https://patessoft.pl/pokemon-access.zip). Extends entry (a).1 above:
adds official **Pokémon Ruby and Sapphire** support plus a large set of
ROM-hack scripts (Unbound, Clover, Amethyst, Materia, Vega, and more),
and a puzzle-assist layer — target selection (J/L), spoken descriptions
(K), and spoken A* routes to the selected target (P) for gym puzzles
(e.g. Clover skeleton/puzzle assists). Speaks via NVDA
(nvdaControllerClient32.dll in the lua tree). Package = bundled
VBA-ReRecording (vba.exe) + lua/ scriptset (403 files, ~12 MB extracted).

**Staged on Sarah's laptop (sonix):** ~/sarah/oga-materials/pokemon-access-plus/
(zip md5 a3906e0978238b5248f6a73c03555962; extracted/ beside it).

**OGA relevance:** when the GBA Pokémon adapter work comes up in the
queue, this scriptset is the reference implementation to mine first —
A* pathfinding with spoken routes, the target-selection model, profile
system per game/hack, and its NVDA speech bridge — alongside JVPA §29's
Pokémon framework notes. Listed here as Devin directed 2026-11-05.

## Addition 2026-11-05 — Devin's dictated OGA direction + MK Trilogy asks

Captured by voice (his transcription-app test, iPhone 16, on-device Apple
Foundation Model) — filed verbatim in substance, his words govern.

**OGA platform direction (long-term targets):**
- OGA on **Windows, Mac, and Linux**, accessible on all three.
- UI: something like **wxWidgets for Python (wxPython)** — natively
  accessible widgets rather than a custom-drawn UI.
- Input: **keyboard controls and/or game controllers**, like on mobile.
- More cores: **PlayStation 2, PlayStation 3, Nintendo 3DS**, and more
  beyond the current set.

**Games to add to the list:**
- **Mortal Kombat Trilogy** — evaluate BOTH variants:
  - *Nintendo 64:* opponents are positioned in the stereo field — the
    stereo audio shows where they are. That built-in spatial cue is a
    point in the N64 version's favor (and a design datum: the game
    already does a form of the proximity radar natively).
  - *PlayStation 1:* has all the characters (the fuller roster).
  - Open question: which version is the better adapter target — the
    N64's stereo positioning vs. the PS1's roster.
- **Ultimate Mortal Kombat Trilogy (Sega Genesis)** — check whether
  that version has the same kind of trick available for putting
  fighters in spatial audio. (Relates to azure's UMK Trilogy recomp
  work — the natural ally for this route.)

## Added 2026-11-06 — three Final Fantasy screen-reader mods (Devin's links; grabbed, verified, staged on sonix ~/sarah/oga-materials/)

- **FFPRCore-Screen-Reader v1.0** (bladestorm360, github.com/bladestorm360/FFPRCore-Screen-Reader) — screen reader for Final Fantasy I–VI Pixel Remaster (PC). Route beacons that keep pinging while walking, HRTF spatial audio using full bearing with rear pitch cues, ally targeting/status review, shop/equipment comparison reading, navigation across connected floors/vehicles/locked doors. Built on Prism 0.18.3. ZIP SHA256 verified against the release notes (a3b3c385…). Staged + extracted: ~/sarah/oga-materials/ffpr-core-screen-reader/. Mine for: beacon/HRTF design, shop comparison reading, multi-floor routing.
- **FFX-X2-Screen-Reader v1.0.2** (bladestorm360, github.com/bladestorm360/FFX-X2-Screen-Reader) — screen reader for Final Fantasy X (X-2 line). Speech for equipment-capacity keep/discard, separate shop descriptions + comparisons, puzzle-guidance mode (L1/LB), route smoothing over actual map geometry with offline equivalence checks, installer hardened with mandatory AV scans of the exact archive. ZIP SHA256 verified (6e43b43b…). Staged + extracted: ~/sarah/oga-materials/ffx-x2-screen-reader/. Mine for: route smoothing over geometry, puzzle guidance pattern.
- **Blind Soldier v0.10.11 (pre-release)** (buu420, github.com/buu420/blind-soldier) — accessibility mod for the ORIGINAL Final Fantasy VII PC (Windows; FFNx/Reloaded-II based, not Remake). Screen reader via Prism, recorded audio description (100 movie tracks + 846 short clips + battle descriptions for 93 Limit Break/summon animations), spatial audio, navigation + auto-walk, story guidance tied to progress state, minigame support (Gold Saucer, Fort Condor, submarine, chocobo racing). Very active: 341 commits, 101 releases since Aug 2026; tester-validated through the Wutai approach; no complete playthrough yet. Staged (zips, checksums verified): ~/sarah/oga-materials/blind-soldier/ (~1.5 GB incl. 2013 x86 portable + AMM packages). Mine for: AD production model, navigation categories (Story/Objects/Exits), auto-walk + progress indicators.

## Added 2026-11-07 — Sticky Business Access (Devin's find, AudioGames "Blind Gaming Mods" post)

- **Sticky Business Access v0.8.3** (MiloSummers, github.com/MiloSummers/StickyBusinessAccess) — accessibility mod for Sticky Business (cosy sticker-shop sim, Unity, Steam 2303350). BepInEx plugin, speech via Prism 0.18.3. Full keyboard play: creator navigation by section (F6 categories / F7 items / F9 placed objects), sticker placement on sheets and packing boxes with spoken coordinates, orders/shop/upgrades/messages read live, configurable day length, native-save announcements. v0.8.3 change: Production separates Saved sheets from read-only stock counts — F7 browses sheets, Enter opens Print/Edit/Delete/Close, F9 reads stock counts in Inventory. Player zip 40,845,990 bytes (sha256 d18a2e67…), source zip 290,242 bytes (sha256 169e9632…); both staged, source extracted: ~/sarah/oga-materials/sticky-business-access/ on sonix. Mine for: sectioned navigation with per-section focus memory, focus restore by stable ID, scan-discipline performance work — see tricks §33.

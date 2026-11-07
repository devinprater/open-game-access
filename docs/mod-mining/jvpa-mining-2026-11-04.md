# JVPA (Jeux Vidéo Pour Aveugles, by djblind) — mining summary, 2026-11-04

What it is: a one-author French platform that makes commercial games
blind-playable. A .NET launcher (JVPAGames.exe, 105 KB) fetches a JSON
catalogue from jeuxvideopouraveugles.fr and installs per-game packages:
Streets of Rage 1 & 2 (Genesis), Dragon: The Bruce Lee Story and Street
Fighter II Turbo (SNES), Fire Emblem: The Sacred Stones (GBA), a Pokémon
collection spanning Game Boy, GBA and Nintendo DS in one framework, plus a
native NVDA add-on for the PokerStars PC client. Games run in a
JVPA-customized RetroArch (the code began on VBA-rr, moved to BizHawk,
and migrated to RetroArch on 31/10/2026, days before these packages were
uploaded on 2026-11-03 — comments are dated through 04/11/2026, so this
is under very active development). Each game's mod code ships as one
obfuscated container (XOR-stream + LZSS) that a small plaintext loader
verifies against a ROM-CRC manifest and `load()`s. Shape of this mining:
the seven packages were downloaded and zip-verified by the parent agent;
I extracted them under `~/workspace/jvpa/extracted/`, read the plaintext
loaders, then decoded the containers with the loader's own algorithm
(payloads in `~/workspace/jvpa/decoded/`) and read the actual Lua and
Python. Full tricks filed as §29 of `accessibility-tricks.md`. The mods
were NOT run — everything below is code-proven unless marked as the
author's own bench evidence quoted from comments; play evidence is the
community's.

## The five most valuable tricks

1. **A speech/audio bridge that evolved file-protocol → emulator API,
   behind one choke point** (§29.1). BizHawk generation: the script
   appends lines to `nvda_cmd.txt` / `audio_cmd.txt` and helper processes
   consume them — `SPEAK|`, `SPEAKNOW|`, `CANCELSPEECH`,
   `PLAYRELID|id|path|VOL|PAN|PITCH`, `UPDATE|id` for continuous tones.
   RetroArch generation: the same calls land on a `sortie` global provided
   by their emulator fork, with braille as a first-class output beside
   speech. Every utterance in every game passes through a single function,
   which is what makes the next trick possible.
2. **Speech queueing under input** (§29.5). Because a screen reader
   interrupts on every keypress and held arrows auto-repeat, narration
   (dialogue, signs) is queued while a direction is held and flushed
   3 frames after release; answers to the player's own commands bypass
   the queue. Punctuation-only dialogue ("...") is spelled out, because
   screen readers don't voice it.
3. **Signature-based address relocation for ROM hacks, pre-computed at
   import time** (§29.3). Masked code signatures from the base game find
   shifted addresses in recompiled hacks; neighbour-shift fallback;
   per-ROM cache preserved across updates; the launcher runs the search
   offline in a background process when a hack is imported, so game start
   stays instant.
4. **A radar built from Manamon's measured constants** (§29.6, §29.7).
   Manamon 2's BGT audio renderer studied from its sources: per-kind dB
   pan/volume steps (person, door, stairs, item, sign, gate), wall loops
   with per-direction contact volumes, south = pitch ×0.85, door-axis
   cues, classified bump sounds. Around it: a scanner with category
   filters and user renames that survive updates, a through-walls free
   camera, and A* pathfinding over the game's own collision data spoken
   as directions.
5. **Sound-queue interception with state-based attribution** (§29.9,
   §29.10). SF2 Turbo zeroes known entries in the game's sound-request
   queue before the sound chip sees them and plays placed MP3 copies;
   music and unknown sounds pass through. The queue doesn't say who made
   a sound, so whiffs are attributed by who just entered the attacking
   state and hits are placed on the victim. Events the game never
   sonifies (jumps, projectiles in flight) get synthesized, continuously
   tracked sounds.

## The other systems worth naming

- **Fire Emblem's tactics layer** (§29.14): cursor-cell speech as
  toggleable layers (unit / HP / class / terrain / spreadsheet-style
  coordinates), the battle forecast composed in words from the game's own
  proc structs, target cycling added at an AudioGames forum request, and
  cursor travel by holding joypad directions so the game moves its own
  camera — including an attack-position solver that finds a tile the
  selected unit can reach *and* strike the chosen enemy from.
- **Touch synthesis for the DS** (§29.8): stylus-only screens (Poffin
  cooking is the worked example) played from the keyboard by writing a
  stylus position every frame, with burn/overflow thresholds taken from
  the game's decompiled code and the controlled state spoken.
- **Brawler discipline** (§29.11): at most one tracked enemy per side,
  a lock-on with a Y-line alignment cue (the belt plane is what makes
  attacks connect), attack-range alerts computed from range values read
  out of the ROM for the weapon in hand, per-threat danger modules.
- **Music replacement without a ROM** (§29.12): a 741-byte IPS patch
  neutralizes the game's music driver; the script watches the game's own
  track-id register and plays recorded renditions in sync — from game
  state, never timers. Bruce Lee's cinematics are narrated the same way,
  fingerprinting the text buffer to tell screens apart.
- **A host-compat shim as a module** (§29.13): FE's old-host API
  re-implemented over the new host — shadowed write-only hardware
  registers, callback-semantics translation, AZERTY positional key
  remapping — plus a dev journal that logs every spoken line.
- **Platform furniture** (§29.15): shared quick-states whose speech
  policy is "announce saves and empty slots, stay silent on successful
  load" (changed at user request), per-game speech/emulator volume
  companions, per-game music-volume defaults, netplay-symmetric
  RAM-writing, and a sighted-partner mode that keeps placed sounds but
  drops speech-only modules.
- **PokerStars in-process** (§29.16): an NVDA add-on (23,297 lines of
  Python) reading the client's process memory (classes found by Ghidra
  decompilation) plus UIA live objects; memory *locations* pre-warmed
  and cached on focus, contents always re-read live.

## Package accounting — what is actually in the zips

**No ROMs are bundled in any package.** An exhaustive extension search
across all seven extracted trees found zero ROM files. ROMs are
user-imported through the launcher — the catalogue says so verbatim for
Pokémon and SF2 ("Vous importez vous-même vos ROM depuis le lanceur") —
manifests pin the expected ROM by CRC32 (SF2: D43BC5A3, the USA Rev 1
dump, on snes9x), the `rom/` folders in packages are empty placeholders,
Pokémon ships empty `roms/<platform>/personnalisees/` import folders,
and Bruce Lee ships a 741-byte IPS patch *instead of* a ROM. What makes
the big zips big is **recorded audio**:

- **Streets of Rage 2 (268 MB zip):** 271 mp3 + 4 ogg — 41 recorded music
  tracks (several 9–13 MB), 53 `voices_rom` + 52 `sfx_rom` (recordings of
  the ROM's own voices/effects, replayed placed), the rest radar foley.
  The mod code itself decodes to 265 KB of Lua.
- **Streets of Rage 1 (126 MB zip):** 183 mp3 — 32 music, ~150 foley and
  cue sounds (navigation, items, enemies, breakables, player, dangers).
  Code: 615 KB decoded.
- **Bruce Lee (62 MB zip):** 27 mp3 — essentially a complete replacement
  soundtrack (tracks up to ~8 MB) plus a heartbeat cue. Code: ~99 KB.
- **Pokémon Collection (15.7 MB zip):** the 7.3 MB encrypted container
  (decodes to 19.5 MB of Lua across 396 modules — the bulk is per-game
  data tables: maps, charsets, text) plus 563 small ogg (7.7 MB) of
  environmental foley: footsteps per surface, doors per type, radar and
  sonar sounds.
- **SF2 Turbo (1.6 MB zip):** 86 mp3 (announcer 19, voices 22, hits 16,
  stage 12, map 10, menu 5, accessibility 2) and a ~50 KB code container
  (131 KB decoded).
- **Fire Emblem (2.4 MB zip):** the 1.8 MB container (4.7 MB decoded,
  including embedded game-text tables) and one wav — no recorded audio;
  it's a speech-only mod.
- **PokerStars (1.2 MB zip):** pure Python (23,297 lines) plus five
  action sounds (check/call/raise/fold/all-in wavs).

Maintenance signals are strong: file timestamps 2026-10-31 → 2026-11-03,
dated change comments that cite reasons and user requests (the FE target
cycling came from AudioGames; quick-state silence was a user request on
02/11/2026), versioned subsystems (autodetect VERSION = 8, versioned
analysis cache), and a launcher keep-list that preserves user saves,
settings, renames and hack registrations across package updates.

## Directly reusable in OGA

- The choke-point output protocol with tracked sound IDs, update-in-place
  and braille as an equal output (§29.1) — an adapter-side design OGA can
  adopt without any host changes.
- The queue-while-moving speech rule and the narration/answers split
  (§29.5), plus spelling out punctuation-only lines.
- Signature-based address relocation with offline pre-analysis (§29.3) —
  the concrete mechanism for supporting ROM hacks in memory-reading
  adapters.
- Manamon's dB radar constants, bump classification, door-axis cues and
  the pan floor (§29.6, §29.10) — drop-in numbers and one-line rules.
- The scanner + persistent user renames + ghost camera + A* pattern
  (§29.7) for tile-RPG adapters; the attack-position solver and
  joypad-driven cursor travel (§29.14) for tactics.
- "Sync from state, never timers" and ship-a-patch-not-a-ROM (§29.12);
  the dev-mode speech journal (§29.13); save/load speech policy and
  netplay symmetry as written design rules (§29.15).

## Honest gaps

- **Not run.** All findings are from reading code (decoded with the
  loader's own algorithm — the shipped code is obfuscated, which is
  itself an ecosystem caveat: the community receives mods it cannot
  easily audit or fork). Play evidence is the community's; the test
  numbers quoted (143/214 sounds placed, 248 jumps) are the author's own
  bench notes in comments.
- **The bridge's native side is not in these packages.** `jvpa_sortie.lua`
  and the RetroArch fork are provided with the emulator packages (not
  among the seven mined), so exactly how the fork calls NVDA — controller
  client vs in-process Tolk — cannot be verified from this source. The
  Lua-side contract is fully visible; the last hop is not.
- **Per-game launcher executables are opaque** (~1 MB each); I read the
  Lua they launch, not the binaries. A two-line `settings.cfg`
  (`mode = "on"`, `announce = "false"`) ships per game; which component
  consumes it isn't identifiable from the packages alone.
- **NDS Pokémon is a work in progress** by the catalogue's own wording
  ("adaptation en cours"); Platinum is the most developed, with per-game
  sub-modules still landing in November 2026.
- **Credit where due:** FE's `gbafe_*` library is pre-existing CC0
  community work integrated and extended here; the A* module is
  RapidFire Studio's (permissive header in file); the Pokémon framework
  descends from Pokémon Access (§5). JVPA's originality is the
  integration, the platform, and the sheer per-game depth — which the
  catalog entries reflect.
- Windows-only throughout (NVDA, .NET launcher, PowerShell locale
  detection), and French-first: English is a full fallback in Pokémon
  and the PokerStars add-on, partial elsewhere.

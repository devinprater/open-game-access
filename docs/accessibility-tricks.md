# Accessibility Tricks Catalog for Open Game Access (OGA)

Harvested from existing game accessibility mods, 2026-10-25.
**Source key:** [PRIMARY] = the project's own page/repo/docs, read directly.
[SECONDHAND] = press, catalogs, forum discussion or auto-generated wiki summaries
describing the project. Techniques are written so an adapter author can lift
the *idea* without the original code.

Adapter short names used below: **Saiyans** = DBZ: Attack of the Saiyans (DS),
**Harukanaru** = DBZ: Harukanaru Densetsu (DS card game, next in queue),
**LoG** = Legacy of Goku II / Buu's Fury (GBA action RPGs),
**AdvAdv** = Dragon Ball: Advanced Adventure (GBA platformer),
plus the existing PSP/DS adapters as noted.

---

## 1. Budokai Access — BlindChallengerX (Budokai 1 Access + Budokai 3 Access)

[PRIMARY — full README read at github.com/BlindChallengerX/BudokaiAccess
(renamed from Budokai3Access); current version Budokai 3 Access 1.0.11]

What it is: a screen-reader mod for the PC HD ports of Dragon Ball Z
Budokai 1 and Budokai 3, by a developer working as BlindChallengerX. It
speaks through the player's own screen reader (NVDA/JAWS/etc.) or the
Windows voice. Budokai 3 draws all text as pictures, so the mod ships the
text of every text picture on the disc, recovered with OCR. Same author
also made Tekken 3 Access for the Tekken 3 PC port; the Budokai fight
sounds deliberately reuse that design.

### 1.1 Dragon Rush: announce the shrinking choice set, in actions not buttons
- **What it does:** When a Dragon Rush starts, the mod announces who is
  attacking ("Dragon Rush! You attack" / "You defend"). Each of the 3
  exchanges then speaks the *currently legal* picks, e.g. "Attack 2: Kick,
  Guard, Ki" or "Guess 1: Punch, Kick, Guard, Ki". A button the attacker
  has already used is left out of the list, because it can't legally be
  picked again. A short click confirms your pick counted (~3 second
  window). After both sides pick, it speaks both picks and the result:
  "You Punch, opponent Ki. Hit!"
- **How it works:** The timed guessing game is converted from a visual
  button-display into a spoken, pruned menu: read the input state from
  memory each exchange, subtract already-used inputs, speak what remains,
  confirm registration with a click, then narrate the resolution. Picks
  are spoken as *actions* (Punch/Kick/Guard/Ki), never raw buttons, so the
  announcement stays correct on any controller, custom layout or keyboard.
- **Transfers to:** This is the template for **Saiyans' timed defence**.
  Attack of the Saiyans' Active Guard asks for a timed press as an enemy
  attack lands — the radar-like version: a rising cue tied to the incoming
  attack's approach (see 1.2), plus spoken confirmation of whether the
  guard counted. Also transfers to any adapter with timed input windows
  (Mortal Kombat-style prompts, BBS reaction commands).

### 1.2 Ultimate Attacks: a pitch-ramped "meter tone" for timed presses
- **What it does:** When an Ultimate starts: "Ultimate! Press as the tone
  tops out, 3 presses." Each fighter's meter sweeps up and restarts almost
  twice a second for 4 seconds; your score is the meter position when you
  press. A tone climbs with *your* meter, a soft low beat marks each
  restart — press just before the top, just before the beat. Each press
  clicks. At the end, both scores and the winner are spoken (100 per press
  is perfect). The fight distance sound ducks (goes quiet) for the whole
  Ultimate so the meter tone owns the ear.
- **How it works:** A periodic timing window is rendered as a sawtooth
  pitch ramp synchronised to the game's internal meter value, read from
  memory each frame; a distinct marker sound at the wrap point defines the
  "ideal press" moment by ear. Non-speech cue + one spoken setup line +
  spoken results = the full pattern for any rhythm/timing mechanic.
- **Transfers to:** **Saiyans Active Guard directly** — a short ramp whose
  peak is the guard moment is exactly the "radar" the user asked for.
  Also: fishing/timing minigames in LoG, BBS shotlock-style holds.

### 1.3 Fusion & Potara: spoken sequences, gated start, ascending notes
- **What it does:** Fusion speaks a 5-action sequence to copy ("Fusion!
  Press Kick, Guard, Punch, Ki, Punch"; changes every time). Presses only
  count after a low beep ~1 second in (early presses are ignored, not
  mistakes); you then have 3 seconds. Each correct press plays a note one
  step higher. One wrong press ends it; failures say why ("Wrong button"
  / "Too slow") and how many you got right. Potara is a single Dragon
  Rush-style exchange with the same announce-both-picks resolution.
- **How it works:** Sequence memorisation games become listen-and-repeat:
  speech delivers the pattern, a gate beep separates "listen" from "input"
  phases, pitch steps give progress feedback without speech, and failure
  is reported with cause + partial score.
- **Transfers to:** Any Simon-says / command-input mechanic (training
  sequences in Saiyans, MK tutorials, BBS command melding feedback).

### 1.4 Distance sound: one continuous tone encodes range, side, height
- **What it does:** During fights a quiet hum tracks the opponent: pitch
  and loudness rise as they close in ("a low murmur when far apart, a soft
  mid hum when you are touching"). It pans to the opponent's side of the
  screen; texture differs by side in the alternate "steady tone" mode
  (reedy = opponent on your left, smooth = on your right). Vertical
  position: a soft chirp slides up (above you) or down (below you) every
  so often. Starts at "Fight!", stops at round end, silent while paused.
  Three user settings: subtle hum (default), steady tone, off — remembered
  between sessions. A matching query key (F6) speaks the same information
  in words: "mid range, to your right, 27 units, above you".
- **How it works:** One oscillator whose pitch/loudness/stereo position
  are driven by the opponent's relative position read from memory —
  continuous sonar instead of repeated speech. The query key gives the
  same state on demand in words: sound for tracking, speech for checking.
- **Transfers to:** **AdvAdv platforming** (enemy proximity while
  platforming), LoG real-time combat spacing, MK/Dissidia-style fighters
  generally. The "same data as sound *and* as a spoken query" pairing is
  the reusable pattern.

### 1.5 Ring-edge proximity knock
- **What it does:** On the ring stage, a short loud knock plays from the
  direction of the nearest ring edge once you're within about half the
  ring's width — knocking faster and louder as you approach, up to a rapid
  rattle at the edge. F6 speaks the edge distance in words.
- **How it works:** A hazard boundary becomes a proximity alarm: pulse
  rate and volume map to distance-to-danger, direction is spatialised.
- **Transfers to:** Platformer ledges/pits in **AdvAdv**, cliff edges in
  LoG, any "you are about to fall / be rung out" geometry.

### 1.6 Dragon Universe guide beep: a steering beacon for open-map flight
- **What it does:** The mod keeps a "guide target" (by default the next
  story event). A repeating beep comes from the target's direction: high
  pitch when it's ahead of you, low when behind, brighter the straighter
  you're heading at it, faster as you get closer. Turn until the beep is
  high, bright and centred, then fly forward. A dedicated key cycles the
  target through a spoken marker list — story events first, then
  everything else nearest-first — announcing "2 of 10, Kame House, 5189
  units, turn left 40". Other keys: where the target is now (F6), and
  "the place you are in plus every marker within 3000 units" (F8).
  Distances wrap correctly around the map edges.
- **How it works:** A heading-error sonification: relative bearing drives
  pitch/timbre, distance drives tempo, and a spoken list provides the
  discrete "map" (name, distance, turn angle) that the continuous beep
  can't. Branching objectives are handled by counting and listing the
  extras ("2 story events… Also Story, Kami's Lookout").
- **Transfers to:** **LoG overworld travel** (fly/walk to towns and story
  points), Saiyans field navigation, BBS world traversal. This is the
  general shape of "radar" navigation: beacon for steering + list for
  choosing.

### 1.7 On-demand status keys & speech discipline
- **What it does:** F8 in a fight: both fighters' health % and ki in bars
  ("You: health 77 percent, ki 3.0 bars. Opponent: health 100 percent, ki
  3.3 bars"). F8 in the Skill Shop: your Zenie. F12 repeats the last
  announcement. Health warnings fire only at thresholds (below half,
  below a quarter). Player-side correctness is checked every match: in
  World Tournament you may be player 2, and the mod verifies which side
  you control so every cue is about *you*. Audio latency is treated as a
  feature: the launcher shrinks the port's sound queue (~25 ms off every
  game sound) because cue timing is gameplay. Mod sounds have their own
  volume (Page Up/Down, 10% steps, remembered).
- **How it works:** Push speech is minimal and event-driven; everything
  else is pull (query keys). Every automatic announcement is something
  that *changed* (threshold crossed, exchange resolved).
- **Transfers to:** All adapters — this is OGA's own doctrine confirmed by
  a shipping project: thresholds + queries, repeat-last, side-checking.

### 1.8 Menu speech convention: item + position, every list, everywhere
- **What it does:** Every menu/cursor speaks the item and its place:
  "Dueling, 2 of 6", "Custom, 2 of 3", "Masenko, 1500 zenie, 1 of 3".
  Locked things say "Locked". Option changes speak the new value ("COM
  Level: Weak"); F5 re-reads the current row. Shops speak your money on
  entry and your balance after each purchase. Skill lists include cost
  and the command in words ("Kamehameha. 1 Ki gauge consumed. forward
  kick, punch, punch, punch, ki. 3 of 5"). Whole static screens (the
  controller layout) are read out in full when they open.
- **Transfers to:** Saiyans/Harukanaru menus and shops; universal.

---

## 2. Hearthstone Access — Guide Dev

[SECONDHAND, well-corroborated — J.J. Meddaugh's review in AFB
AccessWorld (afb.org/aw/22/10/17720), Eurogamer and PC Gamer coverage;
project site hearthstoneaccess.github.io linked from the review]

What it is: a one-person mod (Guide Dev, from Aug 2021) that made
Blizzard's Hearthstone fully playable blind: keyboard navigation +
screen-reader speech over the game's own data. It patched the game client
itself; Blizzard cooperated informally (advance patches) but never built
native support; Guide Dev ended support at patch 24.6 (2022), which is
itself a lesson (see 2.5).

### 2.1 Zone keys: one key per region of the game
- **What it does:** A small set of keys jumps focus straight to a region:
  C goes to your hand; other keys reach the boards, heroes, etc. Inside a
  region, Left/Right move through its items one by one, each announced;
  Down Arrow repeats/expands the focused item's details step by step
  (for a minion: stats as "3-2"; mana cost; card text), and I explains
  any keyword (e.g. Taunt) on demand.
- **How it works:** A card game is flattened into a few stable lists
  (hand, your board, their board) with direct-jump keys instead of a
  free cursor. Detail is layered: name on focus, numbers on one key,
  full text on another, glossary on another.
- **Transfers to:** **Harukanaru Densetsu is the direct beneficiary** —
  its hand of power cards and the two battle rows map onto exactly this
  scheme. Also Saiyans battle menus.

### 2.2 Narration of opponent actions as they happen
- **What it does:** Everything the opponent does — cards played, attacks,
  hero power — is spoken as it occurs, so the blind player holds the same
  running picture as a sighted one. Combined with the zone queries, you
  can re-check any part of the state at any time.
- **Transfers to:** Harukanaru (opponent card plays drive the whole
  game), Saiyans battle narration generally.

### 2.3 Slowing the game against bots
- **What it does:** Tools to slow the game down when playing against bots,
  giving more time to review state and decide (reported in TheGamer's
  coverage of the mod).
- **Transfers to:** A "review speed" or extended-timer option for timed
  mechanics in turn-based adapters (Saiyans, Harukanaru) — change the
  clock, not the rules. (Where an emulator is the host, slowdown/save
  states at decision points achieve the same.)

### 2.4 Mulligan and decision screens are ordinary menus
- **What it does:** Even the opening mulligan (keep/discard each card) is
  navigable and spoken like any other list, so no game phase is a blind
  spot.
- **Transfers to:** Harukanaru setup phases, deck/discard choices in
  Saiyans (item use, capsule choices).

### 2.5 The maintenance lesson (process, not a feature)
- **What happened:** One volunteer re-patching a live-service client
  every ~2 weeks eventually burned out; the community lost access.
  Blizzard's own follow-up was limited to audio-mix improvements
  (separate volume sliders, mono option).
- **Transfers to:** OGA's adapter-over-emulator architecture is the
  hedge: read game state from outside so game updates don't break the
  reader. Also: mono output and separate speech/effects volume are
  cheap, valuable settings for any adapter.

---

## 3. Accessible Arena — Jean Stiletto (MTG Arena)

[PRIMARY — project site read in full: jeanstiletto.github.io/AccessibleArena;
repo github.com/JeanStiletto/AccessibleArena; ko-fi.com/jeanstiletto.
Review corroboration: victordima.net]

What it is: a MelonLoader mod by Jean Stiletto, a fully blind developer,
adding screen-reader support (NVDA/JAWS tested; Prism speech library
reaches many readers + system voice fallback) and full keyboard control
to Magic: The Gathering Arena — menus, deck building, collection, drafts,
and matches. Jean explicitly followed Hearthstone Access's design
principles ("I followed a lot of design principles"), then extended them
for Magic's extra complexity — so differences vs. section 2 show where
the card-game pattern scales.

### 3.1 More zones, sorted rows: scaling the zone-key model
- **What it does:** Zone keys multiply: C Hand, G Graveyard, X Exile,
  S Stack, W Command Zone; Shift+letter for the opponent's matching
  zones; B/Shift+B battlefield creatures (yours/opponent's), A lands,
  R non-creatures. Because Magic battlefields are unlimited in size,
  the battlefield is split into *sorting rows* (creatures / lands /
  other), with Shift+Up/Down switching rows. Inside a zone: Left/Right
  to move, Up/Down for details, I for extended info.
- **Transfers to:** Harukanaru if its board grows crowded — split rows by
  card type rather than one long list. Also any busy field screen in
  LoG/Saiyans (party + enemies as separate jump targets).

### 3.2 Short vs long announcements (name first, full text on request)
- **What it does:** Focusing or playing a card announces just its name by
  default; full card text lives behind the detail keys (Up/Down, I).
  (Independently argued by Lucas Radaelli's design essay on MTG Arena
  access: experienced players recognise cards by name; making them hear
  full text every time slows play. New players can always ask for the
  long version.)
- **Transfers to:** Everywhere, and especially Harukanaru: speak card
  name + power on focus; full effect text on the detail key.

### 3.3 Game log + repeat last announcement
- **What it does:** O opens the game log — a navigable list of recent
  duel announcements, so anything spoken while you were busy can be
  re-read. Ctrl+R repeats the last announcement. T announces turn/phase,
  L life totals — small factual query keys for the numbers players check
  constantly (also library counts, opponent hand count, land summaries,
  timers).
- **Transfers to:** Harukanaru + Saiyans directly: a "what just
  happened" history buffer is the fix for fast opponent turns; query keys
  for turn/round, both health totals, hand sizes.

### 3.4 Phase stops and pass controls: giving the player the clock
- **What it does:** Magic lets you act on the opponent's turn, so the
  mod exposes the game's timing controls as keys: number keys 1–0
  toggle stops at each phase (Upkeep … End Step); P toggles full control
  (temporary), Shift+P locks it; Shift+Backspace = pass until opponent
  acts; Ctrl+Backspace = skip the whole turn.
- **Transfers to:** Design lesson for timed/reactive mechanics
  (Saiyans Active Guard): expose *when* you want to be interrupted as a
  setting instead of hard-wiring every event; let players choose their
  attention points.

### 3.5 Targeting by cycling legal targets
- **What it does:** Tab cycles through legal targets (Ctrl+Tab switches
  filter: all targets / opponent only); Enter selects. No spatial
  cursor: targeting is a pruned list, exactly like Budokai's Dragon Rush
  choice list (1.1).
- **Transfers to:** Saiyans technique targeting, Harukanaru attack
  target choice — enumerate legal targets, cycle, confirm.

### 3.6 Browser pattern for choose-from-a-set screens
- **What it does:** Scry/Surveil/Mulligan screens: Tab navigates all
  cards, C/D jump between the top and bottom zones, Enter toggles a
  card's placement. Draft (pick a card vs bots or real players, with the
  pick timer) is fully supported.
- **Transfers to:** Any look-at-N-cards-and-sort-them moment (Saiyans
  capsule/wish choices, Harukanaru hand management).

### 3.7 Screen-context help
- **What it does:** F1 opens help listing all shortcuts for the *current*
  screen; Ctrl+F1 announces them; F3 announces the current screen; the
  mod adds its own tutorial hints alongside the game's tutorial.
- **Transfers to:** Universal — adapters should self-describe per screen,
  not rely on an external manual.

### 3.8 What Wizards of the Coast themselves did (for the record)
[SECONDHAND — mtgrocks/Reddit reporting] WotC has made no native
screen-reader support for Arena; low-vision players rely on the client's
card-enlarge view, its dark high-contrast menus, distinctive audio cues
and heavy automation of rules bookkeeping. Everything in 3.1–3.7 is the
community mod, not the publisher.

---

## 4. Say the Spire (+ Say the Spire 2) — Brad Renshaw et al.

[PRIMARY-ish — the mod's own Steam Workshop description; SECONDHAND —
DeepWiki architecture summaries and gamingaccessibility.org's Say the
Spire 2 article. Earlier sibling mod: TextTheSpire by Wensber.]

What it is: a mod adding screen-reader support, keyboard/controller
navigation and structured information access to Slay the Spire, the
deck-building roguelike — the closest PC relative of what Harukanaru
needs (cards + a branching map).

### 4.1 Information buffers: separate reviewing from doing
- **What it does:** Game state is organised into buffers (cards,
  monsters, relics, potions, player status, UI) you can browse with a
  dedicated control (right stick / Ctrl+arrows) *without moving the
  game's actual cursor*. You can compare the card you're considering
  against your energy, block and enemy intent before committing.
- **Transfers to:** Harukanaru: a "review mode" for hand/board that
  never risks an accidental play. Saiyans battle planning likewise.

### 4.2 The map as a tree you can inspect before you commit
- **What it does:** The branching map is presented as a navigable tree:
  you can walk possible routes and hear what each node is (combat, shop,
  rest, unknown) before choosing, separating inspection from selection.
- **Transfers to:** Harukanaru's map/board movement (its story progress
  is map travel), LoG world maps, BBS Command Board planning.

### 4.3 Enemy intent, spoken as state
- **What it does:** Combat narration covers draws, buffs/debuffs and —
  crucially — what each enemy *intends* to do next turn, because the game
  shows it visually and blind players need it to plan.
- **Transfers to:** Saiyans (announce telegraphed enemy attacks — which
  also tells the player when an Active Guard moment is coming), any
  turn-based adapter.

### 4.4 Speech verbosity is configured, not fixed
- **What it does:** Settings choose whether positions ("1 of 3"), types
  ("card", "relic"), banner text, proceed prompts and obtain-events are
  read at all, and can exclude whole type-words from announcements.
- **Transfers to:** Per-adapter verbosity settings in OGA: terse for
  experts, full for learners — one switch, not a rewrite.

---

## 5. Pokémon Access — nuiv (Lua scripts, GB/GBC/GBA/DS Pokémon) and successors

[MIXED — AudioGames.net forum discussion of the scripts (community,
SECONDHAND); Pokémon 4th Gen Access repo by arqmeister is PRIMARY for
its own existence/scope: "screen-reader access to 4th gen pokemon
games", github.com/arqmeister/pokemon-4th-gen-access]

What it is: Lua scripts running inside emulators that read the Pokémon
games' memory and speak everything: dialogue, menus, party/battle state,
and your surroundings — for years the standard way blind players play
mainline Pokémon. (OGA's own Stadium work is a cousin of this.)

### 5.1 Warp/door lists + pathfinding to a chosen exit
- **What it does:** The scripts enumerate the current map's warps
  (doors, stairs, cave exits) in a list; pick one and the script computes
  and guides/walks the path to it. Players discuss needing warps
  labelled distinctly ("Stairs up" vs "Stairs down") because identical
  names make places impossible to tell apart — the scripts play
  different sounds when passing stairs going up vs down.
- **How it works:** The map's warp table is data in memory; a path is
  found over the tile collision map. Guidance is spoken step guidance
  plus collision feedback en route.
- **Transfers to:** Saiyans/LoG towns and dungeons — list exits by
  destination name (never bare duplicates), pathfind, and give stairs/
  doors distinct sounds.

### 5.2 Footstep and wall feedback while walking
- **What it does:** Movement is accompanied by sounds for steps, bumping
  into walls/objects, and entering new areas, so free walking is
  possible between pathfinding hops; the current map/area name is
  speakable on demand.
- **Transfers to:** LoG and AdvAdv free movement; Saiyans field gaps
  (audit item 6/7: area names and interaction).

### 5.3 Battle state fully spoken
- **What it does:** Menus, both sides' Pokémon and HP, move lists and
  effects — the games are completely playable including battles, trading
  and multiplayer in later versions.
- **Transfers to:** Reference point for Saiyans battle wiring (items
  8–10 of its audit): menus + state + results, in that order.

---

## 6. Pokémon Reborn "blindstep" — the Reborn team (official, built-in)

[PRIMARY — the developers' own accessibility records,
rebornevo.com/pr/development/records/accessibility-r109]

What it is: accessibility features shipped *inside* the fan game Pokémon
Reborn, enabled by the password "blindstep", with screen-reader speech
via NVDA + Tolk-style FFI hooks. Worth its own section because several
tricks here don't appear anywhere else in this survey.

### 6.1 Contextual bump sounds: the obstacle tells you what it is
- **What it does:** Bumping into things plays *different sounds by what
  you hit* — NPC, strength boulder, item ball, and about ten other
  categories — instead of one generic thud. (Improvement shipped for all
  players, not just blindstep.)
- **Transfers to:** LoG/AdvAdv/Saiyans field: "you bumped a person / a
  sign / a wall" as sound first, speech on query.

### 6.2 Ambient state sounds
- **What it does:** Standing somewhere you can dive or resurface plays a
  background sound; ledge-jump stones and rock climbs got their own
  sounds; fishing got sounds so blind players can time the catch.
- **Transfers to:** Saiyans: a subtle ambient cue for "an Active Guard
  opportunity is live" and other state-dependent spots; LoG interactable
  ambience.

### 6.3 Coordinates on a key + a simplified fly menu
- **What it does:** F9 speaks the current map and your coordinates. The
  big map was hard to use blind, so blindstep swaps in a separate,
  list-based fly menu. There is also a custom item finder (by Orsan).
- **Transfers to:** Replace (don't imitate) any visual map screen with a
  list — Budokai Access makes the same call with its marker list (1.6).

### 6.4 Puzzle skips as an accessibility feature
- **What it does:** 40+ puzzle skips exist so a puzzle that can't be made
  accessible can be stepped over instead of blocking progress.
- **Transfers to:** Honest fallback design for adapter dead-ends
  (e.g. Harukanaru/Saiyans set-pieces that resist reading): a labelled
  skip beats an unplayable wall.

---

## 7. Stardew Access — khanshoaib3 (Stardew Valley)

[SECONDHAND summaries of the project's own repo/docs — DeepWiki feature
pages for stardew-access/stardew-access; GamesRadar coverage of blind
players using it]

What it is: a SMAPI mod making Stardew Valley fully playable blind:
screen reader output, keyboard control, and a set of spatial tools that
are the most developed "farming-sim navigation" design in this survey.

### 7.1 Radar: 360° spatial pings for nearby notable things
- **What it does:** A radar plays spatial sounds for nearby objects and
  NPCs — NPCs, animals, machines, containers, buildings, doors,
  interactables — while deliberately *excluding* noise categories
  (stone, weeds, crops, trees, flooring, water, debris, grass,
  decorations).
- **How it works:** Category filtering is the trick: the radar only
  sonifies what a player plausibly wants to find, so the soundscape
  stays readable instead of becoming noise.
- **Transfers to:** LoG towns (NPCs/doors/shops radar), AdvAdv item and
  enemy radar, Saiyans field — and this is literally the "radar" family
  the user named for Active Guard: filtered, proximity-driven sound.

### 7.2 Object Tracker: categories, favourites, auto-walk
- **What it does:** A tracker lists objects by category, sortable by
  distance, with 10 favourite slots per stack (per location) and saved
  coordinates; selecting an object pathfinds and auto-walks you to it.
- **Transfers to:** Quest/objective tracking in LoG; "take me to the
  shop/save point" in Saiyans.

### 7.3 ReadTile + TileViewer: query the world cell by cell
- **What it does:** A key reads the tile you face; another the tile you
  stand on. A separate tile cursor can be moved around (tile or pixel
  steps), locked to follow you, and you can auto-walk to the cursor or
  open detailed info on its tile.
- **Transfers to:** AdvAdv/LoG inspection: "what's in front of me"
  queries, and a scan cursor for learning a room's layout safely.

### 7.4 Grid movement
- **What it does:** Optional tile-aligned movement (configurable speed,
  tiles per step, first-step delay) so players can count steps exactly —
  vital for precise work like planting. Companion mod "Accessible
  Tiles" grew out of the same need. Warps/doors are detected during
  movement so transitions are announced.
- **Transfers to:** Tile-counted movement mode for LoG dungeons and
  Saiyans fields — pairs with pathfinding (5.1): pathfind roughly,
  step-count precisely.

### 7.5 Warnings and "walked into / standing on" events
- **What it does:** Low health/stamina warnings and status alerts; the
  game announces what you walk over or into (a bed, a working keg) as
  you touch it.
- **Transfers to:** Universal event announcements for contact/trigger
  tiles in every top-down adapter.

---

## 8. Terraria Access — ChipsAhoiMcCoy (Terraria, tModLoader)

[PRIMARY via the repo README — github.com/ChipsAhoiMcCoy/TerrariaAccess]

What it is: a tModLoader mod making Terraria — a 2D sandbox platformer
with free aiming — playable blind. Its spatial-audio list is the richest
in this survey and directly relevant to AdvAdv.

### 8.1 A full spatial-cue vocabulary for 2D platforming
- **What it does:** Positional sounds for: footsteps, enemy proximity,
  other players' footsteps, cursor position, UI slots, wall collision,
  **edge detection**, **fall proximity**, **passage detection**,
  **overhead traversal cues**, and a **cavity sonar** (pinging hollow
  spaces). Plus "guidance tracking" (a beacon) to NPCs, waypoints,
  dropped items, hostiles, ores, chests and dozens of target types, and
  boss attack warnings in the announcement stream.
- **Transfers to:** **AdvAdv wholesale**: edge and fall warnings +
  enemy-proximity + a guidance beacon is most of a platformer kit.
  Boss-attack warnings pair with 4.3 (spoken enemy intent) for Saiyans.

### 8.2 Keyboard gamepad emulation for pointer-only flows
- **What it does:** Controller-only/pointer UI flows (inventory sections,
  smart select, shop selling, dialogue focus) get a keyboard layer that
  emulates the gamepad cursor.
- **Transfers to:** Any DS touch-only screen in Saiyans/Harukanaru:
  emulate the touch cursor with keys rather than skipping the screen.

### 8.3 Build Mode
- **What it does:** Keyboard-driven tile/wall placement and terrain
  shaping — construction as list/coordinate operations, not aiming.
- **Transfers to:** Creative/building mechanics generally: convert
  spatial placement into "current position + direction + confirm".

---

## 9. Final Fantasy VI Pixel Remaster access mod — zkline

[SECONDHAND — inviocean games catalog entry describing the mod and its
hotkeys; attribution to zkline (Zack) of the AudioGames community is
from the same entry. Part of a 2026 run of Pixel Remaster access mods
(FF1–FF6) by blind modders.]

What it is: a mod making FFVI Pixel Remaster playable with NVDA: full
narration of dialogue, battle narration, spoken menus.

### 9.1 Area scan with type filters
- **What it does:** A scan lists all active objects in the zone *with
  distances*, filterable by type (NPCs, chests, exits, event points),
  each with state (interactable? already opened? moving?).
- **Transfers to:** Field adapters everywhere; note the state flags —
  "chest, unopened" vs "chest, opened" is the difference between a map
  and a checklist. (Same pattern as Stardew's tracker + Budokai's F8.)

### 9.2 Two ways to get there: spoken step directions, or teleport
- **What it does:** After choosing a scan target: step-by-step
  navigation ("step right, three steps up") or an outright teleport to
  the target. The teleport exists because some areas/puzzles weren't
  made navigable — an escape hatch so the story never hard-blocks.
- **Transfers to:** Adapter-level fallback for unmappable rooms; pair
  with 6.4 (labelled skips) — honesty over dead ends.

### 9.3 Battle/event narration completeness
- **What it does:** All major story events accessible; dialogue fully
  narrated (the mod's reason to exist is the story).
- **Transfers to:** Baseline expectation for RPG adapters: if dialogue
  isn't readable, nothing else matters (cf. Saiyans audit item 4).

---

## 10. Negative results & loose ends

- **Budokai Tenkaichi:** no blind-access mod or script found (searched
  GitHub + AudioGames). What exists in 2026 is a decompilation-based PC
  port (z3xox/Tenkaichi3Decomp) with no accessibility features — a
  possible future host for an Access-style mod, nothing to harvest yet.
- **Tekken 3 Access** (BlindChallengerX, Tekken 3 PC port): the origin
  of Budokai Access's distance tone (1.4) — "speech, fight sounds,
  audio-described movies" per the author's profile; not separately
  mined here beyond that lineage.
- **Hades II Access** (FlatArther, Thunderstore) [SECONDHAND — its own
  store page]: a tracked-target design worth remembering — cycle
  *categories* of things to track, a beacon to the tracked target, wall
  collision sounds, collision announcements ("speak the name of objects
  you collide with"), and configurable spoken info order for target
  checks (Name, Health, Distance in your chosen order). Transfers to
  AdvAdv/LoG tracking design.
- **Dragon Ball-specific search variants tried:** "Budokai Access",
  Budokai3Access repo, Tenkaichi scripts, Shin Budokai PPSSPP scripts
  (only generic emulator pages). The Budokai line is the DBZ access
  scene's centre of gravity.

---

## Quick-pick index (by OGA need)

- **Timed press (Saiyans Active Guard):** 1.2 (pitch-ramp meter tone) +
  1.1 (announce legal choices, click to confirm) + 3.4 (let the player
  choose their interrupt points) + 2.3 (slow the clock vs CPU).
- **Card battles (Harukanaru):** 2.1/2.2 (zone keys, opponent narration) +
  3.1–3.3 (rows, short/long text, log + queries) + 3.5/3.6 (target
  cycling, browser pattern) + 4.1/4.2 (review buffers, map tree) + 4.4
  (verbosity settings).
- **Action RPG fields/combat (LoG):** 1.6 (guide beacon + marker list) +
  7.1/7.2 (filtered radar, tracker + auto-walk) + 5.1/5.2 (warp lists,
  pathfinding, bump/step sounds) + 6.1 (contextual bumps) + 7.4 (grid
  movement).
- **Platforming (AdvAdv):** 8.1 (edge/fall/enemy cues, cavity sonar) +
  1.4/1.5 (proximity tone, edge knock) + 7.3 (tile queries).
- **Everywhere:** 1.7/1.8 (threshold warnings, query keys, item +
  position menus, repeat-last), 3.7 (per-screen help), 9.1 (state-aware
  area scan), 6.4/9.2 (labelled skips/teleports as honest fallbacks).

---

## 11. Survey source: Molitvan's blind-accessible-games list

[The list itself: github.com/Molitvan/blind-accessible-games-list — "A list
of all blind-accessible videogames", by Molitvan. Started as a June 2024
Reddit thread, then a Gist, moved to this repo Feb 2026; ~250 entries with
an accessibility grade for each (fully / mostly / playable / partial /
work in progress). Read in full 2026-10-25. It is a curated SECONDARY
source: grades and one-line descriptions are the list author's, and each
entry links out to the mod. Use it as the standing watch-list for new
access mods — it is clearly being updated continuously.]

**Cross-check against this catalog:** all five expected projects are on
it — Say the Spire (listed "fully accessible"; Say the Spire 2 likewise),
Hearthstone Access ("fully accessible"), Accessible Arena for MTG Arena
("fully accessible"), Stardew Access, and Pokémon Access ("all GBC and
GBA Pokemon games… with the Pokemon Access mod"). Pokémon Reborn, the
FF1–FF6 Pixel Remaster screen-reader mods, Terraria Access, Hades II
Access and the Toby Doom mod are also there. **Budokai Access is NOT on
the list** (as of this reading) — the list's Dragon Ball entries are
Sparking! Zero and Kakarot instead (see 11.1). A gap in the list, not in
the project: Budokai Access is verified from its own repo (section 1).

### 11.1 Mod-driven games mined from the list (not already in sections 1–10)

Logged as candidates: game — mod — where it lives — why it matters here.
Unless a section above says otherwise, these are [SECONDHAND via the
list]; technique detail was not individually verified for each.

**Dragon Ball (highest priority):**
- **Dragon Ball Z: Kakarot** — DBZ Kakarot Access mod — announced on
  AudioGames (see section 12.1 for the author's own feature list, incl. a
  3-D audio radar). List grade: work in progress.
- **Dragon Ball Sparking! Zero** — Sparking Zero Access mod — work in
  progress; the list notes the game is "accidentally playable" without
  it. (Successor series to Tenkaichi; still no Tenkaichi-era access mod.)

**Card / deck games (for Harukanaru):**
- **Balatro** — the Blindfold mod — fully accessible per list; AudioGames
  New Releases thread mined in 12.2.
- **Marvel Snap** — Snap Access mod — work in progress.
- **Pokémon TCG Live** — a mod hosted at accessibilitymods.com — listed
  fully accessible.
- **XMage** (open-source MTG clone) — XMage Access mod — fully
  accessible; a second data point, after Accessible Arena, that the
  zone-key design works for full Magic rules.
- **Yu-Gi-Oh! Duel Links** — Duel Links Access mod — work in progress.
- **Yu-Gi-Oh! Master Duel** — Blind Duel mod — mostly accessible.
- **Monster Train / Monster Train 2** — access mod / MT2 Access mod —
  deckbuilder combat on a multi-lane board; work in progress.
- **Super Auto Pets** — SAP Access mod — auto-battler; forum playtesting
  thread noted in 12.4.
- **Hand of Fate / Hand of Fate 2** — Hand of Fate Access mod / a mod —
  card-driven action RPG (a deck literally builds the world); WIP /
  unclear status.
- **The Bazaar** — Bazaar Access mod — partially accessible; forum
  thread linked from the list.

**Turn-based / RPG / tactics:**
- **Dragon Warrior 1** (NES, emulated) — Dragon Warrior Access mod —
  work in progress. Emulator-driven like OGA: an existence proof for
  classic JRPG adapters.
- **Zelda 1** (NES, emulated) — Zelda1Access mod — fully accessible per
  list. Directly relevant if OGA ever takes on top-down action-adventure.
- **Persona 4 Golden** — Persona 4 Golden Access mod — WIP; a big
  menu/social/dungeon JRPG made readable.
- **Darkest Dungeon** — Blindest Dungeon mod — base game fully
  accessible; **Darkest Dungeon II** — Harkest Dungeon mod — fully
  accessible. Party-position turn-based combat narration.
- **Fire Emblem: The Sacred Stones** (GBA, emulated) — a mod — partially
  accessible, status unclear; grid tactics is the hard case for speech.
- **Star Ocean: The Second Story R** — SO2RAccess mod — WIP.
- **Digimon Story: Time Stranger** — a mod at accessibilitymods.com —
  fully accessible (controller required); **Digimon World: Next Order**
  — DigimonNOAccess mod — fully accessible; **Digimon World 2** — via
  the RetroArch Accessibility project (below).
- **Heroes of Might and Magic 3** — VCMIAccess mod — WIP; turn-based
  strategy on a big map.
- **Pokémon Generation 5** (DS, emulated) — a project at
  accessibilitymods.com — WIP; extends the Pokémon Access line to gen 5.

**Emulator-level access (architecture cousins of OGA):**
- **RetroArch Accessibility project** — emulator-wide screen reading,
  applied per-core; the list hangs Digimon World 2 off it. The
  alternative architecture to OGA's per-game adapters: shallower per
  game, broader coverage. Worth watching as a complement, not a rival.

**Process/technique notes spotted in list descriptions:**
- **Crusader Kings III mod** achieves full playability *via live OCR*
  rather than memory reading — the fallback technique when a game's
  state can't be read directly. (Budokai Access uses shipped OCR text
  the same way for its picture-text; OGA's memory-first approach is the
  stronger default, OCR is the escape hatch.)
- **Factorio Access** ships "an in-game tutorial specific to the mod
  that teaches you both the game and the mod" — the onboarding pattern
  Accessible Arena also uses (3.7). Adapter tutorials are a feature,
  not documentation.

---

## 12. AudioGames.net forum finds (New Releases room, Play testing, digests)

[Forum: forum.audiogames.net — reachable from this machine. Sources below
are the forum's own threads and its Monthly Digest, which republishes
release posts. An author's own release post = PRIMARY for what the mod
does; other posters' comments = community experience, marked as such.]

### 12.0 Budokai Access on AudioGames: targeted check — negative
Three search-index queries aimed at the forum (project name, author
name, room + game) surfaced **no dedicated AudioGames release thread**
for Budokai Access. Everything indexed points back to GitHub (repo +
releases). The project is very new (repo created Oct 2026) and GitHub is
clearly its home; if a forum thread exists it is not yet indexed. The
catalog's section 1 therefore stands on the project's own README, which
is the stronger source anyway. (The forum *does* host the other Dragon
Ball release — Kakarot Access, next.)

### 12.1 DBZ Kakarot Access — the second Dragon Ball access mod
[PRIMARY via republication — the author's release announcement, quoted
in the AudioGames.net Monthly Digest, July 2026]

- **What it is:** An accessibility mod for Dragon Ball Z: Kakarot
  (Steam, Windows) — an open-world action RPG. Reads menus, dialogue and
  HUD through NVDA/JAWS/SAPI; controller required; nothing changes for
  sighted players.
- **12.1.1 3-D audio radar with a category target picker.** A 3-D beacon
  tracks your quest objective with an arrival cue; a picker key (R3)
  cycles target *categories* — enemies (announced with their level),
  NPCs, collectibles, Dragon Balls, shops, gathering points. Menus all
  read (pause, shops, items/party/characters, save/load, options, skill
  palette and tree, cooking, fishing); status sheet speaks level, EXP,
  HP, Ki, Battle Power and all stats; story dialogue, subtitles,
  tutorials, quest objectives, chapter cards, level-up and pickup
  notifications all spoken. In-game config menu (L3+R3); 13 languages
  via editable text files.
- **Transfers to:** This is Budokai's guide beep (1.6) grown into an
  action-RPG navigation system — the closest published relative of what
  **LoG II / Buu's Fury** will need: objective beacon + arrival cue +
  category picker instead of one fixed target. The editable-text-files
  localisation trick is also worth stealing for adapter line tables.

### 12.2 Balatro — Blindfold mod (New Releases thread)
[Thread: "Balatro accessibility mod available!", New Releases room,
forum.audiogames.net, Aug 2024, 13 pages; mod name per Molitvan (11.1).
Thread discussion = community experience, SECONDHAND for mod design.]

- Balatro is a poker roguelike: a hand of cards, jokers that trigger
  left-to-right, score = chips × multiplier built live. The thread shows
  blind players completing runs with the mod.
- **Reordering as a mechanic:** joker order matters (they trigger in
  sequence) and reordering IS accessible — hold the select button on a
  joker and move it — but the mod does not announce where the card ends
  up; you must release and re-check. Lesson for Harukanaru: any
  reorder/rearrange action needs position feedback *during* the move,
  not after.
- **Drag-position sound (player-requested, not yet in the mod):** a
  sound while dragging that pans/changes with position — suggested by
  analogy to Animal Crossing's menu panning. Same family as Budokai's
  distance tone: continuous sound for a continuous variable.
- **Knowing where the ends are:** players manage hand edges by sorting
  (by rank or suit) and counting (a standard hand is 8) — evidence that
  predictable ordering + position counts (cf. 1.8) carry real play.
- Notable thread participants: Jean Stiletto (Accessible Arena) and
  zkline (FFVI PR mod) play and advise here — the card-mod authors
  cross-pollinate in this one room.

### 12.3 Rift Wizard 2 — Words of Power mod: speech triage for dense turns
[PRIMARY via republication — author's release text quoted in the
AudioGames.net Monthly Digest, April 2026; repo
github.com/EarthboundPromoter/Words-of-Power]

- **Design rule, in the author's words:** "It will never play the game
  for you. It tells you what a sighted player would see… Every death
  should be your fault, not the mod's." Information parity, no
  automation — the same line OGA walks.
- **The technique:** in a tactics game where one turn can generate
  dozens of events, the mod's active development focus is *speech
  prioritisation and compression*: per-turn damage summaries, smarter
  event grouping, removing redundant announcements without losing
  tactical detail.
- **Transfers to:** Saiyans multi-target battles and BBS multi-enemy
  fights: when many events land at once, group and summarise (then let
  query keys carry the detail) instead of reading every event raw.

### 12.4 Say the Spire: the forum lineage (enrichment to section 4)
[Thread: "slay the spire now accessible!", forum.audiogames.net
topic 35130, General Game Discussion; digest updates April 2026]

- The line starts with **TextTheSpire by Wensber** (2020): game info in
  reviewable windows + a command prompt window; companion program
  **ReadTheSpire by pitermach** added Alt+arrow buffer navigation,
  Alt+Home/End to jump to buffer ends, and SAPI support. Say the Spire
  (section 4) is the mature form of the same buffer idea.
- **Say the Spire 2** (per the April 2026 digest): a **points-of-interest
  system for map navigation with a path finder**, many new verbosity
  settings, accessible compendium. The map-tree idea (4.2) grew a
  pathfinder — exactly the Pokémon Access pattern (5.1) arriving in a
  card game. For **Harukanaru's map travel**, plan on: list of places
  of interest + pathfinding between them, not raw coordinates.

### 12.5 Super Auto Pets Access — playtesting thread
[Digest, April 2026, quoting the author's playtesting post] First
public release of an accessibility mod for the free auto-battler Super
Auto Pets, developed with months of Discord playtesting before the
forum post. Logged as another card-adjacent battler in the wild; the
transferable point is procedural: recruit playtesters in the community
rooms *before* the release thread, not after.

---

## Quick-pick addendum (sections 11–12)

- **Action-RPG navigation (LoG):** add 12.1.1 (objective beacon +
  category target picker + arrival cue) alongside 1.6 and 7.1.
- **Card battles (Harukanaru):** add 12.2 (position feedback during
  reordering), 12.4 (points-of-interest + pathfinder for map travel),
  and the candidate pool in 11.1 (Snap, TCG Live, XMage, Yu-Gi-Oh) if a
  second opinion on any card technique is ever needed.
- **Dense combat speech (all adapters):** 12.3 (group, summarise,
  prioritise; detail lives behind query keys).
- **Standing survey sources:** Molitvan's list (section 11) and the
  AudioGames New Releases room + Monthly Digest (section 12) — the two
  places new access mods surface first.

---

## 13. ADOFAI Access — Molitvan (A Dance of Fire and Ice)

[PRIMARY — the project's own page (molitvan.me/projects/adofai-access)
and GitHub README (github.com/Molitvan/adofai-access), read by Sarah
2026-10-25. MelonLoader mod, GPL-3.0, repo created 2026-02-10, 100
commits / 10 releases at reading; Molitvan's list grades it work in
progress, under active development. Added at the user's pointer, for
the rhythm games. ADOFAI is a one-button rhythm game: the path's
geometry IS the rhythm, so playing it means hearing a pattern and
reproducing it blind.]

### 13.1 Two ways to hear a pattern before you must play it

- **Listen-repeat** (modelled on Sequence Storm's Audio Cues 1 mode):
  the chart is split into alternating groups, the group length in
  beats configurable. During a listen group, audio cues play exactly
  the rhythm the next repeat group will demand; the player does not
  tap. Options that matter: duck the song during listen groups;
  group start/end signalled by sound, speech, both or neither;
  **"follow starting BPM"** — the group's timing reference stays at
  the level's opening tempo instead of tracking tempo changes, to
  reduce confusion.
- **Pattern preview** (Sequence Storm's Audio Cues 2): a cue track
  sounds the rhythm a configurable number of beats AHEAD of the
  moment to play it — a lead-in rail rather than a rehearsal. Same
  starting-BPM freeze option.
- Vanilla mode is untouched. F9 cycles play modes; settings live in
  the mod's own spoken menu (F5, or Square/X on a controller).

### 13.2 When the menus are gameplay, replace the menus

ADOFAI's main menu and custom-levels menu are themselves rhythm-based,
so narrating them can't fix them. The mod adds a **separate linear
menu (F6, or Triangle/Y)** that reaches everything those menus reach,
plus ordinary menu narration (F4 toggle) for the menus that are plain
lists. Lesson: if a screen's interaction IS the game's hard mechanic,
build a parallel plain interface instead of narrating the hard one.

### 13.3 A safe listen-through: level preview

F8 auto-plays the level with a sound cue on every tap, and
force-enables practice mode so previewing can never count as a
completion. The whole chart can be heard, start to finish, at zero
risk, before a real attempt.

### 13.4 Cue vocabulary details

User-replaceable WAV cues: `tap`; `extra_tap` layered on top for
multitaps (simultaneous presses get a distinct compound sound);
`hold_start` / `hold_end` (a hold is cued at BOTH ends, not just its
start); `listen_start` / `listen_end`.

**Transfers to:**
- **The rhythm shelves (Rhythm Tengoku, Rhythm Heaven Fever, Space
  Channel 5):** what an access layer adds to an already-audible game
  is preview + structure + menus, not the beats themselves —
  chunked listen-repeat rehearsal, an N-beats-ahead cue rail,
  boundary cues for holds and simultaneous inputs, and a plain
  replacement for menu/results screens. (His Fever already has its
  own WIP mod, RH Fever Access; this is the design vocabulary if OGA
  ever takes on a rhythm title.)
- **Timed mechanics everywhere (Saiyans' Active Guard):** pattern
  preview is the same move as Budokai's meter tone (1.2) — put the
  timing information in sound BEFORE the moment, with a configurable
  lead — and the starting-BPM freeze generalises: when a game's
  tempo shifts, a steady cue reference can be clearer than one that
  tracks it.
- **Menus anywhere:** the parallel-linear-menu pattern (13.2) for
  screens whose native interaction is itself inaccessible.

Source note: both play modes credit **Sequence Storm's** built-in
Audio Cues modes — that game is the rhythm genre's design ancestor
here. Also on Molitvan's list: **Heaven Studio** (a Rhythm Heaven
engine) with a mod, fully accessible except remix-making.

---

## 14. Real-time aiming and 3D action

[Researched 2026-10-25 at the user's pointer: how is real-time AIMING
made playable by totally blind players in shipped work? Four cases —
one commercial suite, two mods, one honest negative.]

### 14.1 The Last of Us suite (Naughty Dog) — the commercial benchmark

[PRIMARY — Naughty Dog's own accessibility documentation for Part II
(naughtydog.com, June 2020), read in full. Part I additions via
PlayStation Blog coverage of Naughty Dog's Part I post. Blind-player
corroboration: Triple Tap Tech (a blind player's own account of
completing Part II twice) and Can I Play That?'s blind impressions —
marked where used.]

**Version map.** The suite debuted in **Part II** (PS4, 2020; 60+
settings). **Part I** (PS5 remake, 2022) uses Part II's suite as its
baseline and adds audio description for cinematics (made with
Descriptive Video Works) and dialogue played through the DualSense
as haptics. **Part II Remastered** carries the Part II suite
forward; blind reviewers confirm both it and Part I are entirely
playable blind. The aiming design below is identical across all
three, so "Remaster" vs "Part II" doesn't change the answer.

**The aim stack, in Naughty Dog's own setting names:**

- **Lock-On Aim — Off / On / Auto-Target.** "Automatically lock-on
  to enemy targets when aiming. Targets the center of the enemy's
  body by default. Use right stick to target the head or legs. If
  set to AUTO-TARGET, you will lock onto the next enemy
  automatically even if they're offscreen." Both the Vision and
  Motor presets set Auto-Target. The design split matters: the lock
  does the *gross* aim, and one stick flick is the whole *fine* aim
  (body / head / legs) — there is no free-reticle step to fail at.
- **Lock-On Strength — slider 1–10**, "adjust the pull strength."
  Note precisely: Naughty Dog's settings list has **no separate
  generic aim-assist slider** — lock-on pull strength IS the
  aim-assist control.
- **Arc-Throw Lock-On (+ its own strength slider)** for throwables —
  and in stealth, bricks and bottles deliberately lock on *slightly
  away* from enemies, to facilitate distractions; the right stick
  overrides to target the enemy directly. The assist reads the
  player's intent (distract vs hit), not just the nearest body.
- **Slow Motion — Off / While Aiming / Toggle** (Combat
  Accessibility). Naughty Dog's framing: "If you have difficulty
  aiming, you can give yourself more time by enabling Slow Motion
  while aiming." Time is an aim assist.
- Supporting aim inputs: Aiming Mode hold/toggle; **Weapon Sway**
  can be switched off; Hold Breath steadies aim.

**Enhanced Listen Mode — the scan.** While in Listen Mode (hold
R1): **Circle scans for items, Square scans for enemies.** Official
description: "Send out a scan triggering audio cues at the target's
location. The pitch of the sound changes based on the target's
height relative to you." **Scan Range** slider 10–30 m; **Scan
Time** slider 1–5 s (how long the scan takes to reach maximum
range). Practical loop, per the blind player's account: the scanned
target gives a continuing cue you home in on; and with Navigation
Assistance also on, **pressing L3 while in listen mode turns you
toward the most recently scanned item or enemy**. Scan (where +
how high) → face it → walk or shoot. One scan serves looting,
stealth and pre-fight reconnaissance alike.

**Navigation Assistance** is a separate thing from aim: press L3
and the camera faces the direction of story progression — a
one-button "face the objective" for the golden path.

**Combat/traversal cue systems.** *Combat Audio Cues* play for:
enemy grab and strike prompts, incoming enemy melee attacks, being
impaled with an arrow, **aiming at an enemy** (an on-target
confirmation — you know the reticle is on someone without seeing
it) and **landing a shot**. *Combat Vibration Cues* mirror the
melee-warning / aiming / landing trio in haptics. *Traversal Cues*
cover jumpable gaps, climbable ledges, squeeze-throughs, crouch/
prone areas, interacts and pick-ups, nearby breakable glass,
gaining/losing movement control, and Ledge Guard firing. Every cue
is catalogued in an in-game **Audio Cue Glossary** the player can
browse and rehearse — the vocabulary is teachable, not tribal.
Text-to-speech narrates all on-screen text and, per the blind
account, a touch-pad swipe reads current health/weapons/items.

### 14.2 Voice of the Old Republic — Jean Stiletto (KOTOR)

[PRIMARY — the project's GitHub README, read in full:
github.com/JeanStiletto/voice-of-the-old-republic. Molitvan's list
grades KOTOR I "fully accessible" with it. AudioGames forum: three
targeted searches found no release thread — a §12.0-style negative;
the project's home is GitHub. The author is a fully blind developer
(README disclaimer) — same author as Accessible Arena (section 3),
and speech again runs through the **Prism** bridge, so any modern
screen reader works.]

**Status.** KOTOR I (Steam/GoG v1.03, Windows): "fully playable and
completable, with all minigames, quests and playstyles"; 41
releases at reading; GPL-3.0. KOTOR II is ported — the installer
sets it up and the game speaks — but has **not** been played
through; its minigames and tutorial messages aren't covered and
blockers remain. The mod also ships its own XInput controller
support, because KOTOR 1 has no gamepad code of its own.

**Targeting: KOTOR aims by selection, not by reticle.** Combat is
real-time-with-pause on d20 rounds (6 real seconds); you can pause
at any time and queue up to 4 actions per character. The mod's
stack:

- **Q / E target cycling** (the game's own keys) steps through the
  creatures, doors and usable objects the camera can see — and the
  mod **speaks each new target**. Whatever is targeted is what
  Enter and the action keys act on.
- **Unified action menu (Shift+Enter):** every action for the
  current target — attack, talk, Force powers, items, special
  abilities — in one spoken menu, replacing the game's separate
  radial, target and personal menus; Shift+1…7 open one category.
- **Action queue review (Shift+H):** hear or clear what you've
  queued. In a queue-ahead combat system, reviewing the queue is
  the equivalent of checking your aim.
- **Auto-pause as the aim enabler:** the game's auto-pause options
  decide when combat stops on its own; the mod's first-steps guide
  tells players to set them up before their first real fight. You
  never aim in real time — you select target + action while paused.
- The game's one true real-time aiming, the **turret minigame**,
  gets a mod setting literally named **"Autoaiming — the aim
  assist in the turret minigame."**

**Navigation systems** (the richest non-combat layer in this
catalog): a continuous **wall-cue layer** — soft positional clicks
off the nearest walls, louder = closer, so you feel the space as
you move; **room-shape descriptions on entry** — room name, shape
(corridor, junction, dead-end, open space) and visible exits,
computed live from the game's walk-mesh; a **discovered-object
cycle** (`,` / `.`, grouped by category — creatures, doors,
containers, transitions, landmarks, your own markers; Ctrl jumps
to nearest/farthest); **autowalk** (Shift+`/`) and an armable
**audio beacon** (Ctrl+`/`) that pings the way; compass facing on a
key, and **N** snap-turns the camera to the next cardinal — or to
the beacon's next waypoint; a navigable map screen using the same
pin vocabulary, where you can drop your own markers; a view mode
(B) to pan the camera and act at a point.

**Menus and dialogue:** all menus and character-creation panels
read; the game's on-screen tutorial popups are **replaced with
custom keyboard hints written for screen-reader users**; an option
speaks voiced lines' subtitles; **H** announces your health,
active effects and equipped weapon at any time; Ctrl+R copies the
last spoken text to the clipboard (a review buffer); an audio
glossary demos the mod's own cues; nearly everything is toggleable
in Mod settings.

### 14.3 Star Fox 64 — no blind-accessibility mod found

[Honest negative. Searched: the web (multiple phrasings), GitHub —
including the sf64 decompilation project and the Starship PC port —
the AudioGames forum, and Molitvan's list, which has **no Star Fox
entry of any kind**. The sf64 decomp's "mods" are developer tools
(level select, RAM editor, spawner), not accessibility. There is
no mod to document.]

Two things worth banking anyway:

- The 1997 game's own sound design already treats audio as
  positional information: Wikipedia (secondary source) notes its
  dynamic audio was designed so allies audibly signal when an
  offscreen enemy is pursuing you. The wingman callouts are a
  warning radar the game shipped with.
- **What would transfer [OUR INFERENCE — not a found mod]:** on
  rails, "aiming" is the ship's position on a 2-D plane, with the
  reticle riding near the ship. An access layer would need (a) a
  continuous ship-position cue — pan for horizontal, pitch for
  vertical (the Enhanced Listen Mode height-pitch trick, 14.1,
  applied to your own ship); (b) an on-target cue when fire
  overlaps an enemy (TLOU's "aiming at an enemy" cue, 14.1);
  (c) approach/direction pings for all-range-mode sections, from
  the §12.1 Kakarot picker and §1.4 Budokai hum families; (d) the
  game's charged homing shot already locks on — an adapter would
  mainly need to announce lock state. CORRECTION (2026-10-25,
  verified by a direct listing of /Games/N64 — 18 files, read in
  full): Star Fox 64 is NOT in the user's Games folder, despite
  being named here earlier on an unverified assumption. The design
  above stays banked as technique only; there is no shelf copy to
  target unless one is added.

### 14.4 Super Blind Kart — Ali-Bueno (Mario Kart 64)

[PRIMARY — the project's own GitHub releases page (first beta
through 0.4.0), read in full: github.com/Ali-Bueno/SpaghettiKart,
default branch `accessibility`. Molitvan's list: work in progress,
under active development. Same author as Kakarot Access (12.1).
Built on the **SpaghettiKart PC port** — not the N64 ROM/emulator;
the player supplies their own US ROM, extracted on first launch.
Windows builds; speech through **PRISM** (NVDA, JAWS, …);
accessibility on by default; stereo headphones strongly
recommended.]

**Steering: keep the engine centered.** Your own kart's engine
sound pans left and right as you steer; **engine centered in your
headphones = on the racing line**; if it slides to one side, steer
that way to bring it back. "Center = good line. That alone is
enough to keep you racing." A **rival-kart volume slider**
(default 60%) keeps your own engine pick-out-able from the pack.

**Turn calls, graded.** Corners are announced ahead ("Left" /
"Right") and **graded by tightness — Easy, Normal, Hard, Hairpin,
plus "Long"** for long ones; back-to-back curves are announced
together. Separate beeps mark the curve's **entry, apex and exit**,
so steering and drift timing have anchors. Drive-assist
*anticipation* (look-ahead) is a setting (default 12), alongside
assist strength and edge sensitivity, in Options → Accessibility;
the Help entries play a **live demo of each cue**.

**Edges.** A panned beep warns as you drift toward the track edge;
a distinct **off-road cue** tells you you've left the road.

**Items, hazards, opponents.**

- **Item boxes:** a 3-D positional blip pans toward the nearest
  box and grows louder as you close in; drive past and it keeps
  sounding **from behind at lower pitch** — a Doppler "you passed
  it" cue; it stops the moment you pick up an item. Toggle + range
  in the menu, demo included.
- **Collision warning (0.4.0):** a fast pulsing cue, panned toward
  the obstacle, fires only when you are genuinely on a collision
  course (head-on or near) with a moving obstacle — oncoming
  traffic, a falling rock, the train, a paddle boat, a cow, a
  piranha plant — not for hazards you'll pass by the side. Toggle,
  range and loop-speed sliders.
- **Shells and bananas:** a spinning-shell cue with a **Doppler
  drop as it passes you** (yours or a rival's); the homing **red
  shell has its own distinct flight sound**, separate from green
  and blue; a banana-on-track hazard cue.
- **Race state:** finishing position announced (Grand Prix and
  Versus); Time Trial results read aloud (three lap times + total);
  final GP placement, full points standings, trophy already won
  per cup, and the Data menu's records (best time/lap and who set
  them) all narrated.
- **Menus:** main menu, character select, course/cup select and the
  full Options screen narrated; settings were moved out of the
  port's unreadable ImGui overlay into the game's own Options menu
  (with an Accessibility category); control rebinding done
  entirely by ear; driving cues fire **only during actual racing** —
  never in countdowns, menus, pause, replays, or after the finish.

### 14.5 Transfers to OGA

- **TLOU sets the bar for 3-D action aiming:** lock-on does gross
  aim, a stick flick picks body/head/legs, a strength slider sets
  the pull, Auto-Target chains to offscreen enemies, and slow
  motion buys time. OGA's Dissidia/BBS lock-on already reads out
  target identity and health; the pieces TLOU names that OGA
  doesn't yet have are chaining, strength, and time. Honest caveat:
  TLOU is a native suite with engine access; OGA reads memory from
  outside, so chaining and slow motion would be input/state work,
  not just announcements.
- **The scan vs the beacon.** Enhanced Listen Mode scans what's
  NEAR (two target classes — items, enemies — position at the
  target, pitch for height, range + sweep-time sliders, and a
  "face the last scanned thing" key). Kakarot Access (12.1) tracks
  what's NEXT (objective beacon + category picker). They answer
  different questions; an adapter can offer both, and the
  height-pitch encoding transfers anywhere elevation matters
  (Supersonic Warriors' flight, Star Fox's plane).
- **KOTOR: dissolve aim where the game allows it.** Pause +
  spoken target cycle + one unified action menu + a reviewable
  queue replaces real-time aiming entirely. Where a game (or a
  minigame) can't pause, a single named auto-aim setting is the
  fallback. Its wall-click layer and live room-shape descriptions
  are also a navigation kit OGA's field adapters (LoG II / Buu's
  Fury) don't have yet.
- **Super Blind Kart is the MK64 design kit:** continuous variable
  (steering error) carried by a continuous sound (engine pan);
  graded turn calls with entry/apex/exit anchors; anticipation as
  a user setting; Doppler pass cues; cue gating to live play only.
  OGA's queued Mario Kart 64 investigation starts from this design
  — re-implemented against emulator memory, since Super Blind Kart
  is a PC-port fork and OGA has no N64 core.
- **Shelf status:** Mario Kart 64 IS on the user's N64 shelf —
  techniques banked, backend-gated. Star Fox 64 is NOT in his
  Games folder (verified by direct listing, 2026-10-25) —
  technique banked with no shelf copy behind it. KOTOR is
  NOT on his shelves (there is no PC shelf) — technique donor only.
  The Last of Us is not on his shelves either — design reference
  only.

---

## 15. Amethyst's mods — code-level mining (TheRealAmethyst)

[PRIMARY throughout — his own repos and shipped packages, read directly
2026-10-26. Verification tags per technique: **[SOURCE]** = read in his
published source code (GitHub repos `pokemon-tcg-access`,
`DigimonNOAccess`, `zelda3-access`, `AccessibilityModManager`, plus the
Pokémon Black & White 1.4 package's Lua files); **[BINARY]** = strings,
metadata or assets inside a shipped package (XivAccess.dll and the Time
Stranger mod DLL are closed-source; the packages were hash-checked
against his signed catalog); **[DESCRIPTION]** = his own catalog control
schemes / release notes only, no code seen.]

What it is: one blind developer (Norway; "Ola" in his code comments)
shipping screen-reader mods through his own Accessibility Mod Manager:
Pokémon TCG Live, Digimon World: Next Order, Digimon Story Cyber
Sleuth and Time Stranger, Digimon Survive, Final Fantasy XIV
("a blind realm" / XivAccess), Pokémon Black & White, Mega Man Battle
Network 6. The headline finding is architectural: **it is the same
accessibility layer re-implemented per game** — the same speech rule,
the same category/item nav list, the same pathfinder, and literally the
same sound files (the wall/person/pathfinder WAVs in the Next Order,
Time Stranger and FFXIV packages are byte-identical sizes and hashes).
An adapter project like OGA, which re-implements per game by design,
is his exact shape — his layer is a reference implementation of ours.

### 15.1 One speech rule everywhere: queue what coexists, interrupt what replaces

- **What it does:** Every mod speaks through one function,
  `Speak(text, interrupt)`. The rule, written down in his shared Lua
  speech core: queue what is on screen *together*; interrupt whatever
  has *replaced* what came before. Arriving at a new screen interrupts;
  a second element of the same screen queues behind the first; a cursor
  move interrupts; a second page of a message interrupts (pressing A
  wiped the box, so page one no longer exists). Leaving a screen stops
  its current *and queued* speech before the next screen's readers run.
- **How it works:** A screen tracker (`screen(next)`) is the single
  choke point; readers declare what coexists, the output function
  never has to guess. The same convention appears in the Time Stranger
  mod's `SpeechManager.Speak(text, interrupt)` — the Lua file names it
  explicitly as "the project's prism queue convention".
  **[SOURCE — BW `core/speech.lua`; DSTS named in its comments]**
- **Content order is fixed too:** counted items speak as
  "10 Repel" (quantity, no comma), then description/details, then list
  index ("3 of 8"). Tab headers introduce their contents and speak
  first; the contents queue behind. **[SOURCE]**
- **Repeat-last is a first-class key,** backed by a `last()` accessor
  over the last utterance, plus `set_last()` — a reader that watched
  the game print a line itself can set what the repeat key will say
  *without* speaking it. **[SOURCE]**
- **The "seamless" rule:** the tool speaks only game content — never
  its own state ("you are in a menu", "loaded"). The one allowed
  exception is a single startup line, and a spoken failure when the
  loader cannot identify the game (a blind player must be able to tell
  "script dead" from "quiet moment"). **[SOURCE — BW headers]**
- **Focus discipline (TCG Live):** speech is silent while the game
  window isn't focused; on focus regain the active screen re-speaks
  its state (`RespeakAll()`), so nothing is lost. **[SOURCE]**
- **Sanitise before speaking (TCG Live):** every string passes one
  text processor that translates icon/sprite tags to spoken words
  *before* stripping tags — energy-type sprites become "Water",
  "Fire"; `{W}`/`{EX}` tokens likewise; localisation debug markers
  ("(Missing)<…>", "(Fallback)") are deleted. Stripped blind, the
  text silently loses meaning ("discard a Energy"). **[SOURCE]**
- **Transfers to:** adapter-wide. This is the missing written spec for
  OGA's speech queue across every adapter — adopt the
  coexist/replace rule, the screen-exit stop, the fixed content order,
  and an icon→word pass wherever game text carries symbol tokens
  (Saiyans/Harukanaru message text included).

### 15.2 The nav list: categories + items, spoken with bearing and position

- **What it does:** His universal field-navigation UI. Two key pairs:
  one cycles **categories**, one steps **items** within the category.
  Each entry speaks as "**name, direction, X of Y**" (BW Lua) — you
  always know what it is, which way it lies, and where you are in the
  list. A fifth key starts pathfinding to the selected entry (and
  pressing it again stops). **[SOURCE — BW + DNO; DESCRIPTION — Cyber
  Sleuth (j/k/l items, i/o categories, p pathfind), Battle Network 6
  (same chords behind L2/R2 modifiers)]**
- **Categories are game-shaped but the shape repeats:** BW uses
  Transitions, NPCs, Items, HM (usable field moves/spots), Signs,
  Objects, Slides, plus dynamic Encounter spots and Dark grass. Next
  Order uses NPCs, Items, Materials, Quest, Transitions, Enemies,
  Facilities. Time Stranger's DLL has the same idea as data
  (Destructible, Elevator, Event, Facility, FastTravel… categories).
  **[SOURCE/BINARY]**
- **Only non-empty categories exist.** Cycling skips empty categories
  entirely (DNO keeps an "active categories" list; DSTS strings name
  `FirstNonEmptyCategory`). The list is built lazily on first use and
  cached until the zone changes — never rebuilt per frame.
  **[SOURCE]**
- **The chosen category is remembered** across battles, dialogue and
  menus, stored by category *name*, not index (indexes shift as
  categories appear/vanish) — his note quotes the bug this fixed:
  "you always end up in transitions." Duplicate names inside a
  category are numbered ("Item 2"), and transitions are named by
  destination, with floors appended ("Celestial Tower 3F").
  **[SOURCE — BW]**
- **Menu announcements follow the same grammar** (Next Order's
  AnnouncementBuilder): on open, "Menu Name. Item name, 3 of 5"; the
  game's button-hint bar is appended *last*, deliberately — it's the
  same on every screen and least urgent, and you can act the moment
  the item itself has been read. **[SOURCE]**
- **Transfers to:** Saiyans field gaps and every future field adapter
  (LoG II/Buu's Fury, Pokémon): this is the category list to copy —
  including the category-memory rule and destination-named exits,
  which §5.1's community scripts only gesture at.

### 15.3 The reachability filter: never offer what you can't reach

- **What it does:** The nav list only shows entries the player can
  actually walk to *right now*. Something across a surf gap, on
  another floor of a slide puzzle, or in a finished Elite Four room
  simply isn't offered, instead of being offered and failing.
- **How it works:** One search, two consumers. A BFS flood from the
  player's tile uses the *same step rule* as the pathfinder (walls,
  height, one-way ledge edges) and marks the reachable set; the list
  filters on it. The step rule exists exactly once in the code — his
  comments record the divergences that happened when two searches
  kept two maps. The flood is cached (it costs ~40 ms) and it
  **fails open**: unloaded chunks, overflow or a failed read never
  hide an entry. Puzzle mechanics extend the state space rather than
  the rule — see 15.4. **[SOURCE — BW]**
- **Transfers to:** any OGA nav/object list (BBS/Dissidia landmark
  Guides included): an unreachable listing is a broken promise; run
  the flood with the router's own movement semantics and fail open.
- **Confirmation (2026-11-03):** Pokémon TCG Access (§25) takes the
  keep-and-flag variant of this rule: unreachable targets stay in
  the scan list, spoken as "can't be reached from here", rather
  than being filtered out — the player learns the room's contents
  and the constraint in one pass. Filtering vs flagging is a
  per-game choice; silently listing with no marker is the only
  wrong answer.

### 15.4 Pathfinding that speaks turns, not steps

- **What it does:** Pick a nav entry and the mod walks you there by
  *spoken directions*: a route is a list of turn segments ("3 up",
  then "1 left"); it speaks the next turn when you reach each corner,
  a bare "1 <dir>" for an off-route single step, and "Recalculating"
  when it genuinely re-plans. Arriving speaks the destination and,
  where it matters, which direction to go next. **[SOURCE — BW]**
- **The search:** A* over the live tile grid, one tile = one node,
  carrying height as signed 16.16 fixed point (queries made at the
  player's own height; a two-storey map breaks everything otherwise).
  One-way ledges are directed edges in the step rule. Difficult
  ground is a *cost*, never a wall: sinking sand +6, slide tiles +20
  — "a penalty must never make somewhere unroutable, only
  unattractive." Actor occupancy is layered on top using the engine's
  own collision rule (read out of the disassembly: footprint from the
  actor's own size fields, one flag bit the only exemption) — people
  and items block their tile, the goal is exempt, and if occupancy
  alone makes the target unreachable it retries ignoring *people*
  but never ignoring hard obstacles. **[SOURCE]**
- **Routes are best-effort and resumable.** Collision only exists for
  streamed-in chunks, so a far target routes to the nearest loaded
  tile toward it and recomputes as you walk. A route survives losing
  control: battles, dialogue, menus and animations arm a "re-enter"
  flag, and on the first frame of regained control it re-plans from
  where you stand and speaks the next turn. **[SOURCE]**
- **Speech is held during scripted motion.** While the engine is
  sliding you on ice or mid two-tile jump, the follower says nothing —
  it detects the in-flight action, not the position, because position
  reads the ledge/ice tile for several frames and used to trigger
  false "Recalculating" on a correct route. Guidance resumes when
  you're back in control. **[SOURCE]**
- **Puzzle legs are separate planners feeding the same follower.**
  When walking isn't the mechanic, a small state-space BFS answers
  "what is the next thing to walk to" and the main router re-asks
  after each landing: (platform × switch states) for a coaster gym,
  (rock positions) for Strength boulder pushes (with a lookahead
  scan before it will offer a push), slide physics modelled as
  multi-tile moves, and for Castelia City's diagonal streets a
  Dijkstra in "rail space" with *no geometric heuristic* — rail space
  isn't metric, so an A* estimate can overestimate and break
  admissibility. **[SOURCE]**
- **Transfers to:** Saiyans (caves/gym-like puzzles, the Active Guard
  work's field side), LoG II/Buu's Fury, Pokémon DS — and the general
  OGA guidance voice: speak turns at corners, hold speech during
  forced motion, keep the goal's identity across interruptions.

### 15.5 The beacon that leads around corners (3-D games)

- **What it does:** In the 3-D Digimon games, guidance is a sound,
  not speech: a repeating beep placed at a **guide point a short way
  ahead of you along the path** (10 units in Next Order), so it leads
  you around corners instead of pointing through the wall at the
  destination. The beep interval compresses with remaining distance
  (1.5 s far → 0.15 s near). Time Stranger exposes the same beacon as
  settings (`AudioPathfinderBeep` enable + volume). **[SOURCE — DNO
  `PathfindingBeacon.cs`; BINARY — DSTS settings strings]**
- **How it works:** Path comes from the engine's NavMesh
  (`UnityEngine.AI` in DNO); direction is measured against the
  *camera*, not the player, so turning the camera moves the beep like
  every other nav sound; spatialisation is Steam Audio HRTF when
  present, stereo pan otherwise, all through one shared mixer.
  Auto-walk is an optional extra on top: virtual stick values follow
  the path (with camera-rotation injection) while it runs.
  **[SOURCE]**
- **One sound vocabulary across games:** wall (×4 directions),
  person, pathfinder/tracking, item, door, ladder, elevator,
  transition, enemy, main-quest, destructible — the same WAV set
  ships in Next Order, Time Stranger and FFXIV packages.
  **[BINARY — package contents compared]**
- **Transfers to:** BBS/Dissidia 3-D navigation (their landmark
  Guides today speak bearings; a path-beacon is the real-time
  version), the fighting-game radar standard (a leading guide point
  beats a destination point anywhere the target can be occluded),
  and LoG/AdvAdv if their pathing lands.

### 15.6 Wall sonar and per-type positional audio

- **What it does:** Standing still or walking, you hear the world:
  in Next Order, every object type in range (items, NPCs, enemies,
  transitions, facilities) gets its own positional sound,
  always on, no speech and no toggle keys; walls get a dedicated
  sonar. In FFXIV the same idea is a named feature — "Wall sonar":
  "Sound the nearest visible walls in front of your camera", with
  its own volume setting. **[SOURCE — DNO; BINARY — XivAccess
  settings strings]**
- **Per-direction wall samples, chosen by quadrant:** the wall tone
  is one of four recordings (wall_north/east/south/west) selected by
  the compass quadrant of the obstacle relative to the player, muxed
  live as you turn. Two refinements matter: a **10° hysteresis** on
  quadrant switching so the tone doesn't flicker at a boundary, and
  **hand-rolled distance falloff** — Steam Audio's occlusion
  simulation was tried for walls and removed because it caused
  volume jitter. If a WAV is missing, a plain tone at a per-direction
  frequency stands in (N 523 Hz, E 659, S 392, W 440). **[SOURCE —
  DNO `CompassWallEmitter.cs`]**
- **Density is budgeted per type:** each object type has its own
  range *and* its own cap on simultaneous sounds; the nearest gets a
  "nearest volume", the rest a quieter "background volume"; the
  tracking scan re-runs twice a second, and immediately on map
  change. XivAccess keeps the same discipline as user settings —
  separate volumes for wall sonar, NPC sounds and the tracking
  sound. **[SOURCE — DNO `AudioNavigationHandler.cs`; BINARY —
  XivAccess]**
- **Transfers to:** OGA's sound-first rule (§ "never speak what the
  game cues") needs exactly this budget table: per-type range, per-
  type cap, nearest-louder. Wall sonar with quadrant hysteresis is
  the field-awareness piece for BBS/Dissidia/LoG movement, and the
  per-channel volume split belongs in the per-game settings panel.

### 15.7 XivAccess: navmesh routing from the game's own data

- **What it does:** FFXIV's `/xiva nav` opens a destinations window
  with tabs — quest objectives (active and available), aetherytes,
  gathering points, NPCs nearby — each entry selectable for a guided
  route. Combat speech, camera-direction speech and spoken Dalamud
  settings/plugin UIs round it out (see the mod's own release notes,
  catalog § checked 2026-10-26). **[BINARY + DESCRIPTION]**
- **How it works (from DLL strings/metadata):** the route is built
  *before* you walk, from the game's own zone data — an "archive
  topology" of the zone plus a DotRecast tile navmesh built from it
  (`TileNavMeshBuilder`, `DtNavMeshQuery`; DotRecast ships in the
  package). While walking, the route is continuously **certified
  against live collision**; the journal strings tell the story:
  routes are discarded when "the zone has changed since the route
  was worked out", when steps "no longer certify", or when the route
  "lost attachment" — with a fallback ladder: try local walking
  routes in estimated order → adopt a straight-line "yalm route" and
  check it live next tick → rebuild the archive corridor from your
  current position. `/xiva status` reports progress and a journal
  keeps the whole routing log readable. **[BINARY]**
- **List caps with an exception that matters:** "How many nearby
  things each tab may list. Quest objectives are never cut." Nearby
  lists are trimmed by distance; the thing you actually need never
  is. **[BINARY]**
- **Speech is channelled, not global:** separate toggles for combat
  enter/leave, progress (EXP/levels), loot/currency notices,
  speaker names, roles, descriptions — the FFXIV counterpart of the
  verbosity settings in §4.4, but per *event channel* with per-
  channel volumes for sounds. **[BINARY]**
- **Transfers to:** BBS/Dissidia 3-D navigation — the two-level model
  (offline map-derived route + live re-validation with spoken
  fallback) is the robust version of a landmark Guide; the
  never-cut-the-objective rule and per-channel speech toggles
  transfer to every adapter's settings.

### 15.8 picture-names.csv: naming the unnameable

- **What it is:** XivAccess ships a 2,867-row CSV:
  `icon_id, name, category, race, tribe, gender, position, of`.
  It names FFXIV's **character-creation pictures** — the appearance
  choices the game presents as bare icons with no text anywhere:
  hairstyles (879), face paint (832), facial features (540),
  tattoos, faces, tail shapes, limbal rings… The names are human
  descriptions, written for ears: "Warm red eyeshadow",
  "Vertical eye scar and cheek scar", "Slender, spined near the
  base" (a tail shape). The race/tribe/gender columns scope the row
  to the creator page it appears on, and position/of turns the icon
  grid back into a list position. **[BINARY — CSV read in full]**
- **The general trick:** when a UI is pictures keyed by an ID, the
  accessibility layer is a *lookup table from ID to a spoken name*,
  authored once, shipped as data. No amount of UI traversal finds
  text that doesn't exist. The same package shows the other half of
  the pattern: an `imgui-manifest.json` pinning hook points into
  Dalamud's ImGui build, so his reader follows ImGui's own
  navigation state (which widget is focused) instead of guessing
  from pixels — that's how "Dalamud settings UI speaks" works.
  **[BINARY]**
- **Transfers to:** any OGA screen whose choices are icons — BBS
  Command Board / shop icon rows, Saiyans capsule slots (the Gas
  Mask's red slot!), character selects in the fighting games. Budget
  for a per-game name table keyed by the game's own IDs.

### 15.9 Reading a Unity card battler (Pokémon TCG Live)

- **What it does:** Full keyboard control + screen-reader output for
  the whole TCG Live client — menus, deck building, collection,
  shop, and battles with zone keys (hand / Active / Bench / discard /
  Stadium / card viewer / end turn). **[SOURCE — repo read]**
- **How it works:** One handler class per screen behind a router
  (`IScreenHandler`, ~40 screens), each handler owning its speech —
  the same shape as Next Order's per-screen handler files. Since the
  game has *no* native keyboard navigation at all, a
  `KeyboardNavigator` synthesises it over the game's own Unity
  `Selectable` objects: it collects the live selectables, orders
  them spatially (world centres, detected columns/axes), filters
  out anything not raycast-reachable (a modal on top silently
  removes what's beneath), resolves each label from the selectable's
  child text components, and drives the game's own
  `Selectable.Select()` so the game itself highlights and confirms.
  The mod never re-implements the game's activation logic — it
  steers the game's own focus. **[SOURCE]**
- **Battle board as a zone model:** a `Zone` enum (your/opponent's
  Active, Bench, Hand, Discard, Deck, Prizes, Lost Zone, Stadium)
  read from the game's board state; hidden-information zones (deck,
  prizes, opponent's hand) are **count-only** — the reader never
  speaks what the player couldn't see. A battle-log hook narrates
  the opponent's actions as they happen. **[SOURCE]**
- **Transfers to:** Harukanaru Densetsu directly — its queued design
  (§§2–3: zone keys, name-first/detail-on-request, game log) gets a
  worked example of the *implementation* pattern: per-screen
  handlers, one navigator that steers native focus where it exists,
  a zone enum with hidden zones count-only. The DS difference: no
  native focus to steer, so the navigator's ordering/labelling half
  transfers and the activation half becomes cursor memory work.

### 15.10 The modifier layer: hold a trigger, the pad becomes the reader

- **What it does:** In the DS Pokémon mod, holding the left trigger
  enters the accessibility layer: **the game receives no input at
  all** while it's held, and the pad drives whatever list is live
  (D-pad = category/item keys, left-stick click = pathfind toggle,
  top face button = place name + coordinates). Releasing the trigger
  hands the pad back to the game untouched. The keyboard readers and
  the pad layer share one virtual key set, so every reader gains
  controller support for free. A second trigger layer drives
  synthetic-touch screens the same way. **[SOURCE — BW
  `core/pad.lua`]**
- **Synthetic touch as a frame queue:** touch-only DS screens are
  handled by queueing gestures (press/hold/move/release as a
  per-frame sequence, with a watchdog so a torn-down screen can
  never leave a phantom finger down) and mapping the screen onto
  list navigation. Where a screen is a freeform cursor, he replaces
  the interaction: the Fly screen becomes a **list of fly
  locations** (the normal cursor remains available). **[SOURCE —
  BW `core/touch.lua` + release notes]**
- **Same layering in the Digimon games:** Cyber Sleuth's controller
  scheme holds L2 for all nav inputs; Battle Network 6 puts field
  info behind L2 chords and enemy info behind R2 chords.
  **[DESCRIPTION]**
- **Transfers to:** Supersonic Warriors 2 (DS) — its tag/assist
  touch inputs are exactly the "synthesize the tap from a spoken
  menu" problem, and the hold-to-enter layer solves the "adapter
  keys vs game keys" collision for every DS/GBA adapter with a
  controller. Also Harukanaru: touch-driven screens become lists.

### 15.11 Zelda 3: scan + tile cursor in a top-down action game

- **What it does:** His ALttP fork (accessibility layer merged via
  PR from the Link's Awakening DX HD manager's design — same
  category/scan DNA) adds: inventory and menu narration, dialogue
  through the game's own rendered-text path, screen-transition and
  area announcements, and query keys for health, location and
  coordinates. **[SOURCE — `src/accessibility.c`, `src/main.c`]**
- **Object scan:** F1 cycles objects in the current category, F2
  switches category (Entrances, NPCs, Enemies, Chests, Items, All).
  The scan iterates the game's 16-sprite array, filters by category,
  sorts by distance, and speaks compass direction + distance to the
  selected target on demand. No pathfinding — in a one-screen-at-a-
  time game, direction + distance + wall feedback is the whole need.
  **[SOURCE]**
- **Surroundings + tile cursor:** one key speaks the tile attribute
  in each cardinal direction from the player ("Deep water", …),
  reusing the game's own tile-attribute tables, indoors and out. A
  separate keyboard tile cursor locks to Link by default; unlock it
  and step it tile by tile, hearing the tile under the cursor — a
  TileViewer in the §7.3 sense, used for inspecting what's ahead
  before walking. **[SOURCE]**
- **Speech plumbing:** Tolk (NVDA/JAWS/SAPI) loaded dynamically;
  a priority wrapper silences current speech before high-priority
  lines. Note the honest platform limit in the code: off Windows
  the speak function is a stub — the layer is Windows-only today.
  **[SOURCE]**
- **Transfers to:** Zelda 1 / ALttP line directly (OGA already ships
  an ALttP reader — his scan categories and tile cursor are the
  interaction model to compare against), and LoG/AdvAdv field
  awareness: scan-by-category with distance sort is cheaper than a
  nav list when the world is one screen deep.

### 15.12 Battle speech and synthesized cues (Digimon)

- **What it does:** Battles get their own layer everywhere: in Time
  Stranger, L3 opens a mod battle menu (active Digimon's health/SP
  and more) while the game's own menus speak; in Next Order, a full
  set of battle handlers (dialog, HUD, items, tactics, order ring,
  results) plus synthesized audio cues — e.g. a rising two-tone
  alert (400 Hz → 800 Hz) when an enemy charges a special attack,
  milestone ticks and target-switch tones, each on its own output so
  tones never collide. **[SOURCE — DNO; DESCRIPTION — Time
  Stranger catalog scheme]**
- **Speech discipline inside battle:** announcements are built from
  the game's own text only — a standing project rule ("never
  hardcode spoken text": everything comes from the game's labels,
  localisation or data tables, so other languages don't silently
  break; the mod's own UI words are the only exception). Structured
  builders keep the format identical on every screen
  (`AnnouncementBuilder`, 15.2). **[SOURCE — DNO `CLAUDE.md`]**
- **Transfers to:** Saiyans battle wiring (enemy telegraphs deserve
  a designed tone, not a sentence — compare the Active Guard cue),
  and the fighting-game standard: per-event synthesized tones on
  separate channels so cues can overlap without masking.

### 15.13 Speech-library discipline: one library, ABI adapters, fail closed

- **What it does:** Every mod speaks through the same library
  (Prism; Tolk in the Zelda fork and older builds), so one speech
  convention — and one set of hard lessons — serves all games. The
  manager keeps old mods speaking on new systems with **tiny
  verified ABI adapter DLLs** (`prism_compat.c`): they translate an
  old Prism config struct (v1 = 16 bytes, v3 = 48 bytes) into the
  current 56-byte v4 config and forward the calls, so a 0.7.1-era
  mod can speak through 0.18.3 backends (Orca / Speech Dispatcher
  under Wine). The cautionary tale is documented: swapping the new
  DLL in directly "worked" in export lists but overwrote 40 bytes
  past the old callers' return buffer — found by probe, not by
  eyeballing signatures. **[SOURCE — AMM `PROTON_LOADER_BACKEND.md`,
  `LINUX_PORT.md`, compat sources]**
- **Fail closed, in the open:** the manager *refuses* packages whose
  loading it cannot verify — unknown speech builds, a mod with no
  load path, an ambiguous pair of launcher EXEs — with a message
  naming the problem, instead of installing a silently mute mod.
  Packages are SHA-256-verified against a signed catalog, installs
  keep receipts, and anything the manager changed (Steam launch
  options, loader configs) is saved first and restored on uninstall.
  Its Proton converter decides how a Windows mod would load by
  reading PE imports (a DLL the game loads by proxy → native-first
  override; an EXE importing process-injection calls → launcher
  replacement; nothing loadable → refuse). **[SOURCE]**
- **Transfers to:** OGA process, not gameplay: pin speech-library
  versions per adapter, adapt ABIs with small verified shims rather
  than blind swaps, and keep the fail-closed habit OGA already
  values (a silent adapter is worse than a refused one). The
  receipt/restore discipline is the model if OGA ever installs
  anything into a user's game setup.

### 15.14 The accessible-frontend pattern (XIVLauncher)

- **What it does:** For FFXIV on Linux, the manager doesn't try to
  make XIVLauncher's ImGui UI speak — it **replaces the front-end**:
  a native Avalonia login window (real accessibility-tree controls)
  driving XIVLauncher.Core's existing login/patch/launch
  controller, with one-time passwords through an accessible dialog
  and credentials in the OS keychain. The launcher engine is
  untouched; only the surface the player touches is rebuilt.
  **[SOURCE — AMM `installer/linux/xivlauncher/frontend/`]**
- **The companion discipline:** before changing a Wine setting the
  game depends on, the setup exports a registry backup and *fails
  closed* ("The game was not started") if the backup or the write
  fails — reversible-first, every time. **[SOURCE —
  `WineSpeechSetup.cs`]**
- **Transfers to:** any OGA-adjacent tooling with an inaccessible
  front-end (emulator setup screens, the BizHawk spike): wrap the
  working backend in an accessible surface rather than fighting the
  original UI; and backup-before-change for anything that touches a
  user's working setup.

### 15.15 Mapping: Amethyst trick → OGA queue item

- **Speech queue/interrupt rule + fixed content order + repeat-last
  (15.1)** → adapter-wide upgrade item (game log + repeat-last,
  verbosity) — this is its written spec; also Saiyans polish (battle
  text splitting/interruption already bit there).
- **Icon→word sanitising before stripping (15.1)** → Harukanaru and
  Saiyans message reading (symbol tokens in card/field text must not
  vanish silently).
- **Category/item nav list with bearing + "X of Y", remembered
  category, destination-named exits (15.2)** → Pokémon DS Gen 4/5
  (his Black & White mod is a working reference implementation on
  BizHawk Lua — Gen 5 done, Platinum in progress in the same
  project), Saiyans field completion, LoG II/Buu's Fury design.
- **Reachability filter sharing the router's step rule, fail-open
  (15.3)** → Pokémon DS, Saiyans, BBS/Dissidia Guide lists.
- **Turn-speaking pathfinder: cost-not-wall penalties, engine-true
  occupancy, best-effort + resumable routes, speech held during
  scripted motion, puzzle leg-planners, heuristic-free search in
  non-metric spaces (15.4)** → Pokémon DS Gen 4/5 first; Saiyans
  caves; any future gym/puzzle-heavy adapter.
- **Path-beacon at a guide point ahead, interval compresses with
  distance, camera-relative (15.5)** → BBS/Dissidia 3-D navigation;
  fighting-game radar standard (lead the target, don't point
  through walls); Mario Kart 64 investigation (a leading guide
  point is the corner-call equivalent for free driving).
- **Per-type positional audio budgets + wall sonar with quadrant
  hysteresis and hand-rolled falloff (15.6)** → BBS/Dissidia field
  awareness; per-game settings panel (per-channel volumes);
  Zelda 1 / ALttP line (wall sonar indoors).
- **Two-level routing — precomputed map route + live certification
  with a spoken fallback ladder; objectives never cut (15.7)** →
  BBS/Dissidia navigation upgrades; Saiyans landmark Guide
  hardening.
- **Icon-ID → spoken-name data tables; read the framework's own
  focus state (15.8)** → BBS Command Board and shop icon rows,
  Saiyans capsule slots, fighting-game character selects.
- **Per-screen handlers + one navigator steering native focus;
  zone enum with hidden zones count-only; opponent-action log
  (15.9)** → Harukanaru Densetsu (its core design, now with an
  implementation pattern), Supersonic Warriors 2 team-state
  speech.
- **Hold-trigger modifier layer + synthetic-touch gesture queue +
  cursor-screen→list replacement (15.10)** → Supersonic Warriors 2
  (tag/assist taps), Harukanaru touch screens, Pokémon DS touch
  screens, Saiyans touch-mode map (already solved the same way —
  his Fly-list is the general form).
- **Category scan sorted by distance + surroundings + tile cursor
  (15.11)** → Zelda 1 / ALttP line; LoG II / Buu's Fury and
  Advanced Adventure field awareness.
- **Synthesized per-event battle tones on separate outputs;
  game-text-only speech rule (15.12)** → Saiyans polish (Active
  Guard family), fighting-game adapter standard (health/proximity
  cues that may overlap), Harukanaru battle narration.
- **ABI shims, fail-closed packaging, receipts (15.13)** → OGA
  build/release process for any bundled speech or loader component.
- **Accessible-frontend replacement + backup-before-change
  (15.14)** → BizHawk PSP core spike and any emulator-front-end
  work in the queue.

## 16. Voyager Access — csm120 (Star Trek Voyager game, menus/away missions/battles)

Source: Voyager Access v0.3.92 release notes (2026-10-28, GitHub
csm120/VoyagerAccess), flagged by the user from Blind Gaming Mods
2026-10-29. Small release, but five clean, transferable patterns:

- **16.1 Tiered status readout on one query key.** In battle, I once =
  short summary: each ship's hull as a percentage, your current order,
  torpedo count, paused or not ("U.S.S. Voyager, hull 99%. Target:
  Kazon Carrier, hull 93%. Order: Attack. 38 torpedoes."). I twice =
  the full readout: shields, systems, the other enemies, your ally,
  the heroes' abilities, energy, and the goal. Press count selects
  the detail level; the short form is ruthlessly curated to what a
  mid-fight decision needs. → OGA: the foe/party query keys in every
  adapter (short form = percents + state; double-press = full stats),
  and the fighting-game standard's fight line.
- **16.2 Never report the same event twice in two forms.** The mod
  stopped reading the game's "Enemy shields destroyed" message
  because it already reports the event itself, better ("Kazon
  Carrier, shield down: port" — which ship, which side). Complement:
  a bug where only the FIRST of the game's battle messages was spoken
  and the rest went silent — fixed so all of them speak. The rule
  both ways: own the event with the richer report and suppress the
  game's redundant text, but never let game text be swallowed
  wholesale. → OGA: the sound-first audit pattern (suppress speech
  only where an equal-or-better cue exists), Saiyans battle text.
- **16.3 State flags inside list readouts.** The enemy list (under
  the full readout) says which ships have surrendered — a per-entry
  state flag read as part of the list, not a separate query. → OGA:
  foe lists / target cycling should carry state inline (stunned,
  nearly gone, fled) the same way.
- **16.4 Speak at the state transition, not at the button press.**
  "Combat resumed" was announced when the Orders wheel OPENED (the
  press), then nothing when the battle actually resumed; fixed to
  speak when the wheel closes and the battle really restarts. →
  OGA: pause/menu-exit speech and any "resumed/started" line must
  hook the real transition, or the announcement lies about timing.
- **16.5 Incoming-threat warning in the game's own words.** New: a
  warning when an enemy torpedo is on its way, phrased as the game
  phrases it. A telegraph for a projectile already in flight, kept in
  the game's vocabulary rather than invented terminology. → OGA:
  projectile/attack telegraphs (Saiyans Active Guard family,
  fighting-game radar) — warn on launch, in the game's terms.

## 17. MT2 Access — amerikrainian (Monster Train 2)

Filed 2026-10-30 from the v0.3.2 release notes (Devin shared the
Blind Gaming Mods announcement). Two small fixes, two transferable
lessons about trusting the game's data over its visual UI:

- **17.1 Show the choice from the data, not from the UI's visibility
  state.** The mod now always presents branch choices even when their
  UI is not active, instead of taking the hint from the game — the
  sighted UI inactivates what a sighted player doesn't need right
  now, and mouse mode interfered as a result. → OGA: choices/menus
  should be enumerated from game state, never gated on what the
  visual layer currently shows or highlights; an inactive-looking
  option may still be a real decision for a blind player.
- **17.2 Don't announce "new" for something that merely changed.**
  The mod stopped saying the word "new" on cards whose value changed
  — a changed card is not a new card, and the false novelty is noise
  that teaches the player to ignore the marker. → OGA: announcement
  vocabulary must distinguish appeared / changed / removed exactly;
  a marker that cries wolf gets filtered out by the player.

## 18. Brilliant Shadows Accessibility Mod — shaysters (Ren'Py visual novel)

Filed 2026-10-30 from the v1.1.0 release notes (Devin shared the
Blind Gaming Mods announcement). A visual-novel mod whose release is
mostly plumbing — and the plumbing carries the lessons:

- **18.1 Speech routing with a graceful fallback chain.** Speech now
  goes through Prism (Amethyst's speech layer — the same prism as
  the BizHawk build behind poketcg-access): if NVDA, JAWS, or
  another supported screen reader is running when the game starts,
  speak through it (the player's own voice and settings); otherwise
  fall back to built-in Windows speech. Never silence. Tolk and the
  NVDA client files were dropped in favor of the single pipe. →
  OGA/mods generally: one speech abstraction, screen-reader-first,
  OS-TTS fallback, decided at launch.
- **18.2 Non-destructive install.** The mod stopped replacing the
  game's own screens.rpy; its screens live in their own file
  (screens_a11y.rpy), and the new standalone installer
  (install/update/repair/uninstall) removes files left by older
  versions — uninstalling leaves the game exactly as it was. →
  Any mod or adapter that ships files into a game install: add
  alongside, never overwrite; make removal a first-class feature.

## 19. OpenRCT2 Access — RossMinor (RollerCoaster Tycoon 2)

Filed 2026-10-30 from the v1.2.1 release notes (Devin shared the
Blind Gaming Mods announcement). A cursor-driven map reader with an
unusually instructive changelog:

- **19.1 Announce region boundaries at the crossing.** Moving the
  map cursor now speaks "Entering" or "Leaving" plus the ride's name
  when crossing into or out of the area a tracked ride covers —
  including a track design about to be placed. Small features are
  still read only when you're on them; big regions get edges. → OGA:
  area transitions on tile/overworld maps (Saiyans map exits, world
  map zones) should speak at the border, not just on arrival events.
- **19.2 Empty is a reading, not a silence.** A bug left empty tiles
  inside a ride's area silent; fixed — the tile reads "Empty", and
  stepping back onto the ride re-reads its name. Silence is
  indistinguishable from a hang or a dead key, so every state the
  player can land on gets a word, including "nothing here". → OGA:
  cursor/grid readouts always answer, even with "Empty".
- **19.3 Tooltips: push is a setting, pull is a key.** Automatic
  tooltip reading became an option (off = only on request), and
  Shift+Enter reads the current tooltip on demand, saying "No
  tooltip" when there is none — again an explicit negative instead
  of silence. → OGA: verbose ambient speech opt-in per player; the
  query key always exists and always answers.
- **19.4 One control, one word; hints must match the real input.**
  Every slider now says "slider" (the old "spinner" unified away),
  on/off settings became checkboxes toggled with Enter to match the
  host game, and the admission price hint was corrected from Enter
  to left/right. → OGA: keep a fixed spoken vocabulary per control
  type, mirror the host game's interaction grammar, and audit hint
  text against the actual bindings.
- **19.5 Focus never lands on hidden things.** Tab used to stop on
  tabs the game hides for that window (a restroom's Vehicle tab);
  fixed across rides, stalls, Park, Finances and Cheats. The inverse
  of §17.1: choices come from data, but navigation must not offer
  what isn't real in this context. → OGA: menu/focus traversal
  filters by applicability, not by the raw widget list.

## 20. Harkest Dungeon — amerikrainian (Darkest Dungeon II)

Filed 2026-11-03 from the public source (github.com/amerikrainian/
harkest-dungeon) — the same author as MT2 Access (§17), a much bigger
mod: ~60 dedicated screen classes over a generic floor, a screen
router, and a 2,987-line accessibility audit document that is itself
a technique (every surface catalogued in encounter order with a
WORKS / PARTIAL / BUILT / NOT STARTED status, updated in the same
change that touches the screen). Code-verified in CombatScreen.cs,
Navigator.cs and the audit's "Phase 0" shared interaction model.

- **20.1 Lay the battlefield out as ONE row, matching the screen.**
  Combat navigation is a single row: the party right-to-left (rank 4
  leftmost, rank 1 at the front line), then the enemies rank 1 to 4
  continuing right — the two front lines meet in the middle, exactly
  as sighted players see it. Team readers, buffers and glances all
  filter this one row by side. → OGA: for rank/formation combat
  (Saiyans, MK Konquest fights), one spatial row in screen order
  beats per-team lists; the player's mental map stays the game's map.
- **20.2 Glance hotkeys: one key per slot, focus never moves.** In
  combat the digit row glances enemies (1 = their rank 1) and QWER
  glances the party (Q = backmost, R = front line); each key speaks
  that combatant's status line in place. A slot with no combatant is
  silent — a deliberate, scoped exception to §19.2: these are pure
  query keys pressed in a volley, and the neighbours answering makes
  the one silence unambiguous. Glances are rebindable and scoped to
  the combat input category so they never shadow other screens'
  keys. Variants per slot: status, effects, resists; S glances the
  acting combatant so the player never hunts for whose turn it is.
  → OGA: slot-glance keys are the fastest possible "how is everyone"
  in turn-based combat — cheaper than a spoken roster, and they
  compose with §1.3-style status queries.
- **20.3 Global glance keys for run state, answered from the model.**
  F = flame level, H = coach condition, B = wallet — composed from
  the run model, so they answer identically on ANY screen and stay
  silent outside a run. Two collision rules: a screen category that
  claims the same chord wins locally; and the letters were chosen
  from keys the game leaves unbound on every screen that shares the
  keyboard (R, the natural wallet key, was rejected — the game binds
  it three different ways elsewhere). → OGA: cross-screen status
  glances are worth real keys, but pick the letters by auditing the
  host game's bindings everywhere, not just on the current screen.
- **20.4 Never repeat a line verbatim just because the screen
  rebuilt.** Screens the game populates a beat after entry rebuild
  and re-land focus on every open; the navigator records the last
  announced line (`MarkCurrentLineAnnounced()` after any announce)
  and a re-land whose composed line is identical stays silent
  (`AnnounceCurrentIfChanged()`). → OGA: adapters poll state and
  re-derive the "current" line constantly; an identity check against
  the last spoken line is a one-line dedupe that kills a whole class
  of repeat chatter (menus re-opening, states re-entering).
- **20.5 Silence under the transition veil.** While the game's
  screen fader / curtain is up during a mode change, stack screens
  and the generic reader say nothing — the game assembles the next
  mode's panels under the curtain, and a screen nobody saw must not
  read (an inn's inventory panel used to announce mid-load). Modals
  bypass the gate, so a dialog interrupting a transition still
  speaks. → OGA: gate announcements on "is this state actually
  presented yet", not just "does it exist in memory"; loading and
  scene-build states are not content.
- **20.6 Honour the hotkeys the game advertises on its buttons.**
  DD2 captions shortcuts on the buttons themselves ("Map (M)",
  "Inventory (I)"). The mod's input gate swallows those keys, so it
  re-fires them itself: M/I/C activate the button carrying that
  caption in the current tree, through the button's own onClick —
  and the handlers stand down whenever the gate isn't captured, so
  shared-keyboard screens never double-fire. → OGA: if the adapter
  intercepts a key the game also uses, re-implementing the game's
  own advertised action for it is the player's expectation; letting
  it die is a silent feature removal.
- **20.7 A text pipeline that turns glyphs into words.** Inline
  effect icons in game text speak as words, pipeline-wide: token/dot
  names resolve through the game's own string tables; icons with no
  name string anywhere get authored words (heal, buff, stress, HP…);
  any other icon speaks its humanized sprite name rather than
  dropping silently ("-2 speed", never a bare "-2"). The game's "???"
  placeholder reads as the word "unknown" — synthesizers voice a
  free-standing "???" as nothing — while runs attached to a word
  ("What???") keep their marks. → OGA: numeric/stat text assembled
  from game data needs this filter pass; an icon dropped is a stat
  misread, and punctuation-only tokens can vanish entirely in TTS.
- **20.8 Review buffers that repopulate on every keypress.** Beyond
  the focus line, named buffers (control / mastery preview / hero /
  enemies / party / combat log / subtitles) cycle with Ctrl+Left/
  Right and step with Ctrl+Up/Down; each buffer rebuilds its lines
  live at the moment of the keypress, so a buffer can never go
  stale, and empty buffers are skipped so each screen cycles only
  the buffers that answer there. → OGA: a review/history surface
  should re-derive from current state on open, not replay a cached
  snapshot — cf. the announcement-history lists in §21/§24.
- **20.9 A global stand-down chord for the whole mod.** Ctrl+Shift+A
  stands everything down at once: active screen leaves, input gate
  releases, speech is disabled at the pipeline (so handlers that
  speak outside the pump also fall silent), the soundscape stops,
  and event queues are dropped every frame so a resume replays
  nothing. The state persists across launches; resuming announces
  the mod's name, then the current screen's entry. → OGA: an
  adapter that can be fully, instantly stood down — with no backlog
  flush on return — is what makes it safe to leave installed.
- **20.10 Focus-line word order is a spec, not an accident.** Status
  words lead ("selected", "owned", "mastered", "unavailable"), then
  label, role, value; refusal reasons (the game's own
  insufficient-funds / invalid-skill wording) trail as part of the
  value. Terse on the focus line; detail lives in the buffers
  (§20.8). → OGA: fix one spoken word order per line type and hold
  it everywhere — the player's ear parses positionally.

## 21. Squadsight — Berenion (XCOM: Enemy Unknown / Enemy Within)

Filed 2026-11-03 from the public source (github.com/Berenion/
Squadsight-XCOM-Accessibility-Mod) — a C DLL injected into the
32-bit game, with the pure logic (sonar field, speech queue) kept
game-free and unit-tested. Code-verified in mod/uihook/sonar.c,
speech.c, scanner.c and the README's key reference.

- **21.1 A continuous wall-sonar FIELD, not per-wall beeps.** Every
  solid face near the cursor contributes intensity (inverse-square
  falloff, tapered to zero at range so faces entering range don't
  pop) into one of four directional sums; a blocker off-axis is
  shared across directions by cos², so it's counted once, never
  multiplied. The output level is the SQUARE ROOT of the summed
  intensity normalized against a precomputed straight-wall
  reference, then soft-knee compressed (tanh above 0.85). The
  comments give the reason: raw intensities span ~50 dB between a
  wall against the tile and one six tiles off, crowded into the
  first tile or two; rooted, the same span is a few dB per tile the
  whole way — "a gradient a listener can count along". The options
  menu includes a sound-practice mode (hear a wall on any side, at
  any distance, corners, all four, silence) and a heartbeat-sound
  tour. → OGA: this is the most rigorous proximity-field model in
  the catalog — for the fighting-game radar (§1.4 family) and any
  tile-map wall sense, sum contributions, root the amplitude, and
  ship a practice mode; a cue the player can't rehearse is a cue
  they can't learn.
- **21.2 The scanner: a categorized list where distance = keys to
  press.** Page Down/Up walk a nearest-first list of everything
  worth knowing (squad, enemies, targets, explosives, civilians,
  doors, windows, objectives, interactables, high cover); Ctrl+
  Page cycles the category, Alt+Page the storey; Home teleports the
  cursor to the selected item, End reports its distance and bearing.
  Distances are measured from the CURSOR while navigating, else
  from the soldier — so the number is always "the keys still to
  press", the only unit the player can act on. → OGA: scanner lists
  (§15.2 family) should measure from the thing the player is
  moving, not from the character, whenever the two differ.
- **21.3 Speech on a worker thread, drop-oldest, interrupt as data.**
  The hook runs on the game's UI thread and both Tolk and SAPI can
  block, so callers push onto a 32-entry ring drained by a worker;
  on overflow the OLDEST entry is dropped ("stale UI text is worth
  less than what just changed"). Each entry carries an interrupt
  flag, because a moving menu highlight must cut off whatever is
  still reading. Output prefers Tolk (the player's own screen
  reader), then NVDA's controller client directly, then SAPI; every
  line also goes to the reader's braille display beside the speech.
  One hard-won sizing note: the utterance buffer is 4096 chars
  because a real mission briefing ran 611 and the old 512 limit
  dropped it unheard. → OGA: this is §15.13/§18.1's speech stack
  with the queue discipline spelled out — async always, drop stale
  before new, and size buffers from the longest real event, not a
  guess.
- **21.4 Fallback keys found by POSITION, not by character.** For
  keyboards without a numpad, the stand-in keys (right of P, two
  right of P, right of L, two right of L) are located by physical
  position, so they work on any layout; the help names them by
  position and only parenthetically by their US-layout character.
  → OGA: keybind documentation and fallback schemes that name keys
  by what they type break on every non-US layout; position is the
  portable name.
- **21.5 Context help lists your keys, then the game's — live.** Ctrl+/
  opens a help list for the current context: the mod's keys first,
  then the game's keys AS THEY ARE BOUND NOW, in the game's own
  words. The mod also follows the player's rebindings for the game
  keys it depends on, and the game's own key-binding screen warns
  when a mission action is bound to a key the mod takes in a
  mission. → OGA: help generated from live bindings can't drift
  from the truth the way a static key list in a README does.
- **21.6 Glide: hold a direction, hear only where you stop.** Holding
  a movement key glides the map cursor across tiles (speed is a
  setting); intermediate tiles are not described — the tile it stops
  on is. → OGA: for long cursor travel, destination-only speech is
  the right verbosity; per-tile reading is for single steps.

## 22. Vaudeville Access — ObjectInSpace (Vaudeville)

Filed 2026-11-03 from the public source (github.com/ObjectInSpace/
vaudeville-access). A Unity 6.3 detective game; architecturally the
odd one out in this batch — it injects NO speech of its own.

- **22.1 Publish an accessibility TREE and let the player's own
  screen reader do the reading.** Unity 6.3 can publish an
  accessibility hierarchy to the OS (UI Automation on Windows),
  which NVDA/JAWS/Narrator then read natively — focus, roles,
  activation, text-field caret review all come free and in the
  player's own voice/settings. The game's build had stripped the
  public API for creating the hierarchy, so the mod rebuilds it by
  hand from the parts the engine still calls, describes each screen
  through it (one generic screen class covers every uGUI screen),
  and moves reader focus when the game's selection changes. Live
  text (arrivals, popups, loading status) goes out as announcements
  on the same channel. → OGA: where the host offers a real
  accessibility tree (Unity 6.3+, some engines), describing state
  through it beats a parallel speech pipe — the player's reader,
  braille display and habits all apply. For emulator work this is
  the exception that proves the rule: no tree exists inside a ROM,
  so OGA's own speech layer remains the route — but for any future
  Unity/PC-side work, check for the tree first.
- **22.2 Frame rate IS speech latency when the engine answers the
  reader once per frame.** Unity services UI Automation requests
  once per frame on the main thread; with vsync on and the display
  off or idle (common for blind players — no monitor in use), the
  game fell to a steady 12.9 fps and every announcement took
  seconds, though the hardware renders at ~450 fps. The mod's first
  fix is one setting: vsync off. Its developer notes lead with this
  as the single highest-value change a developer can make. → OGA:
  when speech feels laggy in any frame-driven host, measure the
  frame rate before blaming the speech layer; idle-display vsync
  collapse is a real and invisible-to-sighted-testers failure mode.

## 23. Sally Face Access — president-lion (Sally Face)

Filed 2026-11-03 from the public source (github.com/president-lion/
sally_face_access) — a MelonLoader mod for a 2D adventure, with
hand-authored world tables per episode and its own strings in all
12 of the game's languages. Code-verified in the README key/setting
reference, TourMode.cs and SaveGuard.cs.

- **23.1 Room scan: cycle things with direction and distance, then
  re-ask for the updated distance.** Page Down/Up step through the
  room's interactables, each announced with its direction and
  distance (in a configurable "step" unit); Home re-reads the
  CURRENT thing with its distance recomputed — the player moves,
  the world doesn't, so the scan target stays meaningful while its
  numbers stay fresh. F4 answers "where am I" (room + thing count),
  F6 describes the room, F5 the current goal. In 3D areas, End uses
  the thing you last scanned to. → OGA: a scan list whose readouts
  are live-recomputed on re-query supports walk-toward-it play
  without a full guidance mode; pair with §15.2/§21.2.
- **23.2 Spoiler-graded hints.** The Hints setting has three levels:
  off; subtle — "something you carry is worth trying on this thing";
  full — names the item. The existence of a solution is itself a
  spoiler at a different strength than the solution, and the player
  picks the strength. → OGA: adventure/RPG adapters should grade
  assistance, not toggle it; a middle tier that confirms "you
  already have what you need" un-sticks players without solving
  the puzzle for them.
- **23.3 Repeat rides on the keys already in the hand.** During a
  cutscene, ANY arrow key repeats the current line of dialogue —
  no separate repeat key to remember mid-scene (F3 still repeats
  the last thing said anywhere else). → OGA: in contexts where the
  player's hands are already on a known key cluster, binding
  "say that again" to the whole cluster beats one more dedicated
  key.
- **23.4 Automated scene tour with saves hard-blocked.** The mod
  ships a developer Tour mode: it visits every scene in the build
  unattended, dumps objects/cameras/cutscene action-lists per
  scene (screenshots taken by moving the CAMERA, never the player,
  so no trigger can fire), saves progress after each scene so a
  crash resumes — and a SaveGuard blocks EVERY save entry point for
  the duration (public and private, logged per block), because
  scenes autosave on load and the autosave is the player's slot 0.
  → OGA: exactly the discipline our emulator benches need for
  unattended sweeps — enumerate content without playing it, and
  make corrupting the player's real saves structurally impossible,
  not just unlikely.
- **23.5 Own-strings localization as overrideable files.** The mod's
  own words live in lang/<language>.json per language; a user file
  in the game folder wins over the built-in one, line by line, so
  players can fix a clumsy translation for themselves without a
  rebuild. The game-text side simply follows the game's own
  language menu. → OGA: any shipped wording (room descriptions,
  hints, object names) is data the community can improve if it's
  in files, not string literals.
- **23.6 Declared skips.** Delete skips a puzzle or autoplays a
  fight "where the mod says it can" — skippability is declared per
  puzzle by the mod, and announced as available, not a universal
  cheat key. Cf. §6.4 (Pokémon Reborn's puzzle skips): the addition
  here is that the availability is part of the spoken surface.

## 24. Eliza Access — shaysters (Eliza)

Filed 2026-11-03 from the public source (github.com/shaysters/
eliza-access) — same author as §18 (Brilliant Shadows), same Prism
speech chain, a much larger mod: per-screen Views, an Element
registry, and a typed announcement system. Code-verified in
UI/AnnouncementComposer.cs, AnnouncementOrderAttribute.cs and the
README.

- **24.1 The scene as an image list.** J/L cycle the visible
  "images" — the background first, then each character left to
  right — speaking name and position, then the full description;
  K re-reads the current image. Position is spoken as a screen
  third ("left", "centre", "right") and is explicitly approximate:
  which third the character stands in is the usable fact. → OGA:
  for cutscene/visual-novel description, an ordered image list
  with coarse position beats a paragraph of scene prose — the
  player can re-walk it, stop anywhere, and build the layout.
- **24.2 Arrivals and departures are announcements.** The mod
  announces when the location changes and when characters arrive
  or leave the scene. Presence deltas are state changes the
  sighted player gets for free and the blind player otherwise
  never learns. → OGA: any scene/room adapter should diff the
  cast, not just read the current frame on request.
- **24.3 Announcements are TYPED PARTS, composed in a declared
  order.** Each UI element yields typed announcement parts (Label,
  Type, Status, Position, Tooltip); a composer groups them by
  type and emits them in the order the element's CLASS declares
  (a per-class attribute), falling back to a canonical default —
  what it is, what kind it is, its state, where it sits in a list,
  trailing detail ("Use NVDA. Checkbox. Checked."). Types a class
  doesn't list are dropped; empty parts are skipped. → OGA: this
  is §20.10's word-order discipline as a mechanism — order is
  declared once per element type instead of hand-assembled in
  every spoken string, so a new screen can't accidentally invent
  a new order.
- **24.4 Verbosity is per-category, and any key can stop speech.**
  Ambient system messages during therapy sessions are individually
  toggleable classes (bootup text, sentiment readout, phase
  changes, misc notifications) rather than one verbosity slider;
  voiced dialogue is NOT read aloud by default (the voice acting
  already says it — but the setting exists and is recommended on
  for braille users, whose display would otherwise miss those
  lines); and ANY key or button stops the current speech. Message
  lists open with the cursor on the NEWEST message. → OGA: split
  verbosity by message class, respect the game's own audio as
  already-spoken (cf. the sound-first rule), remember that braille
  users need the text sighted-hearing players get from voice, and
  start chat/log cursors where the new content is.

## 25. Pokémon TCG Access — Spacefrag5000 (Pokémon Trading Card Game, GBC)

Filed 2026-11-03 from the public source (github.com/Spacefrag5000/
pokemon-tcg-access) — a BizHawk Lua adapter in the Amethyst/prism
ecosystem (§15), reading game memory (no OCR) through the pret
poketcg disassembly. The closest relative to OGA's own work in
this batch; several of its choices deliberately mirror §15, and the
entries below are the ones stated more sharply here.

- **25.1 Parity as the stated design law.** The README's first
  section: "The goal is parity, not advantage: a blind player gets
  what a sighted player sees on the screen, and nothing more. It
  gives no hints, no hidden information and no cheats." Every
  feature choice in the mod is checkable against that sentence.
  → OGA: a one-sentence parity law, written down where players
  read it, settles design arguments (hints? spoilers? wall-peek?)
  before they start — and tells players exactly what to expect.
  (Devin's own "beyond parity" thinking may extend it, but the
  default contract is stated, not assumed.)
- **25.2 Guidance speaks at each TURN, not just the route.** P
  guides to the chosen target: the route up front, then a word at
  each turn as you walk it, then "Arrived" when you're in range to
  talk/use. P again on the same target stops guiding. Unreachable
  targets stay in the scan list, flagged "can't be reached from
  here" (keep-and-flag, as in §15.3's Zelda case — noted there).
  On the island map, choosing a place speaks the D-pad route to it
  before you commit. → OGA: turn-point prompts during the walk
  catch drift the moment it happens; an upfront route alone makes
  the player count steps blind.
- **25.3 Log what you could NOT read.** The mod writes two files as
  it plays: a log of everything it said, and a log of anything it
  could not read — "only for fixing problems", deletable any time.
  → OGA: an unreadables log turns every player's session into a
  bug report for coverage gaps (unknown screens, unmapped text);
  adapters should ship one from the start, not add it after the
  first "it said nothing here" report.
- **25.4 Symmetric query keys, Shift = the other side.** In a duel:
  H = your Hand/Deck/Discard/Prizes counts, Shift+H = opponent's;
  B = your Active Pokémon, Shift+B = opponent's; P = your whole
  play area, Shift+P = opponent's. One key per question, one
  modifier per side — no menus. And during the duel the mod stays
  quiet about anything the game's own messages already announce
  (damage, energy attached, knockouts), leaving the keys for the
  full picture on demand. → OGA: the Shift-mirror convention makes
  two-sided state (any versus fighter, §1 family) learnable as one
  rule; and the restraint rule — never re-speak what the game just
  said — is the sound-first directive (§0/Devin's rule) applied to
  a turn-based game.

## Quick-pick addendum (sections 20–25)

- Battlefield as one screen-ordered row, glance keys per slot: §20.1, §20.2
- Global state glances; pick keys the host leaves unbound everywhere: §20.3
- Dedupe identical re-announcements on screen rebuild: §20.4
- Silent while a transition veil / loading state is up: §20.5
- Re-fire the host's advertised hotkeys you intercept: §20.6
- Glyph→word text filter; "???" speaks as "unknown": §20.7
- Live-repopulated review buffers, empty ones skipped: §20.8
- Whole-mod stand-down chord, no backlog on resume: §20.9
- Continuous sonar field (sum, root, soft-knee) + practice mode: §21.1
- Scanner distances measured in keys-to-press from the cursor: §21.2
- Speech worker thread, drop-oldest ring, interrupt flag per line: §21.3
- Name fallback keys by physical position, not character: §21.4
- Help generated from live bindings, mod keys then game's: §21.5
- Glide movement: destination-only speech: §21.6
- Prefer the host's OS accessibility tree over a speech pipe: §22.1
- Frame-rate collapse = speech latency; check vsync first: §22.2
- Room scan with live-recomputed distance on re-query: §23.1
- Spoiler-graded hints (off / subtle / full): §23.2
- Repeat bound to the key cluster already in hand: §23.3
- Unattended scene tour with all saves hard-blocked: §23.4
- Scene as image list, position as screen thirds: §24.1
- Announce cast arrivals/departures (presence deltas): §24.2
- Typed announcement parts, order declared per element class: §24.3
- Parity as a written design law: §25.1
- Turn-by-turn spoken guidance + "Arrived": §25.2
- Ship an unreadables log from day one: §25.3
- Shift-mirror query keys for the other side: §25.4

## 26. SSFV — Seedy (Street Fighter V)

Filed 2026-11-03 from the public source (github.com/seedy60/SSFV),
code-mined in full the day it released: a native C++ mod (an
xinput1_3.dll proxy) that narrates a game whose menus are Adobe
Flash (Scaleform GFx) and therefore one blank rectangle to Windows
accessibility. Modelled on Skluttrell's Killer Instinct access mod;
its heap offsets were first published by the earlier SFV Access
project (a Python script the user had to run separately — this
mod's whole pitch is needing nothing outside the game folder).
Code-verified in scaleform.cpp, narrator.cpp, speech.cpp,
radar.cpp, fight.cpp, macro.cpp, hotkeys.cpp, xinput_proxy.cpp
and install.ps1.

- **26.1 Find UI objects by sweeping the heap for their vtable.**
  Scaleform text fields (GFxTextField's DocView) all begin with
  the same vtable pointer — module base + a fixed offset — so the
  sweep is a signature scan: walk the process's committed private
  read-write regions and collect every address whose first word
  equals that pointer. Two tiers keep it cheap: a QUICK sweep
  (every scan_ms, 400) only reads regions shaped like Scaleform's
  own allocations (whole 64 KB chunks plus one page) in block
  sizes fields have been seen in (0x11000/0x21000/0x31000 to
  start); a WIDE sweep reads every candidate region and LEARNS
  any new block size the quick sweep should cover. The
  address-space walk itself (tens of ms) runs on a helper thread;
  sweeps read the last completed block list, so a field-heavy
  screen never stalls the game. Entries survive two missed
  sweeps before being forgotten — a field being rewritten is not
  a field that closed. → OGA: any UI framework that allocates
  its text objects from its own heap pages (Scaleform, Flash,
  some custom Flash-like menu stacks) is sweepable this way —
  no hooks, no symbols, just one vtable value to locate per
  build. The two-tier shaped/quick + learning/wide split is the
  reusable performance shape.
- **26.2 Verify the build before trusting a single offset, and
  fail loud.** Every offset in the mod is a fact about one build
  (SFV v07.011), so verifyBuild() reads the two vtable slots it
  depends on and checks their CONTENTS point inside the game's
  own PE image (size parsed from the in-memory headers). If a
  game update moved them, the narrator refuses to start and
  SPEAKS "this version of Street Fighter V is not supported
  yet" — a wrong reader is worse than no reader. The same
  distrust runs per-read: all memory access is an SEH-wrapped
  memcpy that fails quietly (in-process, stale pointers are
  expected on every screen change), pointers must pass a range
  check, and a paragraph whose last character is not the field's
  own terminator is rejected as a torn read (text caught
  mid-write decodes as stray CJK) and retried next poll. The
  Unreal name/object tables used elsewhere get the same
  treatment: shape bounds plus a sentinel check (name index 0
  must read "None") before the table is trusted. → OGA: the
  pattern for every memory-reading adapter — signature-verify
  the build at startup, announce unsupported builds in speech,
  and make every read individually survivable.
- **26.3 Rebuild the display list: position and tint come from
  the ancestor chain.** A field's own object knows neither where
  it is drawn nor its effective colour; both are products of its
  parents. The reader walks the chain (found via the text
  object's listener back-pointer) to the movie root, composing
  each node's 2×4 affine matrix root-down (twips ÷ 20 = stage
  pixels on the 1920×1080 stage) and multiplying each node's
  colour transform, so the field's tint is the EFFECTIVE tint
  the player sees. A chain not ending at a live movie root, or
  with any ancestor's visible flag clear, marks the field
  hidden. Two hard-won corrections: effective ALPHA is not
  trusted for visibility (several plainly visible texts sit
  under a container whose zero colour the renderer does not
  apply — they are kept, at face value, if rooted and in
  bounds), and leftovers are handled at the screen level: once
  the current screen has an opaque selection of its own, any
  zero-alpha text still in the list belongs to the PREVIOUS
  screen and is dropped. Placeholder text (runs of "w", the
  layout filler) is filtered by shape. → OGA: whenever an
  adapter reads a retained-mode UI tree, selection and identity
  live in composed ancestor state, not in the leaf; and alpha
  lies often enough that visibility needs a second witness.
- **26.4 The cursor is a tint; when there is no tint, it is a
  structure.** SFV tints the selected menu text gold — effective
  RGB ≈ (1.0, 0.89, 0.549), i.e. 255/227/140, matched within
  0.05 per channel after the chain multiply. That one test
  drives most menus. Everything else in the mod is a fallback
  for screens that mark the cursor some other way, each found
  by comparing SIBLINGS rather than knowing the screen: (a)
  dialog buttons are not tinted — the chosen button is built
  with more child layers and its label sits one level deeper
  than the others in the same container, accepted only when a
  real question text lives in the panel above (without that
  guard, character select's 1P badge has the same shape and
  collapsed the whole screen to "1P"); (b) list focus drawn as
  a full-row highlight — among same-shaped sibling containers,
  the child visible on exactly ONE item, with bounds at least
  100×10, is the cursor (this is how greyed/disabled entries
  get a voice); (c) table/grid rows (the Command List) — the
  one part whose visibility differs across otherwise identical
  rows is the cursor's part; (d) grids that leave every label
  bright — the cursor's cell is the one whose checkmark slot is
  switched on. Brightness itself is a signal too: a grid cursor
  is the one label at full brightness among 75% ones, and a
  greyed-out row reads as a flat tint level of 0.55–0.70 — rows
  the game skips BETWEEN two selections are collected and
  announced as "…, unavailable" instead of vanishing. → OGA:
  budget for a family of cursor detectors, ordered tint →
  structure → sibling-difference, because real games use all
  of them, sometimes on adjacent screens.
- **26.5 Inline pictures are text too — translate asset names
  into spoken notation.** Move commands are drawn as pictures
  inside the text field (a format run carries an image instead
  of a character). The reader extracts each run's image URL
  from its TextFormat and maps the asset NAME to words:
  cmd_236 → "quarter circle forward", cmd_41236 → "half circle
  forward", a trailing c means held ("hold down"), punch_h →
  "heavy punch". A small composer then speaks the sequence
  naturally: it deletes the bare letter the game prints before
  an icon that already names the strength ("M" + medium-punch
  icon is said once, as "medium punch"), inserts "plus" between
  two buttons drawn side by side with no plus picture, and
  turns "(CROUCHING)" into "down plus". Badges on a move row
  (EX, V-Skill II, V-Trigger I) are recovered the same way from
  image names embedded in the row's HTML text. → OGA: in any
  game whose text fields embed icons (fighting-game command
  lists are the extreme case), the icon's asset name is a
  stable, speakable vocabulary — mine the name table before
  reaching for OCR or image recognition.
- **26.6 Read-only by construction; the only writes are the
  player's own keystrokes.** The mod never writes to game
  memory — dllmain.cpp states it as the design: "Everything
  here is read-only. The only process memory written is the
  loader's own image mapping." Input exists only in the
  optional macro system, and only as ordinary keyboard input
  sent when the player presses a macro key. Why it matters,
  in the mod's own framing: no memory writes means nothing
  for anti-cheat to object to, no way to corrupt a save or a
  match, and a trust story a cautious player (or a tournament)
  can verify from the pitch alone. The honesty extends to the
  macros: the INI says plainly "Use them offline; a match with
  other players may treat injected input as cheating." → OGA:
  "reads only, never writes" is a feature to advertise, not a
  limitation to apologise for — it is the difference between
  an accessibility tool and a trainer, and it should be a
  stated line in every adapter's README, with the one input
  exception named and fenced the same way.
- **26.7 Speech backends: try them in order, log the winner,
  speak from one queue.** At startup the mod tries, in order:
  Prism (prism.dll beside the mod, via its C ABI — reaches
  whichever screen reader or voice Prism is driving, JAWS
  included), then NVDA through NV Access's own controller
  client (counted as available only if nvdaController's
  testIfRunning says NVDA is actually running — a loaded DLL
  with no NVDA behind it falls through), then SAPI 5, which
  ships with Windows and always exists. Whichever won is
  written to the log ("speech backend: prism (…)" / "NVDA" /
  "SAPI") and is part of the spoken status report, so a "no
  speech" bug report names its own backend. All speech runs
  on one worker thread fed by a queue; an interrupting line
  clears the queue and bumps a generation counter, so a line
  already dequeued but not yet spoken is dropped as superseded
  instead of talking over the new one. Stop-speaking is
  per-backend (Prism stop / NVDA cancel / SAPI purge). → OGA:
  the fallback chain and the generation counter are both
  liftable as-is — chain order by reach (multi-reader bridge →
  the reader most players run → OS voice), availability tested
  by asking the backend, never by its file being present.
- **26.8 Settle before you speak, and settle SHORTER for
  cursor moves than for prompts.** The engine polls the screen
  every poll_ms (80) and sweeps for new fields every scan_ms
  (400; two consecutive empty quick sweeps trigger a wide one).
  Nothing is spoken the moment it changes: a selection must
  hold unchanged for announce_delay_ms (80) first, keyed by a
  composite of its text + footer + prompt group + question, so
  a redraw with the same content is silent and a context change
  re-speaks. Prompts and panels get a longer floor — max(500,
  delay) — because their parts arrive a frame apart (question
  after buttons, name/tab after moves), and a first announcement
  is held until the panel stops changing; but character select
  and the main menu deliberately keep the SHORT settle, with
  the reason written in the code: half a second there "is heard
  as the cursor lagging". Long lines (≥24 chars) keep the long
  settle anywhere, since a question is probably still arriving.
  Two dedupe windows guard rebuilds: a line-set identical to
  the last within 4 s is not re-said, and a redraw that merely
  ADDS a short label to what was just said ("Yes" → "No. Yes"
  as the second button paints) is covered for 12 s. During a
  fight, the menu path stands down entirely (see §26.11);
  when a prompt is live, its question is spoken once and later
  moves say only the button that changed. → OGA: two settle
  tiers, chosen by whether the player is WAITING on the content
  or MOVING through it, is the difference between a narrator
  that feels instant and one that feels drunk; dedupe needs
  both an exact window and a superset window.
- **26.9 A review buffer, focus-scoped hotkeys, and speech
  that knows when the game isn't in front.** The last
  announcement is kept as LINES, not a blob: Ctrl+Shift+R
  repeats it, Ctrl+Shift+Down/Up walk it line by line with a
  "Line 2 of 5." prefix (clamped at both ends), Ctrl+Shift+A
  reads everything on screen in reading order (fields sorted
  by y in 12-pixel bands, then x; capped at 60), Ctrl+Shift+X
  stops speech, Ctrl+Shift+M toggles narration, Ctrl+Shift+S
  dumps a snapshot of every field with position and tint to a
  file (see §26.14), Ctrl+Shift+I speaks a status report:
  fields on screen, fields known, last sweep time in ms, the
  speech backend, narration and radar state. All chords are
  Ctrl+Shift+something, registered with RegisterHotKey ONLY
  while the game window has focus and unregistered the moment
  it loses it — the same chords stay available to every other
  program, and a failed registration is logged by name rather
  than leaving a silently dead key. Focus loss also stops
  speech, silences the radar, and resets the dedupe state, so
  alt-tabbing back re-announces where the cursor is instead of
  assuming nothing changed. → OGA: register-on-focus is the
  polite way to own global chords; the spoken status report
  (counts + sweep ms + backend) turns "it doesn't work" into a
  diagnosable sentence the player can just repeat to you.
- **26.10 Fight state: marker-scan the records, then solve
  "which one is the player" as a ladder of fallbacks.** The
  two fighter records are found by the §26.1 trick with a
  different marker (the record type's vtable), then validated
  by sanity: health/critical/V gauges are 16.16 fixed-point
  value+max pairs, accepted only if 0 ≤ value ≤ max ≤ 3000.
  The scan takes about a second, so it runs when a fight
  screen is detected (round clock text, or failing that the
  health-bar labels) and retries on a 2–5 s throttle — never
  per poll. Attributing the records to sides is treated as
  the hard problem it is, with three independent witnesses in
  order: (1) the live battle settings object (found through
  the engine's object graph) names the USER entry's character
  code — the game's own answer, available before the HUD
  paints; a saved-copy settings object is rejected because it
  names a DUMMY with empty slots; (2) the CPU controller
  object points at its fighter's record, so the OTHER record
  is the player's — this is the only witness that settles a
  mirror match, where both records carry the same character;
  (3) the HUD's own name fields, matched to codes. If none
  resolve, the gauges are still read, in memory order, and
  the announcement says "Player 1 / Player 2" instead of
  "You / Opponent" — the uncertainty is spoken, not hidden.
  → OGA: steal the ladder and the honesty rule: prefer the
  game's own bookkeeping over the display, keep a
  controller-based witness for mirror cases, and label
  unattributed state as unattributed.
- **26.11 The fight line, in Devin's standard and against it.**
  Ctrl+Shift+H speaks both fighters: "You, health 73 percent,
  V-Trigger 1 of 2, Critical 2 of 3. Opponent, …" — health as
  a rounded percent, gauges in whole stocks (one stock = 300
  units), exactly the Unchained-style both-sides readout
  Devin's fighting-game standard asks for, minus two pieces:
  no time-left (deliberate — the round clock is drawn as art,
  and the text-field substitute kept returning unrelated HUD
  numbers, so they cut it rather than say wrong numbers) and
  no who's-leading call. The radar (next entry) covers the
  proximity half of the standard. Just as notable is what the
  mod refuses to say automatically: during a round the menu
  narrator stands down, HUD counters are filtered before they
  can become speech (a line with no letters and ≤6 characters
  is "an instrument, not an announcement" — damage numbers
  like "500" never talk over the fight), and the only
  automatic fight speech is "Time." when the clock hits zero,
  because the game's own announcer already calls rounds and
  knockouts. → OGA: Devin's sound-first rule (§0) confirmed
  from the field — never speak what the game already cues,
  and treat HUD numerals as instruments. Where SSFV and the
  standard diverge (timer, leader), the divergence is a
  data-reliability call, worth revisiting only with a real
  clock source.
- **26.12 The character radar: three mappings, all tunable,
  and positions that keep flowing with the sound off.**
  Fighters are located through the engine's object graph
  (every instance of the fighter pawn class), filtered to
  real fighters — hidden effect pawns share a dummy location
  and carry no costume asset, so "has a costume" is the
  reality test — with co-located duplicates collapsed; the
  player is the leftmost pawn at round start. The pulse is a
  40 ms sine blip with a sine envelope, in a ~46 ms block,
  quad-buffered through waveOut so the tone is regenerated
  from the latest values each pulse with no clicks. The
  mappings, each a one-liner worth stealing: pulse interval =
  near_ms + (far_ms − near_ms) × (|dx| / range) — linear in
  distance across the stage-width range (1800 world units,
  120→650 ms defaults); stereo pan = constant-power pan of
  clamp(dx / (range/2)) — the opponent is hard left/right by
  HALF range, so direction saturates early and the centre
  stays informative; pitch = base_hz × (1 + 0.9 ×
  clamp(dz/height, −1, 1)) — height above/below you (dz, at
  220 units ≈ full rise), roughly 0.1×–1.9× the 520 Hz base.
  Every knob is in the INI ([radar] volume, range, height,
  near_ms, far_ms, base_hz), and positions are refreshed even
  when the pulse is off, so the on-demand spoken version
  (Ctrl+Shift+D: "Opponent to the left and above you, close,
  about 300 units away" — side past a 30-unit dead zone,
  height past 40% of the height parameter, distance in named
  bands: point-blank <150, close <450, mid-range <900, far
  <1400, full screen beyond, rounded to the nearest 10) works
  without turning the audio on. The radar runs only in a live
  round — inferred, since there is no flag to read: clock
  under 99, or 99 held for 4+ seconds (training mode), or
  both health bars up when the stage draws its clock as art —
  and a visible pause menu kills it instantly. Against
  Devin's standard: the continuous proximity pulse with side
  pan matches the MK1-style radar he mandates; the difference
  is the pitch channel — SSFV spends pitch on HEIGHT (jump
  arcs, crouching), where Devin's HUD-beep conventions spend
  pitch on health. For a 2D fighter, height is arguably the
  better spend — it is the one spatial fact pan and rate
  cannot carry. → OGA: lift the interval/pan formulas and the
  "positions update even when muted" rule whole; decide the
  pitch channel per game (height for fighters with real
  vertical play, health where there is none).
- **26.13 Numpad-notation macros: a motor/cognitive access
  pattern in one INI grammar.** A macro is a string in
  fighting-game notation — 236236P, [4]6[4]6P, 720P, MP+MK —
  parsed into timed steps: each digit is one direction step
  of step_ms (34 ms ≈ two frames at 60 FPS), a bracketed
  direction is held for hold_ms (1000) for charge moves, 720
  expands to the literal two-rotation sequence, P/K mean
  heavy punch/kick (LP…HK exact), + binds buttons onto the
  last direction step (which then lasts 2 steps). Lookup is
  per character: a [macros.<NAME>] section keyed by the name
  on the health bar, falling back to global defaults (V-Skill
  MP+MK and V-Trigger HP+HK are universal, so only Critical
  Arts need per-character entries — the shipped INI carries
  one for all 43 characters with a fixed input; Eleven, who
  copies the opponent, deliberately has none). Facing is read
  live from the radar's positions and every horizontal
  component is mirrored when the player faces left. Playback
  is defensive in ways worth copying: it sends BOTH virtual-key
  and scan codes (some input paths read only one); it first
  RELEASES the Ctrl/Shift the player is still holding from
  the hotkey chord (otherwise Windows eats the macro's own D,
  H, M as system hotkeys) and never re-presses them; it
  releases only keys it pressed itself, never the player's
  held direction; it aborts the moment the game loses focus;
  and the narrator's hotkeys are suppressed while a macro
  plays and for 300 ms after, because the macro's injected
  keys would otherwise trigger the mod's own chords. → OGA:
  notation-as-config means players and community members can
  author macros without code; the release-the-modifiers and
  suppress-own-hotkeys details are the two failure modes
  every injected-input feature will meet on day one.
- **26.14 Ship as a proxy DLL — and back up the other guy's
  proxy, never overwrite it.** The mod is xinput1_3.dll: SFV
  imports it by ordinal at load, so dropping it beside the
  executable runs the mod before any game code — no injector,
  no launcher, Steam launch options untouched. The proxy
  forwards ALL SEVEN XInput 1.3 exports to the real System32
  DLL, because any later module resolving that name gets the
  proxy and must still work. The installer handles the name
  collision as a neighbour would: a marker file
  (xinput1_3.dll.sfvaccess) distinguishes "our own DLL from
  an earlier install" from "someone else's proxy" (SF5DInput
  uses the same name); a foreign DLL is copied to
  xinput1_3.dll.before-sfvaccess — once, never clobbering an
  existing backup — and the README tells the user the two
  proxies cannot coexist. Uninstall is deleting four named
  files, printed by the installer. The build is one cl
  invocation with /MT (no runtime DLL of its own to go
  missing) and the vendored NVDA client is copied beside the
  mod, so the release zip needs no downloads. → OGA: for
  Windows games with a known early-loaded DLL, the proxy
  pattern plus marker-file/backup etiquette is the
  lowest-friction install story available — and respecting a
  rival mod's file is what makes users trust yours.
- **26.15 Ship the research harness and the self-diagnostics
  with the mod.** tools/ contains the mod's own development
  instruments, kept in the release repo: read_scaleform.py,
  the SAME sweep written as an external, read-only Python
  harness (OpenProcess + ReadProcessMemory, prints what a
  narrator would say, --watch for selection changes) — the
  offsets were proven from outside the process before the
  native mod existed — and game_input.py, which focuses the
  game and sends real keystrokes to drive the narrator for
  testing. In the mod itself: the timestamped log records the
  chosen speech backend, every sweep change, every
  announcement, and any frame where read+fight+poll took
  ≥150 ms ("slow: …"); log_fields=1 dumps every field of
  every announcement; and the snapshot key writes every field
  — position, all four tint channels, hidden/loose/chosen
  flags, text — to a timestamped file a player can send the
  author. → OGA: this is §25.3 (ship the unreadables log from
  day one) grown up: the harness that FOUND the offsets, the
  snapshot that shows a broken screen exactly as the reader
  saw it, and a log written so a player's bug report is
  already the diagnosis.

## Quick-pick addendum (section 26)

- Heap sweep by vtable signature; shaped quick + learning wide: §26.1
- Verify the build first; unsupported builds announced, not misread: §26.2
- Position/tint composed from the ancestor chain; alpha distrusted: §26.3
- Cursor = gold tint, else sibling-structure differences: §26.4
- Icon asset names translated to spoken notation: §26.5
- Read-only as a stated, advertised design line: §26.6
- Speech chain Prism → NVDA (only if running) → SAPI; winner logged: §26.7
- Two-tier settle (short for cursor moves, long for prompts): §26.8
- Review buffer with line walk; hotkeys registered only on focus: §26.9
- Side attribution ladder: settings → CPU pointer → HUD names: §26.10
- Health % + gauges in stocks; HUD numerals are instruments: §26.11
- Radar: linear pulse lerp, pan saturates at half range, pitch = height: §26.12
- Numpad-notation macros, per-character INI, facing mirrored live: §26.13
- Proxy-DLL install; foreign proxy backed up, marker file: §26.14
- Ship the offset-finding harness + field snapshot with the mod: §26.15

## 27. TWD Access — Kong (Kongster90) (The Walking Dead: The Telltale Definitive Series)

Filed 2026-11-04 from Kong's AudioGames release post the day
it appeared, code-mined in full from the public source
(github.com/Kongster90/TWDAccess, MIT; clone at
oga-queue/mining/twdaccess, HEAD 7d58410 = v1.3.0, released
2026-11-03; 1.0.0 was 2026-10-31). The shape is new to this
catalog: not a memory reader but a script mod — plain Lua 5.2
(~15,000 lines across ~55 modules) running INSIDE the game,
loaded through a developer hook the retail build left in,
plus a small C# helper (.NET, self-contained) that owns
speech and tones. It covers all five parts of the Definitive
Series (Season One + 400 Days, Season Two, A New Frontier,
The Final Season, Michonne), each with its own framework
generation and therefore its own module set (lookaround /
lookaround2 / lookaround4, choices / choices2, drag / drag2
/ drag3…). The author says every season was played through
start to end with the mod, mostly picking the first answer.
Code-verified in main.lua, core.lua, hooks.lua, settings.lua,
text.lua, uitext.lua, menus.lua, choices.lua, choices2.lua,
lookaround.lua, actions.lua, struggle.lua, aiming.lua,
sounds.lua, beacons.lua, subtitles.lua, hotkeys.lua,
configurator.lua and the helper's Program / ModInstaller /
Speech / SpeechSequencer / Tones, plus the HANDOFF.md
session log shipped in the repo. The mod was not run; play
claims below are the author's, code claims are ours.

- **27.1 Get in through the developer hook the game left
  behind — and solve the state-reset problem it creates.**
  The title/main-menu scripts contain a leftover dev hook:
  `if ResourceExists("DebugLoader.lua") then
  require("DebugLoader.lua") … end`. The installer places
  an encrypted (game-format) DebugLoader.lua in Archives;
  no game file is replaced. But Telltale throws its scripts
  away per season/episode, and saves remember a state's
  FIRST script — which, for states the mod started, is the
  bootstrap itself. So the bootstrap is a small state
  machine: carryover.lua leaves TWDAccess\next_script.txt
  (with a time-of-day stamp; a note older than 120 s is
  distrusted and discarded, since Lua's os.time here is
  4-byte-float and steps in 128 s jumps) naming the real
  first script; the bootstrap starts the mod, then runs
  that script in the same top-level thread (`loadfile`,
  outside any pcall, so its errors and waits behave
  normally — queuing it with LoadScript deadlocked season
  loading). With no note, it recovers the script from
  saves.txt (slot → first script, recorded at save time),
  the stored current-script name, or the episode's first
  script, in that order. Because making DebugLoader.lua
  "exist" also wakes the menu's next dev step, the
  bootstrap shims `require` to swallow MenuBootUtils.lua
  (installing a harmless MenuBoot_CreateDebugMenu stand-in)
  and stubs the Callback_OnLoadDebugMenu object. → OGA:
  scripted engines (Telltale Tool, and by analogy any
  Lua/script-VM host — the MKU script-VM work included)
  should be scouted for leftover debug-loader hooks before
  anyone reaches for an injector; and a bootstrap that
  becomes the recorded first script inherits a save-load
  obligation: it must be able to reconstruct what the game
  meant to run, from a fresh note, a per-slot record, or
  the episode table, and say which witness it used.
- **27.2 A defensive hook substrate for a hostile script
  host.** Game scripts define UI functions as globals,
  often after the mod loads and sometimes twice, so hooks
  are installed two ways: a metatable `__newindex` on _G
  applies a hook the moment its global is first defined,
  and a per-frame pass re-applies any hook whose global
  was redefined. Several modules can wrap the same
  function; factories chain in registration order. The
  rules are written at the top of hooks.lua and followed
  everywhere: the original is always called exactly as the
  game called it (errors and coroutine yields intact), and
  mod code runs inside pcall so a mod bug can never break
  the game. The substrate reaches LOCALS, not just
  globals: TWDA.upvalue / unwrap walk debug.getupvalue
  (peeling the mod's own wrappers by upvalue name), and
  debug.setupvalue REPLACES a local function inside game
  code — that is how the mod wraps mode_dialog's private
  selectChoice, gamedialog's private SetChoices /
  ActivateChoice, and reads private state (mTimerController,
  mStruggleAgent, mMappedChoices, mHighlightedIdx) that no
  API exposes. The per-frame thread the features live on
  is treated as mortal: the engine kills script threads
  on scene changes, so every hooked call revives it, an
  EnginePostUpdate hook revives it within a frame even
  when nothing else is called, each frame callback gets
  an error budget (5 strikes, then it is skipped) and a
  TWDA.frameOff kill switch names a misbehaving callback
  during debugging. One broken module cannot take the
  others down: safeInclude logs and continues. → OGA:
  this is the reference shape for any in-engine Lua work:
  definition-time + per-frame hook application, chained
  wrappers, pcall isolation, upvalue surgery for private
  state, and a self-reviving frame loop with per-feature
  error budgets.
- **27.3 Speech that leaves the process needs sequence
  numbers.** The Lua side cannot speak; it batches each
  frame's messages into HTTP GETs to the helper on
  127.0.0.1:41234 (/speak, /tone, /log), each request
  carrying a per-script-state id and a sequence number,
  payload URL-encoded and chunked at 1,800 characters.
  Async requests overtake each other, so the helper's
  SpeechSequencer re-orders by (state id, seq): it drains
  in order, waits at most 80 ms for a missing number,
  then skips the gap; a late arrival after the skip is
  delivered immediately; a new state id flushes the old
  state's leftovers and restarts numbering. Interrupts
  are a prefix on the line ("i" vs "q") and clear the
  Lua outbox first — anything queued that frame would be
  cut off anyway. In the helper, one dedicated STA thread
  owns the Prism context (backends are not thread-safe);
  it acquires a forced backend by name or the registry's
  best, tries prism_backend_output and falls back to
  prism_backend_speak on NOT_IMPLEMENTED, and logs the
  backend in use. Everything said is also written to
  speech.log, and the game's side keeps twda.log — the
  README tells stuck players to send twda.log with a bug
  report. → OGA: compare §26.7 (SSFV speaks in-process and
  needs no ordering). The moment speech crosses a process
  boundary over async transport, per-state sequence
  numbers + a short reorder window + gap-skip is the
  whole solution, and it is ~70 lines.
- **27.4 Timed choices: freeze the game's own countdown,
  and read the highlight from the game, not your keys.**
  Choices are harvested at build time, two ways for two
  framework generations: Seasons 1–2 hook
  mode_DialogChoice:OnStart and collect the texts the
  game writes to screen agents while it runs; the newer
  framework's choices are read from SetChoices' upvalues
  (mMappedChoices slot → choice, "Choice Display Text"
  else "First Line"; the default is "...", spoken as
  "Say nothing"), then sorted into reading order by
  on-screen position (rows within 0.02, then x). They are
  announced as one numbered list; 1–4 (top row or numpad)
  pick. On mouse-only generations the mod synthesises the
  keyboard itself: its number keys call the game's own
  Dialog_ActivateChoice(slot) — in a fresh thread, because
  that function waits on the line being spoken — and
  Up/Down move a highlight the mod tracks. The timer
  hold, per generation: Seasons 1–2 call the pause menu's
  own mechanism, Game_PauseAllDialogs(), re-applied EVERY
  FRAME (the pause menu resumes dialogs when it closes),
  and the pick is intercepted by the upvalue-wrapped
  selectChoice so dialogs resume an instant before the
  pick lands — a pick made while paused is kept and takes
  effect on resume. The newer framework instead sets the
  timer bar's controller time scale to 0
  (ControllerSetTimeScale on the mTimerController
  upvalue). After arrow/wheel navigation the mod reads
  the game's own mHighlightedIdx upvalue and speaks that
  option — the code notes counting its own key presses
  "drifted from it". Setting choicetimer = normal keeps
  the game's timing. → OGA: for timed narrative choices,
  do not build your own countdown or estimate the
  deadline: find the dialog system's own pause (or its
  timer controller) and hold THAT, keep re-applying it
  against anything that resumes, and resume inside the
  pick path itself. And cursor truth lives in the game's
  highlight variable, never in a key counter. Directly
  relevant to any Telltale-style adapter and to timed
  choices in visual novels generally; the contrast with
  Devin's Active Guard rule is instructive — a choice
  timer is UI, so freezing it is fair, where a gameplay
  timing window gets a cue (§0, §13-lineage), not a pause.
- **27.5 Exploration as a ranked, self-describing list.**
  Tab/Shift+Tab cycle the usable things; the game is
  never asked to move a reticle for it. Enumeration
  (Season 1): agents with "Game Selectable" true, not
  hidden, not the player, not triggers, whose use dialog
  is not known-unavailable. Names are made speakable by
  distinctName: things the game names alike are numbered
  ("Porte 1", "Porte 2") by sorted agent name — outside
  English it numbers rather than mixing English agent
  words into the localised name (see §27.14). Actions
  come from the thing's live use dialog, NOT its verb
  property, which the code shows going stale ("Talk To"
  where the dialog now offers "Eviscerate"); with ≥2
  non-look actions they are numbered inline. Each entry
  is spoken as name, reach tag (§27.6), direction words
  from the camera/reticle geometry, distance in whole
  metres, and "N of M". Ordering is a rank ladder:
  in-view first, then things to a side, then behind,
  blocked things at 2.5 (last), and the sort key embeds
  the blocked flag ("!") so a thing becoming blocked
  re-sorts the list. When control is handed to the
  player, the count is said once. Later generations keep
  the same shape over their own plumbing (ReticleSelection
  usables + verb data sorted nearest-first in S2/ANF;
  Useable_GetCount's mCurrentUseables in TFS, where Tab
  hijacks Useable_OnButtonUse to run the CHOSEN thing
  instead of the camera-pointed one). → OGA: the
  adventure-game exploration pattern: enumerate from the
  game's own selectable set, speak name + actions +
  direction + distance + ordinal, sort by what the
  player can act on now, and treat the display verb as a
  hint while the dialog is the truth.
- **27.6 Reachability has three names, and failure is
  remembered.** 1.3.0's whole point. A thing you cannot
  use from where you stand is tagged with WHY: "out of
  reach" (plain: Enter walks you there); "blocked" — a
  walk there already stopped short (see §27.7), stored
  in a weak table agent → { where Lee stood, nearest way
  out within 3 m } and spoken in full ("blocked, the last
  walk stopped near Gate"), ranked last, and cleared by
  any successful use, by a later walk that arrives, or
  by walking 10 m away from where it stopped; "in
  another area" — the thing's use dialog is not loaded
  in this area (dialogs load by area, so
  findDialogWithNode returning nothing is an area test,
  not a guess). Two false-positive guards are written
  into reachTag from live bugs: never say "in another
  area" of a character (Lilly beside Lee had no loaded
  dialog yet talked fine when used), nor of anything
  within 3 m. F6's summary says only blocked / another
  area — most things in free walking are out of reach,
  and saying so everywhere is noise. The release also
  synthesises PLACES the game never offered: after you
  open a door whose hotspot then disappears, "Open
  doorway" appears for 60 s if you are still on the near
  side — a walk target 2.5 m past the door, on the walk
  boxes, announced once control returns ("<Door> is
  open. Enter walks through it.") unless a subtitle,
  hint or action scene has since moved the story on;
  and a scene table adds hand-authored places with
  height gating ("Porch steps", listed only within
  1.5 m of the porch's height, with its own arrival
  line for when a scene takes over the walk). → OGA:
  failed navigation is state worth keeping: remember
  where the path stopped and what was near it, demote
  the target, speak the reason, and expire the memory
  on use, success, or distance. And when a door's
  hotspot vanishes on opening, the doorway itself
  becomes the next hotspot — the Macon pharmacy bug
  (nothing said to walk through) is the generic case.
- **27.7 Walk with the game's pathfinder, supervised.**
  Enter on an out-of-reach thing calls the game's own
  AgentPathToAgent / PathToAndUseAgent — in its own
  thread, because it returns only on arrival. A walkTick
  supervises: walkers that were ≥8 m away at the start
  and close within 5 m stop the walk; PathAgentIsBlocked
  held for 2 s speaks "…is blocked on the way to X";
  remaining distance is reported as it shrinks; a walk
  that ends well short writes the §27.6 blocked record
  (with the nearest way out — a door, ladder, stairs,
  gate, hatch, exit within 3 m — named in the message:
  "got to N meters … can go no further this way. Close
  by: Gate, 2 meters."). If a scene takes Lee over
  mid-walk, the place's `taken` line is said instead of
  "the walk was cut short". For using things out of
  view, useWithReticle turns the camera/reticle to the
  target first, then fires its first non-Look action.
  → OGA: never teleport and never blind-walk: pathfind
  with the engine, narrate progress and failure distance,
  and feed failures back into the exploration list's
  ranking (§27.5/27.6). The blocked-record is the piece
  most adapters lack.
- **27.8 Beacons: camera-relative pings with a muffled
  behind.** F4 cycles beacons: on Tab's choice / on
  everything near / off (default: Tab's choice). Direction
  is computed from the CAMERA's forward/right axes, not
  the character's facing — walking keys are camera-
  relative too, so a beacon on the right is reached with
  D even after a fixed-camera cut. pan = sin(angle);
  behind is |angle| > ~100°, rendered as low-pass
  muffling in the helper's tone mixer PLUS pitch ×0.85.
  Each kind has a timbre: thing = 1200 Hz, 30 ms sine
  blip; place = rising two-note chime 660→990 Hz;
  walker = 140 Hz, 110 ms square buzz. Volume =
  0.08 + 0.4 × (1 − distance/25 m). "All" mode round-
  robins the nearest 4 plus the chosen one (chosen sounds
  however far), one ping per 0.22 s (0.4 s chosen-only);
  the candidate list refreshes every 0.25–0.5 s and
  things/walkers re-read their live position per ping.
  Quiet in close-ups, talks and scenes, and while the
  bow is drawn (its own tone owns the channel). The
  HANDOFF records the design pressure: "all" at a few
  pings a second on everything was "too many at once to
  tell apart, the user found" — hence nearest-4. → OGA:
  for exploration targets this complements rather than
  copies Devin's continuous proximity radar (which is
  for an opponent in a fight): discrete kind-coded pings,
  camera-relative, behind-muffled, hard-capped in count.
  The behind treatment (muffle + slight pitch drop) is
  cheaper to read than pan alone and worth adding to
  OGA's spatial vocabulary.
- **27.9 Action scenes: classify the danger, arbitrate
  the slow-down, ration the speech.** Detection is a
  disjunction of witnesses, each distrusted alone: the
  panic-meter scene visible, the reticle in combat mode,
  a struggle active, a turn choice up, cover walking —
  or a countdown, found by scanning the scene's timer
  agents every 5 s and CLASSIFYING them: a timer counts
  as danger if its completion dialog starts "cs_" (a
  cutscene), or lasts ≥5 s and is not chatter (name and
  node filtered for hint/ambient/chatter/banter/idle),
  and a "logic_" timer counts only if its name carries
  a danger word (zombie, walker, death, die, kill,
  attack, break, eat, bite, fail) — a plain logic timer
  at Crawford only brought Molly in and made the mod
  cry "Enter!" at a garage door. Remaining time is
  computed honestly: progress × length / (speed × scene
  time scale), or from the MEASURED fill rate when the
  length lies (one timer reads length 0 and fills in
  ~29 s), and spoken rounded — to the nearest 5 above
  10 s ("about 60 seconds", not 59). Announcements put
  the time first so what to press comes last. Slow
  motion is a shared arbitration: TWDA.setSlowFor(reason,
  on) keeps a set of reasons (action scene, S2 prompts,
  fight targets); while any reason stands and the scale
  is 1 it sets 0.45 — an odd sentinel value, chosen so a
  leftover slow-down is recognisable as the mod's — and
  restores 1 only when the set empties AND the scale is
  still 0.45 (never stomp the game's own slow motion).
  The F11 modes are normal / slow (default for new
  players) / auto; auto queues each newly usable
  non-Look thing in appearance order and uses them one
  at a time through the reticle — with two refusals
  written in code: it will not act during a turn choice
  (whom to help is the player's choice), and shooting /
  killing is never auto-pressed in free walking.
  Announcement rationing during scenes: usable-from-here
  in view comes first and alone; doing beats looking;
  talking comes last; a thing NAMED in a subtitle in the
  last 8 s jumps the queue (a subtitle listener watches
  for hotspot names — "KEYS! NOW!" promotes the cage
  door's key); identical re-announcements are suppressed
  against lastSpoken, and after 4 s of quiet the current
  option is repeated ("Enter!"). → OGA: three liftable
  systems — danger classification by name heuristics
  with a measured-rate fallback, sentinel-valued shared
  time-scale arbitration, and urgency ordering with a
  dialogue-mention boost. The auto-mode refusals are the
  ethics of the thing: automate execution, never
  decisions.
- **27.10 Prompts as a grammar: state, percent, next
  key.** Every QTE family gets a small state machine and
  one spoken sentence shape that always carries the next
  action: "Q!", "Left arrow!", "E, then Q, then E!",
  "Hold S to go on". Struggles (335 lines, four framework
  variants) are the worked example: the meter is read
  from agent properties ("Button Mash - Current
  Percentage" / "Chore Scrubber - Percentage Current",
  max of the two — Michonne fills one, the others the
  other); finish-readiness is detected from whichever
  upvalue the variant exposes (mDeathBlowReadyStart,
  mDeathBlowController, mDeathBlowReadyDelta); the
  opening line encodes the mode: auto → "Struggle!
  Fighting back for you."; a waggle/circle kind → the
  pattern in words ("press the arrows round in a
  circle, Up, Right, Down, Left, over and over"); slow →
  "Press Q over and over, or hold it down. Then E when
  told." F6 mid-struggle: "Struggle, 47 percent. Press Q
  over and over." In slow mode holding Q starts
  auto-repeat after 0.25 s at a 0.07 s interval; in auto
  the mod presses Q and E itself through each variant's
  own handlers. Michonne's mouse-only swipe struggles
  are translated by a stroke graph (NEXT_STROKE: which
  arrow may follow which for waggle-horizontal,
  waggle-vertical, circle-clockwise, circle-counter):
  each correct arrow calls the chain's own progress
  callback (an upvalue) for ~7% of the meter. ANF's QTE
  module (qte3) maps picture prompts to keys through
  QTE_UI_ShowPrompt, honouring the agent's "QTE - UI
  Enable Alt Keyboard Keys" property (E serves for a/b,
  Q for x/y — the letters the game then shows), with a
  0.6 s same-prompt dedupe. → OGA: against the catalog's
  timing-cue lineage (Budokai Access's rising tone +
  restart beat for Active Guard), TWD's contribution is
  the verbal grammar for DISCRETE sequences: announce
  the pattern once in words, keep a live percent for
  mash meters, and name the finishing key the moment
  its readiness flag flips — readiness read from the
  game's own state, never timed.
- **27.11 Translate mouse-only mechanics through the
  game's controller path; don't emulate the mouse.**
  Repeated pattern, five worked cases. Drags: the drag
  only lives while the mouse button is held, so a key
  could never hold it — instead, just during activation
  the mod hooks Input_UseCursor to answer false and
  Input_IsButtonPressed to hold button A, putting the
  game on its controller path where the arrows drag;
  the direction is read from the logic agent's "Drag -
  Direction" property (S2/ANF variants read their
  direction chores / Enabled properties instead, and
  drag3 even holds the button itself while an arrow is
  held, so the arrow alone is enough). Fast/slow sneak
  (fastslow.lua): the mode wants a stick push — under
  ~0.7 tilt plays the quiet dialog, full tilt the jump-
  out — so the mod feeds its OnLeftStick handler
  directly: held arrow = 0.5 tilt, Shift+arrow = 1.0,
  with the wanted direction recovered from the mode's
  own logic properties. Turn choices: the PC arrow
  handlers return immediately (one TV platform only), so
  the mod calls the mode's private transition(side)
  itself. Peek/cover/guided paths read their chore and
  trigger properties: cover walking computes Lee's
  position BETWEEN two moving trigger volumes (0 = rear
  edge, 1 = front) and speaks phase commands at fixed
  thresholds ("Walk: A" below 0.45, "Run: Shift and A"
  below 0.25 until past 0.5, "Stop" above 0.7, back up
  above 0.88), keys derived from the cover's on-screen
  direction and re-derived on every camera cut; guided
  (chored) paths report percent along the path and ask
  the game's own GetRelativeDirection upvalue which key
  the camera-relative forward is. The Final Season's
  mouse-only story recap (configurator.lua) is driven
  through Configurator_SelectButtonByIndex /
  Configurator_OnA with captions read off the button
  agents. → OGA: when a mechanic is mouse-only on PC,
  look for the engine's OTHER input path (stick /
  controller / button events) and drive that — the game
  already knows how to play it that way; and for
  continuous positioning states, speak phase commands
  at thresholds, not raw numbers.
- **27.12 Aiming: trust the hit test, sweep what you
  cannot project.** The shotgun/scope/bow aim tone: pitch
  = 1000 − 700 × off, where off is radial screen distance
  to the target normalised by 0.65; pan = clamp(ex / 0.33);
  on target = a quick double tick (1500 then 2000 Hz,
  square) plus "On target. Enter to fire." said once.
  Where the target is comes from the game's own hit
  test, AgentAtScreenPos(point, …, cop) — the same call
  Shotgun_Fire makes — scanned along the crosshair's row
  first, then a fixed row ladder (0.5, 0.38, 0.62, 0.26,
  0.74, 0.14, 0.86). Horizontal aim can be projected
  through the pan camera; vertical cannot (nothing found
  follows the up-down pan, and AgentGetScreenPos was up
  to a tenth of a screen off the hit test), so the
  assist SWEEPS pointer height between 0.25 and 0.65
  until the hit test fires, and near the target (within
  0.08 screen widths) closes 25% of the remaining x
  error per fresh sample. Tab engages the full assist;
  aim = auto drops the tone and speaks on/off target;
  full auto fires after 0.15 s stable on target. → OGA:
  §14's aiming lineage gains a rule: the shot's own hit
  test is the ground truth for "on target", projections
  are hints to search with — sweep the axis you cannot
  project and let the hit test call it. The two-channel
  tone (pitch = closeness, pan = direction, double-tick
  = on) matches Devin's radar conventions with pitch
  spent on radial distance instead of height (§26.12's
  fighter case) — the channel assignment follows what
  the task's second spatial fact is.
- **27.13 The UI text bus, focus-as-method-call, and a
  describe cascade.** Three substrates carry the menus.
  (1) One hook on AgentSetProperty watches every "Text
  String" write in the game and fans it out to
  onTextProperty handlers — choices.lua harvests choice
  texts from it — while agents whose NAMES match
  helptext / notification / notify / tutorial / hint /
  choicemade / nowplaying are spoken as they change
  (sayOnce: identical text within 2 s is dropped; in an
  action scene a hint interrupts, during cover walking
  it is held so it cannot cut the walk calls). (2)
  Menus: the Button constructor is hooked so EVERY new
  button's Select method is wrapped — focus is a method
  call, so selection speech needs no polling — with a
  resting-mouse guard: a Select is not read if no nav
  key was pressed in 0.5 s and the mouse has not moved
  in 1 s (carousels and legend buttons otherwise read
  whatever the parked pointer sits on), a same-button
  0.5 s re-select dedupe, and value changes spoken as
  only the changed part (a settings row's parts are
  diffed comma-wise; "Volume : 7" is announced as "7").
  Controller-only menu buttons get keyboard stand-ins
  (F9 = X, F10 = Y, PgUp/PgDn = bumpers) that run the
  menu legend's own command where there is one, else
  synthesise the widget input event with Input_UseCursor
  temporarily falsified (the handler ignores controller
  buttons in mouse mode). (3) F6 describe: an ordered
  cascade of ~20 describe* functions (death, action,
  configurator, combat, bow, struggle, aiming, sniper,
  cover, drag, fastslow, turn choice, stats…) — first
  non-nil wins for modal states; then choices; then
  message box, credits, menu/pane; then in play: peek,
  guided, walkers, countdown, hotspots, inventory —
  falling back to the last hint, else "Nothing on
  screen to read." F5 repeats lastSpoken. Subtitles
  hook Subtitle_OnCreate: speaker names are cleaned
  (camel-split "TinyCarlos", a rename table for agents
  the game misnames) and the mode defaults OFF — you
  hear the voices; F7 cycles speaker-names-only / full
  lines (§0's sound-first rule applied to dialogue) —
  and every line also fans out to onSubtitle listeners,
  so mechanics can react to what is SAID (§27.9's
  mention boost, guided paths keyed to a shouted
  "Stop right there!"). → OGA: a single text-write bus
  + focus-as-method-call + one prioritised describe
  cascade is a complete menu/narration architecture for
  script-engine games; the resting-mouse guard and the
  changed-part-only value speech are the two details
  that keep it from feeling noisy.
- **27.14 Localisation: decide by language-free signals,
  never by display text.** The mod's logic cannot match
  on shown verbs — in French, Enter on the Motor Inn
  truck LOOKED at it, because looksOnly matched English
  words. The fix (1.2.0) finds signals that do not
  localise: a hotspot's "WDUseable - Verb" property is
  English in every language; each action choice's
  "Choice Icon" resource name is a picture, mapped to an
  English verb (ui_useicon_eye → Look, _ear → Listen,
  talk, sneak/walk → Move To, punch, hatchetchop → Chop,
  pistolshoot → Shoot, kick…); shown↔English pairs are
  noted as they are met (noteVerb / noteVerbIcon) and
  englishVerb() translates for every later check.
  Thing-kinds (door, gate, ladder, zombie) outside
  English take their words from the agent's name, not
  the label. Case handling is done at the UTF-8 byte
  level because Lua's %u/%l are ASCII-only: lower()
  maps Latin-1 capitals, Œ and Cyrillic by byte pattern;
  caseShape() builds a same-length A/a mask so pattern
  classes can see two-byte letters ("ÉPISODE" →
  "AAPISODE"); normalCaps() un-shouts all-caps text for
  speech with a KEEP_CAPS acronym list; joinSentences
  knows CJK sentence terminators (。！？：) and the
  ellipsis, and Chinese gets no added full stop after
  its own. → OGA: any adapter that will ever run under
  a localised game should key logic on the underlying
  enum / property / icon and treat display text as
  output only; and "speech would spell that out" fixes
  (all-caps normalisation) need the text's real encoding,
  not the host language's ASCII assumptions.
- **27.15 Installer etiquette: hash manifest, foreign
  files refused, saves keep the bootstrap.** Install
  copies the mod folder (which mirrors the game's
  layout) into the game folder and records every file's
  SHA-256 in TWDAccess\installed.txt. A file is replaced
  or deleted only if its current hash is one the
  installer wrote; a foreign file sitting at Archives\
  DebugLoader.lua is NOT overwritten — the install logs
  that it belongs to another mod and that TWD Access
  cannot start until it is removed (§26.14's backup
  etiquette taken one step stricter: refuse, don't
  displace). Stale files from older versions are removed
  by the same hash rule; settings.txt and twda.log are
  user files, kept across updates. Uninstall keeps the
  bootstrap and saves.txt when saves made under the mod
  may exist — detected by slot records in saves.txt or
  save bundles newer than the install manifest — and
  says plainly why ("saves made while the mod ran need
  them to load… To remove them too, run --uninstall
  --all; those saves may then fail to load"). The helper
  is one self-contained exe, Prism embedded and extracted
  at run time; the streaming setting relaunches it under
  conhost in its own titled window purely so OBS's
  Application Audio Capture can find its tones (under
  Windows Terminal its audio belonged to the terminal's
  process). → OGA: the hash manifest is the general
  install-integrity pattern; and any mod whose bootstrap
  becomes part of save files (§27.1) owes the user this
  uninstall honesty — partial removal by default, total
  removal only on an explicit flag with the consequence
  named.
- **27.16 Ship the lab, and test the game logic outside
  the game.** tools/dev holds ~130 probe/watch scripts
  (watch_hotspots, watch_timers, probe_occluded,
  probe_repath, radio_sweep…) — the instruments each
  feature was found with — plus game_input.ps1, which
  drives the real game with keystrokes for testing (the
  SSFV pattern, §26.15). A discovery setting logs the
  game's own UI call patterns (throttled: first 40, then
  every 200th) so features are built on observed calls.
  The standout: offline_lookaround.py runs the REAL
  lookaround.lua under lupa (a Lua 5.2 runtime) against
  stand-in engine functions — 22 checks covering the
  reach tags, blocked ordering and expiry, and the
  doorway offer — so navigation logic is unit-tested
  with no game running. build_mod.py syntax-checks every
  Lua file through the same runtime before packaging,
  because the game fails SILENTLY on a syntax error.
  HANDOFF.md, shipped in the repo, is the session log:
  every change carries its in-game test evidence (scene,
  save, the exact lines spoken) and a note that test
  saves were restored byte-identical (MD5-checked)
  afterwards. → OGA: for in-engine script mods, an
  offline stand-in harness is the difference between
  testing logic and testing luck — build it the week
  the first navigation rule lands, not after; and a
  syntax gate matters most exactly where the host
  swallows errors.
- **27.17 What is claimed but not verified here.** The
  mod was not run for this mining (reading only, per
  brief), so all play evidence is the author's: full
  playthroughs of every season "mostly choosing the
  first answer" — branch coverage beyond first answers
  is explicitly unmet territory in his own notes.
  Subtitle reading in the game's other languages: the
  code path is language-free (Subtitle_OnCreate), and a
  forum user confirmed Spanish full-lines works the same
  day; the forum's narrower question (subtitles vs menus
  specifically in Brazilian Portuguese) was not answered
  by the author in the topic. Controller support was
  asked in the topic and unanswered there: the code
  polls PC virtual keys and synthesises controller-
  button events for menus, but nothing in the code or
  topic verifies playing with a gamepad. Michonne's
  dedicated module is 62 lines on top of the Season 2
  framework modules — coverage there leans on the shared
  code. The demo video the post links (another topic)
  was not watched for this mining. Performance on
  low-end PCs, and behaviour under the Definitive
  Series' own updates since 1.3.0, are unexamined.

## Quick-pick addendum (section 27)

- Leftover DebugLoader dev hook as entry; bootstrap reconstructs the real first script: §27.1
- Hook substrate: definition-time + per-frame reapply, upvalue surgery, error budgets: §27.2
- Cross-process speech: per-state sequence numbers, 80 ms reorder window: §27.3
- Timed choices held by the dialog system's own pause / timer timescale 0: §27.4
- Highlight read from the game's own variable, never a key counter: §27.4
- Exploration list: name + actions + direction + distance + ordinal, actable-first sort: §27.5
- Reach tags (out of reach / blocked / another area) + failed-walk memory: §27.6
- The opened door becomes a walk target ("Open doorway"): §27.6
- Pathfind with the engine, supervise the walk, narrate the shortfall: §27.7
- Beacons: camera-relative, kind timbres, behind = muffle + pitch drop, nearest-4 cap: §27.8
- Danger classification by timer-name heuristics + measured fill rate: §27.9
- Shared time-scale arbitration with a sentinel slow value (0.45): §27.9
- Auto mode refuses decisions (turn choices) and free-walk kills: §27.9
- Prompt grammar: pattern in words once, live percent, finish key on the game's flag: §27.10
- Mouse-only mechanics driven through the controller/stick path: §27.11
- Cover position as phase commands at thresholds between two triggers: §27.11
- Aim by the shot's own hit test; sweep the axis you cannot project: §27.12
- Text-write bus (AgentSetProperty) + Select-method focus + describe cascade: §27.13
- Logic keyed on language-free signals (verb prop, icon names), never display text: §27.14
- Hash-manifest installer; foreign file refused; uninstall keeps save-critical bootstrap: §27.15
- Offline stand-in harness (real module under lupa, 22 checks) + syntax gate: §27.16

## 28. Haptic radar — Devin's design directive (2026-11-04)

Devin's own idea (dictated, shower thought, 2026-11-04), filed as a first-party design directive rather than a mined trick: **an optional haptic channel for the proximity radar** (the fighting-game standard radar, currently audio), using the iPhone Taptic Engine and Android's vibration/haptics equivalent.

### The mapping (as dictated, decoded)

- **Far away:** slow, spaced, distinct taps — a periodic pulse you can count.
- **Closing in:** the taps speed up and get firmer as distance shrinks.
- **Point blank:** the taps melt into a near-continuous, faint vibration — a *tactile hum*, the haptic version of the audio radar's hum, but deliberately **much softer/subtler** than the audio one. Haptics inform; they never startle. Cap intensity low.

Rate carries distance (Geiger-counter logic the audio radar already uses for pitch/tempo); intensity stays gentle throughout.

### Notes and open questions

- **Direction — DECIDED (Devin, 2026-11-04):** a phone's single actuator can't pan, so on the phone the haptics carry proximity and direction stays in audio. **But where a controller offers distinct left/right actuators, use them — the haptic channel then carries the full radar including direction** (taps/hum panned to the side the enemy is on). Controllers with genuinely separate L/R haptics: DualSense (two actuators), Joy-Cons (separate devices entirely), Xbox controllers (handle motors + impulse triggers). Rule: haptics mirror whatever the hardware can express; never fake a pan the device doesn't have.
- **Headset haptics — added to the output list (Devin, 2026-11-04):** his **Razer Kraken V3** (HyperSense) has haptic drivers in each earcup, left and right independently — a full directional radar channel you wear. HyperSense is audio-driven (it converts the audio feed into haptics per ear), so a panned audio radar plausibly produces panned ear haptics with no special haptics API — worth an early bench test before building anything custom. This is a PC-lane device (USB/dongle headset), which connects to the future-ports point below. His Kraken V3 is plugged into his Legion.
- **Kraken direct-drive research (2026-11-04, Devin's ask + his correction "not busy — more bass"):**
  - Retail Kraken V3: **audio-conversion only, no direct motor API.** The conversion DSP runs on the headset itself (Lofelt L5 drivers per earcup, stereo reproduction) and listens in the **20–200 Hz** band — which is exactly why bass content is the lever: the radar signal must carry real low-frequency energy or the motors barely respond. Synapse/the headset button only set global HyperSense intensity (off/low/medium/high). Razer markets it as "requires no integration" — there is no public path to push waveforms to a retail unit.
  - Programmable *directional* drive exists only in Razer's **Interhaptics SDK** (free, GDC 2023) paired with a **Kraken V3 HyperSense Dev Kit** (waiting-list hardware) — dev-kit firmware, not a retail unlock, on the public record; the SDK targets game-engine integration (Unity/Unreal). Worth re-checking if the PC port ever becomes real work; not actionable now.
  - Community routes don't expose the motors either: OpenRazer supports Kraken headsets for settings, and the Windows port of it explicitly leaves the Kraken family's separate protocol unported; no HyperSense motor control anywhere.
  - **Practical design consequence:** give the radar a dedicated **bass track** — a low pulse train (roughly 30–80 Hz) whose rate encodes distance and whose stereo pan encodes direction, mixed additively under the audible radar and routed to the headset. Felt strongly by the L5 drivers, barely audible over game audio, and the same track degrades gracefully (subwoofer/shaker users get it too). The audible radar stays for every other output device.
- **Future platform note (Devin, 2026-11-04):** PC, Mac and Linux ports of OpenGameAccess are a future direction — the Discord community keeps asking for them. Headset haptics (Kraken class) and richer controller haptics live in that lane; the mobile haptic radar above stands on its own meanwhile.
- **Contact requirement:** phone haptics are only felt when the phone is in hand, pocket, or on the lap. With a separate controller in hand, the phone channel is dead air — the natural extension is gamepad rumble as the same channel (iOS GameController haptics: DualSense, Joy-Con HD rumble; Xbox controllers), treated as one radar output with multiple devices.
- **Platform shape:** iOS Core Haptics supports exactly this — transient taps plus continuous events with dynamic intensity/sharpness curves. Android VibrationEffect waveforms with amplitude control exist but actuator quality varies wildly by OEM (Pixel/Samsung good, cheap motors buzzy) — needs per-device sanity caps and a fallback to simple on/off pulses.
- **Why it's worth having:** silent play (no audio needed), noisy environments, and a redundant channel for players who process touch faster than sound. It is an *option* alongside the audio radar, never a replacement.
- Status: idea filed 2026-11-04; not yet built into any adapter.
## 29. JVPA — Jeux Vidéo Pour Aveugles (djblind, 2026-11-04)

A French one-author platform (author handle djblind; the PokerStars add-on is
signed "AlexJVPA") that makes commercial games blind-playable under emulation:
Streets of Rage 1 & 2 (Genesis), Dragon: The Bruce Lee Story and Street
Fighter II Turbo (SNES), Fire Emblem: The Sacred Stones (GBA), a Pokémon
collection spanning Game Boy, GBA and Nintendo DS in one framework, plus a
native NVDA add-on for the PokerStars PC client. Distribution is a small .NET
launcher (JVPAGames.exe) that fetches a JSON catalogue, downloads per-game
packages, imports the user's own ROMs, and launches a JVPA-customized
RetroArch (earlier generations ran on VBA-rr, then BizHawk — the migration to
RetroArch is dated 31/10/2026 in the code comments, days before the packages
were uploaded on 2026-11-03; the code is under very active development, with
dated change comments running to 04/11/2026). Each game's mod code ships as
one obfuscated container (magic "JVR1"/"PKR1", XOR-stream + LZSS) that a
small plaintext loader verifies (ROM identified by CRC32 in a manifest) and
`load()`s; the plaintext loaders, the catalogue and the launcher's keep-list
are readable in the packages, and the containers were decoded with the
loader's own algorithm for this catalog. The mods were NOT run for this
catalog: everything below is code-proven unless marked as the author's own
test evidence quoted from comments. Pokémon here is a descendant of the
Pokémon Access lineage (§5) — one FE source file even notes "some of this
references the work done on pokemon-access" — extended far beyond it:
three platforms, ROM-hack support, NDS touch screens.

### 29.1 The speech bridge, three generations — and the choke-point protocol

**What:** How speech and placed audio get out of an emulator Lua script,
evolved over three host generations. (1) VBA-rr era: real Tolk via FFI plus a
custom audio DLL. (2) BizHawk era: a **file-command bridge** — the script
appends lines to `nvda_cmd.txt` / `audio_cmd.txt` at the emulator root and
external helper processes (BizHawkNvdaBridge, BizHawkAudioTool) consume them.
The protocol is tiny and line-based: `SPEAK|text`, `SPEAKNOW|text`,
`CANCELSPEECH`; `PLAYRELID|id|relative-path|VOL=%.2f|PAN=%.2f|PITCH=%.3f`
with optional `|LOOP=1`, `STOPID|id`, `STOPALL`, and `UPDATE|id|VOL=%.2f`
for continuous tones whose volume tracks a value (radar). The writer retries
`io.open` up to 3 times because the consumer renames the file to read it, and
replaces decimal commas with dots before writing (French locale). Every
speech call in every game funnels through one function (`pa_nvda`), which
also stamps the frame of the last utterance. (3) RetroArch era: the bridge is
**internalized into a fork of the emulator** as a provided script
(`jvpa_sortie.lua`, not shipped in the game packages) exposing a global
`sortie`: `speak`, `speak_now`, `cancel`, `braille` — braille is a
first-class output on the same bridge (Bruce Lee's `nvda.lua` maps
`nvda_braille` straight to it) — plus `play(id, path, {vol, pan, pitch})`,
`update`, `stop`, `stop_all`, `pause`, `resume`, and path helpers. Old APIs
survive as shims: FE's `tolk.lua` is ten lines mapping Tolk's
`output/silence` onto the bridge.
**Evidence:** `pokemon/common_pokemon_bizhawk_compat.lua` (the file protocol,
~lines 400–500), `common_pokemon_retroarch_compat.lua` (the same calls mapped
to `sortie.*`), `fire_emblem/tolk.lua`, `brucelee/nvda.lua`, SoR2 `audio.lua`
(tracked sound IDs in a 256-entry ring so a stop-all can silence effects
without touching the music relay; a "loud" variant allows volume up to 2.0
for sounds that must exceed the cap).
**Use in OGA:** design the adapter↔output boundary as a small versioned
protocol with tracked sound IDs and update-in-place, one choke point per
adapter, speech and braille as equal citizens. If OGA ever gains a host it
can patch, the JVPA arc says the endgame is to internalize the bridge —
but the file protocol carried them for years first.

### 29.2 One container per game, identified by ROM CRC

**What:** Each game ships exactly three plaintext Lua files (loader, main,
manifest) plus one encrypted `.dat` holding all mod code and data. The
manifest pins the expected ROM by CRC32 (SF2: `D43BC5A3`, Street Fighter II
Turbo (USA) (Rev 1), core snes9x); Pokémon instead ships per-platform
manifests (gb→sameboy, gba→mgba, nds→melondsds) that run the same two
scripts, and `pokemon.lua` self-identifies which game is loaded. The loader
decompresses, de-obfuscates and `load()`s the payload.
**Evidence:** every package's `dat/games/<id>/{loader,main,manifest}.lua`;
Pokémon's `dat/games/{gb,gba,nds}/manifest.lua`.
**Use in OGA:** the transferable part is discipline, not the obfuscation
(which is packaging, not security — it also makes the code audit-hostile for
the community, a real cost): pin adapters to ROM CRCs, keep the bootstrap
tiny and readable, and let one script serve a whole platform family by
self-identifying the game.

### 29.3 Signature-based address relocation for ROM hacks

**What:** The Pokémon scripts use fixed addresses per game, but recompiled
hacks (pokecrystal, pokefirered, pokeemerald expansions) shift them. Each
game ships `signatures.lua`: for every address, masked byte patterns
(`??` wildcards) of base-game code that *uses* the address. On an unofficial
ROM the mod searches the ROM for the patterns, reads the true address out of
the match site, and falls back to the base value or to a neighbour's observed
shift when a pattern is missing. Results are cached per ROM
(`adresses_detectees.lua`, in the launcher's keep-list so updates don't
erase them). Better: the launcher runs the search **offline at import
time** — a standalone Lua process in the background at low priority right
after the user imports a hack, caching `analyses/<romkey>_v<version>.lua`
so starting the game is instant (GBA only; the GB search takes under a
second and stays inline). The launcher tells the script a ROM is unofficial
via environment variables (`JVPA_ROM_OFFICIAL`, `JVPA_ROM_PATH`).
**Evidence:** `pokemon/common_pokemon_autodetect.lua` (VERSION = 8),
`common_pokemon_analyse.lua`, per-game `*_signatures.lua` modules,
`common_pokemon_engine_hooks.lua` (exec hooks placed at the relocated
function addresses; when a mid-function hook point can't be found in the
hack, hook the function *entry* instead — arguments are always in r0–r3).
**Use in OGA:** this is the missing piece for hack support in memory-reading
adapters. Steal the whole pattern: signatures from the base game's code,
neighbour-shift fallback, per-ROM cache, offline pre-analysis at import.

### 29.4 Build adapters on community decompilations

**What:** Addresses and struct layouts come from the decomp projects, and
comments cite them by file: Pokémon Platinum is built entirely on
pret/pokeplatinum (symbols per language version; struct offsets annotated
`src/text.c`, `src/menu.c`, `FieldSystem->processManager`); GBA hacks use
the pokeemerald/pokefirered expansion engines; SF2's sound and fighter data
come from a `bank_C0.asm` disassembly; FE8's places/objectives from
`eventinfo.c`/`eventscr.c` in the FE8 sources. State is read at the game's
own seams: execution hooks on named functions (`Text_AddPrinter` — *all*
text the game displays; typewriter text spoken page by page, instant text
grouped by window), cursor hooks (`DrawCursor`, `InvokeCursorCallback`),
per-frame application-pointer reads to announce screen changes.
**Evidence:** `pokemon/games_nds_game_platinum_main.lua` header and
SCREEN_MESSAGES table; FE `gbafe_*` modules read the engine's proc structs
directly (the battle-forecast proc's fields at +0x50/+0x51 hit counts,
+0x52/+0x53 effectiveness).
**Use in OGA:** already OGA doctrine (guides + Ghidra first) — JVPA adds:
prefer hooking the game's text *printer* over OCRing or scraping buffers,
because one hook sees dialogue, menus and windows with their grouping
intact; and record the decomp file each offset came from, in the comment,
so the next maintainer can re-derive it.

### 29.5 Speech discipline under input: queue, don't interrupt

**What:** NVDA interrupts speech on every keypress, and a held arrow
auto-repeats — so in Pokémon, non-interrupting messages (dialogue, signs)
are **queued while any direction is held and flushed 3 frames after
release**. Replies to the player's own commands (scanner, path) bypass the
queue and speak immediately. Two text fixes ride along: a dialogue body of
pure punctuation (`...`) is spelled out ("point point point") after the
speaker's name because screen readers don't voice punctuation alone; and an
abbreviation pass expands levels and similar before speech.
**Evidence:** `pokemon/common_pokemon_tolk.lua` (dated 29/10/2026 comments
giving exactly this reasoning), used by all games through the shared choke
point (§29.1).
**Use in OGA:** the general rule — separate *narration* (queueable, never
interrupts) from *answers* (immediate) — plus the queue-while-moving flush.
Directly applicable to any adapter where the player holds directions
through dialogue-heavy games.

### 29.6 A radar tuned from Manamon's measured constants

**What:** The Pokémon radar (shared across GB/GBA/NDS) is modelled on
Manamon and Zelda 3, with Manamon 2's BGT audio renderer
(`position_sound_custom_2d`) **studied from its sources** and re-implemented:
per entity family, pan and volume steps in dB per tile (person 1.995/3.5,
door 3.687/3.5, stairs 3.347/3.5, item 4.5/4.5, sign 3.0/3.0, gate
3.687/3.5), beacons repeating every 35 frames (38 if the target is south),
inaudible past ~12 tiles; a sound due south plays at pitch ×0.85 (never
sharper to the north). Walls are four directional loops whose volume falls
5 dB per tile out to 5 tiles, with per-direction contact volumes (the low
sounds are mixed louder because they carry less). On top of the model:
a **door-axis cue** — while walking straight toward a door or stairs, each
step plays a sound naming its direction; and **bump classification** — a
failed move plays one sound for something interactable, one for a building's
outer wall (sided), one for a plain wall. Warps are classified
(cave/gate/ordinary door) and get matching sounds. Toggle: O.
**Evidence:** `pokemon/common_pokemon_radar.lua` header and constants
(tuning still being adjusted 04/11/2026: wall falloff 8→5 dB/tile), the
shipped foley under `dat/common/pokemon/sounds/radar/` (door_axis_east/
north/south/west, building_wall, bump_interact, hit).
**Use in OGA:** when a reference implementation exists in the blind-gaming
canon, adopt its measured constants instead of inventing linear curves;
per-kind dB steps and the south-pitch rule are drop-in numbers for any
tile-world radar. The bump classification and door-axis cue are cheap,
high-value additions to OGA's radar family (cf. §26, §27.5).

### 29.7 Scanner, free camera, and spoken A* pathfinding

**What:** Pokémon's exploration stack, one key set across platforms:
**J/K/L** cycle a scanner over nearby entities (previous / re-read / next)
with category filters (Shift+J/L) and **user renames** for items and maps
that persist across package updates (the launcher keep-list preserves
`names_*.lua` and `hacks.lua`). **P** computes an A* path over the game's
own collision data to the selected item and speaks it as directions;
Shift+P cycles the movement-capability assumption (HMs: none / available /
all); "no path" is spoken explicitly. A **free camera** (D/G/R/V, F to
re-centre, C to toggle follow; Shift+movement passes through walls) lets
the player scout the map by ear, and Shift+F pathfinds to wherever the
camera is parked. Supporting queries: coordinates (Y), map name (M), tile
codes around the player (E), on-screen text (T), enemy/own HP in battle
(H / Shift+H). Footsteps are surface-dependent foley (ash, carpet,
concrete, dirt, foliage, glass, grass, gravel, deep water…) read from the
map's tile data.
**Evidence:** `dat/common/pokemon/touches_scripts.cfg` (the shipped key
inventory, bilingual), `common_pokemon_a-star.lua`, `common_pokemon_
footsteps.lua` + per-platform layout modules, the 563-file ogg foley
library under `dat/common/pokemon/sounds/` (doors by type — stone door,
ancient gate, dungeon door — footsteps by surface, radar, sonar).
**Use in OGA:** the scanner+rename+persist trio and the ghost camera are
the transferable designs; A* over the game's collision map with an explicit
capability mode (what movement powers do we assume?) is the right shape
for any spoken-pathfinding feature. Cf. §27.6 (map-independent coordinate
narration) — JVPA shows the complementary, map-aware end of the spectrum.

### 29.8 Touch-only screens made playable by synthesizing the stylus

**What:** Pokémon Platinum's stylus-only screens (Poffin cooking, seal
capsules, trainer-card signature, contest dress-up, battle-frontier
scratch cards) are played from the keyboard by **simulating the stylus**:
the script writes a stylus position every frame. Poffin cooking is the
worked example — arrows rotate the virtual stylus around the pot's centre,
up/down set speed, B lifts it, Y auto-stirs; the script reads the cooking
state from the decomp-derived offsets and speaks what matters (direction
requested, dough speed as too slow / good / too fast, burning, overflow,
stage changes, results), with the burn/overflow thresholds taken from the
game's own code (dough speed ≤910 burns within 3 s; ≥3640 overflows).
**Evidence:** `pokemon/games_nds_game_platinum_touch_screens.lua` (overlay
83 offsets and thresholds annotated from pret/pokeplatinum `overlay083`).
**Use in OGA:** the continuous-input cousin of driving the game's own input
path (§27.11): when a game demands an analogue gesture, synthesize the
gesture parametrically (centre, radius, angular speed) and narrate the
*state variables the gesture controls*, with thresholds read from the
game's code rather than guessed.

### 29.9 Intercept the sound queue; attribute what the queue won't say

**What:** SF2 Turbo replaces the game's SFX with placed MP3 copies by
**policing the sound-request queue in RAM**: the queue (16 two-byte
entries, write/read pointers) is read every frame; a known sound's entry is
zeroed before the game ships it to the sound chip (at most one entry per
frame during vblank), and the copy plays panned instead. Unknown entries —
music, commands ≥$F0 — pass through untouched. The queue doesn't say *who*
made a sound (a routing byte is always 0), so attribution is inferred from
state: each whiff goes to whichever fighter most recently entered the
attacking state (record +$03 = $0A, player 1 wins ties), hit and blocked
sounds are placed on the *victim* — your hits sound at the opponent, his
hits on you at centre, so the two are never confused. Voices follow the
same rules with per-fighter sound IDs and live pan updates (a cry that
crosses the screen, like Blanka's roll, pans across); a character-specific
cry belongs to whoever is playing that character, a shared cry to whoever
just attacked, and "K.O." to the fighter whose HP byte hit the $FF
sentinel. The sound map itself was built by disassembly plus **differential
testing**: play the same fight twice, with and without sound, and diff —
that is how an undocumented sound (Bison's Psycho Crusher plane) was found.
**Evidence:** `sf2turbo/sound_replace.lua`, `fighter_pan.lua`, `voice_pan.lua`
(headers cite test fights: 12, one per character; 143 and 214 sounds placed
with no errors). Related: the GB Pokémon engine **silences calls, not
sounds** — an execution hook on a sound-call site rewrites the `CALL`
bytes to NOPs while the radar supplies its own warp sound, restoring them
when not needed (`games_gb_gb.lua`, `gb_mute_call`).
**Use in OGA:** queue interception is the cleanest possible sound
replacement — nothing is muted that isn't replaced, and music survives.
When an event stream lacks a source field, attribute by nearest preceding
state transition, and place consequences on the victim. Cf. §26 (SSFV reads
a native PC game's state; JVPA gets the same class of result out of a 1993
ROM through an emulator).

### 29.10 Sounds for events the game never sonifies, tracked live

**What:** SF2's jump and projectile sounds are synthesized by the mod
because the originals don't exist. Jumps: takeoff detected as height
leaving ground value with state = jumping (throws and hit-states excluded),
a sound that follows the jumper in real time and stops at landing.
Projectiles: a looping wind sound per projectile, panned by relative
position, volume rising as the opponent's shot approaches, pitch encoding
Tiger Shot high vs low, short fade-in/out, looped if the shot outlives the
sample. Opponent footsteps use a **distance accumulator**: a step every
20 px travelled on the ground, none while airborne, at most one per
8 frames, a teleport guard (>16 px/frame = round reset, not a step), and a
pan floor of 0.3 so the side is audible even point-blank.
**Evidence:** `sf2turbo/jump_sound.lua` (248 jumps across 4 test fights),
`projectile_sound.lua`, `opponent_steps.lua`.
**Use in OGA:** pairs with §27.2/§27.3 (occurrence counters, idempotent
cues) — JVPA's versions add continuous tracking with per-source IDs and
`update` calls, and the pan floor is a one-line rule worth adopting
everywhere: never let a positional cue collapse to centre.

### 29.11 Brawler radar: one threat per side, alignment on the belt plane

**What:** Streets of Rage 1 & 2 scan the actor slots each frame and keep
the model small: for footsteps, at most **two enemies — the nearest on the
left and the nearest on the right** — each with its own tracked sound,
timer and pan/volume/pitch updates; several enemies on one side produce one
sound. A **lock-on target** drives the brawler-specific cue: crossing the
target's Y line plays an alignment sound per side, because on the belt
plane being level with an enemy is what makes attacks connect. An
**attack-range alert** fires when the player could hit the target *now*,
computed from the game's real range values read out of the ROM for the
weapon in hand (fists, jump kick, knife and bat each have their own).
Enemies are excluded the moment their HP state is lethal, not when the
sprite disappears; pre-loaded/dormant actors and containers handled by
other radars are filtered out. Around this core sits a family of small
single-purpose modules: object radars (shared engine; per-type sounds,
alignment signals on both axes, and silence inside the action zone except
the "can take it" signal), wall/pit/breakable/compressor radars, and
per-threat danger modules (Abadede's charge, a grenade on the ground in
SoR2) with their own sounds, pan = clamp(dx/160, −1, 1), re-triggered at
most every 15 frames and stopped when the threat clears. Bruce Lee uses
the same grammar smaller: per-enemy beacon sounds at a relaxed ~1 s pace
(the two enemies offset by half a period so they don't mask each other)
and a low-health heartbeat whose threshold is copied from the game's own
code (the ROM compares HP against max/8 for its flashing — the mod reuses
exactly that as its warning threshold, and the comment is careful to say
what it does *not* mean).
**Evidence:** `sor1/radar_enemy.lua`, `sor1/abadede_charge_danger.lua`,
`sor2/object_radar.lua`, `sor2/explosion_danger.lua`,
`brucelee/enemy_beacon.lua`, `brucelee/enemy_heart.lua`.
**Use in OGA:** for beat-'em-ups, cap simultaneous positional voices per
side, make Y-alignment a first-class cue, and compute "in range" from the
game's data rather than a fixed distance. When the game already computes a
threshold for a visual warning, reuse its value for the audio one.

### 29.12 Replace the music honestly: patch the ROM, follow the game's track id

**What:** Bruce Lee (and SoR, via `external_music`) replaces the
soundtrack with recorded renditions without shipping a ROM: the package
carries a 741-byte **IPS patch** that neutralizes the game's music driver
(on writable buses the same bytes are patched at runtime; the patch data
even overwrites the ROM's assembly-statistics block with a small code
trampoline). The script then watches the game's own native track-id
register and plays the matching recording through the bridge, with
pause/resume/mute and resume positions following the game's state frame by
frame. Synchronization is always from real game state, never timers — the
cinematic narrator works the same way, tracking the movie-script pointer
and bank and fingerprinting the text buffer to tell apart screens that
share the same video registers (victory vs licence screens).
**Evidence:** `brucelee/external_music.lua`, `brucelee/music_patch.lua`,
`brucelee/narration.lua`, the shipped `rom/Dragon - The Bruce Lee Story
(USA).ips`; SoR1's 32 recorded music tracks and SoR2's 41, plus SoR2's
`voices_rom`/`sfx_rom` folders — recordings *of the ROM's own* voices and
effects, replayed placed.
**Use in OGA:** the legal and technical shape for soundtrack replacement:
ship a patch, not a ROM; key playback to the game's track id; and treat
"sync from state, never from timers" as a rule for all narration.

### 29.13 A host-compat shim as a first-class module

**What:** Fire Emblem's `retroarch_compat.lua` re-implements the old host's
(VBA-rr) API over the new one so the game script didn't change: memory
callbacks are re-built on the new host's events with one-callback-per-
address replacement semantics; **write-only GBA I/O registers are
shadowed** (every write recorded, reads served from the shadow) because
the scripts depend on VBA returning the last written value; out-of-range
bus reads return 0 instead of raising, because the new host kills a script
where the old one only warned; input key names are translated, including
an **AZERTY positional remap** (the host names keys by QWERTY position, so
Semicolon→M, M→Comma). A developer journal (dev builds only, stripped from
the release container) logs per frame: intercepted game-function calls
labelled by their constant names with counts, pad changes, key presses,
and every spoken line.
**Evidence:** `fire_emblem/retroarch_compat.lua` (363 lines); the same
shim pattern recurs in Pokémon's compat modules, and SoR2 wraps `input.get`
to alias RetroArch key names to BizHawk ones.
**Use in OGA:** when the host changes, shim the host — don't fork the
adapter. And log every spoken line in dev builds: it's the cheapest
speech-regression test there is (cf. §27.17's "no test harness" gap — a
speech journal is most of one).

### 29.14 Tactics speech: layered cursor cell, target cycling, cursor travel

**What:** FE8's battle map is spoken as **composable layers over the
cursor cell**, each with its own toggle (Shift+M menus/screens, Shift+U
unit, Shift+H HP, Shift+J class, Shift+T terrain, coordinates) — coordinates
rendered spreadsheet-style ("C5"). Everything is read from the game's own
structures: map/unit/terrain via the pointer-chased tile maps, the battle
forecast composed in words from the forecast proc's fields, chapter
objectives and "places" (villages, chests, doors, objectives) from the
chapter's event list, the tutorial's flashing square tracked through the
cursor proc. **Target cycling** (added 29/10/2026 at an AudioGames forum
request for "pathfinding"): E/Shift+E cycle enemies nearest-first, F
allies, D places; **P sends the game cursor to the target by holding
joypad directions** — the game moves its own camera and handles map edges;
Shift+P, with a unit selected, solves for a tile the unit can reach this
turn (from the game's own movement-range map) from which it can strike the
chosen enemy with its weapons' ranges, or the nearest reachable tile if
none exists.
**Evidence:** `fire_emblem/fire_emblem_gba.lua` (help text, `target_list`,
`start_cursor_travel`/`process_cursor_travel`, `go_to_attack_position`,
`range_text`), `gbafe_board.lua`, `gbafe_battle_forecast.lua`.
**Use in OGA:** the layered-toggle model is the right answer to "how much
should a cursor cell say" — let the player compose it. Driving the cursor
with held directions instead of warping it keeps the game's camera,
scrolling sounds and edge behaviour truthful. The attack-position solver
is the tactics counterpart of §29.7's pathfinding: don't just find the
target, find where to stand.

### 29.15 Platform furniture every game inherits

**What:** A shared quick-state script (all Pokémon platforms, and the
pattern recurs per game) binds Shift+F1–F10 to save slots 1–10 and F1–F10
to load: a save is announced ("Savestate N"), a **successful load is
silent** and only an empty slot speaks ("Slot N empty") — behaviour
changed 02/11/2026 at user request: short messages, nothing on resume. The
script also launches shared companions by `script.start`: speech volume
(Shift+Up/Down, per-game `volume.ini`) and emulator volume
(Ctrl+Shift+Up/Down, per game). Per-game settings, user renames and the
autodetect cache all live in files the launcher's catalogue keep-list
preserves across package updates. Pokémon adds per-game music-volume
defaults (`volume_musique.txt`: black/white 25, red/blue 70…) so speech
and cues sit on top of each game's mix. SF2 and SoR plan for **netplay**:
RAM-writing modules are written to behave identically on both peers, the
joining player's perspective is a launcher-set variable (`SF2_PLAYER=2`),
and a sighted-partner mode (`SF2_MODE=voyant`) disables speech-only
modules while keeping the replaced, placed sounds.
**Evidence:** `pokemon/etats_rapides.lua` and the BizHawk original it was
modelled on, `volume_parole.lua`, `volume_emulateur.lua`,
`catalogue.json` (`conserver`), `streetfighter2.lua` header.
**Use in OGA:** save/load speech policy (announce saves and failures,
stay silent on success) and per-game volume persistence are small,
copyable defaults; the netplay-symmetry constraint — an accessibility mod
that writes RAM must write it identically on both peers — is worth writing
into OGA's design rules now, before it bites.

### 29.16 Reading a native PC client from inside the screen reader

**What:** The PokerStars package is a different species: an NVDA add-on
(23,297 lines of Python, v0.3.4, targeting NVDA 2026.2) whose app module
reads the PokerStars.exe process memory directly — seat, card and stack
data from classes (e.g. `CocosPlayerInfoImpl`) found by Ghidra
decompilation of the client in a predecessor project — combined with UIA
live objects for the action buttons and a dealer-message watcher that
feeds automatic announcements. Two latency disciplines: the memory
*locations* of the nine seats and the action panel are **pre-warmed and
cached** (in a JSON file, refreshed when a PokerStars window takes focus)
so the F1–F9 seat queries never rescan the process at keypress time — but
contents are always re-read live; only "where to look" is cached, never
"what it says". Keys are intercepted by the add-on and released to the
application only outside the expected contexts. It was ported (Sept 2026)
from a subprocess-plus-file-exchange design to running entirely inside
NVDA's process — the opposite migration from the games' bridges, because
here in-process was available.
**Evidence:** `/tmp`-extracted add-on: `appModules/pokerstars.py` header,
`core/seat_memory_reader.py`, `core/prewarm.py`,
`core/action_buttons_liveobj.py`, `core/dealer_watcher.py`, `manifest.ini`.
**Use in OGA:** for PC-native targets, prefer in-process reading over any
bridge; cache locations, never contents; and pre-warm on focus so the
first query is as fast as the fiftieth.

### 29.17 Caveats

Code quality is unusually high for one-author mods — dense dated comments
that give reasons and cite user requests, `pcall`-wrapped modules,
reset-on-state-load discipline — but: the shipped code is obfuscated, so
the community gets binaries it cannot easily audit or fork, and the
per-game launchers are closed executables. The native side of the current
speech bridge (`jvpa_sortie.lua` and the RetroArch fork) is not in these
packages, so exactly how the fork reaches NVDA cannot be verified from
this source. Pokémon's NDS support is explicitly a work in progress in the
catalogue ("adaptation en cours"). FE's `gbafe_*` library is pre-existing
CC0 community work that JVPA integrated and extended — credit belongs
there too. The Pokémon framework descends from Pokémon Access (§5). And
the standing caveat for this whole section: the mods were not run for this
catalog; play evidence is the community's and the author's own bench notes
quoted in comments.

Quick-pick addendum (2026-11-04, JVPA): if you only take five — the
choke-point speech/audio protocol with tracked IDs (§29.1), signature-based
address relocation with offline pre-analysis (§29.3), Manamon's measured
dB radar constants (§29.6), sound-queue interception with state-based
attribution (§29.9), and layered cursor speech with joypad-driven cursor
travel (§29.14).

## §30 — Radar occlusion + nearest-cover callout (Devin, 2026-11-05)

Devin's directive, from thinking about Dissidia's stage pillars:

- **Occlusion muffles the beacon.** When the tracked enemy is behind a
  wall, pillar, or other solid level geometry — no line of sight to the
  player — the proximity-radar beacon is MUFFLED: low-pass filtered at
  the same loudness, never merely quieter. A muffled beacon reads as
  "something solid is between us"; a clear beacon means a straight,
  open line. Distance and direction reading must stay honest under the
  filter — muffle is a tone change, not a gain change.
- **Where-am-I names the nearest cover.** The location query also calls
  out the nearest object the player can get behind, so cover can be
  found and used deliberately, not stumbled into.
- **Both work while paused.** The muffled state and the cover callout
  must be available with the game paused, so the player can plan a
  break of line of sight while nothing can hit them.
- **Scope:** 3D action games generally — Dissidia first (stage
  pillars/walls), Birth by Sleep wherever its arenas have occluders,
  and stealth games above all, where breaking line of sight is the
  core verb.
- **Implementation note:** this needs a player↔enemy line-of-sight
  test. Prefer the game's own visibility/LOS data where the engine
  keeps it (stealth games usually track exactly this); otherwise an
  adapter-side raycast against collision geometry. The same LOS test
  feeds both halves — the muffle and the cover search (nearest cover =
  nearest occluder that would break the line).

---

## §31 JVPA source-level pass (2026-11-05)

The 2026-11-04 mining (§29) worked from the packages; the encrypted
`.dat` containers have since been decoded, and this section is a close
reading of the actual Lua source (`~/workspace/jvpa/decoded/` — 583
files; Pokémon 198k lines/397 modules, FE 27.8k/38, SoR1 15.5k/63,
SoR2 6.1k/42, SF2 Turbo 2.6k/16, Bruce Lee 1.8k/21). It extends §29;
it does not repeat it. Street Fighter II Turbo was not in the October
assessment at all and gets its first treatment here (§31.1–31.7).
Full per-title detail: `jvpa-source-mining-2026-11-05.md`.

**Standing caveat (same as §29.17):** the mods were NOT run. This is
source reading. Accuracy figures are the author's bench notes kept in
code comments; play evidence remains the community's/author's.

### 31.1 Screen detection by stack return address (SF2 Turbo menus)

- **What:** on a core whose Lua cannot hook game code, identify the
  current screen by **the return address the game left on its own
  stack** (SF2: word at $1E3C — $13BE title menu, $1404 mode select,
  $1B4B options, $18A4 in-game; a dispatcher return $120A means the
  real address is one stack slot lower; the copyright screen is
  recognised by its caller at $1E3E).
- **Debounce discipline:** a screen must be stable **8 frames** before
  it is announced; the character-select announcement waits **60
  frames** (fade-in) or the first cursor move; attract screens are
  announced once each, latched, until a menu/game resets the latch.
  Player 2's options sub-cursor is suppressed for the first 60 frames
  because the game itself resets it through 1 on entry — transient
  states the *game* produces are not the player's input.
- **Deliberate omission:** P2's between-rounds character pick is NOT
  announced when chosen; it is held until the country-name sound plays
  on the map screen (cross-module sync via a frame stamp set by the
  sound module) or the fight starts. User feedback: announcing early
  spoiled the reveal rhythm.
- **Use in OGA:** any hookless core (or a core where hooks are
  expensive) can get reliable menu state from the game's own call
  stack. Pair with §29.9's layered cursor speech; add the stability
  debounce to every screen detector we write — announcing transient
  states is the menu equivalent of speaking mid-word.

### 31.2 Attribution cascades with measured accuracy (SF2 fighter audio)

- **What:** before a generic fight sound can be placed, it must be
  *attributed* — which fighter, which cause — from the two fighter
  records (state +$03: $0A attack, $0C special, $08 guard, $0E hit,
  $14 down, $04 jump; X +$07; height +$0A, ground $C0; health +$30;
  character +$D1; facing +$F3 bit 6).
- **Whiffs:** whoever most recently entered attack state; a fresh
  entrant (≤2 frames) wins; ties → P1. Your whiff pans toward your
  facing at 0.4.
- **Impacts/blocks — victim cascade:** (1) whose health byte dropped
  *this frame* (authoritative; 214-sound test), else (2) who was in
  guard state within 3 frames (269 blocked hits / 12 fights, all
  placed), else (3) the non-attacker.
- **Landings:** whoever's height crossed back to ground within 5
  frames (302/350 attributed, rest centred; a thrown-state fallback
  recovered most of the remainder).
- **Placement semantics (user-tuned):** your hit ON the opponent sits
  at the opponent with a **0.55 pan floor** on his side (raised from
  0.3 — too central point-blank); his hit ON you plays at **0.8 × his
  facing** — from the side the blow comes; *your blocked attack* plays
  at **−0.55 × your facing** (your side, opposite him — the block
  happened to your limb). Breakables go on the fighter thrown into
  them, else the striker; with no opponent (bonus stages), in front
  of you at 0.5 by the facing bit.
- **Volume ladder** (bridge allows 4.0; >1.0 deliberate): whoosh 1.4,
  impact 1.4, **blocked 1.8 (loudest — must never be missed)**,
  landing 1.6, fall 1.2, voices 1.4; base replacement volume 0.9.
- **Use in OGA:** this cascade is game-independent — any fighter with
  state/health/height bytes can run it. It upgrades §29.10's SF2
  sketch into the full recipe for MK/Unchained-class adapters, and
  the placement semantics (hit-on-you from the incoming side;
  your-blocked on your side) are worth adopting verbatim: they encode
  *whose body* the event happened to, not just where the sprites met.

### 31.3 Pan law, step cadence, teleport guard (SF2 opponent tracking)

- **Pan law:** pan = sign(dx) × (0.3 + 0.7 × min(|dx|/200, 1)) — 0.3
  floor, full pan at 200 px (shared by steps, voices, projectiles).
- **Steps:** one per 20 px of ground travel (cadence coupled to his
  speed), ≥8 frames apart, none airborne; **teleport guard** — more
  than 16 px/frame is a round reset, not a step; accumulator resets
  on landing. Volume 1.6 near → 0.9 far over 200 px (raised from
  1.0/0.35: the earlier falloff made distant opponents untrackable).
- **Use in OGA:** cf. Devin's proximity-radar standard — SF2's answer
  is event-based rather than a continuous beacon, but the floor,
  the speed-coupled cadence and the teleport guard all transfer to
  our radar implementations (§28/§30 work) and to any step tracker.

### 31.4 Tracked voices; your own fighter panned by screen position

- **What:** every voice clip's duration is measured in frames
  (ffmpeg) so the mod knows how long a cry lives; voices play under
  per-fighter sound IDs — a new cry cuts *that fighter's* previous
  one only. Pan updates run every 2 frames and only when |Δpan| ≥
  **0.02** (bridge update throttling).
- **Attribution:** character-unique cry → the fighter whose character
  byte matches; mirror match/shared cry → freshest entrant to an
  attack/special state; "K.O." → whose health hit the $FF sentinel.
- **Screen pan for the local player:** your own voice is panned by
  *screen* position (camera X at $12E7; fighters measured 21–235 px
  across 216 captures; centre 128, half-range 108) — your headbutt
  sweeps the stereo field as a sighted player sees it sweep. The
  opponent keeps relative pan. (User-requested experiment.)
- **Use in OGA:** throttled updates (fixed interval + minimum delta)
  are the general rule for any tracked loop — voices, projectiles,
  radar beacons — so the bridge never floods. Screen-pan for the
  player's own actions is a parity-plus flourish in the spirit of
  Devin's "Beyond Parity" writing: it gives the blind player the
  *spectacle* information, not just the tactical information.

### 31.5 Projectile spawn glide and click-free fades

- **What:** projectile slots $0900/$0950, type at +$0D (00 Hadouken,
  01 Yoga Fire, 03 Sonic Boom, 04 Kikoken, 05 Tiger high, 06 Tiger
  low; Yoga Flame and Vega's claw excluded — not projectiles). The
  synthesized wind loop **starts at the shooter's voice pan and
  glides to the projectile's true position over 10 frames** — the
  shot is heard leaving the hand. Pitch by type (Tiger high 1.15,
  low 0.87). **Fade in 3 frames, out 2** — the record empties the
  same frame the shot dies, and unfaded loops click (82 shots
  verified). Opponent's shot 1.3 near → 0.55 far; yours flat 0.55.
- **Jumps:** takeoff = height leaves ground *while the state byte
  says jump* — throws/hit-states excluded by construction (248 jumps
  / 4 fights); sound capped at the clip's real 81 frames.
- **Use in OGA:** the spawn glide is the general fix for "object
  appears already mid-flight" in any tracked-object cue (Dissidia
  projectiles, BBS shotlocks). Always fade tracked loops in/out over
  2–3 frames; record-death and sound-death rarely share a frame
  otherwise.

### 31.6 Netplay rollback determinism (SF2 queue, generalises §29.15)

- **What:** the sound-queue dedupe originally zeroed queue entries
  from the per-frame Lua loop. RetroArch netplay does not redo
  frame-loop memory writes inside rollback frames → the erasure
  happened on one peer only → desync. **Fix (03/11/2026):** the write
  moved into **execution hooks on the game's own two queue-add
  routines** (after the pointer write, at $C0:0B55/$C0:0B7D), so it
  runs inside the game's deterministic execution and rolls back
  with it.
- **Companion rules from the same mod:** full module reset on
  savestate load *and* when the frame counter runs backwards; each
  module's update under its own `pcall` with per-module error
  logging (one broken module never kills the rest); local-player
  indirection (a launcher flag flips which fighter record is "you"
  and every positional decision follows).
- **Use in OGA:** the hard form of §29.15 — any adapter write that
  netplay state depends on must happen inside game execution, never
  in the frame loop. The pcall isolation and frame-backwards reset
  are cheap insurance for every multi-module adapter.

### 31.7 Duplicate suppression and documented failed experiments

- **What:** the game often requests the same sound twice within a
  frame; SF2 collapses only repeats inside a **1-frame** window. The
  comment records why: an earlier approach (cutting the previous copy
  of a repeated sound) **killed the score-count beeps**. The dedupe
  window is what survived testing.
- **Differential testing:** the author's method for auditing a sound
  map — play the same fight twice on the analysis core, with and
  without replacement, and diff the request streams (this caught
  Bison's Psycho Crusher reusing the map-plane sample).
- **Use in OGA:** two habits worth adopting as house style — dedupe
  windows tuned per stream with the failure mode written down, and
  differential capture as the verification method for any
  interception layer (it is the audio analogue of frame-by-frame
  screenshot verification).

### 31.8 Speech-queue internals (Pokémon tolk module)

- **Queue rule, exact form (§29.2 deepened):** FIFO pending list; a
  non-interrupt message queues if a direction is held **or the queue
  is already non-empty** — ordering survives the release, so a burst
  that starts queued finishes in order. Flush in an `onframeend`
  handler once a released-counter reaches **3 consecutive
  direction-free frames**; all pending then speak in order as
  separate utterances. Command answers bypass via a `direct` flag set
  by the command handler. `silence()` clears the queue and cancels
  speech.
- **Normalisation pipeline (order matters):** abbreviation expansion
  first, *then* punctuation-only detection — split at the first ": "
  for the speaker; if the body is only dots/ellipses/punctuation/
  spaces, speak it as "point point point" after the speaker's name.
- **Use in OGA:** the "or queue non-empty" clause is the difference
  between a queue that preserves bursts and one that interleaves
  them; adopt verbatim in the OGA speech bridge (§29.1). Keep the
  normalisation order — expanding abbreviations first changes what
  counts as punctuation-only.

### 31.9 A* for spoken directions (Pokémon pathfinding)

- **Heuristic:** Manhattan, not Euclidean (Euclidean "favored
  zigzags" on a 4-way grid). **Turn penalty +0.001** per direction
  change vs the previous step: among equal-length routes, prefer
  fewer turns — simpler *spoken* directions.
- **Graph:** neighbours = all offsets with Manhattan distance ≤ 2,
  resolved through a position index holding **multiple nodes per
  tile, one per height level** (bridges: walk-over and walk-under
  are different nodes). Distance-2 neighbours are how ledge jumps
  enter the graph. Open set = binary heap, closed = hash set (the
  stock pairwise version froze the game on big routes).
- **Dynamic obstacles:** objects and every warp *except the
  destination* are stamped impassable into a copy of the collision
  map before searching — a route never leads through an unrequested
  door. Map exits try the 8 nearest border tiles, then append the
  step off the map. Multi-tile objects fall back to their other
  tiles.
- **Spoken form:** per-step optional HM prefix ("Cut on way…", only
  when the player has the HM), run-length grouped — "3 north, 2
  east" — one utterance per group.
- **Honesty note:** a [start][goal] path cache exists but its store
  line is vestigial; do not treat it as working.
- **Use in OGA:** the turn penalty is the headline — pathfinding
  whose cost function includes *how hard the answer is to say and
  follow*. Directly reusable in any grid RPG adapter (the Saiyans
  overworld included) and in any future "take me to X" feature.

### 31.10 Radar orchestration: budgets, staggering, map-change discipline

Extends §29.6 (Manamon dB constants) with how ~20 simultaneous loops
stay intelligible (Pokémon radar, 1,037 lines):

- **Mixer law:** their mixer attenuates the opposite channel
  linearly, so dB values convert as pan = 1 − 10^(−|dB|/20), gain =
  10^(dB/20). Pan from the E–W offset only; **volume from Manhattan
  distance**; beacons at unity gain.
- **Voice budgets per family (hard caps, nearest first):** people 4;
  doors, stairs, items, signs, gates, caves, objects 2 each.
  Same-family beacon slots staggered by (i−1)×9 frames.
- **Map-change discipline:** 40 frames of quiet on entry; families
  start staggered (people 0, gate +6, door +12, cave +12, sign +18,
  stairs +24, item +30, object +36); map id must be stable 8 ticks
  before its data is trusted (GB changes the id a frame before the
  load); data re-read at +30 and +90 frames.
- **Cadence/pitch per family:** people/doors 35 frames N / 38 S,
  south ×0.85 — except samples with direction baked in
  (pnj_up/pnj_down) stay at pitch 1. Stairs 75/85, south ×0.92
  (constants from the Zelda 3 mod). Signs 100/110, **outdoors only**
  (indoors, "signs" are furniture — the object family covers them,
  ~50 types mapped to interact sounds, per-sound trims). **Items are
  loops** with each instance's base pitch randomised 0.97–1.03 so two
  nearby items differ by ear.
- **Walls:** per-direction contact volumes N 0.40 / S 0.56 / W 0.20 /
  E 0.12; falloff 5 dB/tile (was 8 — "wall at 3 tiles almost
  inaudible"), range 5; E/W walls also pan. Standing on a warp tile
  suppresses the distance-1 wall on the exit side.
- **Sonars** (water/land/ice): to the first tile of the kind within 5
  tiles, 6 dB/tile, pan ±0.6; land sonar active **only while standing
  on water** (find-the-shore); water/ice silent while stood on.
- **Door axis:** 10-tile range, unobstructed straight line required;
  N/S volume ramps 1.0→0.25, E/W fixed 0.8 panned; 4 rotating slots.
- **Warps:** 18-kind taxonomy with family + beacon/enter/exit/pass
  sounds; entry direction inferred from whether the destination is
  outdoors; the game's own door sound is muted **only when a
  classified replacement will play** (per-platform timing quirks
  recorded — GB takes the warp the same frame as the step; DS
  registers it one tile past the mat; BW two-tile mats deduped).
- **Bumps:** if the game's own bump sound can be hooked, play the
  *classified* sound at the game's rhythm and stand the detector down
  (one bump, one sound). Fallback detector needs position-still AND
  same direction held for **12 frames** (a U-turn after a pause used
  to false-trigger a ~10 ms/frame search — audible crackle on slow
  PCs); classification throttled to the 14-frame bump repeat.
  **Building test = bounded flood fill** (≤80 cells in an 8-tile box)
  over the connected impassable region: touching a door tile ⇒
  building wall, panned to the pushed side.
- **Self-silencing:** radar is off whenever the player is locked
  (dialogue/menus/cutscenes), off-map, or on title screens. Phases
  profiled; anything ≥3 ms is logged.
- **Use in OGA:** this is the missing engineering layer between
  §29.6's constants and a shippable radar: budgets first, staggering
  second, entry discipline third. The flood-fill building test and
  the "mute the game's sound only if a replacement will play" rule
  are individually adoptable anywhere (BBS/Dissidia door and wall
  work especially).

### 31.11 Relocation voting, literal pools, data anchors

Deepens §29.3 (Pokémon autodetect + anchors):

- **Voting, not first-match:** every signature match yields a
  candidate; plausibility filters apply (GBA EWRAM/IWRAM/ROM windows;
  GB $C000–$DFFF). Recompiled-engine addresses need **≥2 agreeing
  votes** (an isolated signature can land on a neighbouring function
  under a different compiler); plain addresses need 1; 3 sightings of
  one value short-circuit as settled. **Exception:** a signature
  beginning exactly at a prologue ($B5 push at +2, pattern ≥64 hex
  chars, offset ≤1) is trusted on one vote.
- **Literal-pool extraction:** the address is rarely in the matched
  bytes — the match is an `ldr rX,[pc,#imm]`; pool address =
  (match+4) aligned down + (imm low byte)×4; read the u32, subtract
  the per-signature field offset. Signatures deliberately match code
  using a *field* of the struct, never the base load.
- **Version election:** matches vote for the engine build via tags;
  version-unique markers tally separately; RHHEXP stamps choose the
  nearest reference version not exceeding the stamp. Hook signatures
  carry per-version register maps.
- **Struct-size recovery:** the most frequent `movs rX,#n` multiplier
  near loads of an array = its stride under the hack's compiler.
- **Pool fallback:** binary hacks often overwrite the *original*
  code's pool slot with the new address — read it as a candidate.
- **Neighbour shift, exact:** an undetected address follows
  neighbours only within **$80 bytes**, same memory zone, and only
  if ALL detected neighbours shifted identically.
- **Anchors (compiler-independent):** start from data (byte-identical
  under any compiler), find its loader function, follow by **rank**
  (nth call, nth constant of a kind). Rules generated from symbolled
  reference builds. **Quorum: ≥2 independent rules must agree.**
  Function ends estimated by scanning for a return followed by the
  next prologue, capped at $2000 bytes. Mid-function hook missing →
  hook the entry (args are always in r0–r3). Once-per-frame dedupe
  for hooks that fire twice (cursor erase+draw).
- **Use in OGA:** this is the full discipline for surviving ROM hacks
  and regional variants — voting + quorum + plausibility beats any
  single clever signature. The anchors method pairs with our Ghidra
  workflow: generate rules from a symbolled/decompiled reference,
  apply to variants.

### 31.12 Tactics layer: range-in-words, target cycling, cursor travel, attack solver (FE)

- **Range display in words:** don't read the overlay's colour —
  recompute its meaning: the game's movement-cost map (cost < max ⇒
  "can move here" / "occupied") plus the range map and the display
  proc's mode flags (green ⇒ "in staff range", red ⇒ "in attack
  range"), else "out of range". Three spoken states replace a colour
  wash.
- **Forecast template:** "{unit}, {HP} HP, {**net** damage} Attack
  [times N], {Hit}% Hit, {Crit}% Crit" per side, net damage =
  attack − defender's defense (the plannable number); "Cannot
  counterattack" stated instead of zeros; foe's weapon named from
  the forecast proc's text id. **Battle narration:** the game
  pre-computes the whole fight into a hit array (≤7 hits, attribute
  bits: crit/miss/retaliation/end); narration follows the
  *animation's own progress pointers* (per-side hits-shown counters,
  in both animated and map modes) so speech lands when the blow
  lands. Level-up stats are named via the game's own message tables
  (Str/Mag variants chosen by class).
- **Target cycling:** factions from unit-id ranges (allies $01–$7F,
  enemies $80–$BF, places from the chapter event list); lists sorted
  by Manhattan distance from the active unit (or cursor), ties by y
  then x; each entry spoken as name + relative offset + **"N of M"**;
  the chosen target's position is re-read live (units move) and
  reported "gone" if it left the map.
- **Cursor travel state machine:** drive the cursor with joypad
  injection, **horizontal axis first, then vertical**; arrival ⇒
  release all buttons, speak the arrival line, and let the arrival
  cell announce fresh. Failure: cursor unmoved for **90 frames**
  (menu opened / map edge) or a **600-frame** timeout ⇒ "The cursor
  cannot get there." During travel, per-cell announcements are
  suspended — only the destination speaks.
- **Attack-position solver:** among tiles that are reachable
  (movement-cost map), unoccupied (or self), and at a distance to
  the target inside ANY of the unit's weapon ranges → **least
  movement cost, tie-break FARTHEST from the enemy** (an archer
  keeps his distance). Fallback: nearest reachable tile, announced
  honestly as out of reach this turn. Weapon ranges decoded from
  item data: range byte nibbles (high = min, low = max); $FF = 1–99
  ("total"); $10 = 1–2; staves excluded by attribute bits; items
  from the unit's 5 inventory slots.
- **Use in OGA:** the whole tactics kit transfers to any grid SRPG
  (and to Pokémon-style target finding): recompute display meaning
  from data + mode flags; ordinal "N of M" cycling; a travel state
  machine with honest failure; a solver whose tie-break encodes
  tactics (distance-keeping) rather than mere reachability. The
  net-damage forecast is the model for any "will this kill?" query.

### 31.13 Text reading: text-machine bounds + VRAM scraping (SF2)

- **Text machine:** where a game has a script-text system (number at
  $1850, read cursor into bank $C4 at $0068), ship the full text
  corpus and speak a text when the cursor *enters* its recorded
  address bounds; re-trigger on a new number or the cursor moving
  backwards (same text shown again).
- **VRAM scraping:** tile grids read every 6 frames, low byte of each
  cell = ASCII; messages matched by string/pattern with an `unless`
  guard ("CONTINUE" suppressed while "V.S. OPTION" is up); blinking
  messages re-arm only after **90 frames** absent; name extraction
  ("YOU HAVE MASTERED X") uses word-boundary checks so "KEN" isn't
  found inside another word.
- **Use in OGA:** two complementary text routes for games whose
  dialogue isn't hookable — ground truth from the text machine where
  one exists, screen scraping with debounce/guards where it doesn't.
  Bruce Lee's narration adds the third route: fingerprint screens by
  probing the text buffer itself (victory and licence screens share
  every video register; only the displayed text tells them apart),
  and take timing from the game's own script-duration counters, never
  from elapsed time.

### 31.14 Brawler refinements (Streets of Rage 1) and boss exceptions (Bruce Lee)

- **SoR enemy radar, source level:** actor scan = 32 slots × $80
  from $B900 with per-level enemy-type tables; two tracked voices
  (nearest per side) inside a belt window (X ≤ 500, Y ≤ 80); steps
  on a global 32-frame timer (≈ the 0.53 s sample). **Pitch carries
  the vertical axis**: direction-specific step files (up/down
  variants) with a wide pitch range 0.75–1.50 over 75 px for
  distance, plus a general pitch 0.90–1.12 with a 10-px deadzone.
  **Volume tracks Y-proximity** (0.30–0.90), not X. Identity markers
  (per-boss step sounds) attenuated to ×0.30 **except bosses at full
  volume** (user ruling). Rank factors 1.0/0.75/0.60.
- **Alignment:** |dy| ≤ 16 px = aligned (the game's true belt band),
  repeat every 4 frames while aligned.
- **Range alerts with hysteresis:** enter box (melee 40×20, jump
  kick 110×26) vs larger exit box (+12/+8, +30/+20); re-arm 8 frames
  out; ranges per weapon read from ROM tables, melee/jumpkick as
  measured estimates; **one shared alert voice** — four independent
  voices once summed in the mixer in enclosed stages ("it stacks and
  gets louder", user report) and were merged.
- **Pan full distance 160** (was higher; at combat distance enemies
  stayed nearly centred) — cf. §29.11.
- **Bruce Lee boss exception:** the low-health heartbeat threshold
  is max/8, *except* the Demon boss, whose zero-HP flag means "needs
  a finishing defeat," not death — for it the warning fires **only**
  at zero HP, never at 12.5% (found in the disassembly: $C0:A87A).
  Enemy beacons: 40-frame interval, second enemy offset 30 frames,
  roster changes reset the timers; modules gate on ROM-state
  validity words before trusting actor slots.
- **Use in OGA:** hysteresis (separate enter/exit boxes) is the
  general cure for edge-flicker in every range/alignment alert we
  ship; pitch-as-vertical-axis frees pan for the horizontal axis in
  belt scrollers; and boss/state exceptions belong in the data, found
  by decompilation — the §26/§27 lesson again: read the code before
  trusting the threshold.

### Quick-pick addendum (§31) — the five highest-value techniques

1. **Attribution cascades + placement semantics (§31.2).** Ordered,
   evidence-ranked rules for deciding which fighter caused a sound,
   with measured accuracy and user-tuned placement (hit-on-you from
   the incoming side at 0.8; your-blocked on your side at −0.55;
   0.55 side floor). Drops straight into OGA's fighting-game
   adapters — this is the cue side of Devin's fighting standard,
   solved and field-tuned.
2. **Radar orchestration: budgets, staggering, map-change discipline
   (§31.10).** Per-family voice caps, 9-frame slot staggering,
   40-frame entry quiet with staggered family starts, map-settle and
   re-read timings. The layer that turns §29.6's constants into a
   radar that stays intelligible with 20 loops running — applies to
   every radar we build, Pokémon-style or §30 occlusion work alike.
3. **Relocation voting + data anchors (§31.11).** Signatures vote;
   plausibility filters; ≥2 votes for recompiled code; literal-pool
   extraction; data-first anchors with a ≥2-rule quorum. The
   difference between an adapter that survives ROM hacks/variants
   and one that breaks on the first revision.
4. **Cursor travel + attack-position solver (§31.12).** A joypad
   state machine that walks the cursor somewhere with honest failure
   (90-frame stall / 600-frame timeout), and a solver that picks
   where to attack from — least cost, tie-break *farthest from the
   enemy* — with weapon ranges decoded from item data. Any tactics
   or menu-heavy adapter wants both.
5. **Spawn glide + throttled tracking (§31.4, §31.5).** Tracked
   sounds start at their source and glide to the object (10 frames),
   update at most every 2 frames and only past a 0.02 pan delta, and
   fade in/out over 2–3 frames. Small constants, large perceptual
   payoff — projectiles, voices and beacons in every action adapter.

*(End of §31.)*

## §32 — Final Fantasy screen-reader mods mining (2026-11-06)

Source-level pass over three Final Fantasy accessibility mods, at Devin's
ask: **FFPRCore-Screen-Reader v1.0** (bladestorm360; FF I–VI Pixel
Remaster, one native C++ core for all six games), **FFX-X2-Screen-Reader
v1.0.2** (bladestorm360; FF X/X-2 HD Remaster, native C++), and **Blind
Soldier v0.10.11** (buu420; original FF VII PC, C# on Reloaded-II).
Unlike the JVPA packages, all three authors publish full source, so this
pass read code, data files, configs and design docs, not just packages.
**Standing caveat: none of the three mods was run.** Behaviour below is
what the source and shipped docs say; coverage and accuracy claims are
the authors' own, and both bladestorm360 projects track "gameplay
confirmation pending" per feature separately from "implemented."

### §32.1 FFPR — one core, six games, per-game data packs

- The unified mod is a single native core; everything game-specific is
  data: `data/ffN/` per game holds `maps.bin` (baked navigation grids),
  `mapstate.json`, `eventlabels.json`, `map_titles.json`, `puzzles.json`,
  `condition_visuals.json`, `mod_text.json`, `translation.json`, plus
  small override files (`warp_arrival_overrides.json`,
  `event_label_overrides.json`) for the cases the general data gets
  wrong. Overrides-as-data beats special-case code: a wrong arrival
  label is a one-line JSON fix, not a rebuild.
- The mod is a rewrite of five older per-game C#/MelonLoader mods. The
  port was graded against a written feature checklist (docs/FEATURES.md)
  built from the old code — "where a readme or plan disagreed with the
  code, the code was taken" — and settings kept their old
  MelonPreferences key names in the new INI file, so user settings
  survive the rewrite. **Use in OGA:** when an adapter is rewritten,
  freeze the old behaviour into a checklist first, and never rename
  settings keys.

### §32.2 FFPR — bake the map offline; keep the router pure data

- The game's own route searcher works in a ~31-tile window. The mod
  replaces it with A* over the **whole sub-map**, baked offline:
  `tools/bake_maps.py` turns map dumps into `maps.bin` (collision grids,
  layers, hidden passages, shop counters, warp edges, vehicle pads).
- The router is pure data — no game pointers — so the identical code
  runs in an offline tool (`tools/route_check.cpp`) against every
  baked map before shipping. The movement model inside it came from
  decompiling the game: per-cell direction masks, diagonal stair
  connections, hidden-passage cells open to the player but never to the
  game's searcher, layer changes by stepping on a ToLayer cell,
  world-map movement by transportation-group layers (on foot = group 1;
  vehicles extend walking once owned). Live blockers (NPCs, closed
  objects) are passed in per request.
- Fallback discipline: for an unknown/unbaked map, use the game's
  searcher rather than failing.
- Puzzle guidance is authored data, not code: `puzzles.json` steps are
  `{map, x, y, layer, text}` and the router treats teleporter pads as
  warp edges, so one destination can carry a guide across floors.
  **Use in OGA:** this is the §31.11 data-anchor lesson grown up — bake
  per-game nav data offline, keep routing testable without the game,
  and author guides as data files an adapter ships, not as code paths.

### §32.3 FFPR — two beacon modes, and a per-entity soundscape

- The beacon has two modes, switchable live (Shift+F6), saved even
  while off: **Follow route** (default) pings each leg of the computed
  route; **Object location** pings the selected entity directly, even
  through walls, current floor only, with bearings wrapping across
  world-map edges. Both keep pinging during continuous movement and
  replanning (no idle frame required), fall silent on arrival, and
  resume when you move away.
- Separate from the beacon, the **Soundscape** gives every nearby
  pathfinder-eligible entity its own repeating cue: one timbre per kind
  (NPC chirp, etc.), stereo pan, straight-line tile distance. Entities
  on another floor, opened chests and layer transitions are silent.
  Same-kind neighbours get slight pitch offsets in pathfinder order so
  two NPCs don't merge into one sound; crowded scenes **defer** cue
  starts rather than dropping entities — every in-range entity keeps a
  voice schedule. Long clips leave a gap before repeating.
- **Use in OGA:** "where is my target" (route-following beacon) and
  "what is around me" (soundscape field) are different questions; this
  mod answers both with different machinery instead of overloading one
  beacon. The defer-don't-drop scheduling is the §31.10 radar
  orchestration problem solved for a continuous ambient field.

### §32.4 FFPR — HRTF as a safe, reversible layer

- HRTF (OpenAL Soft) renders in a **loopback** on the audio worker: it
  never opens a physical device and never touches a process-wide
  context; SDL stays the sole playback owner, and the HRTF mix is fed
  back through it. If HRTF is unavailable, stereo + pitch cues take
  over automatically.
- The spatial frame is **map-north-up and fixed** — north ahead, south
  behind — regardless of which way the character faces. In stereo
  fallback, the southern (behind) half uses lower pitch; in HRTF mode
  pitch is not used for direction at all. Wall tones keep their own
  directional pitches in both modes.
- Volume memory is **per mode**: the same sliders store separate HRTF
  and stereo levels; switching modes restores that mode's values. HRTF
  applies a single 2× output boost; zero still mutes. Cue WAVs are
  user-replaceable files (`beacon.wav`, `beacon_blocked.wav`, …).
- **Use in OGA:** spatial audio as an optional render layer over an
  unchanged cue set — never a fork of the cue logic — with automatic
  fallback and per-mode loudness memory. Fits Devin's radar work: the
  §30 occlusion muffle can live in this layer too.

### §32.5 FFPR — speech and settings discipline

- Speech goes through Prism (0.18.3, pinned by lock file); a dedicated
  deduplicator guards repeated announcements (the old mods'
  `AnnouncementDeduplicator`, inventoried per game in
  docs/DEDUP_INVENTORY.md). Menus announce position ("3 of 12"),
  details are on-demand (I = details, U = usable-by, Shift+I = this
  screen's controls), and battle results are a reviewable phase buffer
  rather than a one-shot announcement.
- Equipment reading compares against what's worn: displayed comparisons
  and stat-delta verbosity are their own settings; enemy HP display is
  a three-state cycle (numbers / percentage / hidden) because the
  native panel doesn't show it.
- Settings defaults are conservative: speech and auto-detail on;
  autowalk, beacon-with-directions, soundscape, HRTF, landing pings
  all default **off**, each with its own volume. A Navigation-audio
  master switch silences every mod sound while preserving the
  individual preferences underneath. Every hotkey is
  foreground-focus-gated, Alt suppresses all mod hotkeys, and F5/F7/F8
  require no modifier held so Alt+F4 still reaches the game.
  **Use in OGA:** an audio master that mutes without forgetting, and
  defaults that add nothing audible until the player opts in.

### §32.6 FFX — one committed route feeds every consumer

- Navigation captures the native scene (actors, parts, interaction
  geometry) on a worker; a single **committed route** is then shared by
  spoken directions, the beacon, the reachability filter and auto
  walk. The docs state the rule explicitly: no consumer may compute its
  own straight-line shortcut, and an empty/no-path result must
  propagate to every consumer. Scene facts carry identity checks
  (allocation, epoch, player/control identity) before they're admitted;
  stale geometry, pose, gate state or epoch retires a committed route.
- Routes run over the game's decoded collision, including dynamic gate
  groups, and end at the target's **usable interaction zone**, not its
  stored point. (Known wrinkle, kept deliberately at the user's
  request: list distances are measured to an exit's reference position
  while the route crosses its trigger area, so list bearing and route
  bearing can differ near a wide exit.)
- Water is treated as a 3D transition problem: verified water sections
  are joined to their dry crossings by baked links rather than a direct
  swim line, and interaction endpoint height is preserved instead of
  flattening floors onto X/Z.
- **Use in OGA:** route disagreement between speech and beacon is a
  whole class of "the mod lied to me" bug. One route object, owned
  once, consumed everywhere, with explicit invalidation — adopt this
  for any adapter with both spoken directions and a beacon.

### §32.7 FFX — proved-unreachable-only filtering

- The destination list can hide unreachable targets, but the filter
  (default off) removes **only a proved unreachable** target; pending
  or unresolved collision data keeps the target listed. Reachability
  results are computed in the background with context/target identity
  attached, and survive a scene refresh only while geometry, pose,
  gates and epoch still match.
- Identification is conservative the same way: an entity exists when
  native active channels, actor binding/readiness and actual
  interaction geometry agree; unsupported or unready data stays
  unresolved instead of becoming passable. Treasure labels stay
  type-only (no contents spoilers); a developer/HUD name may be used
  for pathfinding identification before its story reveal, by the
  player's explicit exception.
- **Use in OGA:** never let an adapter's uncertainty delete the
  player's options. "We don't know" must render as listed-but-unproven,
  not as absent — the same honesty rule as our no-spoilers AD rule,
  applied to navigation.

### §32.8 FFX — auto walk and cue staleness

- Auto walk is default off; **speaking the path first is a separate
  default-off setting**. Any ordinary movement cancels immediately;
  menus, dialogue, focus loss, combat, map change, stale context,
  route loss and stall also cancel. Arrival never auto-interacts —
  the player interacts manually. Native input handback must survive a
  worker failure without waiting for a mod result.
- Audio cues are generation/context-tagged so stale sounds retire:
  independent cancellation for preview, warning, arrival, timing and
  position cues, and a 100 ms age cutoff for time-critical launch
  tones (obsolete commands are dropped, not played late). Beacon,
  walls and arrival have separate volumes; wall range is set in
  walking steps (1–5); the arrival sound requires the beacon on and
  the real final destination.
- The settings surface also separates **cues from assists**: minigame
  auto-success toggles (lightning auto-dodge, Jecht Shot, Blitzball
  auto-win in 1.0.2, chocobo/butterfly races) are each default off and
  context-scoped, while their manual cues (warning beep at the flash's
  first frame, timing tones) default on and stay independent. A
  "voiced dialogue" policy (Suppress / Braille only / Speak anyway)
  handles a natively voiced game without double-speaking.
- **Use in OGA:** an assist that plays the game for the player and a
  cue that informs the player are different features — ship them as
  separate switches, cues on, assists off.

### §32.9 FFX — state-inferred puzzle guides; installer as accessibility

- Puzzle guides (Cloisters and others) are step lists where F11 reads
  the current step, F9/F10 move between steps, and **Shift+F11 infers
  the current step from game state** — the guide resyncs itself
  instead of trusting its own counter. Auto-solving is a separate
  default-off option that intercepts only the qualified native script
  entry; it refuses when identity/hash checks fail and leaves native
  behaviour untouched. Treasure collection and story completion are
  tracked as separate phases so an already-collected reward is never
  re-solved.
- The installer is treated as part of the accessibility surface: it
  verifies original game files, backs up and removes conflicting mods
  and leftovers (Tolk, old Frida files), repairs launch redirection
  with a restorable backup, preserves the settings file across
  updates, and ships SHA256SUMS. For 1.0.2 the author added an
  exact-artifact scan gate (the shipped bytes are what got scanned),
  process-local DLL-search hardening and publisher metadata after a
  Defender download block — and still states plainly that the build is
  unsigned and a past AV detection is unresolved.
- **Use in OGA:** a guide that can resync from game state beats a
  guide that assumes the player followed it; and an installer that
  verifies, backs up and preserves settings is an accessibility
  feature for users who can't easily recover a broken install.

### §32.10 Blind Soldier — walkmesh routes, merged legs, honest progress

- Routes are built from the player's live position over the game's
  **native walkmesh** — the same triangles, ladders, jumps and
  elevations that govern movement — and on the world map over native
  map triangles joined into connected regions (by vehicle/character,
  so unreachable targets are filtered before selection). Spoken
  directions merge consecutive waypoints that continue in the same
  usable direction into one leg ("up 20", not five "up 4"s); a new
  instruction is spoken only on a real turn, an elevation change, a
  traversal link, or an obstacle detour. Navigation tracks live
  movement and re-routes only past a stray threshold — small movement
  inside a wide path causes no correction chatter — and routes pause
  across battles and resume on return.
- Progress is a **native Windows progress control** (the screen
  reader's own progress announcements, no speech code) showing percent
  along the whole connected route including elevation. Backtracking
  lowers it, deliberately. Interval is selectable: 5/10/15/20 percent.
  The README recommends leaving it on precisely because directions can
  sound circular in stairs, multi-level rooms and wrapped world maps
  while the percentage still tells the truth.
- **Use in OGA:** percent-of-route is the cheapest possible "am I
  actually getting anywhere" signal, and borrowing the platform's
  progress control borrows every screen reader's existing progress
  behaviour for free. Leg merging is how spoken directions stay
  listenable over walkmesh-grade route detail.

### §32.11 Blind Soldier — auto walk that never confirms

- Auto walk holds **only the directional inputs** for the current
  segment. It never presses the action/confirm button: at a door,
  switch, chest, NPC or ladder entrance it releases the keys and
  leaves the interaction to the player. Once mounted, it can follow a
  ladder's live climb direction, including horizontal climbs.
- Release conditions are an explicit list: dialogue, menus, movies,
  battles, control locks, loading transitions, focus loss, unreadable
  game state, route failure, arrival. Changing category or target
  stops the walk before the new selection. Browsing navigation on the
  controller (trigger held) pauses automatic movement.
- Vehicle cases are handled as their own problems, not waved through:
  the submarine rises to entry depth, travels, and stops **above**
  underwater destinations for a manual descent — specifically so auto
  travel never blunders into the Emerald Weapon superboss whose
  position the mod refuses to read; sighting it is announced, losing
  sight pauses input, sustained loss stops the route. Destinations
  with no water route are reported unreachable from the submarine.
- **Use in OGA:** the safety contract is the feature. "Never confirms,
  releases on an enumerated list of states, special vehicles get
  special rules with the dangerous case named" is the bar for any
  autowalk we ship — including FFPR's and FFX's, which follow the same
  contract independently.

### §32.12 Blind Soldier — story guidance as state-keyed definitions

- The Story category's targets are **definitions keyed to the game's
  progress state and field scripts**: for the state you're actually
  in, the definition names the next required interaction or
  destination. It covers the opening through the Northern Crater plus
  optional branches like Wutai, and is explicit about what it is not —
  not a walkthrough, and it makes no story or party choices.
- The catalog is validated by automated tests **against the installed
  field scripts**, and the README separates "implemented" from
  "played" in the project's own status section: tester logs cover the
  opening through the approach to Wutai (including recorded navigation
  failures and manual workarounds); no complete playthrough exists.
- **Use in OGA:** objective guidance generated from progress state —
  not a static checklist — is how a long RPG stays guided after the
  player wanders off the walkthrough's path. Validating the guide data
  against the game's own scripts is the offline-check equivalent for
  content, matching §32.2's route checking for geometry.

### §32.13 Blind Soldier — the audio-description production model

- Three tiers of recorded description, three formats:
  1. **Movies:** one full AD track per FMV (the opening + 99 others),
     named by the game's movie name (`bike_audio_description.ogg`),
     each with a JSON cue schedule and a `credits.txt` carrying voice
     provenance — the narration voice is a cloned voice ("Brice"),
     used with permission, generated locally with Qwen3-TTS; the
     credits file ships inside the package.
  2. **Cutscene/field clips:** 846 short clips for cutscene actions and
     area arrivals, **content-addressed**: the file name is the
     SHA-256 of the exact UTF-8 bytes of the text it speaks. A manifest
     maps text → (file, duration); durations matter because they hold
     and release the dialogue window. **No normalisation anywhere** —
     a stray space must not quietly match the wrong clip.
  3. **Battle animations:** 93 Limit Break/summon animations, 175
     recorded cues, described **once per save** (with a settings-menu
     reset for the current save), pausing when the battle pauses; the
     summon's name is still read on every cast.
- Failure is designed as fallback, never silence: a missing clip, a
  missing entry, or a malformed manifest (parse yields an empty
  manifest rather than throwing; entries missing a file name or
  duration are skipped) all fall back to ordinary screen-reader
  speech. A catalog hash mismatch is logged, not enforced — its only
  effect is more fallbacks.
- Description volume is its own master (50–300%, 25% steps, never
  mutable to zero), multiplying per-track calibration gains, with a
  limiter for boosted peaks; it never touches the screen reader's
  voice, the music, or cue sounds. Area-arrival descriptions describe
  the screen on entry and can be re-read (K); the author states the
  coverage limit plainly — runtime-animated gestures and reactions are
  mostly undescribed because they still need reliable visual capture.
- **Use in OGA:** this is a complete, shippable AD pipeline for our
  cutscene work (cf. the KHBBS AD reels): write text, hash it, drop in
  the recording — no ID bookkeeping, no code change, and a recording
  that isn't there yet degrades to TTS instead of a gap. The
  once-per-save rule for battle animations and the provenance credits
  file are both worth copying as-is.

### §32.14 Blind Soldier — tuned proximity cues, layered config, package cost

- Proximity cueing runs on measured constants in the shipped config:
  exit cues sound within an outer range of 400 units (inner 80) every
  3,200 ms; ladder cues every 1,600 ms with a separate 700 ms mount
  cue; footsteps switch between walk (500 ms) and run (300 ms)
  intervals against a measured run speed, using the game's own
  footstep sound bank per area. The motorcycle escape uses its own
  cadence ladder (enemy/truck beacons 320 ms, steering 700 ms,
  critical steering 260 ms) with distance hysteresis on warnings
  (warn at 1,200 units, recover at 900).
- Configuration is two layers: a large base `config.json` of
  per-feature flags and timing constants, overridden by
  `player-settings.json`, which the installer preserves across
  updates. The spoken settings menu (F11) stays audible even with mod
  speech turned off, and announces when a change needs a restart.
- Packaging lesson, counted: the portable ships the asset tree three
  times (shared `Assets/`, `x64/Assets/`, `x86/Assets/` — the same
  1,514 cutscene-voice files in each), which is most of why a speech
  mod weighs ~500 MB per download and ~1.6 GB across its four
  packages. Dual-runtime support (2013 x86 FFNx legacy + modern x64)
  is genuinely maintained, with parity tests — but asset duplication
  is the price, and it is paid by every user's bandwidth.
  **Use in OGA:** per-runtime folders should hold binaries, not a
  second and third copy of the audio; share one asset tree.

### Quick-pick addendum (§32) — the five highest-value techniques

1. **One committed route, many consumers (§32.6).** Spoken directions,
   beacon, reachability filter and auto walk all read the same route
   object; no consumer recomputes its own shortcut, and a no-path
   result propagates everywhere. Kills the whole "speech says left,
   beacon says right" bug class.
2. **Content-addressed description clips (§32.13).** Filename =
   SHA-256 of the exact text; a manifest carries durations; a miss
   falls back to TTS. Adding AD to a scene is dropping in a file —
   the right production model for OGA's cutscene description work.
3. **Bake the map, keep the router pure (§32.2).** Offline-baked nav
   grids plus a router with no game pointers means the shipping router
   is the same code the offline checker runs against every map —
   route quality becomes testable without launching the game.
4. **Percent-of-route progress on the platform progress control
   (§32.10).** The screen reader's own progress announcements carry
   "am I advancing" truth through stairs and wrapped maps where
   compass directions mislead; backtracking lowers it, on purpose.
5. **Auto walk that never confirms (§32.11).** Directional inputs
   only, release at the interaction point, an enumerated release list
   (dialogue, menus, battles, focus loss, stall, unreadable state),
   and named special handling for the dangerous cases. The safety
   contract is what makes autowalk shippable at all.

*(End of §32.)*

## §33 — Sticky Business Access mining (2026-11-07, source read, mod not run)

Source: MiloSummers/StickyBusinessAccess v0.8.3 (BepInEx/Prism mod for the Unity sticker-shop sim Sticky Business). Staged on sonix ~/sarah/oga-materials/sticky-business-access/.

- §33.1 Sectioned navigation for dense screens: split a complex screen into named sections (categories / items / placement / controls); Tab cycles sections skipping empties, arrows browse within a section, dedicated jump keys per section (F6/F7/F9), and each section remembers its own last focus while the screen stays open. Directly applicable to OGA's dense menu screens (KHBBS Config/Reports, MKU Konquest menus).
- §33.2 Focus restore by stable ID, never by display name or index: after an action that rebuilds the catalogue (placing a sticker replaces the catalogue object), restore the same choice via the game's stable item ID; duplicates and renamed entries can't break it; if the choice is gone, fall back to the nearest available. Mirrors the §32 lesson — identity over position.
- §33.3 One announcement per completed action: placement completion speaks a single focus announcement instead of announcing the controls and immediately replacing it. Speech economy — never queue an announcement you are about to supersede.
- §33.4 Navigator scan discipline (performance): the 0.7 navigator re-gathered/filtered/raycast-sorted all controls every 150 ms and blocked input 250 ms after category clicks. 0.8: hash-set dedup, remembered section membership, heavy screens gather at most once per second while unchanged, prompt refresh only on real state changes, category input guard cut to 50 ms. Any polling adapter (all of OGA's) should budget its scan the same way: cheap change-detection often, full gather rarely.
- §33.5 Observe the native save, don't invent one: the game has no manual save; the mod hooks successful return from the native Game.Save and queues "Game saved" after the action feedback. Announce saves where the game actually makes them; never present a design/template save as a game save.
- §33.6 Separate the editable from the read-only in production/inventory screens: v0.8.3 splits Saved sheets (actionable: Print/Edit/Delete) from stock counts (read-only, on-demand via F9) instead of mixing them in one list — keeps action menus short and puts pure information behind a query key. Fits OGA's on-demand-query pattern (foe health exact numbers on demand).

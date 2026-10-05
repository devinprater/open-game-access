# How the mods make visual game mechanics accessible

A design catalogue. Companion to `accessibility-mods-survey.md` (which covers *where*
each mod runs); this one covers **what the player actually hears, and how that stands in
for something they cannot see**.

Read from the mods' own words: their READMEs, and where they have them, their design
notes (`sf6Access/docs/accessibility-patterns.md`, `wt-orientation.md`,
`wt-radar-improvements.md`, and the 1,004 spoken strings in `SF6Access/lang/en.txt`).
Nothing here is inferred from a repo name.

Written as lists, not tables, for screen-reader reading.

---

## 1. The problem each mod is solving, stated plainly

Iron Lung's README has the best account of what this work actually is, and it is worth
quoting because it reframes everything: the game is set in a submarine with **no
windows**, and the game's own briefing says *"Since you can't navigate by sight, pay
attention to your coordinates and consult the map."*

> "The pilot is canonically blind. Everyone who plays Iron Lung plays it by instruments.
> You are not getting a watered-down version of this game — you are playing it exactly as
> written."

So the job is not "add speech to a visual game". It is **find the game's own information
structure and give it a non-visual channel**. Every good technique below is that, done
for a different kind of game. Where a game's information is genuinely visual-only — a
bullet-hell screen, a colour-matching puzzle — the honest mods say so and either
substitute something playable or flag a skip.

---

## 2. Menus and lists — solved, and solved the same way everywhere

The universal formula, seen in every mod with menus, is four things per item:

- **Its label** ("Sow Seed", "Door, closed")
- **Its value or state** (on/off, price, level)
- **Its position** ("2 of 5", "3 of 8")
- **What the confirm button does here**

Notes worth stealing from specific mods:

- **Icon-only buttons get invented names.** Kingdom Access: "Icon-only buttons get a name."
  A button with no text is not announced as nothing; a human label is supplied.
- **The screen announces itself on entry**, plus how to move around it and what each
  button does (Zomboid).
- **A re-read key and a history.** Kingdom Access keeps the **last 50 messages** with
  repeat / previous / next. Duel Links repeats the current item on Tab and the last
  announcement on Ctrl+R. This exists because speech is transient — a sighted player
  re-glances, a blind player needs a re-speak.
- **Every list is browsable after the fact**, not only at the moment it appeared. SF6's
  `Battle Hub`/reward panels and Kingdom Access's on-screen notifications become
  "collected in a browsable list".
- **Jump keys, not just arrow keys.** Blindest Dungeon maps every Hamlet building to a
  letter (A Abbey, B Blacksmith, C Stagecoach…). Dressmaker: "1-7 jump between rooms."
  Zomboid's tutorial offers "Radio: choose a lesson" to jump to any lesson.
- **Remember the selection by name, not index, when a list rebuilds** (SF6's destination
  picker remembers the last category across rebuilds).

---

## 3. Space — turning a 3D or 2D world into directions and distance

This is the richest area, and the mods converge on a small vocabulary.

### 3a. Direction is spoken as a clock position or a stick push

- **Clock positions** (SF6 World Tour): `wt.at_clock_meters` = *"{0} at {1} o'clock, {2}
  meters away"*. So: "Style Lab Beauty Salon, shop, at 2 o'clock, 85 meters away, 3 of 12".
- **Stick pushes**, which are more actionable than compass points: Zomboid gives *"Door,
  closed, 4 metres up-right, in the kitchen, 3 of 8"* — "up-right" is literally the way to
  push the stick.
- **World compass as an option.** Wasteland 2 makes it a setting: `UseClockPositions`
  (default off) speaks "3 o'clock" instead of compass names.

### 3b. Camera-relative vs world-relative is a real tradeoff, and FFXII documents the cost

FFXII gives directions **relative to the camera**, because that is the frame the stick
actually acts in, and then states the consequence honestly:

> "The game moves the camera on its own: stepping onto a ledge or stairs, hugging a wall,
> and constantly in battle as it tracks your target. When it does, the same route is
> described from the new angle, so a route that was 'northeast' can become 'southwest'
> without you having gone wrong… There is no way for the mod to prevent this without
> breaking battle targeting, which uses the same camera to lock onto enemies."

⛔ This is the single most transferable *pitfall* in the whole set. A direction that is
true for the input device can silently invert when the camera moves. Any adapter that
speaks directions must decide which frame it means, and say so in its docs.

### 3c. The beacon: continuous bearing as sound

FFXII's beacon is the clearest design. Turn-by-turn directions tell you the route **once**;
the beacon keeps telling you **while you walk**:

- a repeating sound **placed in the direction** of the next waypoint
- **each turn of the route is a point**: reaching one moves the beacon on to the next,
  silently
- **pitch and filter encode front/back** — "A sound behind you is quieter, duller, and
  about a fifth lower in pitch than one in front"
- **left/right keeps working all the way round**: "Something behind and to your left is
  played to your left and carries the behind quality" — pan and the behind-filter are
  independent channels, so 360° is expressible
- **it re-plans silently** if you wander off: "the mod quietly works out a new one and
  re-aims the beacon. It does not say anything."

⛔ Note the *silence* discipline: the beacon does not narrate the re-plan. It just changes.

### 3d. Approach cues: cadence as distance, and "the game's own prompt" as arrival

- **Beeps get faster as you close** — the near-universal distance encoding. Zomboid's
  guide mode: "the target beeps from where it is, faster as you get closer." Kingdom
  Access's bomb cave: "a heartbeat guides you to the bomb… faster and louder when closer,
  in the ear of the side where it is."
- **Distance as an estimated time or count**, for planning: Zomboid's autodrive says
  "Turn left in 40 metres", then "Turn left now". SF6's navigation says `Clear for {0}
  meters` for how far ahead is open.
- **Arrival is confirmed by the game's own interaction prompt**, never by proximity.
  P4G: "auto-walk that arrives on the game's real interaction prompt." SF6's radar plan
  says the same: "Arrival uses a 4 m Euclidean distance alone… Nearby across a wall is not
  confirmed arrival", and requires "matching contact data and active prompts".
- **Direction-change announcements carry the new distance with them**: "when the way to
  push the stick changes you hear it ('up-left, 6')" (Zomboid).

### 3e. Obstacle sensing: what is ahead, and is it passable

SF6's navigation radar has a full vocabulary, from its string table — this is the
complete set of things it can say about the ground in front of you:

- `Clear ahead`, `Low step`, `Waist-high obstacle`, `Wall`, `Tall wall`, `Blocked`
- `Wall you can run along` (a wall you can wallride)
- `{0} at {1} meters` / `Clear for {0} meters` — obstacle and clearance with distance
- `left open`, `left blocked`, `right open`, `right blocked`, `left clear for {0} meters`
- `floor ahead` / `drop ahead` — a drop edge
- `Enclosed space`
- `Exit ahead` / `Exit on the left` / `Exit on the right` / `Exit behind`
- `opening at {0} o'clock, {1} meters wide, {2} meters away` — a real opening with a width
- `gap at {0} o'clock, only {1} meters wide` — too narrow to use
- `No way through in range`

⛔ **And SF6 documents the hard limit of a ray-based radar**, which is the best honesty in
the set: "A beam is 'passable' when the waist-height body sweep sees ≥ 6 m of clearance…
So an *exit* is 'a beam that happens to look far', not 'a place you can go'." A 1.6 m alley
6 m away subtends ~5°, and three 90° beams never see it — "even 24 rays miss it". Its
planned fix is to take openings from the **navmesh's own link edges** so an "exit" means a
real navigable link, and to **pan the cue to the real bearing of the edge** rather than to
the beam it fell in.

### 3f. Zones and "where am I"

- **On-demand position**, separate from turn-by-turn, on one key. SF6's Z gives zone,
  nearest landmark with clock direction and distance, facing, and where the guidance
  target lies: *"In Hong Hu Lu Chinatown - Plaza. Beat Square, at 3 o'clock, 40 meters.
  Facing north. Destination: objective, at 11 o'clock, 120 meters."* Plus a height
  difference when relevant: "12 meters higher".
- **Zone announced on entry, from the game's own tracker** — never re-derived. SF6
  explicitly prefers the game's section banner and falls back in this order: banner →
  section id → landmark region → city as last resort. Among Us's `RoomWatcher` does the
  same for rooms.
- **Dedupe and debounce**: SF6 debounces boundary flicker (a change needs the new landmark
  meaningfully nearer, confirmed twice), and marks zone entry as "first entry instant".

---

## 4. Turn-based and tactical combat

Wasteland 2 is the model here, and its design is a set of separate keys for separate
questions rather than one omniscient narrator:

- **The cursor jumps to the active actor each turn** and announces **whose turn it is**.
- **An initiative tracker on T**: "turn order with HP, AP, status" — the whole future of
  the battle, on demand.
- **A tile cursor** for the grid: move one step, hear what is on the tile, act on it.
  `Shift+Left/Right` changes step size; `Ctrl+Arrow` moves **until blocked** by terrain.
- **Detailed tile announcement on Backslash**, with toggles for **announcement order (K)**
  and **line of sight (Y)** — "clear line of fi[re]..."
- **Distance in tiles when a combat grid exists** (`UseTileDistances`, default on, "one
  tile is about…"), and a **line-of-sight announcement** per tile gated by the character's
  perception.
- **Targeting by category**: `PgUp/PgDn` cycles combatants, `Ctrl+PgUp/PgDn` cycles
  combatant **category**.
- **Skill checks announce the requirement and the result**: "Skill-check options announce
  the required level and whether you pass."
- **Conversations**: "While the NPC is still speaking, Enter skips the voiceover instead of
  selecting" — the confirm key is remapped contextually so a blind player does not
  accidentally choose an option while trying to skip audio.

Blindest Dungeon (Darkest Dungeon) adds the real-time feel of a turn-based game:

- **A drumming sound plus the hero's name** marks the start of that hero's turn — you hear
  whose turn it is without reading anything.
- **Skills selectable by number** (1-4) if you know their position, on top of browsing.
- **Every skill and effect has its own sound effect** — a per-ability audio signature, so
  you learn the game's vocabulary by ear.
- **Targeting reads the maths before you commit**: "you'll hear your chance to hit,
  estimated damage, and chance to crit as you move between enemies."
- **A tooltip buffer** (Ctrl+up/down) that reads through tooltips for anything in the room
  or in battle, and a **`tilde` jump to the action bar from anywhere**.
- **Tile-stepped movement** (shift+A/D) exists specifically because held movement keys
  "cut off announcements" — so the mod offers a movement mode that cannot outrun speech.
- **An event log** (period) to review anything that happened, since speech is transient.

### 4a. The fighting-game case: SF6, the one asked about

SF6's combat design is different because a fighter is real-time and the accessible player
still has to *input* the fight. What it actually does:

- **It speaks inputs the way a fighting-game player says them.** From the string table:
  `motion.236` = "quarter circle forward", `motion.214` = "quarter circle back", `motion.623`
  = "forward, down, down-forward", `motion.41236` = "half circle forward", `motion.236236`
  = "double quarter circle forward", `motion.44` = "back, back", `motion.66` =
  "forward, forward". Directions are named `neutral`, `down-back`, `down`, `down-forward`,
  `back`, `forward`, `up-back`, `up`, `up-forward`.
- **It fills in the button glyphs the screen reader cannot see**: `input.BTL_X` = "square",
  `input.BTL_Y` = "triangle", `input.BTL_A` = "cross", `input.BTL_B` = "circle", plus L1/R1/
  L2/R2/L3/R3 and "up"/"down"/"left"/"right". A combo the game shows as a picture of
  buttons becomes a spoken sequence.
- **Combo tracking reads the game's own counter** — `cTeam.mComboCount` (a short) and
  `mComboDamage`, i.e. the on-screen "X HITS" — and announces the combo once it is
  confirmed ended, not per hit.
- **It announces results at the moment the game judges them**: hit / down / killed / miss,
  and training's frame data and attack data panels.
- **Rank and progress are data-driven, not read off animating text.** The rank gauge
  animates; the mod reads `RankInfoAfter` as *data* rather than polling the moving text.
  Same for `league_rank` → a resolved rank name ("Diamond 3") plus the point value.
- **Combos are suppressed, not narrated live.** Its design notes record removing a gate
  that silently dropped whole combo readouts, and keeping the announcement to the
  confirmed end.
- **A speech arbiter with priorities** is the planned field design: "blocked ahead >
  exit / junction > beacon line > district / banner > radar class change. Higher
  interrupts, lower is dropped inside 1.5 s of a higher one." Key presses always answer.

⛔ **And SF6 states what it does not cover** — Battle Hub and World Tour are named as
gaps, and the World Tour plan flags navmesh exits as "an investigation, not an available
solved feature".

---

## 5. Real-time action: sound as the mechanic, not a description of it

Zomboid Access is a complete worked example of turning a survival game's visual state into
audio. Its **own-sound list is the clearest cue taxonomy in the whole set**, and every
one of these is a *different kind of information*:

- "A low **double thump** from each of the 3 nearest zombies that are chasing you within 20
  metres, or any within 10 metres. It comes from where the zombie is, and gets faster as it
  gets closer." → **threat position + urgency**
- "A **wood-block tick** while you're aiming at a zombie close enough to hit." → **weapon
  range / valid target**
- "A **soft bell** from the place you picked in guide mode." → **destination**
- "**Two quick rising whistles** from the best way out while something chases you." →
  **escape route, a categorically different thing from a threat**
- "Driving: a **low blip** means steer left, a **high blip** steer right; faster the more
  you need to turn." → **a continuous control loop as two pitches**
- "Fishing: **two little water plips** when a fish bites." → **a discrete event**
- "The tutorial: a short **radio crackle** before the guide speaks." → **speaker identity**

Note the discipline: **only the player hears these** ("zombies don't"), each family has
**its own on/off switch**, and the switches live in the same list as everything else
("Zombie sounds / Escape help / Driving help, then Square").

Other Zomboid mechanics worth stealing:

- **The scanner is categorised and nearest-first**: "Door, closed, 4 metres up-right, in
  the kitchen, 3 of 8". Categories include *You, Markers, Zombies, Ways out, Animals,
  Loot, Doors, Windows, Stairs, Containers, Things on the ground, Water, Crops, Beds and
  seats, Lights and appliances, Bodies, Vehicles, Places, Houses* — the game's own world
  taxonomy, not a generic "objects" list.
- **"Ways out" is its own category**, "best first, leaving out any that are towards a
  zombie", and each says what to do: "Door, closed: go through, then Cross closes it
  behind you".
- **Things are ordered by the player's own reachability**: "Things on your floor come
  first, then things on your side of the walls, then the rest."
- **Straight-line distance is admitted as a limitation**, with a label for the case it
  cannot resolve: something inside a building you are not in "says 'inside' or 'in another
  building'".
- **Out-of-sight is labelled, not hidden**: zombies "the ones your character can't see say
  'out of sight'; 'coming for you' if it's chasing you."
- **Continuous processes are narrated on progress points**: "Long actions (reading,
  crafting, bandaging…): named after 2 seconds, then 25, 50 and 75 percent, then Finished
  or Stopped."
- **Cooking is followed as a state machine with warning stages**: "Steak cooking, in the
  chrome oven" → "half cooked" → "cooked: take it out before it burns" → "about to burn!"
  → "has burnt". The player can act on the penultimate state, which is the point.
- **Time and world events announce themselves**: "It'll be dark in about an hour", "Night
  has fallen", "Dawn", and the day the power or water goes off for good.
- **The player can name places** (Mark this spot → "Home" by default), which is how a blind
  player builds their own cognitive map.
- **Autodrive exists but driving does not have to be automated**: "You drive; the mod tells
  you where the road goes." The steering tone is a *control* cue, and cruise control is the
  game's own. Autodrive is an option, not the design.
- **Walking round zombies is a routing decision, spoken**: "With zombies within 12 metres,
  walking goes round them… The route is planned square by square to keep away from them."
- **A guided tutorial built as a challenge mode**, with a radio guide, "Each step waits
  until you've done it. Stuck for 40 seconds, you get a hint", progress kept in the save,
  and the fighting lessons make zombies unable to hurt you. This is the "teach the game by
  playing it" pattern, and it is the most under-used idea in the whole set.

---

## 6. Minigames, QTEs and the genuinely visual — where mods refuse or substitute

The honest answer to "how do you make a visual mechanic accessible" sometimes is **you
don't; you substitute something playable and say what you did.**

- **Among Us Access** makes "every task minigame on The Skeld genuinely playable by ear…
  each with its own sound design", and is explicit that you are not teleported through it:
  "you are not just teleported through it." This is the high bar — a real audio
  re-implementation per minigame.
- **Undertale** is the counter-example, and states the limit plainly: "due to this Game's
  status as a bullet hell title, direct adaptation of the combat was impossible. Following
  the directional beeps with your arrow keys will teleport your soul to safe places on the
  board. While not the exact same, it should still provide an engaging and challenging
  experience." It also "clearly flagged with a skip" the "handful of puzzles that truly
  need eyesight".
- **Undertale's Mettaton minigames** each get "its own guidance" — a jetpack climb, a
  coloured-tile maze, a bomb defusal.
- **Friendly versus hostile projectiles are distinguished by timbre**: "friendly 'touch me'
  projectiles get their own warm chirp so you don't dodge the thing that heals you."
- **SF6** names the modes it cannot do (Battle Hub, World Tour's unverified parts) rather
  than pretending.
- **Iron Lung** turns its limitation into the design: the game already has no windows, so
  instruments are the intended experience.

---

## 7. Describing things: audio description as a first-class feature

Several mods treat *description* as separate from *playability*, on its own key, because a
blind player may want the game's art and story, not just its mechanics.

- **Undertale** has "audio description… what each character looks like, what every room
  looks like (press L to re-hear), the silent visual moments in cutscenes, and what each
  monster looks like in battle (press D)."
- **P4G** reads on-screen subtitles during anime cutscenes "plus optional hand-authored
  descriptions of the visuals, synced to the movie."
- **SF6** ships **603 `avdesc.*` strings** to describe avatars — `"average athletic build"`,
  `"shaved to the skin"`, `"medium straight hair, long fringe covering the eyes"`,
  `"red skin and glowing eyes, demonic look"`, plus full one-line composites like
  `"long dark hair and full beard"`. It describes *characters*, which is content that has
  no gameplay function at all.
- **Bad Dream: Coma** is a point-and-click with no text, where "the scenery and other
  objects… provide a lot of the content, so they have a[ll been described]" — "notes,
  pictures, and posters are also described for you to get a good sense of the world."
- **Dressmaker** describes the client, the fabric shelf items, and reads "how the design
  meets the client's wishes" (T).

⛔ The rule that emerges: **describe once per scene, and re-describe on demand.** Undertale
puts room description on L "to re-hear"; SF6's orientation doc requires zone lines that
"queue, never interrupt" and are "muted during dialogue".

---

## 8. Making the difficulty itself accessible

Two mods move game *settings* into the accessible layer rather than leaving them in a
visual options screen:

- **Undertale's in-game Accessibility menu on K** — "toggle features, difficulty, music" —
  also on the title screen. And **N/B to turn the music down so the cues come through**,
  which every audio-cue mod needs and few ship.
- **Kingdom Access** puts "the game's own words" in the game's language: "Follows the game
  language automatically… All 11 languages of the game are included." Several mods do this
  (SF6 ships 12 `lang/*.txt` files; Kingdom Access 11; Blindest Dungeon 12). **Localisation
  of the accessibility layer** is standard here and easy to forget.
- **Verbosity is a user setting, not a developer choice**: SF6 has `combat_verbosity`
  0/1/2/3 and FFXII has "Combat verbosity… Normal and Verbose" on F4.
- **Default new features off during development, on before handoff** — and say so in the
  README (Blindest Dungeon's status list, Kingdom Access's "What it does not do (yet)").

---

## 9. The cross-cutting rules, distilled

These appear in multiple mods and are the transferable ones:

1. **Speak discrete events; sonify continuous state.** Speech for menus, results, dialogue.
   Sound for position, distance, health, speed, proximity. Zomboid's cue list is the
   reference.
2. **Never interrupt what the player asked for.** Zone lines queue; ambient cues never cut
   dialogue; "ambient cues must never interrupt/requeue into dialogue; one-shot events may"
   (SF6). A speech arbiter with explicit priority ordering is how it is enforced.
3. **Confirm arrival from the game's own prompt**, never from a distance calculation.
4. **Announce on change, not on a timer.** SF6 moved its guidance from 8 s/15 s repeats to
   events (clock-hour change, distance band halved/doubled, corner reached, arrival), with
   a 20 s fallback only when nothing changed.
5. **Every cue family gets its own switch, in the same list, with a spoken change.** And
   the switches persist.
6. **Distance as the player's own unit when the game has one** (tiles, stick pushes,
   route metres) — not raw world units.
7. **A re-read key, a history, and an event log**, because speech is transient and visual
   UI is not.
8. **Name the refusals.** Both in the README and, where possible, in speech ("No way
   through in range", "Location unknown", "not reachable from here"). SF6's `wt.nav_no_openings`
   and `wt.zone_unknown` are exactly this.
9. **Camera-relative directions can invert.** Pick a frame, document the tradeoff, and
   offer the compass alternative.
10. **The game's own data beats re-derivation.** Zone from the game's tracker, rank from
    the game's rank table, combo from the game's counter, openings from the game's navmesh.

---

## 10. What OGA can take directly

Ranked by cost, and honest about the gap:

1. **The direction and distance vocabulary is adoptable as-is.** Clock positions
   (`at 2 o'clock, 85 meters`), `Clear ahead` / `Blocked` / `drop ahead` / `Exit on the
   left`, and `Clear for {0} meters` map straight onto what a NES/SNES/DS reader can say
   about a tile map.
2. **The "confirm arrival from the game's own interaction prompt" rule** applies to every
   adapter and costs nothing.
3. **The cue taxonomy from Zomboid, and the "one switch per family" discipline**, is
   app-level and applies to every reader.
4. **The refusal vocabulary** (`No way through in range`, `Location unknown`) is cheap and
   stops a player hunting for a missing feature.
5. **The beacon needs an audio path OGA does not have.** Pitch, pan and cadence cues
   (Zomboid, FFXII, Kingdom Access) all need non-speech audio the app cannot yet emit —
   this is the same gap recorded in the survey, and it is the biggest single ceiling here.
6. **Undertale's "flagship puzzles get a skip"** and **Among Us's "re-implement the
   minigame by ear"** are the two ends of the option space for a genuinely visual mechanic.
   A reader should choose deliberately which end it is on, and say so.

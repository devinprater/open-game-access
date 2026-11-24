# Auto-walk for OGA adapters — design

WHY THIS EXISTS: Blind Soldier (an FFVII accessibility mod, mined
2026-11-22) lets the player press one key and be WALKED to the selected
target along a route built from the game's own walk geometry. Our
adapters stop one step earlier: the ALttP Guide speaks a real walking
route, and the player then has to execute it by ear — screen by screen,
door by door, including the places where the execution is the hard part
(the Eastern Palace climb is a 16-pixel stair strip that has to be hit
at one exact x). Speaking the route removed the knowledge wall. Walking
it would remove the execution wall. This doc designs that, and nothing
else: it is a design, not a build. No code in this commit implements it.

THE SOURCE DISCIPLINE (Blind Soldier's, adopted whole): auto walk holds
ONLY directional input and NEVER presses the action/confirm button — at
a door, chest, NPC or ladder it releases and hands the interaction back
to the player. All input is released on dialogue, menus, movies,
battles, control locks, loading transitions, focus loss, unreadable
game state, route failure, and arrival.

## What exists today

- `Host::set_button(void* ctx, int button, bool down)` (Core/adapter.h)
  already lets an adapter write the emulated pad. Every core implements
  it (oga_core.cpp maps the shared pad slots per console; the SNES core
  keeps one `buttons[]` array its joypad callback reads).
- That array is SHARED with the player: the app's own input path writes
  the same slots. There is no arbitration, no ownership, and no expiry —
  last writer wins per button, and a direction written once stays down
  until something writes it up. That is safe for the scripted,
  momentary presses existing users make (menu echoes, the FE harness's
  cursor keys) and unsafe for a held direction.
- An adapter cannot see the player's input at all. The host forwards
  game-pad down-edges as Commands for tracked menus and the OSK, but
  there is no feed of the player's held buttons or presses. "Player
  input overrides instantly" is therefore impossible to honour today.
- The steering itself is proven on the bench: Core/snes_probe.cpp's
  walkers drive the real game deterministically (set buttons, run a
  frame, read state), including the Palace climb's cross-axis servo —
  align to the stair strip's exact x first, then drive the main axis.
  What the bench has and the app lacks is the safety contract around
  the steering.

## Proposal

### 1. A direction-only host verb, appended

Add ONE member to `oga::Host`, appended (the struct is part of the
C ABI shared with Swift; nothing is reordered, exactly as `announce_q`
was added):

    /// Walk input. `dir` is 0=up, 1=down, 2=left, 3=right — a direction,
    /// never a pad button index, so this verb cannot express a face
    /// button at all. Cores map it onto their own pad bits.
    void (*set_direction)(void* ctx, int dir, bool down);

"Never presses Confirm" stops being a rule the adapter must remember
and becomes a thing the interface cannot say. `set_button` stays for
its existing scripted uses; walking code links only against
set_direction, and the host-stub tests can assert that (a stub whose
set_button records any call from a walk test fails the test). Two
strengthenings from the consult are part of the proposal, not options:
the host hands a walking adapter a Host view with set_button nulled,
so the guarantee is structural rather than a comment; and a null
set_direction or player_pad (a host or stub that predates the verbs)
means the walker REFUSES to start — without the override feed there
is no override, and without the verb there is no walk. The new
members go after ctx, truly last in the struct.

### 2. Directions expire (the dead-man's switch)

A direction set through set_direction EXPIRES unless the adapter
refreshes it within 3 frames. The core owns the clearing: expired
adapter directions are dropped before the core samples input each
frame. Frames, not milliseconds — a wall-clock lease misbehaves
under slow frame rates, fast-forward and pause, and the rest of this
design already counts in frames. The player's own buttons are
host-tracked separately and never expire.

Consequence: if on_frame stops being called — pause, focus loss, a
hung frame loop, detach — the game stops receiving walk directions
within a few frames, with no cleanup code having to run. Silence is
stop. This is the property that makes a held direction safe to offer
at all.

### 3. A player-input feed, appended

Add ONE member to `oga::Host`, appended:

    /// The player's OWN pad state as the host last saw it from physical
    /// input, before any adapter writes: `held` is the current mask,
    /// `pressed` the edges since the adapter's last frame. Button bits
    /// use the shared pad numbering oga_core.cpp already maps per
    /// console. Adapters read; only the host writes.
    void (*player_pad)(void* ctx, uint32_t* held, uint32_t* pressed);

Arbitration lives in the CORE, not the adapter: any physical player
press zeroes the adapter direction layer at once, before the next
input sample — the adapter's next frame is not part of the stopping
mechanism, so there is never a frame of conflicting input. The feed
(latched: presses are held until the adapter's next frame reads them)
then tells the adapter its walk was cancelled, so it can update its
state. A player cancel is silent in speech: the player has taken over
and knows it, and speech would talk over their own play. (System
cancels below DO speak one short line where the cause does not
already announce itself: the player did not cause those, and needs
to know the walk ended and why.)

### 4. One new command: WalkToggle

Append `WalkToggle` to `oga::Command` (after `Enemies`, raw value 44)
with its Swift twin in AdapterCommand.swift, and opt adapters in
through supported() one at a time — ALttP first, and only when its
bench proof passes. Pressing it with a current Guide route starts
walking that route; pressing it again stops. With no route selected
the adapter answers honestly in its own words (ALttP: the Guide's
"ask for a route first" equivalent). No adapter is obliged to
implement it; an adapter that doesn't stays silent, as with every
unmapped command. On a successful start the adapter first SPEAKS
the target ("Auto walk: Kakariko Village.") before the first
direction is written — the selected landmark may be minutes old,
and the player confirms the destination with their ears before
their feet move. Auto walk is also a per-game SETTING, off by
default (the per-game-settings mechanism, KhbbsHudGaugeCues is the
precedent), with a one-time spoken explanation the first time the
command is used; WalkToggle with the setting off answers with that
explanation instead of walking.

### 5. The walker lives in the adapter

The adapter owns the route and the position reads, so the walker is a
small state machine in its on_frame: Idle / Walking / Arrived /
Blocked. While Walking, each frame:

- RE-VALIDATE before touching input: adapter ready() plus the game's
  own state gates (for ALttP: a gameplay module, no dialogue index
  active, no menu module, StateSane()). Any failure is a release
  condition (below), never a "keep holding and hope".
- STEER along a TILE-LEVEL path, not toward the corridor: the
  spoken route's geometry (screens, regions, rooms) is the right
  granularity for speech and the wrong one for steering — a region
  can be U-shaped, with bushes, fences or water inside it. While
  walking, the adapter paths over the same assembled tile grid its
  graph generator already replays, with the walkable mask shrunk by
  Link's collision box and hazard tiles excluded (pits, spikes,
  deep water, ice), plus the tiles speech never needed to treat as
  floor — the 0x22 stair treads, which are how climbs exist at all.
  The path is computed only while walking and cached per screen.
  Where the path demands an exact line (stair strips, door
  channels), servo the cross axis to the line first, then drive the
  main axis — the probe climb's technique, generalised. The mask
  criterion matters more here than it ever did for speech: a tile
  that is walkable on any one subtile is a wall the walker will
  grind on, so the steering mask uses the collision-shrunk form,
  not the speech form.
- TRACK progress along the path: distance still to go ALONG THE
  PLANNED PATH (never straight-line distance to the target, which
  plateaus or grows around obstacles and would fake a stall). On
  query, progress is reported in the route's own units — "two
  screens to go", "three rooms to go" — not as a percentage, which
  jumps around when legs differ in length or after a re-plan. This
  is also the missing architecture for route-progress reporting
  (Blind Soldier trick 3). Unasked progress speech is off; a
  quarter-way spoken milestone may exist as a per-game setting,
  default off (the game already sounds walking; it does not sound
  "halfway there" — the setting is the player's call, not ours).
- STUCK: if path-progress has not improved for ~90 frames, release
  and cancel with one line ("Auto walk stopped: blocked."). The
  walker may re-plan from the live position under the re-plan
  limits in §6; it never grinds against a wall, and the health gate
  in §6 stops a walk that is being hit long before the stuck
  window matters.

### 6. Release conditions (the contract)

The walker releases ALL directions and leaves Walking the moment any
of these is true — checked every frame, before steering:

- dialogue starts (the game's dialogue state goes active);
- any menu opens (menu module / tracked-menu state);
- a battle or encounter state begins (per-adapter gate);
- the player's health DECREASES, or the player character is stunned
  or knocked back (ALttP has no battle state to key on; harm is the
  signal — and the walker refuses to START at all while health is
  at the adapter's own low-health threshold);
- a screen/room transition the route did not schedule (a scheduled
  one ENDS a leg; an unscheduled one cancels the walk);
- arrival (the route's end condition reads true);
- state unreadable or insane (ready() false, StateSane false);
- focus loss or session stop — covered structurally by expiry (§2)
  and by detach releasing, belt and braces;
- player override (§3);
- WalkToggle pressed again;
- route failure: stuck twice on one leg, the live region no longer
  matches the route's terms, or the target became unreachable.

RE-PLANS, LIMITED: a re-plan is allowed only for recoverable
displacement (knockback within the same region is the type case),
only to the SAME leg target, and only if the new plan adds no
costly or irreversible leg the current plan lacks. At most three
re-plans per walk; each speaks "Re-routing." once — a silent
re-plan hides that the route moved. A re-plan cannot fix a missing
obstacle model and is never a substitute for the tile path in §5.

WARP SCREENS: screens whose transitions are scripted loops rather
than geometry (the Lost Woods is the named case) are a denylist:
the walker refuses to start on one and stops on entering one.

Arrival speaks the adapter's existing Where Am I line (the player
hears where they ended up, in the words they already know). System
cancels speak one short line naming the reason in plain words —
but only where the cause does not already announce itself (a
conversation needs no "a conversation started" line over the
dialogue's own speech; blocked, hurt and unreadable state do get
their line). "Auto walk off." covers WalkToggle. Player override
is silent (§3).

ARRIVAL, DEFINED: the walk ends when the route's target transition
reads reached — the target (screen, region) outdoors, the target
room indoors. For an ENTRANCE target (a cave mouth, a dungeon
door) the walker stops at the entrance tile and does NOT step
through: stepping in is an interaction, and interactions belong to
the player (Blind Soldier's door rule, applied to thresholds).

COSTLY LEGS STOP THE WALK (direction-only is not action-free):
walking alone can hop a ledge (no way back up), fall through a pit
to the floor below, open a locked or big-key door and SPEND the
key, or push a block into a wedge. The walker therefore stops
BEFORE any leg whose data marks it costly or irreversible — the
route graphs already classify pit, locked, big-key and ledge legs
— speaks what the leg does ("Locked door ahead: it will spend a
key."), and waits for a fresh WalkToggle press to walk that one
leg. Push blocks: ALttP keeps them in a live table, not in the
room data. `movable_block_datas` at `$7E:F940` holds 99 slots of
{u16 room, u16 tilemap}; the game rebuilds it on every dungeon
entrance and rewrites a slot when a block falls through a pit, so
the room below inherits it. `oga::AlttpPushBlocksInRoom`
(`Core/alttp_walk.h`) counts a room's blocks from that table and
returns their 16px tiles; it answers -1 (unknown) outdoors, when
unattached, and when the table reads all zero. The rule for the
indoor walker: a room with any push block, or an unknown answer,
is a costly leg — stop before entering it and wait for a fresh
WalkToggle, exactly like a ledge. The walker is still overworld
only, so the census has no steering caller yet; DumpState logs
`pushblocks=N` for the current room so a bench run can check it
against a real dungeon.


## Which adapters, in which order

- **ALttP first, and alone until it lands.** It is the only adapter
  with all four ingredients already proven: routes from the game's own
  geometry (overworld (screen, region) graph, dungeon room graph with
  door kinds), live position and module/dialogue gates the walker can
  re-validate against, a Guide that re-plans from the live position,
  and a bench walker that has already driven the hardest steering
  case (the Palace climb) end to end.
- **DQ9 is a plausible second, at smaller scope**: it has no routes,
  but its nearby scan selects a target and speaks step directions to
  it; "walk me to the selected nearby object" is a one-leg walk. It
  waits until ALttP's contract has survived contact with a real game.
- **The PSP adapters are not candidates yet.** Their guides give
  bearings, not routes, and PSP movement is analog (psp_set_analog
  exists, but a direction verb for analog is a design of its own —
  magnitude, camera-relative steering). Bearings-first games get
  auto walk only after a guide grows routes.
- **Fire Emblem is explicitly out.** Its input shape is discrete
  cursor taps on a grid, not held directions; its harness already
  drives keys for cursor movement, which is a different feature
  (query answering, not route following) with its own record.

## Proof required before any app exposure

Bench first, like the snes_probe walkers; the app button
(supported()) is the LAST thing added, after every proof below is
green. ALttP proofs run on COPIES of the golden save — the fixture
dir is never a proof target.

1. Host-stub unit tests for the walk state machine: every release
   condition in §6 fires and releases; the walker never calls
   set_button; an unrefreshed direction expires.
2. Bench arrival proofs: Link's house → Kakariko Village along the
   spoken Guide route, and the Eastern Palace climb route, both
   ending in the arrival state with the walker idle. The harness
   logs every direction write; the log is asserted to contain no
   face-button write (there is no verb that could make one — the
   log is the belt to the interface's braces).
2b. A steering corpus, not just two routes: every gap edge in both
   worlds walked from a bench-set start (teleporting to a leg start
   is a bench privilege; the app never teleports), a sample of
   random start/target pairs per world, and the negative cases — a
   wrong starting region, a death mid-walk (the death/continue
   prompt is a named case: a menu opening in the same frame as a
   held direction is the known leak shape), a scroll in progress.
   Release timing is MEASURED in end-to-end frames of leaked input
   and the number documented — "released the same frame" is a
   claim the SNES's vblank pad sampling does not let us make for
   free. The expiry itself gets a per-core bench test: stop
   refreshing, and Link must stop within the grace frames, on every
   core that offers the verb — a host-stub test does not test the
   core's clearing.
3. Bench interruption proofs: a menu opened mid-walk (directions
   released the same frame; the menu then works normally); dialogue
   triggered mid-walk (released before the dialogue can be advanced
   by a held button — the walker holds no button that advances it,
   and the proof shows the release anyway); a player press injected
   mid-walk through the player_pad feed (cancel within one frame);
   a wall block on a leg (stuck release inside the window, one line).
4. Regression: scripts/adapter-tests.sh, scripts/systems-test.sh and
   scripts/snes-alttp-proof.sh all green; the spoken Guide, Objective
   and Where Am I outputs byte-identical when no walk is active
   (auto walk must not change what the existing commands say).
5. App-level proof, separately: the bench proves core + adapter.
   The Swift glue, the player feed, the core arbitration and the
   per-console direction mapping each need their own verification,
   and every core either implements the verbs or the adapter
   reports walking as unsupported on that core — never a button
   that silently does nothing.

## Open questions (for the design consult)

1. set_direction as a new verb vs disciplined reuse of set_button
   with a direction-only helper inside each adapter. The verb costs
   one appended Host member per the ABI rule and a mapping in every
   core; the helper costs nothing and enforces nothing.
2. Who owns expiry: the core clearing before input sampling, or the
   host glue refreshing a lease? 120 ms at 60 fps is ~7 frames — is
   that the right grace, and does any core sample input off the
   frame loop (sub-frame polls) in a way that breaks the model?
3. Progress speech: query-only, or spoken at coarse milestones
   (quarters) by default? Sound-first says the game already sounds
   walking; it does not sound "halfway there".
4. Player override silent vs one short line. Blind Soldier is silent
   (it just releases); a bumped button on a touch controller is
   easier to make than on a keyboard.
5. One re-plan per leg vs none. ALttP's router makes re-planning
   cheap and honest; it also makes the walker's failure surface
   larger (a re-plan can itself be wrong in a way a stop is not).
6. WalkToggle as its own command vs Guide-pressed-again semantics.
   A separate command is clearer in the app list; a repeated Guide
   press needs no new ABI value but overloads a query with an
   action — queries in this repo never mutate play today.

## Consult addendum

(Added after the BlindPilot critique; each point is marked accepted
or rejected with the reason, and the text above is revised where a
point was accepted.)

### Consult record — BlindPilot session, 2026-11-22

The critique ran critique-only against a packet containing this
doc, Core/adapter.h, Core/alttp_graph.h and the SNES pad numbering.
Verdicts (the text above already reflects every ACCEPT):

ACCEPTED, and the design is changed by them:
- **Direction-only is not action-free.** Ledges, pits, key-spending
  doors, entrances and push blocks are all walk-into-able. The
  costly-legs stop (§6) is now a load-bearing part of the contract,
  keyed on the leg kinds the graphs already carry. Push blocks are
  recorded as the known remainder, gated on a room-data flag that
  does not exist yet.
- **Steering must path at tile level** over the assembled grid with
  a collision-shrunk mask and hazard exclusions (§5). The region
  geometry stays what speech uses. The stair treads (0x22) are
  named as steering tiles speech never needed.
- **Stuck/progress measured along the path**, never straight-line
  (§5); the ~90-frame grind concern is answered jointly by that and
  the health gate.
- **Health gate** (§6): release on any health decrease or on
  stun/knockback; refuse to start at the low-health threshold.
- **Arrival defined** (§6): target transition reached; entrances
  stop one tile short and are handed back.
- **Expiry in frames (3), owned and cleared by the core** before
  input sampling (§2); release timing must be MEASURED in leaked
  frames, not claimed as "same frame" (proof 2b); expiry gets a
  per-core bench test, not only a stub test.
- **Arbitration in the core** (§3): a physical press zeroes the
  adapter layer immediately; the feed only notifies the adapter.
  The feed latches presses between frames.
- **The verb's guarantee made structural** (§1): walking adapters
  get a Host view with set_button nulled; null verb or null feed
  refuses the walk; new members go after ctx, truly last.
- **Progress in route units on query** ("two screens to go"), no
  percentages; unasked milestone speech demoted to a per-game
  setting, default off (§5).
- **Re-plans limited** (§6): same leg target only, recoverable
  displacement only, no new costly legs, cap three per walk,
  "Re-routing." spoken once each.
- **WalkToggle stays its own command** (Guide's presses already
  cycle targets — Guide-again was never actually available), plus
  the spoken destination on start and an off-by-default per-game
  setting with a one-time explanation (§4).
- **No redundant cancel lines**: a cause that announces itself
  (dialogue) gets no line over it (§6).
- **Warp-screen denylist** (Lost Woods) (§6); **steering corpus +
  negative cases** and **app-level proof** added to the proof list
  (2b, 5); **tile paths computed only while walking, cached per
  screen** (§5).

BUILT (2026-11-24):
- **A non-speech stop earcon** (consult Q3/Q4): the player gets a
  sound, not silence, whenever a walk that was moving stops —
  arrival, WalkToggle off, player override, dialogue, harm, and the
  stop before a costly leg. A refusal to start, or a stop before
  the first step, makes none (nothing was moving). ALttP now has a
  `cue_snapshot`: its only field is the one-frame
  `CueSnapshot::walk_stop`, carried as bit 16 of
  `poke_cue_health` (and of the Android PSP-path poll). The synths
  own the recipe: one short 392 Hz tick, gated only on game audio
  being on, not on battle cues. Speech lines are unchanged.
  Not yet heard on a phone.

REJECTED, with reasons:
- **Enemy-radius release for ALttP** (consult point 4's third
  limb): no measured sprite/enemy census read exists in the ALttP
  adapter — there is no Enemies command for this game — so the
  gate would need new reverse engineering before it could refuse
  honestly. Health decrease + knockback/stun (both readable from
  Link's own state today) carry the safety the radius was for.
  Revisit if an ALttP sprite census is ever measured.
- **A struct size/version field for Host** (consult Q1): the
  repo's established detection is the null check — hosts and stubs
  that predate a member leave it zero, and §1 already makes null
  a refusal. A version field is a second mechanism for a problem
  the first one covers; if a third input-era member is ever
  proposed, the question reopens.

The consult also verified two facts the design now leans on: the
SNES samples the pad at vblank (hence measured, not claimed,
release timing), and Guide presses in ALttP already cycle landmarks
(hence WalkToggle could never have been Guide-again).

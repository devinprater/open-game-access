# A Link to the Past — routes, the Dark World, and dungeons

Design for the follow-on work named in docs/plans/alttp-reader.md: real
routes instead of compass bearings, guidance and objectives in the Dark
World, and room-to-room routes inside dungeons.

The rules that govern everything below are the house rules from
docs/design/alttp-guidance.md: refusal over guessing (a route is spoken
only when the data derives it; otherwise the player hears the honest
fallback, never silence and never an invented path), answers arrive after
ready(), player answers are High priority, and no game text or ROM bytes
are ever committed — only derived facts, through generated tables, the
pattern Task 10 set with scripts/gen-alttp-rooms.py.

No new commands are added. Guide, Objective and WhereAmI carry all three
phases, so the adapter's command table and the Swift side do not change.

## Phase A — the overworld screen graph and spoken routes

### Where the graph comes from

The graph is derived at build time from the zelda3 decompilation's data —
the same ground truth the room database came from — by a generator script
(scripts/gen-alttp-graph.py) that writes a generated table
(Core/alttp_graph.cpp). The script reads no file that is not already in
the decomp tree or the one ROM dump the adapter supports, and it asserts
that dump's payload CRC (777aac2f) before deriving anything.

Per screen (the overworld is an 8 by 8 grid of screens per world, screen
indices 0 to 159 including the special areas), the game assembles its
tiles exactly this way (src/overworld.c,
Overworld_DecompressAndDrawOneQuadrant), and the generator mirrors it:

1. The screen's two compressed streams (kOverworld_Hibytes_Comp and
   kOverworld_Lobytes_Comp; pointer tables at ROM 0x82F94D and 0x82FB2D)
   are decompressed with the decomp's own algorithm (assets/util.py,
   decomp()) into 256 map32 tile ids.
2. Each map32 tile expands to four map16 tiles through the map32-to-map16
   table (assets/map32_to_map16.txt), giving a 32 by 32 map16 grid per
   screen.
3. Each map16 tile's four map8 subtiles and their attribute bytes come
   from the ROM tables at 0x8F8000 and 0x8E9459 — the same tables
   scripts/alttp-walkability.py already reads — and a tile counts as
   enterable when any of its subtiles carries a behaviour in the game's
   walkable set (TileBehavior_NothingOW in src/tile_detect.c), the
   criterion the walkability script established.

### Nodes and edges

Nodes are area ids — the value the adapter already reads at 0x7E040A:
0x00 to 0x3F in the Light World, 0x40 to 0x7F in the Dark World. A small
area owns one grid cell; a big area owns four (its base cell, +1, +8,
+9). Ownership comes from the game's own area-head table (ROM 0x82A5EC,
the is_area_head test in the decomp's resource compiler), cross-checked
against the size field of the overworld YAMLs; the generator refuses to
emit a graph unless the 64 cells of each world are partitioned exactly
once.

An (undirected) edge joins two areas when, along some shared cell border,
a position exists where the border tile on both sides is enterable — you
can walk across there. Ledge tiles are the exception: a ledge behaviour
on the border makes a one-way edge in the drop direction (you can jump
down; you cannot climb back). Deep water and every other non-walkable
behaviour never form an edge: routes are walking routes. Where no route
exists between two areas, Guide falls back to the plain bearing it speaks
today — the graph never turns an answer into a refusal.

The generated table holds, per area, its neighbours with a direction and
a one-way flag. No tile data, layout or text ships — only the derived
edge list, with the generator's statistics (edge counts, one-way count,
unreachable areas) printed at generation time and spot-checked in tests
(Link's House reaches Kakariko and the Eastern Palace grounds; the
Pyramid of Power is reachable in the Dark World graph).

### What a spoken route sounds like

Guide keeps its behaviour — first press aims at the current objective,
further presses cycle the landmarks — and its answer becomes a route when
the graph derives one:

"Guide: Eastern Palace. Route: two screens east, one screen north, then
east into the palace grounds."

Legs are counted in screens, consecutive legs in the same direction are
merged, and the final leg names the arrival. When the player already
stands in the target area, "You are here" stays. When no route exists,
the bearing answer is spoken unchanged ("north-west, about three screens
away"). Indoors, guidance still refuses on the overworld graph — Phase C
gives the indoors answer its own graph.

### The Dark World landmark set

Guide cycles the current world's landmarks. The Dark World set, all of
them areas the name table already knows: Skull Woods (0x40), Ganon's
Tower (0x43), Turtle Rock (0x47), Thieves' Town (0x58), the Pyramid of
Power (0x5B), the Palace of Darkness grounds (0x5E, the hedge maze area),
Misery Mire (0x70), the Ice Palace shore (0x75, the frozen Lake Hylia)
and the Swamp Palace marsh (0x7B). The first press aims at the Dark World
objective (Phase B). The old refusal ("Guidance is not mapped for the
Dark World yet") is removed; an unrouted pair falls back to the bearing,
exactly as in the Light World.

## Phase B — Dark World objectives

The light-world stages were measured from progress bytes (Task 9); the
dark-world stages are measured the same way, from bytes the game itself
maintains:

- sram_progress_indicator (0x7EF3C5). The game sets it to 3 when Agahnim
  falls — PrepareDungeonExitFromBossFight (src/dungeon.c) sets it as the
  boss exit lands Link in the Dark World, and toggles
  savegame_is_darkworld with it. It is the game's own dark-world phase
  marker. (The pendants-stage public save reads 2, so the marker is
  distinguishable on real saves.)
- link_has_crystals (0x7EF37A), one bit per crystal dungeon, the bit
  assignment measured from kDungeonCrystalPendantBit (src/zelda_rtl.c):
  Misery Mire 1, Palace of Darkness 2, Ice Palace 4, Turtle Rock 8,
  Swamp Palace 16, Thieves' Town 32, Skull Woods 64. The game's own
  all-seven test is (value & 0x7F) == 0x7F (src/ancilla.c).

The stages:

1. Progress below 3: the existing light-world stages, unchanged.
2. Progress 3 with crystals outstanding: "Objective: rescue the seven
   maidens. Three of seven crystals held. Next: the Palace of Darkness."
   The next dungeon is the first incomplete one in the dependency order
   the game itself imposes — Palace of Darkness, Swamp Palace, Skull
   Woods, Thieves' Town, Ice Palace, Misery Mire, Turtle Rock — and the
   target area is that dungeon's entrance area (the Dark World area its
   entrance stands in, by the mirror rule the name table already
   reflects: 0x5E, 0x7B, 0x40, 0x58, 0x75, 0x70, 0x47).
3. All seven crystals: "Objective: confront Ganon. Ganon's Tower stands
   on Death Mountain." Target 0x43.

A bearing (or route, Phase A) is given only when the target area is in
the same world the player is standing in; otherwise the goal is named
without a direction, the same honesty rule Objective already uses
indoors. The stage is chosen from the progress bytes, never from the
player's current area, so standing in the Dark World early (a portal, the
mirror) cannot produce a wrong stage.

## Phase C — dungeons: the room graph and indoor routes

### Where the graph comes from

A second generator (scripts/gen-alttp-dungeon-graph.py) extends the Task
10 pattern over the same room database (assets/dungeon/dungeon-*.yaml in
the decomp tree) and emits Core/alttp_dungeon_graph.cpp: per room, its
door neighbours, plus the rooms that hold each dungeon's points of
interest.

Door edges. Each room's YAML lists doors on its layers ({type, pos, dir}
per door; the binary form the resource compiler writes is byte0 =
dir | pos<<4, byte1 = type). The direction encoding and the neighbour
math are the game's own (src/dungeon.c, Dungeon_CheckAdjacentRoomsForOpen
Doors and the Dungeon_StartInterRoomTrans family): the room grid is 16
rooms wide; dir 0 is the north wall (neighbour room -16), 1 the south
wall (+16), 2 the west wall (-1, when the room is not in column 0), 3 the
east wall (+1, when not in column 15). Door types that connect rooms
(src/dungeon.h): the regular and shutter families, invisible doors, warp
doors, slashable doors, and — flagged with their cost — small-key doors
(locked) and breakable walls (bombable). Entrance and exit door types
are not room-to-room edges; they mark the room as a dungeon entrance.
Stair-mask door types are not edges either: stairs are taken from the
room header's stair and pit destinations (stair0 to stair3, hole0; each a
[room, quadrant] pair, room 0 meaning none), which name their landing
room directly.

An edge ships only when both rooms agree: the neighbour must list a
passage door on the opposite wall (or name this room as a stair or pit
destination). The game's own transition code pairs doors across a wall
the same way. One-sided doors are counted and printed by the generator;
a dungeon left disconnected by the rule is a finding to report, not a
reason to quietly relax it.

Points of interest per dungeon component (connected components of the
room graph): the entrance room(s), the big-chest room (the RoomNote data
already names it), and the boss room — kDungeonExit_From
(src/dungeon.c) lists the twelve boss rooms {200, 51, 7, 32, 6, 90, 41,
144, 222, 164, 172, 13}, cross-checked against the boss sprites in the
room YAMLs (room 200 carries the Armos Knights, room 0 is Ganon's room
and is added by sprite evidence).

### Speech indoors

Guide indoors stops refusing and routes within the current dungeon:
targets cycle between the big chest (when the dungeon has one), the boss
room, and the entrance (the way out). A route speaks in doors and stairs,
not screens:

"Guide: the big chest. Route: north through the door, east through the
door, then north up the stairs."

If the chosen route crosses a locked or bombable door, the leg says so
("north through the locked door") — a route that spends a key the player
may not have must never sound free. Pathfinding prefers free edges, so a
costly leg appears only when no free route exists. WhereAmI keeps its
room note; Objective indoors keeps naming the goal without a bearing.

### The live proof

Room notes are unit-tested only today because the harness could not
steer into a dungeon. The proof closes that gap by steering, not by
teleporting: the probe's eight-way walker, given the Phase A graph and
the per-screen walkability paths (the method scripts/alttp-walkability.py
prototypes), walks from Link's house to the Eastern Palace entrance on
the restored save's copy — the save sits at the pendants stage with a
Fighter's Sword, and the palace is the first pendant dungeon — enters,
and the proof asserts the adapter's own words indoors: a named dungeon, a
room note, and a spoken Guide route to the big chest or the boss room.
The checksum-fixed high-heart fixture from the memory map keeps the
walker alive on the road. If steering defeats the attempt, the phase
lands with the gap documented exactly, as Task 10 did — that is the
floor, not the goal.

## Verification, per phase

Each phase lands on its own commit, merged to main when green:
scripts/adapter-tests.sh, scripts/systems-test.sh and
scripts/snes-alttp-proof.sh pass, the ALttP adapter test count only
grows, and the live proof for the phase is quoted in the running record
(~/workspace/oga-queue/alttp-routes-darkworld-dungeons-2026-11-22.md).
Phase A extends the --walk live proof with a spoken route; Phase B uses
an SRAM-crafted save at progress 3 (the slot checksum rule is measured:
the first 0x280 words sum to 0x5A5A, checksum word at slot +0x27E) to
prove the dark-world Objective and Guide answers live; Phase C proves
indoors as described above.
## Consult addendum (2026-11-22, BlindPilot critique)

The design above was critiqued by the BlindPilot Claude session before any
code was written. What changed because of it:

- **Regions.** A screen can be split into disconnected walkable regions,
  so border edges alone can invent a route. The generator flood-fills each
  screen's map16 grid; every border gap records its region; routing runs
  over (cell, region) states and is region-consistent through every
  intermediate cell. The adapter cannot know which region of the start
  cell Link stands in, so a route is spoken only when all of the start
  cell's regions agree on the first leg — otherwise Guide speaks the
  bearing fallback. An edge needs a run of at least two adjacent
  enterable border positions on both sides (Link's collision width), and
  each gap records which third of the border it sits in.
- **Fallback honesty.** In Guide, an unrouted answer says so: "No walking
  route known; it lies north-west, about three screens away." A bearing
  is never dressed as a route.
- **Ledges** are directed edges, treated as costly: pathfinding prefers
  routes without drops, and the leg says "hop down the ledge; no way back
  up".
- **Indoor speech.** Consecutive same-direction doors merge ("north
  through two doors"); when the exit wall holds two or more passage
  doors, the leg names the half ("the west door"); stairs are spoken as
  "take the stairs" — the data does not say up or down. A route that
  spends a small key says so, and adds "you have no key" when the player
  holds none. Rooms whose header tags gate their doors behind combat or
  switches are flagged in the route ("the way is gated").
- **Dungeon invariants.** The dungeon generator prints, per connected
  component, whether an entrance reaches the boss room and the big chest
  with all edge types allowed, the two-way symmetry count, and one-sided
  doors classified by cause (diagnostics only — only both-sides doors,
  stairs and pits ship). Pits are a one-way edge class of their own.
- **Phase B wording.** No authoritative "Next:". Objective speaks the
  crystals held, the remaining dungeons, and the nearest remaining one by
  graph distance; Guide's first press aims at it. The final stage names
  Ganon's Tower as the way to Ganon. (The consult's medallion claim was
  checked against the decomp: vanilla fixes Ether to Misery Mire and
  Quake to Turtle Rock; Objective never names medallions, so nothing is
  hard-coded.)

Deferred, deliberately: route-progress announcements (Guide recomputes
per press), per-door opened state and finished-target skipping (needs
live measurement of the per-room SRAM state bits), cave/portal edges
between overworld areas (the generator prints unreachable landmarks so
the gap is measured), and a words-only executor proof. The Lost Woods'
looping transitions are a documented limitation: its border edges are
physically walkable and entry routes stay valid.

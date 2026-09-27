# Dissidia stage guide (host-captured, ULUS10437)

Twelve Quick Battle loads, WoL (HP 1000) vs CPU Firion (HP 1368),
one live battle per fixed stage plus the Random draw. Stage identity
per run comes from the Battle Setup screenshot (value text), never
from model guesses. Opponent Info confirms Firion + BGM each time.

## The stages

1. Random: "?" icon, no preview. Observed draw: brown hexagonal rock
pillars over beige mist, purple cloudy sky, rift portal, reddish
cliff wall. Resembles the Crystal World sector but unconfirmed.
Blind note: with Random you learn the stage at load, never before.

2. Old Chaos Shrine (FFI): flat checkered gray stone ruin, single
level, no platforms. Low broken walls open to the void; distant
dark mountains and pyramid shapes under a violet storm sky.
Landmarks: shattered stone structures.

3. Pandaemonium (FFII): small enclosed flat stone cell. Gray cracked
floor, tall purple honeycomb-pattern walls on every side. No
platforms, no void, no hazards. Tightest arena: fights stay close.

4. World of Darkness (FFIII): enclosed temple interior. Green-blue
reflective square-tiled floor, rows of massive cylindrical pillars
with blue diamond pattern receding into dark. No void. Moody lighting.

5. Lunar Subterrane (FFIV): flat gray stone floor with blocky stumps
and tall pillars, single level. One raised edge drops to the void.
Scattered blue lights on the ground. No hazards.

6. The Rift (FFV): floating castle platforms in bright blue sky.
Brick battlement, distant tower, purple portal glow, grassy rooftop
arena, orange spires, black sky-rift. Platforms at different heights:
multi-level. Open sky, no walls.

7. Kefka's Tower (FFVI): enclosed magitech interior. Flat tiled floor
with metal grate sections, maze-carved brown walls with inset blue
lights, giant urns. Rear staircase climbs to an upper walkway: two
tiers. No void. Most enclosed multi-room feel.

8. Planet's Core (FFVII): floating stone-tile arena in a green
auroral void. Main floor plus overhead floating rock platforms
(fighters perch above: real verticality). Dark block monoliths sit
on the floor as obstacles and cover. Open void edges.

9. Ultimecia's Castle (FFVIII): dark gothic temple interior. Flat
floor, tall pillars, wooden upper bridges: multi-level. Enclosed
walls, no void. Dim purple-lit mood.

10. Crystal World (Dissidia-original): floating brown-topped rock
pillars over a cloudy void. Stormy purple sky with a large moon or
vortex. Giant ribbed red crystal wall: the orientation landmark.
Multi-level platforms, open void.

11. Dream's End (Dissidia-original): circular stone tower over a
glowing orange lava abyss under a starry night sky. Tall carved
central pillar, smaller detached floating rock platforms nearby.
Open edges, no walls. The lava is scenery below, never touched.

12. Order's Sanctuary (Dissidia-original): flat endless frozen lake.
Dark stormy sky, lightning, jutting ice shards, water reflections.
No platforms, no levels, no walls. Simplest geometry of the twelve:
nothing to trip on, nowhere to fall inside the arena.

## Battle-wide facts (seen in these captures)

- Movement is free 3D: fighters walk, run, dash fast across gaps,
and fight airborne as much as grounded. Launched players get an
"X: Aerial recovery" prompt. Camera follows and pulls back with
distance. This is the "zoom around the stage" part: no separate map
or zoom mode exists; traversal is the movement itself.
- Triangle in battle is Quickmove (Map Action): wall runs, rail
grinds, and dash-jumps between platforms, but ONLY while the yellow
target marker shows. Probed twice with no marker in view: no effect,
no overlay. The blue guard bubble seen in one capture was the CPU
guarding on its own; guard is a separate mechanic (front-only
blocking, timed blocks stagger for a counter opening).
- Lock-on shows as a blue reticle ring over the opponent: the visual
anchor for a centered audio beacon.
- EX Cores spawn mid-fight ("EX Core in play!" banner, glowing orb
visible): touch one for EX Force toward EX Mode.
- NO stage hazards observed in any of the twelve: no damaging lava,
no traps, no moving parts. Edges are void/ring-out boundaries.
- Defeat plays a "DEFEATED..." KO card with the loser's line as a
subtitle, then the post-battle menu: Return to Battle Setup,
Rematch, Return to Character Selection, Return to Start Menu.
- Opponent Info also names the BGM (e.g. FFII Battle Theme 1).

## Methods (harness lessons, verified the hard way)

- psp-capture does NOT create its output dir. No mkdir means
"Error opening file for write" and zero saves. Always mkdir -p both
dirs before running. (This cost two full chain attempts.)
- Tap cycles WRAP past their end (verified 11/11: every setup walk
overshot by exactly 2 until accounted for). Total taps =
frames/period + 1, tap@0 fires. Size frames to fit or count the wrap.
- When a tap and an interval shot share a frame, the shot sees the
PRE-tap state.
- Keep the trailing `-- 0 8` args on every invocation.
- Never trust model fighter/stage names: WoL's horned helmet reads
as Garland, Golbez, Gilgamesh, or Kain, and stages get lore-matched
to the wrong name. HP totals (player 1000, Firion 1368) and the
setup-screen value text are authoritative.

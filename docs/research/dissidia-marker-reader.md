# Dissidia triple-chevron marker: occlusion tracker, not (only) Quickmove

## What the art is

Three stacked yellow triangles / chevrons. Proven by captures to be an
**off-screen / occluded position indicator**: it shows where a fighter is
through a wall when camera or geometry hides them.

- `mx-wdash/fb-000450`: camera clipped behind a wall, walked fighter
  occluded; chevrons float on the pink wall marking their position
  through it.
- Battle openings (`mx-wpin/fb-000030`, `mx-widle/fb-000030`,
  `mx-o6/fb-000030`): camera still settling, one fighter briefly
  occluded or off-frame; chevrons flash on a wall / over the void, gone
  by ~frame 60-90.
- 70+ mid-battle frames with both fighters visible: never present.

## What it is NOT (proven)

- NOT reliably an actionable Quickmove marker: pressing Triangle
  (exact one-shot input, frames 28-35) inside the marker window produced
  no wall-run, normal dash only (`mx-wtri2`).
- The manual's Quickmove marker shares the same triple-triangle art, so
  an actionable marker looks identical; none has been captured yet.

## Negatives catalog (other yellow)

EX Cores, WoL white cape / gold horns+trim (tracks player model), stage
lights and orange decor chevrons (static), CRITICAL text, damage
numbers, projectiles, hit flashes, floor tiles, platform tops, HUD
digits. Blue lock-on reticle/arrows are separate (enemy location).

## Reader design (v1, honest)

Update 2026-09-30: battles stay silent except QTE prompts (player rule),
so chevron sightings no longer announce; the detector spec below is kept
for the edge/latch path, which stays silent.

Announce chevron appearance with screen direction: "Marker left" =
someone is behind geometry that way. Usually the opponent (player is
usually the visible one); when the camera is clipped it can be the
player's own fighter. Identity disambiguation is open.
Do NOT claim a wall-run is available: Triangle test failed.

## Detector spec (app side)

1. Mask yellow (R>200, G>170, B<130), ignore bottom HUD band.
2. Frame-diff for NEW clusters (all static decor drops out).
3. Shape: ~3 stacked triangular blobs; position = announcement
   direction (left / right / edge).
4. On appear send `QuickOn`; on disappear send `QuickOff` (silent).
   Adapter once-rule covers repeats. Both commands exist and are tested.

## Open

- Who is occluded (opponent vs self vs EX Core) per sighting.
- Actionable Quickmove marker capture (near-element + dash contexts).
- Swift framebuffer-pixel access for the live sampler unverified.

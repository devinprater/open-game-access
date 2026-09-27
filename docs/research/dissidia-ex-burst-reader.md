# Dissidia EX Mode / EX Burst reader: QTE tables and detector spec

## Identity (portrait-pixel proof, 2026-09-27)

Quick Battle pair = player GARLAND, CPU Firion Minimal. 2x-upscaled
HUD portrait crops: left = horned demon helm + white mane = Garland
("not the silver knight WoL"); right = red bandana + bare face =
Firion. Earlier full-frame "WoL" labels were VLM noise (two
horned-helmet fighters confuse it). The farm below is Garland's
gauge; the Burst target is his Soul of Chaos.

## Flow (input-echo + detector, no RAM)

1. Gauge fills (EX Force orbs, EX Cores 1/2/4 wings). FULL = edge pillar
   bar turns yellow (filling = purple). YELLOW-FULL PHOTOGRAPHED on
   Firion's gauge (2x edge crop: full yellow-gold bar); purple grows
   from the bottom with a gold needle alongside.
2. Detector sees yellow in gauge ROI -> `ExReady` -> "EX Mode ready."
   (once per fill; `ExSpent` re-arms silently.)
3. Player presses R+Square. App knows (it sends inputs) -> `ExActive` ->
   "EX Mode on." Regen runs, gauge drains (~20s, gear-dependent).
4. In EX Mode, landing an HP attack shows the Square popup. Detector ->
   `ExBurstGo` -> "Press Square now!" (popup visual unvalidated).
5. Player presses Square -> Burst minigame. Detector classifies each
   prompt -> `ExQteUp/Down/Left/Right` or `ExQteCircle/Square/Triangle/
   Cross` -> speaks "Up!" etc. Every prompt speaks (SayRaw bypasses the
   once-rule: "Up! ... Up!" are separate timed steps).
6. Burst ends EX Mode -> `ExEnded` -> "EX Mode over."
7. Perfect (all inputs) = full damage; miss = weaker finisher. Either
   way EX Mode ends.

## QTE tables (original Dissidia, not 012)

### Garland: Soul of Chaos (MASH Circle, timed meter) <- OUR PLAYER
- Garland knocks the foe back, raises his sword, charges evil power.
  MASH Circle to fill an on-screen meter within a time limit.
- Power levels with visual distortion + CHIMING SOUND per level: no
  levels = 1 attack, up through twin swords -> chain -> spear -> axe
  -> spinning two-blow finisher.
- Reader: ExBurstGo = "Mash Circle now!"; level chimes are natively
  audible (blind-accessible as-is); counting levels via meter steps or
  chime onsets through the app's PSP audio tap is future work; v1 also
  gives a timed "Stop." at the limit end.
- Garland EX Mode = Class Change: CAPE TURNS WHITE with runic edges
  (major visual flag for the Active detector) + Indomitable Resolve
  (no flinch during his own attacks).

### Reference: Warrior of Light Oversoul
- SIX d-pad directions, shown one at a time in a center-screen box.
- Analog stick REFUSED. React fast, press shown direction.
- Miss one -> skips to the final slash (weaker).
- Last input is ALWAYS Left. Quote: "I give my all - to this sword!"

### Firion: Fervid Blazer <- OUR CPU OPPONENT
- FIVE commands in shown order within a time limit: THREE d-pad
  directions + TWO face buttons. Lance, daggers, axe, staff, sword,
  then bow finale. Miss = weaker.

### Other 20 (slots; same command set covers all)
Onion Knight, Cecil, Bartz, Terra, Cloud, Squall, Zidane, Tidus,
Emperor, Cloud of Darkness, Golbez, Exdeath, Kefka,
Sephiroth, Ultimecia, Jecht, Kuja, Gabranth. Timing-style Bursts
(Jecht, Tidus, Tifa-style) need a rhythm cue, not a direction cue:
open design (metronome ticks at the hit moment).

## Detector spec (app side)

- Gauge ROI: far-left / far-right edge pillars (player side only).
  Yellow = full. Geometry validated (purple quarter-full bars visible
  in walk captures); yellow-full since PHOTOGRAPHED on Firion's gauge
  (attack farm, 2x crop).
- Burst popup + QTE arrow/face-icon classifiers: unvalidated. Arrow
  templates (4 d-pad directions, white on center box) + face-button
  icons need capturing from a real Burst (requires EX Mode + landed HP
  attack, scripted-attack work open).
- Fallback honestly built in: support abilities Auto EX Burst / Auto EX
  Command exist in-game (Lv15, CP cost); the reader can suggest them if
  QTE classification is unavailable.

## Captured in the attack farm (Garland vs Firion Minimal, 2026-09-27)
- EX MODE ENTRY UI: right-side banner `EX MODE!` (white on blue-silver)
  + top-center box `Blood Weapon equipped!` — Firion entered EX Mode
  organically on a full yellow gauge. Reader targets for Active/BurstGo.
- Firion in EX Mode did NOT Burst: the kill (~frame 6000) was a plain
  983-Brave HP hit, no cinematic. Minimal AI entry does not imply Burst.
- FARM FINDING: getting BROKEN drains the EX gauge. Garland's purple bar
  collapsed across BREAK frames (4200->4800, 4860->5400); white-cape pixel
  test (0.28 -> 0.05 after chord) confirms R+Square chords on a slivered
  gauge correctly do nothing. Mash top-offs (+150f, +600f) both ended in
  breaks first. A scripted player Burst needs a no-break topping window
  (guard-fill, core grab, or cheat) — open.
- Chord input itself verified (CAP-INPUT logs both buttons, same frame).

## Still open
- Garland EX entry: white cape + drain, via a no-break top-off.
- Soul of Chaos mash meter + level steps on camera.
- Burst popup + QTE arrow/face-icon classifiers for Simon-Says Bursts.

## Adapter surface (landed, tested)

ExReady/ExSpent/ExActive/ExEnded/ExBurstGo + 4x ExQte direction +
4x ExQte face (ABI 16-28), all battle-gated; QTE prompts use SayRaw.
8 new unit checks green. Swift mirrors all 13 as detector-driven.

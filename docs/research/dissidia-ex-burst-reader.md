# Dissidia EX Mode / EX Burst reader: QTE tables and detector spec

## Flow (input-echo + detector, no RAM)

1. Gauge fills (EX Force orbs, EX Cores 1/2/4 wings). FULL = edge pillar
   bar turns yellow (filling = purple).
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

### Warrior of Light: Oversoul
- SIX d-pad directions, shown one at a time in a center-screen box.
- Analog stick REFUSED. React fast, press shown direction.
- Miss one -> skips to the final slash (weaker).
- Last input is ALWAYS Left. Quote: "I give my all - to this sword!"

### Firion: Fervid Blazer
- FIVE commands in shown order within a time limit: THREE d-pad
  directions + TWO face buttons. Lance, daggers, axe, staff, sword,
  then bow finale. Miss = weaker.

### Other 20 (slots; same command set covers all)
Onion Knight, Cecil, Bartz, Terra, Cloud, Squall, Zidane, Tidus,
Garland, Emperor, Cloud of Darkness, Golbez, Exdeath, Kefka,
Sephiroth, Ultimecia, Jecht, Kuja, Gabranth. Timing-style Bursts
(Jecht, Tidus, Tifa-style) need a rhythm cue, not a direction cue:
open design (metronome ticks at the hit moment).

## Detector spec (app side)

- Gauge ROI: far-left / far-right edge pillars (player side only).
  Yellow = full. Geometry validated (purple quarter-full bars visible
  in walk captures); yellow-full NOT yet observed live (soak run
  pending).
- Burst popup + QTE arrow/face-icon classifiers: unvalidated. Arrow
  templates (4 d-pad directions, white on center box) + face-button
  icons need capturing from a real Burst (requires EX Mode + landed HP
  attack, scripted-attack work open).
- Fallback honestly built in: support abilities Auto EX Burst / Auto EX
  Command exist in-game (Lv15, CP cost); the reader can suggest them if
  QTE classification is unavailable.

## Adapter surface (landed, tested)

ExReady/ExSpent/ExActive/ExEnded/ExBurstGo + 4x ExQte direction +
4x ExQte face (ABI 16-28), all battle-gated; QTE prompts use SayRaw.
8 new unit checks green. Swift mirrors all 13 as detector-driven.

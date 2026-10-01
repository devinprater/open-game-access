# Audio Description (AD) — practices + Dissidia cutscene scripts

Date: 2026-10-01. Sources: Gov. of Canada AD checklist, 3PlayMedia,
AMI Accessible Media tiers, RVS Digital / W3C description writing tips,
Netflix timed-text AD guide.

## Best practices (distilled for game cutscenes)

1. **Describe what is seen, not what it means.** Actions, settings,
   clothing, expressions, on-screen text. No interpretation, no
   editorializing, no guessing at motives ("cold loyalty" is out;
   "he bows his armored head" is in).
2. **Present tense, concise.** Speak in the gaps: description must fit
   between dialogue lines, never over them. Fewer words win.
3. **Name the speaker when the game doesn't.** Dissidia's dialogue boxes
   have no speaker names — only portraits. AD must say "Chaos speaks"
   / "Garland answers" so a blind player can follow turn-taking.
4. **Voice on-screen text the game doesn't speak.** Narration cards and
   unvoiced dialogue must be read aloud — otherwise they don't exist
   for the listener. Mark dialogue as quote for clarity.
5. **Identify new people, places, and actions once, then refer back.**
   ("The same burning realm" — no re-description.)
6. **Note visual-only state changes.** Empty/active boxes, continue
   arrows, scene transitions ("A small arrow blinks: more to follow").
7. **Match tone, stay neutral.** Urgent scenes get tighter sentences;
   never sound bored, never upstage the scene.
8. **AMI tiers applied:** Tier 1 (who is in the scene) → Tier 2 (what
   they do) → Tier 3 (what it means for the story). Our scripts below
   cover Tiers 1–2; Tier 3 belongs in a plot recap, not AD.

## Dissidia scripts (Trunks save, Odyssey I intro)

Scene 1 — Opening narration card (white text on black void, typewriter):

> Opening. A black void, streaked with faint blue mist. Centered
> storybook text glows white, and types itself out, line by line. It
> reads: The world is shrouded in darkness. It seems that Chaos's
> shadows would engulf all... But light is not gone. The crystals,
> shining even in the depths of despair — The final line flares into
> white light, and the words are lost in the glow.

Scene 2 — Chaos speaks (realm of Discord, two dialogue boxes):

> The realm of Discord. A burning wasteland under a black and purple
> sky. Streaks of fire fall like rain, and jagged, lava-lit spires
> glow orange on a mirrored black plain. Two dialogue boxes frame the
> screen. Top left, half lost in shadow: a demonic face with glowing
> red eyes. This is Chaos, god of discord. Bottom right, in profile: a
> knight in ornate silver plate, a single yellow eye burning through
> his helm. This is Garland. Chaos speaks. Quote: The conflict will be
> brought to an end as soon as I regain my lost strength. End quote.

Scene 3 — Garland answers (same scene, turn passes):

> The same burning realm. The top box stands empty. The turn passes to
> the knight. Garland answers. Quote: Of course, my lord. End quote. A
> small arrow blinks: more to follow.

Audio renders (edge TTS, 2026-10-01) — re-render on script change:

- Scene 1: `tts_20261001_113201_643487.ogg`
- Scene 2: `tts_20261001_113204_374852.ogg`
- Scene 3: `tts_20261001_113206_585269.ogg`

Stills: `~/enemy-out/trunks5/step0.ppm` (narration),
`~/enemy-out/trunks7/step2.ppm` (Chaos speaks),
`~/enemy-out/trunks8/step2.ppm` (Garland answers).
Resumable states: `trunks5..trunks8/final.ppst` (same boot chain).

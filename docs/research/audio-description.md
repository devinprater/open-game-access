# Audio Description (AD) — practices + Dissidia cutscene scripts

Date: 2026-10-01. Sources: Gov. of Canada AD checklist, 3PlayMedia,
AMI Accessible Media tiers, RVS Digital / W3C description writing tips,
Netflix timed-text AD guide. Division of labor per Devin (2026-10-01):
dialogue boxes belong to the SCREEN READER; AD covers scene visuals.

## The split

- **Reader:** speaks every dialogue/narration box as `Speaker: line`
  ("Garland: Of course, my lord."). Dissidia boxes show no speaker
  names — only portraits — so the reader needs a portrait→speaker map
  per scene (open task, see TODO).
- **AD:** describes characters and environment ONCE per scene, plus
  visual-only beats (turn passes, box empties, continue arrow). Never
  re-describes a standing scene.
- **AD yields to voiced narration:** if a narrator is speaking, AD does
  not read the captions. Unvoiced text is the reader's job, framed as
  "Story text:" — AD only describes the card itself.

## Best practices (distilled for game cutscenes)

1. **Describe what is seen, not what it means.** Actions, settings,
   clothing, expressions, on-screen text. No interpretation, no
   editorializing, no guessing at motives.
2. **Present tense, concise.** Speak in the gaps: description fits
   between dialogue lines, never over them. Fewer words win.
3. **Name the speaker when the game doesn't** — via the reader's
   `Speaker: line` format, not inside AD prose.
4. **Voice unvoiced on-screen text** (reader's job); **skip captions a
   narrator already speaks.**
5. **Identify new people, places, and actions once, then refer back.**
   ("Same scene." — no re-description.)
6. **Note visual-only state changes.** Empty/active boxes, continue
   arrows, scene transitions.
7. **Match tone, stay neutral.** Urgent scenes get tighter sentences;
   never upstage the scene.
8. **AMI tiers applied:** Tier 1 (who is in the scene) → Tier 2 (what
   they do) → Tier 3 (what it means). Scripts below cover Tiers 1–2;
   Tier 3 belongs in a plot recap, not AD.

## Dissidia scripts (Trunks save, Odyssey I intro)

Scene 1 — Opening narration card (AD describes the card; reader reads
the text):

> Opening. A black void, streaked with faint blue mist. Centered
> storybook text glows white, typing itself out line by line. Story
> text: The world is shrouded in darkness. It seems that Chaos's
> shadows would engulf all... But light is not gone. The crystals,
> shining even in the depths of despair — The final line flares into
> white light, and the words are lost in the glow.

Scene 2 — Chaos speaks (scene established once; line in reader form):

> The realm of Discord. A burning wasteland under a black and purple
> sky. Fire falls like rain, and lava-lit spires glow orange on a
> mirrored black plain. Two dialogue boxes frame the screen. Top left,
> half lost in shadow, a demonic face with red eyes: Chaos, god of
> discord. Bottom right, a knight in ornate silver plate, one yellow
> eye burning through his helm: Garland. Chaos: The conflict will be
> brought to an end as soon as I regain my lost strength.

Scene 3 — Garland answers (standing scene: no re-description):

> Same scene. The turn passes to the knight. Garland: Of course, my
> lord.

Audio renders (edge TTS, 2026-10-01 v2) — re-render on script change:

- Scene 1: `tts_20261001_114729_482630.ogg`
- Scene 2: `tts_20261001_114731_979119.ogg`
- Scene 3: `tts_20261001_114733_982894.ogg`

Stills: `~/enemy-out/trunks5/step0.ppm` (narration),
`~/enemy-out/trunks7/step2.ppm` (Chaos speaks),
`~/enemy-out/trunks8/step2.ppm` (Garland answers).
Resumable states: `trunks5..trunks8/final.ppst` (same boot chain).

## DES01_001 — Destiny Odyssey I scene 1 (WoL vs Garland duel, 42 s)

Source: `DES01_001.PMF` (8.7 MB, 480x272, 42.1 s) extracted from the disc
image (`PSP_GAME/USRDIR/DATA/MOVIE/` holds 60 PMFs: OPN/END, DES00-10,
M_FF01-10, M_ST, M_TUTO, LOP, ANOTHER_EPISODE). Story scenes are
PRE-RENDERED video, not engine-rendered: the engine only plays them
(cf. decomp `talkevent/` strings are EBOOT-resident, not live state).
Voiced English dialogue is present; no subtitles are shown and no STT was
available, so lines are not transcribed — AD covers visuals only and yields
to the voices. Stills: `~/scframes/f01..f11.png` (1 frame / 4 s).

> A horned knight in black skeletal armor and a purple cape — Garland —
> looms in a golden doorway, a giant silver shield before him.
>
> The Warrior of Light in silver armor and a yellow cape leaps through a
> collapsing hall as carved stone faces crumble and rubble falls.
>
> Smoke fills the frame. Garland's horned shoulder passes; pink magic burns
> beside a distant tower.
>
> Red and silver fragments tumble through gray smoke.
>
> The Warrior charges down a red-carpeted hall, sword raised.
>
> Steel meets steel: his sword crashes against Garland's raised shield in a
> columned hall.
>
> Face to face. The Warrior levels a spear; Garland towers over him.
>
> Garland swings his greatsword; the Warrior answers with the spear.
>
> A dark warrior with a streaming yellow mane lunges, twin blades flashing.
>
> Garland raises a fist beneath a war banner, triumphant.
>
> Above a night city, the Warrior hurls himself shield-first at Garland.

Audio render (edge TTS): `tts_des01_001.ogg` — re-render on script change.

## DES06_001 — Destiny Odyssey VI scene 1 (Terra vs Kefka, 65 s)

Source: `DES06_001.PMF` (13.4 MB, 480x272, 64.6 s) from disc `MOVIE/`.
Film-AD rules applied (Ofcom/ITC + ITU-T T.701.21, 2026-10-02): describe
ONLY in non-dialogue pauses, never over dialogue or plot-critical music;
present tense; name characters early and reuse names; do not fill every
moment — let the scene breathe; prioritize plot-relevant visuals.
Voiced lines below are the scene's known dialogue (playthrough transcript
timestamps 6:08–10:32); exact PMF sync is DRAFT — the PMF carries video
only and the voices live in the data archive (AT3, future extraction for
sample-exact gap timing). Stills: `~/sc6b/h01..h13.png` (1 frame / 5 s).

Cast (named once): Terra — blonde ponytail, red top, white skirt, magic
glider. Kefka — white clown makeup, jester motley, shrieking laugh.

> Terra falls backward through a carved stone temple, arm outstretched.
> [gap: landing music]
> She dashes left across a dark stone arena, sleeves streaming.
> Kefka: "If you just let your powers take over... you would have made
> such a better toy."
> Above a rooftop, Kefka flings his arms wide, laughing.
> Kefka: "A coward that refuses to destroy anything is better off being
> destroyed by me. Come on — let's play!"
> Terra drops through a black industrial chasm past glowing orbs.
> Kefka lies flat on his back, motionless.
> [gap: he rises — scrambling pipes and valves behind him]
> Terra races on through the temple, determined.
> Kefka looms over her, grinning. Terra: "I can protect everything. I
> won't be defeated!"
> A glass cylinder glows on a carved pedestal; a masked figure floats in
> light while a white beast watches.
> Terra, resolved, straight to us: "A dream can be about the smallest
> things. Having a dream gives a person strength."

Audio render (edge TTS, description track only): `tts_des06_001.ogg`.

## Synced AD — Kefka-Terra conversations (original-audio gap timing)

Source audio: no-commentary playthrough audio (`EIJRbzStFMY`), sliced to
the two Kefka-Terra dialogue movies; gaps measured with
`ffmpeg silencedetect=-35dB:d=0.3` on the ORIGINAL mixed audio
(voices+music+sfx). Audio kept local only, never committed.

Movie A — first meeting (playthrough 4:44–5:40, 56 s): Kefka appears
("hello, my pretty"), tempts Terra ("come with me and destroy the
world"), Terra refuses ("I found a future that I want to protect").
Gaps >= 0.3 s: NONE. Wall-to-wall dialogue.

Movie B — taunt (playthrough 6:06–6:56, 50 s): Kefka's "better toy"
sneer through "come on, let's play!" Gaps >= 0.3 s: exactly ONE —
1.5 s at ~22 s (playthrough ~6:28, between "destroyed by me" and
"come on, let's play").

Film-AD treatment for gapless dialogue (the standard craft answer): a
setup line BEFORE the audio (who + where, once per scene), silence
during, one short action line only where a true gap exists.

> [pre-roll A] Destiny Odyssey Six. The clown villain Kefka corners
> Terra, the green-haired magic user, in a carved stone temple.
> [Kefka and Terra talk, no gaps — no description]
> [pre-roll B] Moments later, on a rooftop at sunset, Kefka taunts her
> again.
> [Kefka: "...better off being destroyed by me."]
> [gap 1.5 s] Kefka laughs, arms wide.
> [Kefka: "Come on — let's play!"]

Audio render (description track only): `tts_des06_sync.ogg`.

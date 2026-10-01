# Queue wiring — device proof checklist (v0.4.0)

Goal: prove one-line-in-flight, estimate pacing, and the completion hooks on
real devices, with the screen reader on and off. Anything that talks over
itself, loses a line, or resumes after Stop is a bug — report it as
device / reader on-off / ROM / step / heard-vs-expected.

## iPhone, VoiceOver ON

1. FE Shadow Dragon: boot → title speaks; New Game → difficulty rows speak.
   Flick rapidly across rows — expect only the LATEST row (replacement), no
   pile-up of stale rows behind it.
2. Same menu: two fast WhereAmI reads with the cursor moved between them —
   both answers speak, the second cutting in.
3. Mid-speech, Stop speech → silence that STICKS (nothing resumes a second
   later — that was the queue-drain bug this release fixes).
4. Dissidia, Trunks save (Order's Sanctuary): D-pad across tiles — tooltips
   speak; rapid moves collapse to the latest tile.
5. UI replies (ROM loaded, "Not ready yet") still come through immediately,
   over game speech.

## iPhone, VoiceOver OFF (synth voice)

6. Repeat 1–3: same behavior through the synthesizer.
7. Lock the screen mid-dialogue → speech continues (synth path).
8. Two rapid menu commands never talk over each other.

## Android, TalkBack (Pokémon Black)

9. Script lines speak in order; Stop silences; no regression. (The queue is
   not on this frontend yet — this run only proves the new TTS ids and the
   progress listener changed nothing.)

## Notes for the tester

- Rapid-move collapse (latest wins) is INTENDED, not a lost line.
- QTE/battle prompts (Dissidia EX Burst) must EVERY one speak, even two
  identical prompts in a row — no dedup there by design.

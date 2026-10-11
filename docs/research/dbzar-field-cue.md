# The field-mode cue, rendered: two harnesses, five bugs, and one measured result

The field beacon had a design and no proof. This is the proof, and the record of what it took:
`scripts/cue-field-drive.sh` (drives the game into the field through the real PSP core ABI and
writes a savestate this build wrote), `scripts/cue-field-capture.sh` (loads that state, flies a
circle, mixes the beacon into the game's own audio, and writes a WAV).

## The measured result

    render: 458,640 frames, 14.0 s, stereo 32,768 Hz, peak 7551, rms 1542, 99.95% of samples nonzero
    adapter ready: dbzar          ring engaged at step 1: kind=2 (CITY)
    freq walked 780 -> 780 Hz     mean pan over 840 cued frames: +0.497
    cue pulse rises: 20 over 14.0 s = 1.43 per second

Checked against the BYTES, not against the harness's summary — Goertzel magnitude over the whole
render: **780 Hz = 246.9**, 520 Hz (the enemy timbre) = 16.8, 1120 Hz (the ally timbre) = 4.5, and
1000 Hz — where nothing should be — = 2.2. The city tone is in the audio and the other two are not,
which is what the adapter's own ring said (kind 2).

And the pulse rate agrees with the encoder's own arithmetic: the tracked city sat 4268 world units
away, FIELD_FAR is 2500, so `t` clamps to 1 and `pulseHz = FIELD_FAST + (FIELD_SLOW - FIELD_FAST)*t`
= 1.2 Hz — the far, slow end of the distance scale. 20 rises in 14 s (1.43/s) is that, measured.

**What this does NOT prove**, the same disclaimer the lock cue's harness carries: the cue is
synthesised in Swift on the device, and this harness re-implements the encoding in C++. It proves
the encoding is audible over real gameplay at the chosen rates and gains, and that the adapter's
snapshot really tracks a live target. It does not prove `CueSynth.swift` produces byte-identical
output, and it has never run on hardware. If the two drift, the Swift file is what shipped.

## Five bugs, four of them in the harnesses themselves

⛔ **1. A savestate is bound to the build that wrote it.** The state on this host came from a desktop
PPSSPP v1.20.4 and dies on load with `found 1869636946 (0x6F706552) instead of save marker 66` in
MipsCallControl. Hence the drive harness. Its control, `DBZ_LOAD=<state>`, loads a state at the PSP
level and reads the field's own addresses — that is what proved the state good.

⛔ **2. `poke_*` cannot press a button on a PSP game.** `pokecore.cpp` creates the PSP core but the
button path reaches a backend ops table this build fills for NDS alone, so every press goes into a
field nothing reads and the game sits in its own attract loop ignoring all twelve buttons.
`Core/psp_core.h` is the ABI that carries input; `scripts/psp-host-proof.sh` is the only build on
this host that links it cleanly, and the drive mirrors that recipe.

⛔ **3. THE DIALOG: "Save completed" with one control, `○ Back` — and neither run pressed CIRCLE.**
The fixed script had no CIRCLE at all and the weighted search never chose one, so two runs of
35-40 minutes each sat on a modal dialog one button away from moving. It was found in minutes by
dumping the frozen frame and LOOKING at it (the drive now writes one PPM on the first declaration of
stuck; `scripts/ppm2png.py` turns it into an image). A screen that ignores every press is usually a
dialog waiting for its own dismissal button, so the stuck path now presses CIRCLE and START.

⛔ **4. TWO ORACLES FOR ONE PROPERTY, AND THEY DISAGREED.** The drive called the field "live" with
its own test — the pointer at `0x089B02A4` reading `0x089B02A8`, plus a city with capacity — and it
saved a state in which the ADAPTER's gate reads zero: `FIELD_LIVE = 0x089B03E4`, the word that is a
pointer only while the field's own task objects are resident, read `0x00000000`. Cities populated,
field tasks not. The capture harness then said "adapter not ready" about a state the PSP level
happily reports as field data, and it cost two runs and a load control to see why. The drive now
asks exactly what the adapter asks. **One oracle for one property, and it must be the one the thing
being fed actually uses.**

⛔ **5. The harness could not see its own output, and never exited.** Its stdout is block-buffered
into the log, so a run that printed "adapter not ready" left nothing else behind — and the process
then hung at a futex with 52 threads instead of returning. One from 9 October was found still alive
44 hours later, burning CPU, while three captures ran at once writing the same files. Fixed with
`stdbuf -oL -eL` in the invocation path, `setvbuf` line buffering in the drive, and a timeout on
every run.

Two smaller ones, both in the capture harness: it asked the adapter whether it was ready
**immediately after loading the state and before running a single frame**, which reports "not
ready" on a good state (the adapter's answers are read while frames run); and its pulse counter
counted the **carrier** rather than the pulses — 143.61 "pulses" per second on the first render,
which is the 780 Hz timbre crossing a raw threshold twice per cycle, and would have gone into this
doc as the distance cue's rate. A one-pole envelope at 25 Hz leaves only the pulse shape.

## How to run it

```bash
# 1. drive into the field (needs the ISO, the PPSSPP tree and $HOME/ffmpeg-host)
DBZ_MAXFRAMES=60000 bash scripts/cue-field-drive.sh      # -> ~/cue-run3/field-ours.ppst
# 2. render the beacon over real audio with that state
DBZ_STATE=$HOME/cue-run3/field-ours.ppst DBZ_SHOT=/tmp/field.ppm \
  bash scripts/cue-field-capture.sh                      # -> ~/cue-field-out/field-cue.wav
```

Environment worth knowing: `DBZ_FUZZ=1` swaps the fixed script for a deterministic weighted search,
`DBZ_MAXFRAMES` is the budget in EMULATED frames, `DBZ_FRAMES=dir` turns on the frozen-screen dump,
`DBZ_LOAD=state` runs the load control instead of driving, `DBZ_SAVE=state` writes a state as soon as
the field is reached, `DBZ_SHOT=file` dumps the frame the render is taken from.

The route the drive follows, from the field-mode doc and confirmed here: title `CROSS` → main menu
`UP` to row 0 → STORY MODE → cutscenes → CHARACTER SELECT → TEAM EDIT → the field. Once the mode
flag is set, **CIRCLE must not be pressed again** — it is BACK, and it bounces the drive out of the
menus it still has to walk. Two phases, chosen by `AR_MODE`, is what finally arrived (7,414 frames).

## Still open

- The distance cue's near end is unproven: this render sits at the FAR clamp (1.2 Hz). Closing it
  means steering the flight toward the target until the rate rises, which needs a heading source
  the harness can trust — the adapter derives heading from movement, and the harness's circle does
  move, so this is a matter of flying toward the target instead of in circles.
- The cue is host-measured only. No device has ever played it.
- The three cities read 1500/1500, so the pan is exercised but the *damage* bands are not; that is
  the same gap `docs/research/dbzar-field-mode.md` records, and it needs an enemy to attack a city.

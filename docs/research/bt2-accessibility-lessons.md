# BT2 accessibility mod (r12): transferable lessons

Source: `DBZ-BT2-Accessibility-Guide-Playtest-2026.09.26-r12-win-x64` (PCSX2 +
PINE, full source shipped). A third-party mod for Dragon Ball Z: Budokai
Tenkaichi 2 (PS2) — menus, world-map guidance, pursuit. Studied 2026-09-27 for
what OGA should steal. Nothing below is OGA behavior yet; items marked ADOPTED
have been folded into our design docs.

## 1. Guidance tones encode a vector (audio.py) — ADOPTED

One tone carries three channels: stereo pan = left/right, pitch =
forward/back, pulse rate = distance. Arrival is a distinct three-note rise.
A second, hollower timbre marks *degraded* guidance (bearing honest,
destination unconfirmed) — the player hears confidence without reading anything.

OGA's beacon was centered-only. Upgraded `docs/design/announcement-queue.md`
to pan + pitch + rate, plus the degraded-timbre rule for unconfirmed targets.

## 2. Speech: interrupt-by-default, `once` dedup, silent silence — ADOPTED

- The queue is purged on interrupt; queued game narration goes stale fast, so
  new speech cuts old ("you heard where you *had* been").
- `once=True` suppresses repeats of identical text (their version of our
  same-key coalescing).
- `silence()` clears the queue and stops the synth *without speaking anything*
  — a purge command, not an announcement.

Folded the `once` rule into the queue doc's coalescing section. Our matrix
already purges on request; their field report (queued speech = stale speech)
confirms the direction.

## 3. Never trust an address (menu_reader.py, memory.py)

BT2's menu labels are textures, so the reader recognizes the menu package,
finds the selector by shape, and translates a semantic id through a text
catalog. Cached addresses are per-session hints revalidated against structure;
a moved allocation causes rediscovery, never a crash, and internal names never
reach speech. Their RAM tables are found by sentinel/shape scan (a 99999.0
triple + stride + record-type filter), with a hardcoded address kept only as a
one-read scan hint for the common case.

This is our FE11 rule stated independently: anchor by content scan, revalidate,
never speak internals. No change needed — confirmation we match best practice.

## 4. Fixed cue structure, honest units (navigation.py)

Every cue reads identically ("Fly {dir} for {dist} units toward {target}"),
compass quantized to 8 points, arrived/approaching thresholds explicit — and
distances stay in game units because no metre conversion is known ("calling
them metres would be inventing a scale"). Axis mapping was measured from
paired RAM/screenshot captures, not assumed.

Applies directly to our Where-am-I / opponent replies: fixed phrasing, no
invented units, verify axes per game.

## 5. Player-in-the-loop classification (hotkeys.py)

The mod sees a visit happened but not whether the story advanced — the player
knows instantly, so one keystroke (F = free event, S = story, U = nothing
there) records it. Human labels what automation cannot observe.

Status 2026-09-27: deferred — unneeded for now. Revisit only if adapters
start guessing story progress from RAM.

## 6. Temporal confirmation kills jitter-speech (objective.py)

`same_target` compares positions directly (not rounded keys, which straddle
boundaries) so jittering pixels never reset arrival or re-speak. Confidence
tiers: confirmed / candidate (seen N frames in a row) / unconfirmed.

Mirrors our coalescing rules; the quantized-key-boundary warning is a real
pitfall to keep when we implement arrival detection.

## 7. Gates from measurement, honestly scoped (memory.py)

Their render-vs-sim position gate was set from live measurement (38–57 units
normal) with the gate at 200 — and the comment says what it is actually for
(half-loaded frames, hundreds of units apart), not what it pretends to catch.
Keep this habit: every threshold in OGA cites its measurement and its real
purpose.

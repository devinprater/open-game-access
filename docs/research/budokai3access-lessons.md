# Budokai 3 Access (native PC port): transferable lessons

Source: `BlindChallengerX/Budokai3Access` v1.0 (MIT, C, released 2026-10-10) — a
screen reader mod for *Dragon Ball Z: Budokai 3 HD Collection* running under
NovaPowers' ReXGlue static recompilation of the Xbox 360 game
(`novapowers0/DBZ-Budokai-3-HD-Collection`). Studied 2026-10-10 by reading the
released source and the mod's own README. **Source reading only — no game data
was available on this host, so nothing here is from playing it.** Items marked
CONFIRMS need no change; items marked PROPOSED are not OGA behavior yet and are
tracked in `TODO.md`.

Why it is worth the read even though its target is a PC port and not an
emulator: it is the first mod studied here whose reader is *not* in-process
.NET (Family A) and *not* driven by an emulator (Family B). See §5.

## 1. Text drawn as pictures: catalog it offline, look it up at the draw — CONFIRMS

Budokai 3 draws every menu label, story line and skill description as a
pre-rendered image inside `#AZT` texture packs. The mod does not OCR at
runtime. `data/text.tsv` (708 KB, 2026-10-10) holds the words of every text
image on the disc, keyed by **(texture pack checksum, texture index)**.

At runtime it hooks the texture bind (`guest 0x82096ff0`), reads the current
pack from the game's global (`0x8238ce10`), resolves the texture number through
the pack's index table (`0x8238ce14`), and looks the pair up in a hash table.
That is the whole text reader. `src/text.c`

This is BT2's design arriving independently from a different platform: catalog
once offline, translate an id at the moment the game draws it. Two independent
solutions to text-as-textures converging is the strongest signal we have that
our catalog-by-content rule (`docs/research/fe11-menu-reader.md`) is right.

## 2. Speak what stayed on screen; forget what left — PROPOSED

- A text is spoken only after it has been drawn for **120 ms**, so fades and
  flicker never speak. Two numbers, both in `text_poll`: 120 ms before speaking,
  400 ms without a draw before forgetting.
- F2 re-reads everything currently on screen, newest first, and says "No text on
  screen" when there is nothing — a query, not an announcement.

Our coalescing already suppresses repeats, and this confirms BT2 §6's "confirm
before speaking" tier rather than contradicting it. What is new is that these
are concrete shipping numbers for a question our queue doc leaves open, so the
120/400 pair should become its starting values next time we touch
`docs/design/announcement-queue.md`.

## 3. One screen, one owner — PROPOSED

Two mechanisms keep the generic text reader from talking over the specific one:

- Texts that are control hints (they begin with `:`) are never read
  automatically, because they repeat on every screen (`is_hint`).
- Where a menu reader speaks a screen itself (with position, price, stat), the
  whole texture pack is **muted** — `text_mute_pack`, or
  `text_mute_pack_except(pack, keep_idx)` to keep exactly one picture of the
  muted pack read. A second muted pack covers the Options screen.

The rule to fold in: when an adapter owns a screen, the generic reader yields the
screen, not just the line.

## 4. Speech in, through the host's own screen reader — CONFIRMS, plus one habit

`prism.dll` (Prism, in-process, speaks through NVDA / JAWS / other) is loaded
next to the mod, with the Windows SAPI voice as fallback if Prism is missing or
finds no reader; which one won is written to the log. **Hooks never speak** —
they queue; one worker thread drains the queue; an interrupt purges it first;
F12 repeats the last line; every line is logged as `SAY` / `SAY+` so a bug
report shows exactly what was spoken. `src/speech.c`

Same conclusion as BT2 §2 (queued game narration goes stale, so new speech cuts
old) reached from a third codebase. Our queue already purges on interrupt; the
habit worth copying is the `SAY` log, so a field report carries the speech itself.

## 5. A third family: the mod inside a static recompilation

`accessibility-mods-survey.md` splits the scene into Family A (in-process .NET
mods calling NVDA on Windows) and Family B (script/port readers driven by an
emulator) and places OGA in B. Budokai 3 Access is neither:

- It is a native C DLL injected into a **recompiled console binary** — the
  game's PowerPC code is compiled to host code and linked into `dbz3.exe`.
- Hooks are placed at **guest** addresses (`0x82096ff0`), resolved to host code
  through the pointer table the runtime keeps at `0x826D0000`, indexed by
  `(address - 0x82080000) / 4`. `src/guest.c`
- Guest memory is mapped at a fixed host base (`0x100000000`) and is
  **big-endian**; every read goes through `guest_ok(a, len)` first (a validated
  page cache, because `VirtualQuery` over thousands of regions is too slow per
  frame) and then byteswaps. Reads return 0 instead of faulting.

Two things transfer. First, the survey §2a rule ("hook the draw, do not poll for
it") gets a third independent confirmation: the text reader exists only because
the mod watches the texture bind. Second, the guest/host discipline is the same
discipline an emulator adapter needs — addresses that move, reads that must be
validated, and a per-read cost that forces caching. ⛔ Its mechanism does not
transfer: OGA is Family B and stays there.

## 6. An in-game reverse-engineering workbench, driven by a text file — PROPOSED

`src/guest.c` ships the tooling the author used to find everything else.
Commands are written as lines into `Budokai3Access\cmd.txt` while the game runs,
and the file is deleted once read:

- `count arm | reset | dump` — hook every game function with a call counter, and
  dump the counters to a file (find what runs on this screen).
- `trace ADDR N` — log the next N calls of one guest function with `r3..r8`.
- `watch ADDR LEN rw|w` — a hardware watchpoint that logs *which guest function*
  touched an address (find the writer of the cursor).
- `poke ADDR VALUE` — write a big-endian value for testing.
- `pad BUTTONS MS` — hold controller buttons, so the tool can walk a menu with
  no hands on the controller.

⭐ This is the strongest single lesson in the repo. Our equivalent surface is
split across tools (PPSSPP WebSocket debugger, BizHawk Lua, one-off
`scripts/*-live-addresses.mjs`). The pattern worth adopting is a **file-driven
command channel with a pad-injection command**, because it lets the person
driving the game and the person reading logs be the same blind player with
scripted hands. Tracked as open work in `TODO.md`.

## 7. Verify your assumptions, then shut up honestly — CONFIRMS

`src/health.c` logs the port's versions and expected layout at startup, and the
mod turns itself off and says so when something it depends on changed — the game
keeps running without it (`selfcheck_*`). The port's launcher re-installs the
mod's loader on every start, because a port update puts the original graphics
plugin back.

This is our standing rule (an honest no-fix beats a workaround) in shipped code:
a reader that cannot verify its anchors goes quiet and says why, rather than
speaking wrong state.

## 8. Audio carries information, and can time an input — PROPOSED

- Distance tone: pitch = range (two octaves), pan = which side, and **timbre
  encodes side redundantly** — reedy on the left, smooth on the right. Above and
  below are a chirp sliding up or down at intervals.
- Ring edge (the one stage with a ring): a knock from the side of the nearest
  edge that speeds up and louden as you approach it.
- Ultimate attack: a rising tone per meter sweep and a low beat at each restart,
  so "3 presses" become a timed action, and the result is spoken after.
- Dragon Rush: each exchange says the buttons still available in words, and a
  click confirms the press landed.

BT2 §1 gave us pan + pitch + rate for the beacon. This adds two things: a second
channel for the same fact (timbre repeats side), and the notion that a tone is
not only for finding a thing — it can time an action. Fold into the audio design
when an input-timing screen shows up; no current reader needs it.

## 9. What this does not change for OGA

- There is no PS2 core, and Budokai 3 on PS2 is a different build. This mod
  reads nothing from it and cannot be pointed at it.
- The writeup is source reading, not playtesting. Anything here that only shows
  up in play (pacing, queue pressure, how the tones actually feel) is unverified.
- The mod's user-facing README is a full spoken manual: what speaks on every
  screen, the key list, and an explicit "not done yet" section. Our reader docs
  could end with that same section instead of implying completeness.

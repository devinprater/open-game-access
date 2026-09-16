# Steins;Gate: My Darling's Embrace (PSP) — reader investigation

Investigation status: **TEXT LOCATED, LINE CURSOR NOT FOUND.** A reader cannot be built
from what is known so far. This document records exactly what is and is not established,
because the next person (or the next session) should not have to rediscover it.

## ⛔ First: this is NOT the game the backlog entry meant

`docs/research/game-backlog-ranked.md` lists **"Steins;Gate — PSP — pure visual novel,
would be the easiest game here if a PSP core existed"**. The file actually in the library
is a **different title in the same series**:

| | |
|---|---|
| File | `Dropbox/Games/PSP/Steins Gate - Hiyori Renri no Darling (English v0.5).cso` |
| Game ID (verified via PPSSPP debugger) | **`ULJM06040`** |
| Title | **Steins;Gate: My Darling's Embrace** |
| Version | 1.02 |
| Released | 2012-04-26 |
| Also known as | Hiyoku Renri no Darling; "Darling of Loving Vows" |
| Type | **Fandisc** — a daily-comedy spinoff, not the serious sci-fi VN |

**The original Steins;Gate is `ULJM-05887`** (2011). The widely-cited dumped CWCheat
codes — including the debug-menu unlock `_L 0xD10E67E4 00000001` / `_L 0x110E67E4
00000004` and the alternative `0x0027C500 0x00000080` — are for **`ULJM05887`**, and do
**not** apply to this game. Do not carry them over.

## Second: "easiest" was an assumption that has not held up

The reasoning was that a visual novel has no combat, no movement and no timed input, so a
reader only has to speak the current line of text. That part is right. What was not
anticipated:

- The script lines live in RAM as plain ASCII, and are easy to find.
- **Nothing obvious tracks WHICH line is current.** This is the whole reader problem, and
  it is unsolved. See "The blocker" below.
- A VN is still a game with modes (attract / title / scene / dialogue / menus), and the
  script only loads once you are past the attract loop.

So the difficulty is not where it was expected to be.

## What IS established (verified, with evidence)

### The debugger works

PPSSPP 1.20.4, WebSocket debugger at `ws://127.0.0.1:12345/debugger`, subprotocol
`debugger.ppsspp.org`. Confirmed reading:

```
game id   : ULJM06040
title     : Steins;Gate: My Darling's Embrace
version   : 1.02
```

⛔ **The `--debugger=PORT` COMMAND-LINE FLAG ALONE IS NOT ENOUGH.** The effective config
lives at `Documents/PPSSPP/PSP/SYSTEM/ppsspp.ini` and had:

```
RemoteISOPort = 0
RemoteDebuggerOnStartup = False
RemoteDebuggerLocal = False
```

With those values no port is ever bound and `--debugger=12345` changes nothing. The
symptom is a debugger that is simply not there, which reads as "the flag is wrong". Set
all three (`RemoteISOPort = 12345`, `RemoteDebuggerOnStartup = True`,
`RemoteDebuggerLocal = True`) and restart. **Only one debugger client can hold the
connection at a time** — a second client gets `version timed out` until the first
disconnects or PPSSPP is restarted.

### Input injection works, and is verifiable

`input.buttons.press` takes **`button` (singular)** and **`duration` in FRAMES, not
milliseconds**. Sending `buttons` fails with `Missing 'button' parameter`.

Proven by subscribing to the `input.buttons` broadcast, which fires on every `sceCtrl`
change:

```
{"event":"input.buttons","buttons":{...,"start":true,...},"changed":{"start":true}}
{"event":"input.buttons","buttons":{...,"start":false,...},"changed":{"start":false}}
```

So injection reaches the emulated pad. When the screen then does not change, that is the
game's answer, not a tooling failure.

### ⛔ The attract prompt BLINKS — a single press can fall in the dark

The opening screen shows **"Press START button"**, and that prompt blinks. A single
`start` press, and even 12 presses at ~420 ms spacing, failed to clear it. Twelve presses
at the same spacing *did* clear it on a later attempt. **Mashing is required to get past
it reliably.** Any conclusion drawn from "I pressed START once and nothing happened" is
worthless here.

Note also that screenshots taken after the press show a screen that **changes every
frame** — the attract loop animates. A "N of N frames differ" result is therefore
**animation, not navigation**, unless a hash repeats (the loop) or the content visibly
changes. Do not read motion as progress.

### The script text IS resident, and this is the English-patched build

Once past the title, RAM at **`0x08AEA000`–`0x08AF3000`** contains the script as plain
ASCII: **686 text runs**, each a single-character speaker code followed by the line.

```
0x08AEAEB2  'gOur time slowly ticks away from the moment of our birth to the moment of our death.'
0x08AFB002  'gThe wise men of old understood that the greatest wisdom was oft spoke by those who '
0x08AFB08D  'gHe who knows that he is wise may drown in his own wisdom'
0x08AFB220  'C Mayuri. Can I use these paper cups?'
0x08AFB258  'C sure. I guess so.'
```

Character names are present in quantity — `Okabe` (8+), `Kurisu` (8+), `Mayuri` (8+),
`Daru`, `Suzuha`, `Faris`, `Luka`, `Moeka`, `Rintaro`, and `El Psy` (5 hits). The English
patch is confirmed by content, not assumed.

⛔ **THE SCRIPT IS ONLY THERE ONCE YOU ARE IN-GAME.** Every earlier scan found nothing
but SDK and CRI library strings, because the game was still on the attract screen. A
"there is no text in RAM" conclusion taken at the title is wrong.

## ⛔ THE BLOCKER: nothing found tracks the current line

This is the reason no reader exists. A pointer-to-current-line or a line index must be
found, and neither has been.

What was tested, and the result:

| Test | Result |
|---|---|
| 4-byte words pointing into the script window | **49 found** |
| Of those, changed between two different dialogue steps | **0** |
| The script block itself changed between steps | **0 of 36864 bytes** |
| Small index-like words that changed, outside art/scratch | a long list of plausible counters, **none confirmed** against a screen |

The 49 pointers into the script are stable — they are most likely dispatch or table
pointers established at load, not a moving cursor.

**Why this matters more than usual:** a reader that guesses the line from table order
announces the *wrong line* with full confidence. For a blind player that is worse than
silence. So this is reported as **located but blocked**, not as partially working.

There is also an unresolved observation that must not be glossed over: **between the
dialogue steps dumped, no dialogue box with text was visible on screen.** The scene
artwork was showing, with the in-game HUD (`8/5 (THU)`, mail and phone icons) but no text
box. So it is not even confirmed that the line advanced during those steps. The next
session must establish a state where a text box is demonstrably on screen *before*
concluding anything about the cursor.

## The decompile path (NOT started)

The user's standing instruction is that decompiling is available when needed. It is very
likely needed here: the script format and the current-line logic live in the executable,
not in RAM. The route:

1. Decompress the `.cso` to `.iso` — `maxcso.exe` is present at
   `Dropbox/Games/PSP/maxcso.exe`; `maxcso --decompress` handles CISO. ~1.8 GB output.
2. Extract `PSP_GAME/SYSCONF` (read the real game ID) and `PSP_GAME/EBOOT.BIN`.
3. **Decrypt/decompress `EBOOT.BIN`** — on PSP this is a signed PRX, not a raw ELF, so
   Ghidra cannot load it directly. Needs a PSP PRX decryptor (`pspdecrypt`/`PRXdecrypter`
   class of tool) to produce a loadable ELF.
4. Load in Ghidra (scoop-installed, 12.1.3, project at `C:\Users\Devin Prater\oga-ghidra`)
   as MIPS, and find the code that walks the script block — the reference to
   `0x08AEA000`-range addresses is the thread to pull.

**This was not done in this session.** It is a multi-hour piece of work and should be
started deliberately, not left half-finished.

## Tooling built (all read-only, all reusable)

| Script | Purpose |
|---|---|
| `scripts/psp-probe.mjs` | `status` / `dump` (24 MiB user RAM) / `scan` / `press` / `shot` |
| `scripts/psp-walk.mjs` | press a plan and capture a shot + RAM dump per step; `wait:N` steps press nothing |
| `scripts/psp-burst.mjs` | press once then capture a rapid series, hashing each frame, to catch fast transitions |
| `scripts/psp-input-check.mjs` | prove injection reached `sceCtrl` via the `input.buttons` broadcast |
| `scripts/oga-psp-analyze.py` | find text + keywords in a dump; densest-string-cluster analysis |
| `scripts/oga-psp-script.py` | locate the script block; hunt pointers to the current line; `--compare A B` |
| `scripts/oga-psp-diff.py` | which RAM blocks changed between two dumps, with density and delta hints |

⛔ **NONE OF THESE WRITE EMULATED MEMORY.** They read, and they press the player's own
buttons. No pokes: the project's rule is that accessibility reads state rather than
mutating it.

### Traps hit while building the tooling

- **`duration` is frames, not ms.** 12 frames ≈ 200 ms. A short hold falls between the
  pad's per-frame polls.
- **`input.buttons.press` answers only AFTER the hold completes.** A 90-frame press does
  not return for ~1.5 s. Expected, not a hang.
- **The captured window includes PPSSPP's chrome.** Fullscreen (`FullScreen = True` in the
  `[Graphics]` section of `ppsspp.ini`) removes it entirely and gives a clean frame. A
  52 px crop was not deep enough and left `File/Emulation/Debug` in frame, where a diff
  would report menu changes as game changes.
- **`PrintWindow` needs flag 2 (`PW_RENDERFULLCONTENT`)**, in-process via ctypes. A plain
  BitBlt returns black on a flip-model swapchain. In-process is ~15 ms/frame; spawning a
  process per frame is ~460 ms and useless for a live loop.
- **The capture is letterboxed** — 1706x1066 with a 74 px black bar at the top. Game
  content is 1706x967, which is exactly 480:272. Crop to that before measuring pixels.
- **Build a PPM by concatenating header and body**, not by pre-computing an offset: the
  header length varies and PIL reports the mismatch as "image file is truncated", which
  reads as a capture fault rather than an arithmetic bug.
- **Read ALL 24 MiB, and get the base right.** User RAM is `0x08800000`–`0x09FFFFFF`;
  file offset is `address - 0x08800000`. A wrong base silently reads unrelated bytes.

## Where to pick up

1. Get the game to a state with **a dialogue box visibly on screen**, and capture that.
2. Re-run `oga-psp-script.py --compare A B` **between two states where the on-screen text
   demonstrably differs** — the earlier comparison may simply have had no advance in it.
3. If nothing tracks the line, decompile `EBOOT.BIN` (above). The answer is in the code.

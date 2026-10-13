# Chrono Trigger SNES live verification (BizHawk EmuHawk, 2026-09-20)

- Method: EmuHawk with temp --config (owner EmuHawk.xml untouched), ROM read-only, no files copied to any repo.
- Lua memory domains used by the mod: WRAM (SNES 7E work RAM), CARTROM (ROM reads), CARTRAM (save RAM).
- Mod output paths: speech Build/speech_latest.txt, crash log Build/Data/runtime_crash.log, sound commands Build/sound_bridge_command.txt.
- Probe 1: minimal WRAM sampler (0x0D13, 0x0100, HP), Start presses, 30 s, 114 log lines.
- Full mod boot: Build/ChronoAccess.lua, 35 s, then log/file inspection.
- Probe 2: A/Start/B mash script, 100 s, 192 log lines, pushed from boot into live gameplay.

## Verified live (address: observed values)

- 0x2603 party HP slot 1 (u16le): steady 70 from boot through gameplay.
- 0x2653 party HP slot 2 (u16le): steady 65 from boot through gameplay.
- 0x26A3 party HP slot 3 (u16le): steady 75 from boot through gameplay.
- 0x0100 location id (u8): 00 at boot, B1 on title, 00 in menus, F0 then 0F in intro cutscene, 02 in first gameplay room.
- 0x0D13 context (u8): 00 boot/cutscene, 05 title menu, 7F briefly, 1D name entry, 01 live gameplay.
- 0x0D13 = 0x1D name entry: appeared for about 120 frames, mash-through accepted the default name and continued.
- 0x0D13 = 0x01 gameplay: reached at probe frame 3180, location 02, held for 87 samples to end of run.
- 0x0D76 transition/busy (u8): 00 on boot/title/menus, 0E/07 blips, steady 07 through intro AND gameplay.
- Full mod load: crash log grew 315 to 319 lines with exactly 4 new lines (start banner, solidity decoder loaded, the 2 known missing-module errors, nothing else fatal).
- Speech bridge: Build/speech_latest.txt created on first full-mod boot, content 1789882874001|Chrono Access loaded.
- Known missing modules Navigation.lua and SimpleEntityList.lua: confirmed still missing, location summary unavailable as expected, not a failure.

## Failed or not reached

- No shop reached: 0x0D13 = 0x2F never seen, shop RAM 0x1000/0x2400 not sampled.
- No battle reached: 0x0D13 = 0x29 never seen, battle focus bytes never left idle values.
- Battle bytes at idle (0x095D5 foc, 0x095DC cmd, 0x09615 sub, 0x099E0 inl): boot garbage 80/38/87/04 style values, settled to 80/80/00/80 in gameplay; meaningful only inside battle, still unverified.
- sound_bridge_command.txt in Build: never created during silent title sit (expected, no sound events fired).
- EmuHawk force-kill (taskkill /F) is gated by policy in this environment; used powershell CloseMainWindow twice per shutdown (first call only dismisses a child window).

## Caveats for OGA adapter

- 0x0D76 reads 07 during normal gameplay, so a blanket nonzero-means-ignore rule would blank live play; gate on it only during sampled transitions or calibrate per scene.
- Two newly seen context values are undocumented: 0x0D13 = 0x05 (title menu) and 0x0D13 = 0x7F (brief, between menu and name entry).
- HP values 70/65/75 appear before any gameplay, so they look like defaults or pre-seeded new-game stats, not proof of mid-game correctness.
- Joypad Start/A/B injection via joypad.set worked, exact key spelling accepted by this BizHawk build is Start/A/B on port 1 with P1-prefixed fallback.

## Single next step

- From the gameplay state (d13 01, loc 02), drive Crono out of the house and toward the fair with D-pad script-plus-probe logging, because shop 0x2F and battle 0x29 stayed unreachable on dialog-mash alone and their focus/cursor bytes are the highest-value unverified OGA inputs.

## Walk-to-fair run 1 (2026-09-20, ~3.5 min emulation, f=30..12600, full log C:/temp/ct-walk-run1.log)

- Probe: per-30f samples of 0x0D13, 0x0100, 0x0102/0x0103, 0x095D5, 0x09615, 0x099E0, live HP 0x5E30/0x5EB0; boot mash (Start/A/A/B, f<3600, proven in probe 2) then walk phase (Down held, A-tap every 200f, Left/Right/Up strafe windows).
- Contexts seen (0x0D13): 00 boot/cutscene, 05 title, 7F brief, 1D name entry, 01 gameplay. 2F shop and 29 battle never appeared.
- Max progress (location ids visited): 00 -> B1 -> 00 -> F0 (intro) -> 0F -> 02 (Crono's house); gameplay 01 reached at f=3180 in loc 02, held to end of run (f=12600).
- Shop/battle bytes: foc 0x095D5 = 0x80, sub 0x09615 = 0x00, inl 0x099E0 = 0x80 steady through all gameplay; live HP 0x5E30 = 35723 / 0x5EB0 = 0 steady (garbage-outside-battle values, still unverified). Shop RAM 0x1000/0x2400 not sampled.
- Movement failure: tile x/y 0x0102/0x0103 frozen at 08/08 from loc 0F through all of loc-02 gameplay despite D-pad holds. Suspect input bug: script called joypad.set twice per frame (port-1 table plus P1-prefixed no-port table), and the second call may clear port-1 D-pad state; Start/A/B worked but D-pad was never proven before this run.
- Single next step: rerun the walk with joypad.set(t, 1) only (drop the P1-prefixed fallback call) plus denser A-taps to clear the waking dialog, then check x/y moves within the first minute of gameplay.

## Walk-to-fair run 1 (2026-09-20, v1 double-set script, archived log C:/temp/ct-walk-run1.log; v2 detail lives in the run 2 section below)

- Probe: C:/temp/ct-walk.lua logging every 30 frames to C:/temp/ct-walk.log (v1 archived as C:/temp/ct-walk-run1.log): 0x0D13, 0x0100, tile x/y 0x0102/0x0103, battle foc 0x095D5, submenu 0x09615, in-list 0x099E0, live HP 0x5E30/0x5EB0, plus commanded-input tag.
- Script: proven Start/A/A/B boot mash to f=3600, then A-tap dialog clear, then Down-hold walk with A taps every 150 frames and Right/Left/Up strafe windows; v1 double-called joypad.set per frame, v2 single joypad.set(t,1) only.
- Contexts seen: 00 boot/cutscene, 05 title, 7F transient, 1D name entry, 00 intro, 01 live gameplay; 0x2F shop and 0x29 battle never appeared in either run.
- Location ids visited: 00, B1 (boot), F0 then 0F (intro cutscene), 02 (Cronos house); furthest reached is 02, held from f=3180 to end of run (f=14910), x=08 y=08 frozen for the entire walk phase in both runs.
- v1 vs v2: identical boot path and identical stuck state, so the double joypad.set call was not the cause; D-pad holds had no observable effect on x/y or location.
- Shop bytes: 0x0D13 = 0x2F still unreached, 0x1000/0x2400 still not sampled.
- Battle bytes still unreached, idle gameplay values only: foc 0x095D5 steady 80, sub 0x09615 steady 00, inl 0x099E0 steady 80 throughout loc 02 gameplay.
- Live HP 0x5E30/0x5EB0: boot garbage (184/15622, then 16647/52668), steady 35723/0 in gameplay; not real HP outside battle, the 0x2603 party block stays the trusted field HP source.
- Title-menu note (not a target): at d13=05, foc=3E sub=25 inl=00, menu-cursor-like but uninterpreted.
- Single next step: extend the probe with dialog-state reads (0x0215 text status, 0x0F03 choice-active) plus object-table position (0x1801/0x1881) to tell an undismissed mom textbox apart from Crono moving with 0x0102/0x0103 stale indoors, because blind D-pad holds cannot distinguish those two states.

## Walk-to-fair run 2 (2026-09-20, ~4.2 min emulation, f=30..15120, live log C:/temp/ct-walk.log)

- Probe: single joypad.set(t, 1) per frame plus inp= column and working TRANSITION tags; boot mash (f<3600), dialog-clear A burst (f<4300), then walk cycle (Down default, A-tap every 150f, Right/Left/Up strafe windows).
- Contexts seen (0x0D13): 00, 05 title, 7F brief, 1D name entry, 01 gameplay. 2F shop and 29 battle never appeared.
- Max progress (location ids visited): 00 -> B1 -> F0 (intro) -> 0F -> 02; gameplay 01 in loc 02 from ~f=3180 to end (f=15120).
- Shop/battle bytes: foc 0x095D5 = 0x80, sub 0x09615 = 0x00, inl 0x099E0 = 0x80 across all 400+ gameplay samples; live HP 0x5E30 = 35723 / 0x5EB0 = 0 steady. Still unverified (out-of-battle garbage values).
- Movement still frozen: tile x/y 0x0102/0x0103 = 08/08 for every gameplay sample despite single-set D-pad holds in all four directions plus denser A-taps. Double-joypad.set hypothesis is disproven; remaining suspects are an undismissed dialog/event gate (mom wake-up) or wrong D-pad button spelling for this BizHawk build.
- Single next step: run a 60 s probe that reads back joypad.get() and samples dialog state bytes (0x0215 status, 0x0F03 choice-active) alongside x/y, to distinguish input-not-registering from dialog-gated before any more walk scripts.

## Probe 3: stuck-Crono diagnosis (2026-09-20, f=30..16800, 561 lines, log C:/temp/ct-probe3-run1.log)

- Probe: C:/temp/ct-probe3.lua; boot mash to f=3600 then dedicated 400-frame D/U/L/R holds separated by A-bursts; per-30f log adds dialog bytes (0x0215 tstat, 0x0217 tcnt, 0x0F03 ch), obj slot0 pos (0x1800/0x1801/0x1880/0x1881), and joypad.get(1) readback (seen=) vs commanded (cmd=).
- Input verified working: seen= matched cmd= on every commanded sample (D/U/L/R/A/B/S all echoed); D-pad spelling and joypad.set(t,1) are correct, no input bug.
- Movement still zero: x/y 0x0102/0x0103 = 08/08 on all 455 gameplay samples despite verified 400-frame holds in all four directions.
- Dialog bytes: tstat 0x0215 = 00 nearly throughout (one 0C blip with tcnt 0F early), tcnt 0x0217 static at 1B after entry (14 at first gameplay frame), so no text engine activity during the walk phase; ch 0x0F03 cycles 00/01/02/03/04/0F/10/16/79... in gameplay, contradicting the documented A5/03 choice-active meaning (that meaning likely only holds inside menu contexts, not field).
- Obj slot0: ox/oy = FF/FF always (inactive slot); slot 0 is not Crono, position must be found by scanning all 0x40 slots.
- Single next step: probe-scan all 0x40 object slots (identity 0x1100/0x1101, x 0x1801+s, y 0x1881+s, move flag 0x1A00+s) plus 0x0D76 busy across one D-hold window to find which slot is Crono and whether any slot moves, because verified input plus silent text engine leaves wrong-position-source as the top hypothesis.

## Probe 4: object-slot scan during Down-hold (2026-09-20, f=30..12210, 26k lines, log C:/temp/ct-probe4-run1.log)

- Probe: C:/temp/ct-probe4.lua; boot mash to f=3600 then Down held with A-tap every 150f; per-30f HDR (d13/loc/d76/cmd) plus one OBJ line per slot with id/type/x/y/mv where id, x, y not all FF (filter too loose: untouched RAM reads 00/80 so nearly all 64 slots logged every sample).
- Result: zero movement anywhere. Across all gameplay samples (d13=01 loc=02, d76 steady 07 as in normal play) every one of the 64 slots has exactly 1 distinct (x,y,mv) tuple; nothing displaces under a verified working D-pad.
- Candidate Crono slot: s=02 shows x=18 y=09 with mv stuck at 0x18 (move flag set, no displacement); s=01 x=A4 y=FF looks like offscreen/scratch. Slot 15 y-flap (DD/54/11/E3/FF at f=3600-3870) is cutscene teardown, not gameplay movement.
- Interpretation: input registers (probe 3), text engine silent (probe 3), no slot moves (this probe), busy byte reads normal-play 07. Remaining hypotheses in order: (a) Crono pushing into blocking geometry (bed/wall/mom) so all holds bump in place; (b) wake-up event gate needing face-specific A talk-to-mom, which blind Down+A never satisfies.
- Single next step: script a talk-and-sweep (at several x offsets hold Up + A-burst to face/talk north, then step Right/Down and repeat) while tracking s=02 x/y/mv, because random-direction holds have now failed three times and the mom trigger is the only known gate in this room.

## Bedroom escape + fair run (2026-09-20, probes 5-17, screenshot-steered)

- Breakthroughs: obj slot 2 (0x1803/0x1883) is Crono in loc 02 (verified: x/y track held D-pad axes, mv 0x18 while stepping vs 0x10 idle); 0x0102/0x0103 stay 08/08 indoors and are NOT player tile pos there.
- Input rules learned: joypad.set(t,1) + joypad.get(1) echo verified (seen==cmd always); textboxes need A-tap EDGES (held A never advances); mom blocks the spawn pocket until talked past (direction+A overlay unblocks, pure holds bump forever).
- Escape path (reproduced 4x): waypoints (19,08)->(13,09)->(13,0C) with A-overlay, then banister creep to the stair gap; loc 02->01; living room door hunt (slow D/L ping-pong onto the mat) to outside loc F0.
- Screenshots proved each step (C:/temp/ct-room.png mom wake-up box; ct-room2.png bedroom layout with south stair gap; ct-liveN.png living room mat, world map, fair stalls).
- Fair reached (loc 05 = Leene Square): Marle recruit + Telepod-queue dialog observed on screenshots; party HP still starter 70/65/75 (companions in shots unconfirmed, likely Marle only).
- Fair dialog trap: arrival box needs brief A-taps then ZERO A (re-taps reopen Marle chatter and pin the scene); detour around the counter blockade is D120/R240/U1500/L600 loop via the east lane.
- Outdoor d13 caveat: on loc F0 0x0D13 cycles 00-20+ uniformly (~300f/step) and is NOT the context byte there (area/sub-code?); loc 0C/0D seen briefly at the house door; world map reached Medina Island and 600 A.D. Leene Square label on later wanders.
- Shop 0x2F and battle 0x29 still unreached after ~90 min of scripted play across 13 probes.
- Next step used: probe18 = full journey + live steering via C:/temp/ct-cmd.txt (UDLRASBNTX/. polled every 15f) + auto TRAP lines dumping shop RAM (0x1000/0x2400) and live HP (0x5E30/0x5EB0/0x5F30) on any 2F/29 sample, so no reboot is needed to react to screenshots.

## Probe 5: RetroArch MCP — the freeze reproduced, and input proven to reach the game (2026-10-13)

Method, no BizHawk: RetroArch nightly 1.22.2 with the `snes9x2010` core, driven over
its MCP server. `SET_INPUT` holds a RetroPad direction, `RUN_FRAMES` steps an exact
number of frames, `READ_CORE_MEMORY` reads WRAM at `0x7E0000 + offset` — the same
domain the addresses below live in. Raw output: `docs/evidence/ct-snes-mcp-probe.txt`.

⭐ **Why `snes9x2010` and not the usual `snes9x`.** A read only works on a core that
declares a memory map, and the cores checked here mostly do not:

| core | memory map |
|---|---|
| `snes9x2010` | ✅ WRAM `7e0000`, 20000 |
| `snes9x` | ⛔ none |
| `bsnes` | ⛔ none |
| `mednafen_supafaust` | ⛔ none |
| `mesen` | ⛔ would not load the SNES ROM at all |

So SNES state reads need `snes9x2010` specifically. Everything below was read through it.

**The verified addresses hold up independently.** `0x2603` party HP slot 1 read 70
from boot onward, the value `snes-verify.md` records. The context/location walk came
out in the same order as the BizHawk runs — `ctx 0x0D13` 00 → 05 → 7F → 1D → back to
00, and `loc 0x0100` 00 → B1 (title) → 00 → F0 (intro) → 0F → 02 (Crono's house).
Different emulator, different reading mechanism, same addresses and same order.

**The frozen tile pair reproduced.** `0x0102`/`0x0103` read `08/08` in loc `0F` and
again in loc `02`, through both a 300-frame DOWN hold and a 300-frame RIGHT hold.
That is what all four earlier probes saw. It is not a BizHawk artifact and not a
script artifact.

⭐ **New: an idle control, and it is the missing piece of method.** At the title menu
(`ctx 05`) the window `0x0000`–`0x01FF` was diffed across equal-length runs:

```
idle 300 frames    0 bytes changed
DOWN press 1       3 bytes changed   0x0002 0x0054 0x0081
DOWN press 2       5 bytes changed   0x0000 0x0001 0x0002 0x00C0 0x00C1
DOWN press 3       8 bytes changed   0x0000 0x0001 0x0002 0x0054 0x0055 0x0081 0x00C0 0x00C1
```

Idle changes nothing; a direction changes bytes. **A direction reaches the GAME, not
just the frontend.** That is a stronger statement than the earlier probes' `seen=`
echo, which only proved the frontend was holding the button.

⛔ **And under that same control, in loc 02 (Crono's house, `ctx 01`), Crono still
does not move.** DOWN for 300 frames and RIGHT for 300 frames changed the `0x1800`
object window by **zero bytes**, and outside it changed only the bytes that also
change while idle. The blocker is the game's own state, not the tooling, and it now
stands under a second emulator, a second instrumentation path and a control the
earlier probes lacked.

⛔ **Method lesson, and my own error.** My first diff in loc 02, taken without an idle
control, showed 32 bytes changing under a DOWN hold, including object-table bytes at
`0x181A`/`0x181B` and `0x189A`/`0x189B` that differed between DOWN and RIGHT. That
looked like movement and was not: it was the scene still initialising after the
cutscene. With an equal-length idle control the object window changes by zero.
**Never diff game memory without an idle control of the same length over the same
window.** Two of the four earlier probes could have been misled the same way.

⭐ **New, uninterpreted: title-menu cursor candidates.** The bytes a DOWN press moves
at the title menu are `0x0000`/`0x0001`/`0x0002`, `0x0054`/`0x0055`, `0x0081` and
`0x00C0`/`0x00C1`. The earlier note called the title menu "menu-cursor-like but
uninterpreted"; this is the first set of addresses shown to change with a direction
there, with an idle control proving they do not drift.

⛔ **`0x0102`/`0x0103` should stop being described as the player's tile position.**
Nothing in this probe ever moved them: not DOWN or RIGHT in loc 02, not DOWN at the
title menu, where direction input is proven to work. They are inside the diffed
window in every case.

### Single next step

In loc 02, read the dialog/event bytes (`0x0215` text status, `0x0F03` choice-active)
alongside `0x0D76`, with the idle control running, to tell "an event is pending and
holding the player" from "the room is walkable and something else is wrong". Then one
much longer hold — 1800 frames rather than 300 — under the same control, so slow or
blocked-in-place movement cannot hide. Only after that is it worth hunting for the
player's real position source.

## Probe 6: the freeze is a CONTROL LOCK, and the "tile" addresses were the wrong pair (2026-10-13)

Tooling: RetroArch nightly + the `snes9x2010` core over MCP, plus the
`BelthasarProject` disassembly notes, cloned to `third-party/belthasar`. Raw output:
`docs/evidence/ct-snes-mcp-probe.txt`.

### What the disassembly says

`disasm/notes/collision.md` decodes the per-frame player controller at `C0/881E`,
which runs with Direct Page `= $1D00`:

```
882C  JSR $88E5        ; seed: $1D2E = $1D2A, $1D30 = $1D2B  (carried momentum)
882F  STZ $1D2C; STZ $1D2D   ; clear the d-pad INTENT
8833  if ($0162 == 0 && $011F != 0 && $0138 == 0): JSR $88EE   ; <-- d-pad dispatch
8840  if ($0120 != 0): JSR $8A6D                              ; tile collision + position
884C  JSR $9175        ; edge clamp -> applied velocity $1D32/$1D33
884F  JSR $99DE        ; finer map-rect clamp
```

⭐ **With `$011F == 0` the d-pad is discarded at `8833`, before any intent is formed.**
Nothing further down the chain can matter, because nothing reaches it.

### What was measured live

Reached Crono's house (loc `0x02`, ctx `0x01`, the live-gameplay context) and read the
gates and the object table:

```
on arrival   ctx=0x1 loc=0x2 | 011F=00 0162=00 0138=00 0120=01
             0215=00 0217=07 0F03=03 0D76=07 | idx=02 pos=1A80,08FF
             old 0102/0103 = 08/08
idle 480     obj window 0x1800-0x19FF changed 0 bytes
DOWN 480     obj window changed 0 bytes       011F still 00
RIGHT 480    obj window changed 0 bytes       011F still 00
A presses    011F still 00
```

⛔ **`$011F == 0`. Control is locked.** That is the whole cause. It is not geometry,
not the emulator, not the script, not the input path, and not a wrong button name —
all four of which the earlier probes ruled out one at a time, correctly, while
looking in the wrong place.

⭐ **Where the player's position actually is.** `$1800,X` and `$1880,X`, with
`X = $0197` the player index — for Crono, `$1802`/`$1882`, read here as
`1A80,08FF`. **Not `$0102`/`$0103`.** That pair read `08/08` again and never moved
in any trial, in this room or at the title menu; it has never been the player's
position. The earlier note's "tile `0x0102`/`0x0103`" is retired.

⭐ **Someone else measured the same thing.** `disasm/notes/movement_pinned_open.md`
records, independently and against the cartridge: `locked frames ($011F == 0): 169`,
`of those, $1902/$1982 != $1D32/$1D33: 169/169`, `e.g. f0..f168: ... pos = 18A4,08FF
(frozen)`. Same flag, same freeze, same address family — `18A4` there, `1A80` here.

### What this is worth to the adapter

**`$011F` is a control oracle.** It read `01` at the title screen, `00` throughout
loc `0F` and in the house. So a reader can tell "the game has taken control away"
from "the player is standing still", which is exactly the distinction four blind
probes could not make. A Chrono Trigger reader should ask `$011F` first and say so,
rather than narrating a player who is not being listened to.

### Still open

⛔ **What clears `$011F`.** Not A presses, not waiting, not direction holds. The
`BelthasarProject` notes mark the identical gap: *"Why `C0/3040` is not executed while
`$011F == 0` ... I did not disassemble the object-VM dispatch table entry that selects
`3040`, nor watch the VM's IP for the player object across the lock boundary."*
Next: log `$011F` across the whole opening scene, and watch the object-VM IP for the
player object to find the moment control is handed back.

### Tooling lesson, for whoever drives this next

The opening runs `loc F0` for thousands of frames and needs **no input**; mashing
Start inside the cutscene appears to delay it, while Start is required at the title.
`RUN_FRAMES` is capped near 10 s by its deferred-reply timeout, so waiting out the
intro costs many calls — roughly 7,000 frames from boot to the house. One read of
4,096 bytes at `0x7E0000` covers every byte this probe needs in a single call, which
is what makes the driving affordable.

## Probe 7: WHAT CLEARS THE LOCK — the scene does, and then Crono moves (2026-10-13)

The answer to Probe 6's open question, and the end of the movement question.

### The flag and its writer

`$011F` is the field engine's **explore** (player-control) flag. It is written by the
field-VM opcode **`0xE3`** — dispatch entry `0xE3 → C0/3C05`, the store itself at
`C0/3C0D` (`STA $1F`, running with `D = $1D00`, so `$1F` is `$7E:011F`):

```
E3 00   control LOCK   ($011F := 0)
E3 01   control UNLOCK ($011F := 1)
```

Confirmed in `third-party/belthasar`: `disasm/notes/field_touch_preempt.md`
("`0AC3: E3 00` the control lock (`$011F = 0`, writer `C0:3C0D`)"),
`frame_wiring.md` ("`$011F` the PREVIOUS body's VM wrote (op `0xE3`, `C0/3C05`)"),
`anim_family_delegation.md` ("`0134 E3 01  $011F := 1  <- UNLOCK, deliberately`") and
`bell_marle_regroup.md` ("`$011F=01` from the `$16FF` routine's own `E3 01`
(`C0:3C0D`)"). The port calls it `CtSceneExplore`.

⛔ **So no input can ever clear it.** Not A, not a direction, not waiting. Only the
script's own `E3 01` does it — and every earlier attempt failed for a second reason
on top of that: they watched for a few hundred frames, and the scene needs about
**2,900**.

### The live timeline, minute by minute

Restored from the savestate taken in Crono's house (loc `02`, ctx `01`, `$011F = 0`)
and advanced with an A tap every 120 frames. Each line is one read of everything.

```
watch  011F  0215 0217 0F03 | lead obj1 IP   lead pos     obj8 IP   obj10 IP
 00    00    00   07   03   | 0408           1A80,08FF    058C      0761
 05    00    00   07   03   | 0408           1A80,08FF    05C3      079C
 06    00    10   14   97   | 041A           1A80,08FF    0607      0761   <- the wake-up dialogue starts
 12    00    00   24   03   | 0421           1A80,08FF    0623      0761
 18    00    00   1B   0B   | 042F           1A5D,08FF    065E      0761   <- CRONO MOVES, by the script
 19    00    00   1B   0E   | 042F           18AB,08FF    065E      0761
 20    00    00   1B   00   | 0436           18A4,08FF    065E      0761
 21    00    00   1B   79   | 043E           18A4,08FF    0669      0761
 22    00    00   1B   6D   | 0445           18A4,08FF    0669      0761
 23    01    00   1B   6C   | 0408           18A4,08FF    0669      0761   <- UNLOCKED
```

⭐ **The scene moves the player itself.** Watches 18-20 walk the lead's position
`1A80 → 1A5D → 18AB → 18A4` with `$011F` still 0 — the get-out-of-bed sequence. The
position is script-driven, so "nothing moves while locked" was true only of *input*.

⭐ **`18A4` is the decomp project's own recorded value.** Its notes have
`pos = 18A4,08FF (frozen)` for the same room. Same figure, reached independently.

⭐ **The room's own script can lock but cannot unlock.** In this scene's script image
there are five `E3 00` sites and three `E3 01` sites: the four earliest locks sit
*before the first object's script* — the room's shared code — and **none of the three
unlocks is among them**. The unlocks are two in the lead's own script (`$242A`,
`$244C`) and one in object 8's (`$272B`, tightly paired with an `E3 00` at `$2725`).
So control comes back from the lead's script or object 8's, never from the room's.

### The payoff: Crono moves

With `$011F = 1`, the same tool that had "proved" a frozen player now moves him:

```
HOLD right   011F 01->01   pos 18A4,08FF -> 1904,08FF   dX=+96    MOVED
HOLD right   011F 01->01   pos 1904,08FF -> 1904,08FF   dX=+0     no change (a wall)
HOLD down    011F 01->01   pos 1904,08FF -> 1904,09FF   dY=+256   MOVED
HOLD left    011F 01->01   pos 1754,09FF -> 1754,09FF   dX=-432   MOVED
HOLD up      011F 01->01   pos 1754,09FF -> 1754,07FF   dY=-512   MOVED
```

**Four directions, three of them moving, one blocked by geometry.** Movement works,
the address is right, and the input path is right.

### What to carry forward

- **`$011F` is a control oracle, and the first thing a Chrono Trigger reader must
  read.** While it is 0 the game is running a scripted scene and the player is not
  being listened to. Saying "control is with the game" is correct and useful;
  narrating a frozen player is not.
- **Position is `$1800,X` / `$1880,X`** with `X = obj*2`; the lead in this room is
  object **1**, so `$1802`/`$1882`. `$0102`/`$0103` was never the player's tile.
- **The opening is long.** Roughly 2,900 frames from the house's arrival to control,
  and the pre-house intro is several thousand more. Any script that gives up after a
  few hundred frames will conclude, wrongly, that the player cannot move.
- ⛔ **Do not A-mash blindly through it.** The A taps advanced the dialogue, but the
  scene's own clock is what unlocks: the flag came up on a watch where nothing
  special was pressed.

### Single next step

Now that movement works, the original goal is reachable: drive Crono out of the house
with `SET_INPUT` and read `$011F` before every narration, then find the exit/door
handling and the shop and battle contexts (`0x0100` loc changes, ctx `0x2F`/`0x29`)
that the four BizHawk probes never reached.

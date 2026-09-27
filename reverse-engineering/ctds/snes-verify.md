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

# Dissidia New Game flow → Name Entry (host-mapped, ULUS10437)

RESEARCH ONLY. No repo code changed. Two independent host lineages agree on
the flow and the RAM verdict:

- **Lineage A** (`psp-ramdump` + plan files, 24 MiB guest-RAM dumps, `r1–r4` /
  `s2b–s2i` runs, sibling subagent).
- **Lineage B** (`oga-ppsspp-proof/psp-capture` savestates + framebuffer shots,
  `s1–s15` runs, 12-frame holds unless noted, `once:` = 60-frame hold).

DS button ids (capture harness): Cross(A)=0, Circle(B)=1, Start=3, Left=5,
Right=4, Up=6, Down=7. Every screen change verified with framebuffer
screenshots (raw RGBA → PNG, vision-read, transition hash-checked).

CORRECTION to the task's assumed order: the name-entry OSK comes FIRST
(title → nickname warning → OSK), and Play Plan → Bonus Day come AFTER the
name is finished (Start). The task assumed Play Plan/Bonus Day precede name
entry; measured flow says otherwise.

## Full screen sequence (fresh memstick,wof first boot)

| # | Screen | Exact visible strings | Controls (verified) | Evidence |
|---|--------|----------------------|---------------------|----------|
| 1 | Autosave warning | `This game has an autosave function. / While autosaving, the Memory Stick Duo™ access indicator will flash. / Please do not remove the Memory Stick™ or turn off the power.` / `OK` / `X Enter` | Cross dismisses | b1,b2 (boot f300/f600) |
| 2 | Square Enix logo | `SQUARE ENIX®` | auto-advances | s1-000100 |
| 3 | White flash/transition | (none) | — | s1-000500 |
| 4 | DISSIDIA logo splash | `DISSIDIA / FINAL FANTASY.` | auto-advances | s1-000600 |
| 5 | Title menu | `DISSIDIA / FINAL FANTASY.` / `New Game / Load Game / Data Install` / `© 2008, 2009 SQUARE ENIX CO., LTD. All Rights Reserved.` Hand cursor on **New Game** (default) | Cross confirms New Game. D-pad nav NOT exercised | s1-000700/000800 |
| 6 | Data Setup nickname warning | Header `Data Setup` + `X Confirm` / `Enter a nickname.` / `Other players can see your nickname during wireless play. Avoid entering any sensitive information.` | Cross dismisses | s1-001000/001100 |
| 7 | Player Name Entry (OSK) | Header `DataSetup / Player Name Entry / X Confirm`. Name field (prefill = PSP nickname). 5×12 key grid (§OSK). Footer: `X Select / O Delete / Start Finish / Select Shift / L English Full-width / R 日本語 / Square Space` | D-pad moves highlight (all 4 dirs + both wraps verified); Cross types; Circle deletes; Start finishes. Select/L/R/Square NOT exercised | r1–r4-*.png, s4-*, d1,d2 |
| 8 | Play Plan | Header `Data Setup / Play Plan` / `What sort of gamer are you?` / `Casual (selected, blue+hand) / Average / Hardcore` | Cross confirms Casual. D-pad nav NOT exercised | s2-000200, s7-000200, s2b-0200..1500 |
| 9 | Bonus Day | Header `Data Setup / Bonus Day` / `Which day of the week do you play the most?` / `Mon (selected) / Tue / Wed / Thu / Fri / Sat (blue) / Sun (red)` | Cross confirms Mon. D-pad nav NOT exercised | s3-000150, s9-000200/000300, s2c-0200/0500 |
| 10 | Confirm settings | `Data Setup` / `Proceed with these settings?` / `Player Name <name> / Play Plan Casual / Bonus Day Mon` / `YES (selected) / NO` | Cross confirms YES. Displays the FULL typed name (12-char entry `PPSSPP111111` confirmed on screen, s10-000300) | s3-000225+, s10-000200/000300, s2d2-000150 |
| 11a | Save slot select | `■ Save` / slot `DISSIDIA FINAL FANTASY / <date> 297 KB / GAME DATA / DATE: ... / PLAYER: PPSSPP / TIME: 0:00:53`, empties show `NO DATA` / `X Enter / O Back` | Cross selects slot; Circle backs | s11-000400, s2g-000150, s2e-0050 |
| 11b | Overwrite prompt (occupied slot only) | `Do you want to overwrite the data? / Yes / No (No SELECTED, safety default)` / `GAME DATA / <date> / 297 KB` / `X Enter / O Back` | **Left** moves No→Yes (verified); Right is INERT here (PPSSPP `PSPSaveDialog`: Left fires only when choice==0, Right only when choice==1). Cross confirms | s12-000200, s13-000200, s14-000100 |
| 12 | Save completed | `Save completed / GAME DATA / <date> 297 KB / ○ Back` | Circle backs | s14-000500..1200, s2f-0150/0600 |
| 13 | Post-save Yes/No (no-save path) | `Game was not saved. / Proceed to the save or deletion screen? / Yes (selected) / No / X Enter` | Cross on Yes | s2g-000550 |
| 14 | Proceed-to-save confirm (no-save path) | `Proceed to save? / (Selecting "No" will take you to the deletion screen) / Yes (selected) / No / X Enter` | Cross on Yes → black transition → back to Save slot screen (loop) | s2h-000250, s2i-000200 |
| 15 | Opening FMV (after save → Circle Back) | In-engine cutscene, no HUD. Subtitle `Might I be too late...?`, then summon/battle shots | skippability NOT verified; FMV→title tail NOT mapped | s15-000300/001000/001500 |

Notes: vision misreads game identity on plain-menu screens — the Dissidia
strings above match the adapter's known values and the ULUS10437 boot. Setup
menus (8/9/10) have animated shimmer (frame hashes never repeat — compare
content, not hashes); save screens (11/12) are byte-static. Do NOT trust
vision for the OSK text field's exact trailing glyph (highlight bleed adds a
phantom char at 480×272); RAM is ground truth every time.

## OSK layout (§7 detail)

- 5 rows × 12 cols, English lowercase (PPSSPP `OSK_KEYBOARD_LATIN_LOWERCASE`,
  `numKeyCols[0]=12, numKeyRows[0]=5`), verified on screenshots in both
  lineages and against `PSPOskConstants.cpp` (`oskKeys[0]`):
  - R1: `1 2 3 4 5 6 7 8 9 0 - +`
  - R2: `q w e r t y u i o p [ ]`
  - R3: `a s d f g h j k l ; @ ~`
  - R4: `z x c v b n m , . / ? \`
  - R5: `= < > ' € ¥ & § £ * ( )`
- Cursor model = PPSSPP host-side `selectedChar` (flat `row*cols+col`, 0–59).
  Verified live against savestate HLE ground truth (lineage B, §RAM):
  Left 0→11 (`+`, in-row wrap), Right 11→0, Down 0→12 (`q`), Up 12→0,
  Down 11→23 (`]`). Left/right wrap within the row, up/down wrap modulo 60 —
  exactly `PSPOskDialog::Update`.
- Entry anchor: highlight starts on `1` = index 0 (`selectedChar = 0` init).
- Key repeat: threshold 10 frames, rate 5 dialog-updates (≈half-rate → first
  repeat ≈20 frames). **12-frame holds = exactly one char/step, no repeat**
  (verified: 6 walk steps + 1 type, zero drift). 60-frame holds repeat
  (≈+5 chars per hold — use only for fast-fill, never for single steps).
- Max name length 12 (`FieldMaxLength`; extra Cross presses at full buffer are
  no-ops — verified across s1 f2500/3000/4000, text pinned at `PPSSPP111111`).
- Prefill = PSP system nickname (`PPSSPP` here — environment-dependent, never
  assume). The game passes the SAME guest buffer for `intext` and `outtext`.

## RAM findings

Lineage A dumps: full 24 MiB guest RAM (`0x08800000–0x0A000000`) via
in-process `psp_debug_read`. Lineage B: 32 MiB `psp-capture` savestates,
RAM section parsed with scratch `ppzparse.py` (zstd + `Memory` section,
cookie-validated), dialog truth with `ppzosk.py`.

### Entered-text buffer: FOUND, VERIFIED LIVE (both lineages)

- Address: **`0x09B3FC00`**, UTF-16LE, NUL-terminated. `intext == outtext`
  (same pointer — game reuses one buffer for seed and result).
- Lineage A: unique `PPSSPP1` UTF-16LE hit in 24 MB, stable across a
  120-frame no-press resample; live R3 series base(11) → Delete(10) →
  Select-`1`(11), exactly one UTF-16 unit per key. Pointers to the buffer at
  `0x09B3FB44`/`0x09B3FB4C` inside adjacent Osk-param-like structs.
- Lineage B cross-check (guest buffer vs host `inputChars` ground truth):

| state | host `inputChars` | guest `0x09B3FC00` | match |
|---|---|---|---|
| s1-1800 (typing) | `PPSSPP11111` (11) | `PPSSPP11111\0` | ✓ live |
| s4-1900 (full) | `PPSSPP111111` (12) | `PPSSPP111111\0` | ✓ live |
| s2-200 (post-Finish) | `PPSSPP` | `PPSSPP\0` | ✓ finish write |
| s7-200 (post-Finish) | `PPSSPP111111` | `PPSSPP111111\0` | ✓ finish write |

  The buffer tracks typing live AND receives the Finish write (confirm screen
  shows the full 12-char name). `outtextlen = 13` (12 + NUL) read from the
  game struct — matches the observed cap.
- Game struct chain (deterministic across 7 boots/sessions — same addresses):
  `SceUtilityOskParams` AT `0x09B3FAE4` (size=64, fieldCount=1,
  fields=`0x09B3FB24`) → `SceUtilityOskData`: intext(+32), outtextlen(+36),
  outtext(+40), result(+44).
- Status: VERIFIED-LIVE. Heap address — re-discover per boot by UTF-16LE
  content scan (name known from input-echo), NOT hardcoded. Cross-boot
  stability NOT tested (hypothesis: shifts).

### OSK highlight cursor: NO guest-RAM cursor (honestly blocked, by design)

- Lineage A: exact-series search over ALL 24 MB, 5 dumps, 4
  screenshot-verified highlight states → 0 hits; row/col split hypotheses
  killed by control + held-out transition; sole small-cluster survivor is
  float mantissa in animation data (non-ordinal).
- Lineage B: bounded diff (cursor 11→0 vs no-press control, `0x09B00000–
  0x09E00000` + `0x08800000–0x08C00000`): 11k+ differing u32s (render churn),
  **0 small-ordinal control-stable candidates**.
- Ground truth from PPSSPP source (`Core/Dialog/PSPOskDialog.*`, pin
  f293b10): the OSK is HLE — highlight is host-side `int selectedChar`,
  footer `English Full-width` matches verbatim. No guest-RAM cursor can exist
  by construction. Wrap behavior verified on-screen regardless (Left 0→11,
  Right 11→0).
- Adapter pins `0x09B3FA30` (cursor) / `0x09B3FA38` (max) on setup screens:

| screen | cursor | max | meaning |
|---|---|---|---|
| OSK open | garbage (heap reused by OSK structs) | garbage | pins INVALID here |
| Play Plan | 0 | 2 | PlayIndex 0–2 ✓ |
| Bonus Day | 0 | 6 | 7 days ✓ |
| Confirm YES/NO | 0 | 1 | 2 options ✓ |
| Save/overwrite (system dialogs) | 0 | 1 | STALE (host-side dialog) — needs screen gate |

## Reader proposal (input-echo tracking — the only viable design)

1. **OSK-mode enter/exit (ready-gate additions):**
   - ENTER when the nickname-warning screen validates (game-side screen;
     needs a fingerprint — follow-up RE) — fallback enter: Play Plan/Bonus
     Day/Title fingerprints all invalid AND confirm screen invalid AND a fresh
     boot is inside Data Setup (no board live, no manager).
   - While in OSK mode, route D-pad + Cross/Circle/Select/Square/L/R/Start
     to the echo tracker instead of menu nav. (Cursor/max pins are garbage on
     this screen — never consult them here.)
   - EXIT when Play Plan validates (adapter `PlayIndex()>=0`) — proves the
     name was finished; also exit on Bonus Day/title/manager-live.
2. **Echo state:** `idx` (start 0 = `1`), `cols` (12 for EN; re-read per L/R
   language switch), `rows` (5). Moves: left/right ±1 with in-row wrap, up/down
   ±cols with modulo-total wrap (mirror the HLE code). Maintain `text` mirror:
   Cross appends label[idx] (fixed reader-side key table per language —
   layout is PPSSPP-fixed, cite `PSPOskConstants`), Circle drops last char,
   Square appends space, Select toggles case table.
3. **Grounding (anti-desync):** after every Cross/Circle, re-read the guest
   buffer (content-scan rediscovery per boot, else cached pointer validated by
   matching the echo `text` as UTF-16LE prefix — the buffer tracks typing
   live, §RAM); on mismatch, announce the RAM text and resync echo to it. On
   Start (Finish), read final RAM text once and announce it in the Play Plan
   speech. Post-Finish confirm screen (`Player Name <name>`) is a second
   visual ground-truth.
4. **Exact speech strings:**
   - Enter: `Player name. Type your name.`
   - D-pad: `<key>. Row <r> of 5. Column <c> of 12.` (e.g. `2. Row 1 of 5. Column 2 of 12.`)
   - Cross: `<key>. <text-so-far spelled>` then `Name <full>.` (e.g. `1. Name P P S S P P 1.`)
   - Circle: `Deleted <char>. Name <full>.` / at empty: `Name is empty.`
   - Square: `Space. Name <full>.`
   - Where-Is: `Player name entry. <full>. Cursor on <key>.`
   - Finish: `Name <full>. Play Plan. Casual. Row 1 of 3.` (existing Play Plan speech takes over)
5. **Screen ID for the gate (today, no new RE):** OSK up = NOT
   Title/Play/Bonus (all invalid) AND no board/manager AND boot in Data Setup.
   Positive fingerprint (warning-screen IDs or OskData struct chain from a
   static) is explicit follow-up work — do NOT ship content-scan-only gating.

## Environment notes

- Harness memstick: absolute path recommended (`/home/devin/dissidia-ne/save`
  → full tree, `ULUS10437GameData00` 297 KB written, `Save completed` shown).
  Relative `./dissidia-ne/save` also booted/saved in lineage B (save dated
  2026/09/29 13:33 written by the overwrite path).
- Lineage A tool: `psp-ramdump` (scratch, `dissidia-ne/psp-ramdump`)
  reproduces `psp-capture` framebuffers bit-identically and dumps 24 MB RAM
  in ~1 s. Plan syntax: `hold <btn> <start> <len>`, `dump <frame> <name>
  <addr> <size>`, `shot <frame> <name>`, `frames <n>`.
- Lineage B tools (scratch, Windows dir): `ppzparse.py` (ppz→32 MB RAM, needs
  `pip install zstandard`), `ppzosk.py` (host dialog truth), `verify_chain.py`
  (struct walk), `ramdiff.py` (bounded diff), `run-s*.sh` capture plans.
- Shared memstick: saves persist across runs — later runs see occupied slot 1
  (overwrite path) instead of empty slots. Use a fresh save dir for the
  first-boot sequence.

## Open questions (not this task)

1. Title/Play Plan/Bonus Day/confirm D-pad navigation (only defaults+Cross exercised).
2. Select/L/R/Square on the OSK (untested); Japanese/kana layouts unmapped.
3. Cross-boot stability of `0x09B3FC00` and of the title/setup heap structs.
4. Warning-screen ("Enter a nickname.") fingerprint for a positive OSK gate.
5. FMV→title tail after the opening movie (game start not attempted).

## Artifact index

- Screens (Windows): `C:/Users/Devin Prater/dissidia-nameentry/` — lineage B:
  `s1/s2/s3/s4/s7/s9/s10/s11/s12/s13/s14/s15-*.png` (full chain boot→FMV),
  `m-*.png`, `d1/d2` (prior OSK refs with full control legend); lineage A:
  `r1/r2/r3/r4-*.png`, `s2b/s2c/s2d2/s2e/s2f/s2g/s2h/s2i-*.png`.
- Dumps (WSL): `/home/devin/dissidia-ne/` — lineage A: `r1–r4` plans/dumps,
  `s2e-ram-confirm.bin`, `val-ram.bin`; lineage B: `ram-{1200,1400,1600,1800}.bin`,
  `ram-s{2,4,5}-*.bin`, `v-*.bin`, `s1–s15/` states+shots.
- Tools (scratch, uncommitted): lineage A `ramdump-main.cpp`,
  `psp-ramdump`, `rgba2png.py`, `cursor-hunt.py`, `run-s1/s2b/...` plans;
  lineage B `ppzparse.py`, `ppzosk.py`, `verify_chain.py`, `ramdiff.py`,
  `run-s1/s2/s3/s4/s5/s7/s9/s10/s11/s12/s13/s14/s15.sh`, `check-*.sh`.

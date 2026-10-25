# Mortal Kombat: Unchained (PSP, ULUS10102) — reverse-engineering notes

Status: **SCAFFOLD, first live session done** (2026-10-16). The adapter
exists (`Core/mku_adapter.cpp`), is registered, and is host-tested against
synthetic RAM (17 checks: identity, honest scaffold speech, the DumpState
anchor scan). The first live session (below) pinned the EBOOT load base
from RAM and ran the anchor scan against the real game. **No game
structure is verified yet** — the cursor has not been chased — so the
adapter still speaks its honest scaffold lines.

Where this leads (the user's roadmap, 2026-10-16): this adapter is stage
one — Deception's Konquest, played through Unchained. Stage two is MK
Armageddon (PS2), stage three the end goal, MK Shaolin Monks (PS2).
The PS2 stages need an emulator backend this project doesn't have yet;
they get their own notes when they start.

## Identity

- Mortal Kombat: Unchained (USA), PSP — the PSP port of Mortal Kombat:
  Deception, with the Deception roster plus Kitana, Jax, Frost and Blaze.
- Disc ID **ULUS10102**, read by `Core/psp_core.cpp` from the disc's
  PARAM.SFO; the adapter registry exact-matches the full string (see
  `adapters.cpp` on why PSP matching is never a prefix match).
- Development image: the USA ISO from Internet Archive
  (`pspmortalkombatunchainedusa`), held OUTSIDE this repository per the
  no-ROMs rule, like every other game's image.
- **Verified 2026-10-16 from the image itself:** PARAM.SFO reads DISC_ID
  ULUS10102, TITLE "Mortal Kombat: Unchained", DISC_VERSION **1.03** —
  the adapter's registry entry is correct. EBOOT.BIN is 4,922,640 bytes
  with the `~PSP` magic, i.e. **encrypted** as retail discs are.
  **Decrypted 2026-10-16** with pspdecrypt (tag C0CB167C, type 1):
  `EBOOT.dec`, 4,922,301 bytes, ELF 32-bit LSB MIPS, sha256
  `c4266d1f3aaa2ff77b065b3b0e3f35c2d2534fc207a0d69cb4fd2b7484168b08`,
  held with the image (outside the repo). Its program headers start at
  vaddr 0 (a relocatable PRX), so the load base must come from a live
  boot — never carried over from another game (project rule).
  **Load base pinned 2026-10-16 from live RAM: `0x08804000`** (method and
  evidence in "First live session — results" below). Note the program
  headers: the first PT_LOAD starts at file offset **0x74** (filesz
  0x3AD660, R+X), the second at vaddr 0x3AD660 (filesz 0x964, memsz
  0x229960, R+W) — **the ELF header itself is in no loaded segment**, so
  it never appears in RAM, and a raw BinaryLoader Ghidra import would
  skew every address by 0x74 exactly as Dissidia's first import did.
  Import with the ElfLoader (project `MKU_ELF`). The image
  carries **68 .PMF movies** under `PSP_GAME/USRDIR/MOVIEPSP/` (OPENING,
  title/logo clips, VTC_* character clips, V_MKD*/V_MKM* Deception-era
  clips) — the Konquest story scenes are NOT among them; they play
  in-engine, which the audio-description research notes
  (~/workspace/games/mk-unchained/ad-research.md) plan around.

## What needs mapping, in the order a player meets it

1. **Main menu + cursor.** One vertical list (expected: Arcade, Versus,
   Practice, Konquest, Chess Kombat, Puzzle Kombat, Endurance, Krypt,
   Kontent, Options — confirm the exact items and order against the running
   game before speech ever names them). This is the Dissidia title/menu
   shape: find the text, find the cursor, speak the row on move.
2. **Character select.** A grid of fighters; names first, grid geometry
   (rows/columns, wrap behaviour) second.
3. **Fight state.** Both fighters' health, round wins, the match timer —
   the values a blind player cannot get from the screen at all, and the
   natural `CueSnapshot` candidate once located (distance/facing cues are a
   later question; health and rounds are not).
4. **Konquest.** The story mode: dialog and objective text. The heaviest
   text surface in the game and the one with the most speech value.
5. **Chess Kombat / Puzzle Kombat boards.** Cursor position on a grid;
   pieces/blocks named by table, not by pixels.

## Method (the Dissidia path, reused)

- Boot the image in the host PPSSPP core (`scripts/psp-host-proof.sh`
  proves that core on Dissidia; its game-ID grep is Dissidia-specific, so
  for this game run the proof binary directly until Unchained gets its own
  proof script).
- At each screen, run the adapter's **DumpState anchor scan**: it walks
  user RAM (0x08800000–0x0A000000) for the game's own menu words (KOMBAT,
  KONQUEST, CHESS, PUZZLE, ARCADE, VERSUS, PRACTICE, ENDURANCE, OPTIONS)
  in ASCII and UTF-16LE and logs candidate addresses. A candidate is a
  lead, not a finding.
- Chase values that change with the cursor using the RAM watch/scan tools
  (`Core/ramwatch.cpp`, `Core/ramscan.cpp`); confirm every address against
  the screen it names before any speech uses it — the project's standing
  rule, bought with the DBZ one-stride error.
- Expect heap-allocated text to move per boot (the Dissidia tooltip
  lesson): when it does, pin the TEXT table in the adapter, not the
  address.
- Once text buffers are located, a Ghidra decomp of the EBOOT (the
  `*-DECOMP_INDEX.md` pattern in this directory) maps the menu/widget
  system properly instead of address-by-address.

## First live session — checklist

- [x] 2026-10-16: host proof boots the retail ISO end to end —
      `PROOF: gameid=ULUS10102`, 900/900 frames, fb=480x272,
      nonzero framebuffer, shutdown-clean (psp-proof run directly;
      scripts/psp-host-proof.sh asserts Dissidia's ULUS10437, so its
      final grep fails MK by design). Toolchain that got here:
      bootstrap-deps (PPSSPP pinned f293b10), ~/ffmpeg-host built from
      the PPSSPP ffmpeg fork with the proven minimal config,
      ppsspp-stage-assets.sh for the boot assets. Recurring
      UNKNOWN_SEMID semaphore errors in the boot log are PPSSPP HLE
      noise; the game runs through them. The boot log does NOT print
      the EBOOT load base — pin it from RAM in the scan harness,
      do not assume 0x08804000.
- [x] 2026-10-16: anchor scan against the live game (results below) —
      the menu strings turned out to be **static image data** (they sit in
      the EBOOT's own rodata), resident from the first checkpoint on, so
      "resident at title vs menu" dissolved: they are always resident.
      Which screen the tap script ended on is NOT yet confirmed — the
      cursor hunt will settle it, since a value that tracks the D-pad
      only exists on a live menu.
- [ ] Move the main-menu cursor one step at a time; find the cursor index
      (a small integer that tracks the D-pad exactly).
- [ ] Enter Arcade and a fight; find health by controlled damage (P1
      health decreases only when P1 is hit; verify against the round
      ending, not against a plausible-looking number).
- [ ] Record each finding here with the screen it was confirmed against,
      the same day — unrecorded addresses are how this file stays a
      scaffold.

## First live session — results (2026-10-16)

Bench harness: `~/mku-scan/mku-scan.cpp` (outside the repo; links the
repo's Core objects + the PPSSPP host objects from `~/oga-ppsspp-proof`).
It boots the ISO on `psp_core`, attaches the real adapter through a
hand-built Host routed to `psp_debug_read`, taps Start/Cross every 180
frames from frame 900, and snapshots RAM at checkpoints. Run log:
`~/mku-scan/run1.log`. A savestate from frame 6000 sits at
`~/mku-scan/after-boot.ppst`.

### Load base: 0x08804000 — measured, not assumed

The ELF-header hunt failed for a structural reason: the first PT_LOAD
starts at file offset 0x74, so the header is never loaded into RAM (see
Identity above). The base was pinned from the anchor strings instead.
Each anchor's RAM address minus its vaddr (file offset − 0x74, segment 0)
must give the same base for the string's static copy:

- **PRACTICE occurs exactly once in the whole EBOOT**; its RAM copy at
  0x08ABD538 gives base **0x08804000** with no ambiguity.
- PUZZLE, KOMBAT, CHESS, KONQUEST, ARCADE and OPTIONS each have several
  file occurrences, and **every one of the seven is consistent with
  0x08804000** among its candidates.

RAM address = `0x08804000 + vaddr` for this build (ULUS10102 v1.03)
only. The image spans 0x08804000–0x08BEE660 (segment 0) with the data
segment at 0x08BEB660, BSS to ~0x08E0EFC0.

### Anchor scan (adapter DumpState, live, frame 6000)

65 hits, all ASCII, **zero UTF-16** — the game's menu text is plain
ASCII. Counts: KOMBAT 22, KONQUEST 10, CHESS 10, PUZZLE 8, ARCADE 5,
PRACTICE 4, OPTIONS 4, ENDURANCE 2, VERSUS 0. The static image clusters
(RAM → vaddr), which are the leads worth pulling:

- **Menu-label candidates**: `ARCADE` 0x08AD4128, `PUZZLE` 0x08AD4140,
  `KONQUEST` 0x08AD414C (vaddrs 0x294128/0x294140/0x29414C) — mode names
  ~0x18 apart in one string pool, the closest thing yet to the main
  menu's own table.
- **Stride-8 run**: `KOMBAT` 0x08ABF408, `PUZZLE` 0x08ABF410, `CHESS`
  0x08ABF418 (vaddrs 0x23B408+) — packed names or a pointer table's
  targets in a row.
- **Stride-0x30 records**: `CHESS` at 0x08AC016E, 0x08AC019E, 0x08AC01CE,
  0x08AC020C, 0x08AC021C, 0x08AC0238, 0x08AC0248 (vaddr 0x23C16E+) —
  fixed-size records beginning with CHESS text; Chess Kombat data is the
  obvious suspect.
- **PUZZLE stride-0x1C run**: 0x08ABA914, 0x08ABA930, 0x08ABA94C,
  0x08ABA964 (vaddr 0x227914+).
- Single static copies: `KONQUEST` 0x08ACFA10 (vaddr 0x23BA10),
  `PRACTICE` 0x08ABD538 (vaddr 0x229538), `OPTIONS` 0x08ABC9A6 /
  0x08ABD581 / 0x08ABD5B0.
- **Heap copies — do not pin**: hits at 0x09988094 (ENDURANCE) and
  0x09E4D380–0x09E725DF (PUZZLE, ENDURANCE, PRACTICE ×3) are a loaded
  data-file region; they will move per boot. The Ghidra byte search
  (below) settles two of these: **ENDURANCE and VERSUS occur zero times
  in the EBOOT image** — both live only in data files loaded at runtime,
  so their menu entries are reached through the data-file string tables,
  not the static image.

Checkpoint counts (frames 1500/3000/4500) were stable for the static
strings from the first checkpoint; KOMBAT's total moved 19→27→26, i.e.
dynamic buffers come and go around a static core.

### Next

1. Cursor hunt: sit on the menu (confirm the screen via a value that
   tracks the D-pad), diff RAM across one-step moves with ramscan.
2. Ghidra follow-ups (project ready, see below): decompile
   FUN_000c43c4 and FUN_001ad3c4; find who references the string-pointer
   table around vaddr 0x2F3200–0x2F3900 — that table's owner is the
   menu system.
3. Fight state after the menu speaks: health by controlled damage.

## Ghidra — MKU_ELF index (2026-10-16)

Project: `~/mku-scan/ghidra/MKU_ELF` — EBOOT.dec imported with
`-loader ElfLoader -processor MIPS:LE:32:default` (the ElfLoader applies
the segment mapping itself, so listing addresses ARE vaddrs; RAM =
vaddr + 0x08804000). **12,640 functions.** Full function list + anchor
reference dump: `~/mku-scan/mku-decomp-index.txt`, generated by
`~/mku-scan/MkuIndex.java` (byte search per the Dissidia rule).

Cross-validation of the load base: the byte search found the anchors in
the image at exactly (live RAM address − 0x08804000) — KOMBAT at vaddr
0x2B7EF4, KONQUEST 0x2CBA10, PRACTICE 0x2B9538, the stride-8 run at
0x2BB408+ — so the strings resident in RAM are the image's own, at the
measured base. The base now rests on the strings twice over (live
arithmetic + static image), before any code is trusted.

Who references the menu words (the threads to pull):

- **FUN_000c43c4** references all three of the stride-8 run (KOMBAT
  0x2BB408, PUZZLE 0x2BB410, CHESS 0x2BB418) — one function touching the
  packed mode-name table; prime menu-system suspect.
- **FUN_001ad3c4** references `ARCADE` at 0x2D0128 — the menu-label pool
  copy (the ARCADE/PUZZLE/KONQUEST cluster at 0x2D0128–0x2D014C).
- **FUN_00106830 / FUN_00106c08** reference the stride-0x30 CHESS
  records (0x2BC20C, 0x2BC21C, 0x2BC238, 0x2BC248) — Chess Kombat data
  handlers.
- A **string-pointer table around vaddr 0x2F3200–0x2F3900** references
  KOMBAT (from 0x2F32D8–0x2F32EC, 0x2F3888), KONQUEST (0x3113D4+),
  OPTIONS (0x2F36BC, 0x2F38E4) and PRACTICE (0x2F38A8, 0x2F38BC) from
  data, not from inside any function Ghidra defined. Finding the code
  that walks this table is the direct route to the menu structure.
- VERSUS and ENDURANCE: **0 occurrences in the image** (see above).

## Cursor hunt 1 — the pause menu (2026-10-16)

Method (the screenshot-first workflow): `~/mku-scan/mku-hunt.cpp` boots
the game (or restores a state), captures a **framebuffer PNG and a full
24 MB RAM dump at every step**, and walks the D-pad on a script. The
screenshots are the ground truth — every RAM claim below was checked
against a screen that was actually looked at. Snapshots + analysis:
`~/mku-scan/hunt1/`, `~/mku-scan/analyze-hunt.py`.

The run-1 state turned out to be mid-fight, paused — the tap script had
fought through character select (Scorpion vs Hotaru) and a Start tap
opened the pause menu. Lucky: a real, simple menu to learn the method
on. **Pause menu, verified from screenshots at all 10 steps**: RESUME /
MOVES LIST / PLAYER SELECT / MAIN MENU, and the cursor **wraps** at both
ends. Cursor sequence across the snapshots (idle pair, 6×Down, 2×Up):
0,0,1,2,3,0,1,2,1,0.

Findings:

- **No plain cursor index exists** for this menu — not as u8/u16/u32,
  not 0- or 1-based, not scaled by a stride, not one-hot, not as any
  group-constant encoding anywhere in the 24 MB window. A verified
  negative: this widget does not keep a naked selection counter in main
  RAM.
- **Selection is a pointer.** The word at RAM **0x090645DC** stepped
  through exactly four values in menu order — 0x09063630 (RESUME),
  0x09062B40 (MOVES LIST), 0x09062050 (PLAYER SELECT), 0x09061560
  (MAIN MENU) — a constant −0xAF0 per step down, +0xAF0 per step up.
  Each target is an engine pool object (magic ` OBJ` at +4, shared
  class word 0x08BAF3C0, records 0xAF0 apart, sub-object inside); no
  byte inside the objects separates selected from not — selection
  lives in the external pointer, held by a neighbouring object around
  0x09064580. **These are heap addresses from one session: they will
  move per boot.** The durable routes are (a) find the code that
  writes this pointer in the decomp (the pause input handler), or
  (b) relocate per boot from the objects' ` OBJ` + class signature.
- **The block at RAM 0x08BBD800–0x08BBDDFF (and its +0x31000 mirror) is
  display data, not state** — hundreds of bytes flip deterministically
  with the selection because they ARE the rendered highlight. It will
  happily waste a future session's afternoon if mistaken for structs;
  it is the GE/render side. Idle churn is real too: ~63 KB of RAM
  changes across 30 idle frames behind the pause box (background fight
  animation), which is why the idle control pair matters.

Next: the same hunt on the **main menu** (the adapter's actual first
target) — from pause, select MAIN MENU, confirm by screenshot, walk
it. Expect the same object-pool pattern; with ~10 items, watch for a
current-item pointer like the one above. In parallel, the Ghidra route:
the writer of the 0x090645DC-style pointer is the menu input handler —
finding it in the decomp turns per-boot heap addresses into a static
structure.

## Cursor hunt 2 — the main menu: VERIFIED, adapter speaks it (2026-10-16 night)

Method per the house skill (~/workspace/custom-skills/screenshot_verified_ram_hunting):
screenshots are ground truth, control pair first, encodings tested against
the screen-written sequence. Benches: mku-goto.cpp (pause → MAIN MENU; caught
a **QUIT GAME "Are you sure?" (NO/YES)** confirmation on the way — selecting
MAIN MENU in the pause menu does not go straight there), mku-hunt.cpp
(parameterized walk), mku-hunt4.cpp (cold-boot re-verification), mku-live.cpp
(adapter attached to the live core).

### The main menu (screenshot-verified, all 16 walk frames viewed)

Nine items, in order, game's own spelling, each with its own description
line (the adapter needs only the names):

0. **KOMBAT** — "Classic Mortal Kombat fighting"
1. **CHESS KOMBAT** — "The perfect blend of classic chess and Mortal Kombat"
2. **PUZZLE KOMBAT** — "A deadly puzzle game. Mortal Kombat style."
3. **KONQUEST** — "Explore the MK universe in this single player adventure mode."
4. **MK WIRELESS** — "Test your MK skills against other PSP players in your Wireless area."
5. **THE KRYPT** — "Unlock hundreds of items with your MK kurrency."
6. **KONTENT** — "View movies, images and other unlocked items you've earned."
7. **PROFILES** — "Manage your profile."
8. **OPTIONS** — "Change settings for game rules, sounds and other options."

Footer hint: "L — ENTER PROFILE KODE". There are NO top-level Arcade/Versus/
Practice/Endurance entries — they live inside KOMBAT, which explains the
ARCADE label pool in the image (a submenu's strings). The menu's character
render (Jax, Mileena, ...) rotates between sessions; it is decoration, not
state. Highlight sequence over the walk: control pair at 0, DOWN ×11 →
1,2,3,4,5,6,7,8,**0**,1,2, UP ×3 → 1,0,**8** — **wraps at both ends**.

### The selection state — TWO representations (unlike the pause menu)

* **A plain 1-based index: u32 at RAM 0x09E61750** — the UNIQUE index hit in
  the whole 24 MB (values 1..9 tracking the walk exactly, at u8/u16/u32
  widths, same address). Its surrounding struct is byte-identical across
  boots. ⛔ The address is HEAP (past the image end 0x08E0EFC0); it held
  across two independent boots (savestate chain AND cold boot — hunt4 read
  1 at arrival, 2 and 3 after two DOWNs, screenshots agreeing), which says
  PPSSPP's allocation is deterministic along this front-end path, not that
  the address is contractual. No static pointer to the struct exists (the
  only pointers to it are heap-internal, a doubly-linked pair). The durable
  fix is still the decomp route: find the writer.
* **The object-pool pointer also exists**: u32 at 0x0911F868 stepping
  **−0xCE0 per item** through nine pool objects 0x0911F700 → 0x09119000 —
  the pause menu's architecture at a different stride, per-boot addresses,
  useful as a session-scoped cross-check only.
* Control-pair churn on this menu: ~37K bytes (per-item preview art makes
  real steps churn ~890K). The flag word below was silent across the pair.

### The screen gate: 0x08BA94A8 — ⛔ RETRACTED (see "Gate repair", below)

Derived by elimination: ==1 on the main menu in both boots at multiple
cursor positions, ==0 in the paused fight and at the quit dialog. Two other
words matched the same pattern (0x08DDA778 in BSS, 0x09EB6C9C in heap); the
static one is the adapter's gate. ⛔ NOT yet checked against the title
screen or the KOMBAT submenu — if it proves to be a broader front-end flag,
the adapter's "Main menu" wording must narrow. Checks queued with the
submenu hunt.

**Caught trap**: the struct around the cursor (+0x08 context words
0x08B14830 / 0x08BAF6F8) also exists in the fight with the cursor word
reading 1 — the word is the shell's main-menu cursor variable, persistent
across screens, NOT a "current screen" indicator. Speech is safe only
because the flag gates it. Never read the cursor without the flag.

### Adapter wiring (mku_adapter.cpp, 2026-10-16)

OnMainMenu() = flag==1 AND cursor in 1..9. WhereAmI/MenuState speak
"Main menu. <Item>, <n> of 9."; MenuNext/MenuPrev speak the row landed on;
ready() = OnMainMenu(). Host test: 23 passed (6 new: ready gating, row
speech, nav speech, flag-off silence, out-of-range rejection). **Live test
(~/mku-scan/mku-live.cpp, cold boot, adapter attached)**: ready=1 at the
menu; spoken sequence Kombat 1 → Chess 2 → Puzzle 3 → Chess 2 → Kombat 1 →
**Options 9 (the wrap)** — every line matching the walk's screenshots.
First game screen this adapter speaks.

### Next

1. KOMBAT submenu hunt (Arcade/Versus/Practice/Endurance + whatever else) —
   same walk; also settles whether 0x08BA94A8 is main-menu-only.
2. Title-screen check for the flag.
3. Decomp route to the cursor writer (ghidra-allegrex re-import still
   first) — replaces the heap address with a static anchor.
4. Fight state (health/wins/timer) — now the biggest unmapped piece.

## Gate repair — the "flag" was a strobe (2026-10-16 night, later)

The submenu hunt (next section) read the gate word at a state restore and
got 0 on a screen the screenshots proved was the main menu. Auditing the
word across **all** dumps on file killed it: it reads 0 in 8 of the 20
main-menu dumps (including a control-pair member) and 1 once in a paused
fight. It is a phase/strobe word that three samples happened to flatter.
**The flag gate shipped for a few hours on three samples; no speech built
on it was ever reliable.** Lesson recorded in the adapter source and the
house skill: a gate candidate must survive EVERY dump, not the dumps that
were open when it was derived.

The search for a real gate also eliminated the obvious families: no word
is constant-and-distinct in the static data/BSS region across all screens,
and the cursor word itself is the shell's persistent variable (it keeps
the main-menu selection through submenu, character select, and fights —
it describes where the menu WILL resume, not where you are).

What survived every dump — **gate v3**, now in the adapter:
OnMainMenu() = cursor in 1..9 AND the menu's current-item pointer
(0x0911F868) holds exactly the pool object the cursor names
(0x0911F700 − (cursor−1)·0xCE0, ` OBJ` magic at +4) AND the Kombat
submenu's current-item pointer (0x09118348) does NOT hold one of its
three objects. The third clause exists because a child screen leaves the
parent's state untouched underneath it: on the submenu the first two
checks still pass. Presence is not focus; **top screen wins**. In the
fight the holder cell is garbage; on character select it is null.
Host tests 24 passed; the live run re-verified both directions (menu
speech on the menu, ready=0 + fallback + silence on the submenu).

## The Kombat submenu (2026-10-16 night)

Three items — **ARCADE, ENDURANCE, PRACTICE** — wrapping, reached with
Cross on KOMBAT. (No Versus: on PSP, versus play lives under MK WIRELESS.)
Its selection is the front-end's uniform pattern again: a current-item
pointer at **0x09118348** stepping **−0xCE0 per item** through three
` OBJ` objects (0x091181E0 / 0x09117500 / 0x09116820); no numeric index
found (the main menu's index word does not move here). The submenu's
holder cell is 0 on the main menu, in the fight, and on character
select — it is this screen's "I am on top" tell, and gate v3 uses it.

Bench oddity, recorded unresolved: **no button exits the Kombat list** —
Circle, Left, Triangle, Square, Start, L, R, Right, Select all probed
from a parked state; the list stays. Players presumably leave it only by
entering a mode (and backing out of that mode's flow). The adapter is
unaffected (its gate reads live state), but a bench "return to menu"
step cannot use this list.

## Character select — VERIFIED, adapter speaks it (2026-10-16 night)

Hunts 8–10 (~/mku-scan/: mku-grid.cpp, analyze-grid.py, mku-grid2.cpp,
mku-goto5.cpp, mku-live2.cpp). The screen: Arcade's fighter grid,
**10 columns × 3 rows**, cursor starts on Scorpion, and **clamps at all
four edges** (RIGHT at Sindel goes nowhere; DOWN at Liu Kang goes
nowhere — no wraps, no row spill). Each cell's name was read off the
screen's own name panel; portraits were used for nothing (they mislead:
cell 0's portrait reads as "Jax in a beret" at thumbnail size and is in
fact Kenshi — Jax is at row 2, column 6). The full roster, row-major:

Row 1: Kenshi, Jade, Scorpion, Mileena, Goro, Kitana, Baraka, Sub-Zero,
Havik, Sindel. Row 2: Raiden, Li Mei, Kabal, Ermac, Blaze, Jax,
Nightwolf, Bo' Rai Cho, Noob-Smoke (panel: "NOOB - SMOKE"), Tanya.
Row 3: Shujinko, Hotaru, Ashrah, Dairou, Shao Kahn, Frost, Kobra,
Darrius, Kira, Liu Kang.

(The panel also shows a per-fighter DIFFICULTY rating, 1–5, and the
fighter's three styles — flavour for now, not yet read by the adapter.)

State:
* **Index: u32 at 0x08C1B3B0 (STATIC image)** — 0-based linear row-major,
  the unique direct-pattern hit. Like the menu cursor it persists
  off-screen as "last hovered fighter"; it is gated, never trusted alone.
* **Gate**: the current-object pointer at **0x09F9C1A8** must hold
  **0x09112080 − index·0x490**, and that object must carry ` OBJ` magic
  at +4 (the fighter records are 0x490-byte engine objects, table stored
  in reverse). Formula matched **all 33 dumps** of the mapping walk and
  a **cold boot** (arrival + RIGHT×2 + DOWN; implied base 0x09112080
  identical across sessions), zero mismatches.
* **Trap family found en route**: at least five words (three statics
  near 0x08BA93xx, a small struct near 0x091FC1C8) are **move counters**
  — +1/+80 per effective move, direction-blind. While the walk was a
  monotonic RIGHT sweep they were indistinguishable from the index; the
  DOWN/LEFT/UP steps exposed every one. Monotonic walks certify nothing.
* Also present: two more reverse-stride pointer tables into per-fighter
  data (0x210 and 0x140 strides, holders at 0x09F9C140 / 0x09F9C094) —
  the same "current fighter" fact in other data layouts; unneeded while
  the 0x490 gate holds.

Adapter wiring: OnCharSelect() as above; WhereAmI/MenuState speak
"Character select. <Name>, row R, column C."; all four nav commands
speak the cell landed on; ready() = OnMainMenu() || OnCharSelect().
Host tests: **30 passed**. Live (mku-live2, cold boot): arrival
"Character select. Scorpion, row 1, column 3."; RIGHT→Mileena,
RIGHT→Goro, DOWN→Blaze, LEFT→Ermac, UP→Mileena, LEFT×3→Scorpion, Jade,
Kenshi; a final LEFT at Kenshi re-spoke Kenshi — **the clamp, spoken
truthfully**.

## Kombat submenu speech (2026-10-16 night, hunt6 state + live)

Wired the same night the submenu was mapped. WhereAmI on the submenu →
"Kombat menu. Arcade, 1 of 3."; MenuNext/Prev speak the landed row.
Gate = KombatSubIndex() (holder at 0x09118348 equals one of the three
pool objects AND that object carries the ` OBJ` magic). Host tests 35.
Live cold-boot (mku-live): Arcade → Endurance → Practice → wrap to
Arcade → UP wraps to Practice, all spoken.

## Fight state (2026-10-16 night, hunt11) — MAPPED AND SPOKEN

Route in: character select (Arcade) → CROSS locks the fighter → the
"Choose Your Destiny" tower screen (TIME: 00:00, DIFFICULTY: 25%,
opponent portraits stacked on a stone pillar over lava) → CROSS starts
the first match. Verification fight: Scorpion vs **Goro**.

One BSS fight struct holds the whole match (all addresses RAM):

| field | address | form |
|---|---|---|
| P1 fighter id | 0x08D929B4 | u32 = charsel index (2 = Scorpion) |
| P1 HP | 0x08D929B8 | float 0..1 |
| P1 round wins | 0x08D929EC | u32 |
| P2 HP | 0x08D92A24 | float 0..1 |
| P2 fighter id | 0x08D92A2C | u32 (4 = Goro) |
| P2 round wins | 0x08D92A58 | u32 |
| round number | 0x08D92AE0 | u32 |
| timer | 0x08D92AF0 | u32 seconds (twin in static data at 0x08C1613C) |

Fighter blocks sit 0x6C apart with the same layout (wins at +0x34
across the stride). Verified against paused screenshots at eight
captures (fight2.ppst → p0..p7): P1 HP [1, 1, 1, 0.904, 0.904, 0.904,
1, 1]; P2 HP [1, 0.741, 0.333, 0.333, 0.333, 0 at the KO, 1, 1]; P1
wins [0×5, 1, 1, 1]; timer [58, 54, 51, 48, 48, 45, 60, 60] — the
displayed seconds at every capture including the KO freeze and the
round-2 reset. A lagging DISPLAY twin of P2's HP exists at 0x08B40E8C
(reads 0.9 during round 2's intro while the true word is already 1.0);
speech uses the true word.

Gate (InFight): round 1..3, timer ≤ 99, both ids < 30, both HP floats
in [0, 1], not both zero. Speech: WhereAmI → "Round 2. Scorpion 90
percent, Goro 33 percent. 48 seconds left. You lead 1 to 0." Host
tests 39 passed. Live (mku-live3, from fight2.ppst): round start, one
attack burst (Goro 100 → 71 percent, timer 58 → 55), and the paused
fight all speak correctly. ready() now includes InFight().

**Trap recorded:** the health bar GHOST-DRAINS — it eases toward the
true HP and lags behind it, so pixel fractions read a few points HIGH
while damage is fresh (early estimates said Goro 0.875/0.413 where the
true word said 0.741/0.333). Never calibrate a RAM word against a
fresh-damage bar; give the bar time to settle, or bracket single hits.

## Pause menu speech (2026-10-17, hunts 12-13) — MAPPED AND SPOKEN

The pause block is **per-boot**: a cold-booted second session put the
holder at 0x0906177C and the RESUME object at 0x090607D0, exactly
0x2E60 below hunt 1's addresses, with the holder riding at
top+0xFAC in both boots. No static pointer to the holder exists.
The adapter therefore RELOCATES it per match:

1. **Freeze gate.** on_frame watches the round timer: InFight() with
   the timer unchanged for 240 frames = the game is frozen (live play
   ticks it every 60). Known soft edge: round intros and win banners
   also freeze the timer, so a WhereAmI in the last second of a long
   banner in a match where the player paused earlier can briefly say
   "Paused".
2. **Signature scan.** Cells whose value heads (at any position) a run
   of four ` OBJ` objects (magic at +4, class word 0x08BAF3C0 at +0xC)
   at -0xAF0 steps, window 0x09000000-0x09200000, EXCLUDING cells that
   sit inside the run's own objects. Two traps recorded: (a) the
   objects' own link fields point into the run in numbers that
   overflow a small cap — an ascending scan capped at 24 cut the true
   holder out entirely on live4's first run and confirmed a trailing
   cell instead; the cap is 192 with the inside-the-run exclusion;
   (b) the pause widget keeps TRAILING COPIES of the selection in a
   record ring nearby (cells around top-0x1A5xx write the previous
   object after the holder moves) — a naive one-tap match confirms a
   copy and every row speaks one tap late.
3. **Nav confirmation.** on_frame polls the candidates and stamps the
   frame each value last changed; a nav command confirms the stepper
   (in the tap's direction, wrap steps included) whose change came
   EARLIEST. One stepping tap confirms; cached for the match;
   re-validated (magic+class, in-run) on every use.

Speech: WhereAmI → "Paused. Moves List, 2 of 4."; MenuNext/Prev speak
the landed row; left/right stay silent (they belong to the quit
dialog). Host tests 46. Live (mku-live4, real paused state): the
confirming tap itself spoke "Moves List, 2 of 4", the full walk and
both wraps spoke in sync.

**The QUIT GAME dialog stays unmapped.** — ⛔ SUPERSEDED 2026-10-23:
it is mapped and spoken now; see "The QUIT GAME dialog" below. What
this paragraph got right: its NO/YES selection does live in a generic
widget list (two 0x18-apart linked nodes, reachable from the static
"current UI object" pointer at 0x08B12964 — which also points at
unrelated objects all session, so it cannot gate). No distinctive
signature survived testing; two boolean flag candidates (0x08B12938,
0x08B40E74) and a 9-word cross-boot pause-flag set all proved to be
animation/render state. Same for a true pause FLAG: none exists in
static memory (cross-boot test over 13 paused dumps); the
timer-freeze gate is the substitute.

Also settled incidentally: **TRIANGLE is the front-end's back button**
(it exits the profile screens to the main menu). The Kombat submenu's
"no button exits" bench note was a wrong-button artifact.

## The QUIT GAME dialog (2026-10-23, hunts quit1/quit2 + live7) — MAPPED AND SPOKEN

Choosing MAIN MENU on the fight pause menu opens a confirmation
dialog: title **"QUIT GAME"**, question **"Are you sure ?"**, options
**NO (left, the default) and YES (right)**. Behaviour, all
screenshot-verified (hunt13/q0-q3, hunt-quit1, hunt-quit2): LEFT and
RIGHT move the gold highlight and **wrap at both ends** (RIGHT on YES
lands on NO; LEFT on NO lands on YES); **UP and DOWN do not move it**
(their screenshots are byte-identical to the pre-tap one); settle
lengths of 39-43 frames change nothing.

### The dead end, settled for good

The 0x08B12964 focus pointer is **tap parity, not the selection**.
In hunt-quit2 (hunt1's first-boot lineage), one UP tap flipped the
pointer 0x09145328 -> 0x09145310 while the screenshot still showed NO
highlighted; across six taps the pointer alternated strictly per tap
while the selection followed only the LEFT/RIGHT ones. The two 0x18
nodes relink symmetrically on every commit (the pointed-at node
always carries +08 == 0), so neither the pointer nor the links can
carry the selection. The dialog's text strings are pre-loaded in RAM
in every state including mid-fight, so text presence cannot gate
either. And both static UI cells (0x08B12964/68) keep their dialog
values indefinitely after the dialog closes — through resume and
re-pause — so they cannot gate presence.

### Presence: the current-context cell 0x08D9742C

A static cell at **0x08D9742C** (BSS, just past the fight struct)
names the UI layer's current context object. Method: labeled dumps
(premain / dialog NO / dialog YES / back-at-pause / unpaused) from
two independent boots, intersecting the static words that sit at a
constant dialog-vs-pause relation. The cell is constant for a whole
pause in each lineage; opening the dialog moves it by exactly
**+0x98**; confirming NO restores the pause value:

- hunt12/13 lineage: pause **0x09E2A990**, dialog **0x09E2AA28**
  (restore proven in hunt13/back.ram and hunt-quit1).
- hunt1 first-boot lineage: pause **0x09DAF138**, dialog
  **0x09DAF1D0** (predicted before the run, confirmed; restore
  observed 30 frames after confirming NO).

The pause value is a heap address that differs per lineage (other
screens read yet other values: main menu 0x09EA3F00, character
select 0x09E71758, plain fights 0x09EA3878), so the adapter
self-calibrates: it samples the cell while the plain pause menu is
up and gates the dialog on sample + 0x98. The +0x98 step itself is
an observed constant on two boots, not a proven struct size.

**The cell blinks.** Single settled reads never showed it, but
watching every frame over an open dialog (live7's instrument
window) shows ~10-frame runs of the dialog value broken by exactly
one frame of another layer's context (0x09E26D98, 0x09E28850,
0x09EA4328, even 0x09EA3F00) or 0, on a ~26-frame cadence — the
cell names the layer being serviced that frame, and the top layer
dominates. The adapter therefore **latches**: the pause sample is
only trusted after the same value repeats on consecutive frames
(blinks never repeat), entry takes one frame at sample + 0x98 with
a valid selection field, exit takes the pause value back for 3
consecutive frames, and blink frames while latched are ignored.

### Selection: the dialog object's +0x28 field

The dialog's own object — same ` OBJ` family as the pause items
(magic at +4, class 0x08BAF3C0 at +0xC) — sits at a fixed offset
below the pause run's top object, and its **+0x28 field points at
the selected option's widget**. As offsets from the pause top
(RESUME), identical on both boots:

- selection field: **top - 0x2C28** (field address)
- NO widget: **top - 0x3AE0** (hunt13: 0x0905CCF0; hunt-quit2 boot:
  0x0905FB50)
- YES widget: **top - 0x45C0** (hunt13: 0x0905C210; hunt-quit2 boot:
  0x0905F070)

The field reads 0 before the dialog has ever opened, follows the
highlight exactly (checked against the screenshot at every tap in
both boots), and keeps its last value after the dialog closes — so
it is read only under the presence latch, and the gate fails closed
on any value that is neither widget.

### Wiring and proof

`Core/mku_adapter.cpp`: a dialog section after the pause plumbing
(kScreenCtxAddr, the offsets above, TrackPauseCtx / OnQuitDialog /
QuitDialogIndex / QuitDialogRow), screen 7 in MenuWatch above the
pause, and a WhereAmI branch ahead of the pause branch:
**"Quit game. Are you sure? No, 1 of 2."**, with settled-row speech
("Yes, 2 of 2") as the highlight moves. The adapter's player-facing
description no longer lists the dialog as unmapped.

- Host test: a synthetic dialog scene joins the pause scene in
  `Core/mku_adapter_test.cpp` (plant the ctx pair + selection field
  at hunt13's real addresses): gate shut at the pause value, entry
  line, YES/NO row speech both directions, blink frames mid-dialog
  say nothing and WhereAmI still speaks the dialog, close after 3
  steady frames speaks the pause again. **83 checks pass**;
  test-strength floor for the file raised 71 -> 84 sites.
- Live (mku-live7, the real adapter on hunt12/paused.ppst, app-style
  drive): "Paused. Moves List, 2 of 4." -> "Main Menu, 4 of 4" ->
  WhereAmI "Paused. Main Menu, 4 of 4." -> **"Quit game. Are you
  sure? No, 1 of 2."** -> 240 blink-filled frames of silence ->
  RIGHT **"Yes, 2 of 2"** -> RIGHT **"No, 1 of 2"** (the wrap) ->
  WhereAmI "Quit game. Are you sure? No, 1 of 2." -> confirm NO ->
  "Paused. Main Menu, 4 of 4.", ctx back at 0x09E2A990.

### Still unproven

- **Only the fight pause's dialog is verified** (the 4-item menu, in
  versus/arcade-style matches, two boots). The Konquest free-roam
  pause's QUIT GAME item belongs to a different 3-item menu and was
  not driven into its dialog; the adapter's Konquest speech takes
  precedence there regardless. Any other confirmation dialogs in
  the game are unchecked.
- **Confirming YES was never driven with the adapter attached.**
  The latch clears when OnPause drops and the main menu is a mapped
  screen, but that exact transition is unobserved live.
- The hunt1-lineage pause state reads Konquest flag A = 1, so in
  that lineage the adapter says "Konquest." until the pause holder
  confirms (the pause branch then wins by precedence, and the
  dialog gate rides on it the same way). What sets flag A in that
  lineage was not chased here.

## Profiles and Konquest entry (2026-10-17, hunts 14-15)

Konquest is **profile-gated**: selecting it with no active profile
pops "PROFILE REQUIRED" over the main menu. The full unlock path,
driven end-to-end on the bench:

1. **PROFILES → CREATE PROFILE** (list: Create / View / Delete).
2. **Name entry** is an in-game keyboard (not the PSP OSK): field
   pre-filled "PLAYER 1"; grid rows 1234567890 / A-J / K-T / U-Z with
   the cursor starting in column 5 (DOWN walks '5' -> 'E' -> 'O' ->
   'Y' -> DONE, screenshot-verified).
3. DONE -> **"ARE YOU SURE?" — Triangle NO, Cross YES** (button-
   labeled, no cursor).
4. **CHOOSE YOUR ICON**: 8x3 grid of portraits/symbols.
5. **ENTER YOUR KODE**: any 6 button presses (face buttons, L/R,
   D-pad); then **CONFIRM YOUR KODE** with the same 6. (The bench
   profile's kode is Cross x6.)
6. **SAVE PROFILE?** — Square creates the "MKU profile kollection"
   (a real savedata folder ULUS10102MKU0000 on the memory stick),
   then pick one of 4 slots with Cross. "REMEMBER YOUR KODE!" OK.
7. Back at the main menu, press **L ("ENTER PROFILE KODE")** and
   re-enter the 6 buttons: the footer flips to "L RESET PROFILE" —
   the profile is now ACTIVE, and Konquest opens.

VIEW PROFILE shows the stats card (fighting/chess/puzzle W-L, koins
by color) — evidence the created profile is loaded for viewing, but
Konquest still demanded the kode activation in step 7.

**Konquest opening** (from hunt14/konq.ppst): fade to a dusk sky ->
"MORTAL KOMBAT KONQUEST" title card -> in-engine cinematic (village
courtyard, fighters training) -> Bo' Rai Cho's tutorial in the
training hall (young Shujinko, Mantis style vs Sumo). Dialogue boxes
carry an "OK X" prompt and advance with Cross.

**Dialogue text — located.** The CURRENT line sits in a live buffer
(0x09178568 in the hunt15 boot): across two captures it held first
"How you maneuver in kombat plays as much a part of your strategy as
how you punch or kick." and then "Your first exercise will be to
step toward me three times..." — the buffer is rewritten per line.
Both lines also exist permanently in a script pool (0x09DDB840 region
this boot, the two lines 0xA4 apart) — the scene's loaded script.
The buffer is heap, so adapter speech needs relocation or the
static-side writer route, same as the pause menu; the struct before
the buffer references the static word 0x08AFD1C0. NOT yet wired.

**Konquest fights differ.** The first training bout (Shujinko vs
Bo' Rai Cho, styles Mantis/Sumo, objective counter "0 / 6") uses the
fight struct's TIMER (0x08D92AF0 read 89 = displayed) and ROUND words
at the arcade addresses, but the fighter-id fields do NOT hold
charsel indices there (read 2/0 mid-bout). The arcade fight gate
could fire inside a Konquest bout and speak wrong names — Konquest
bouts need their own verification (and a mode discriminator) before
any fight speech is trusted there.

## Konquest mode flag, dialogue, objectives (2026-10-17, hunts 16-17)

**Mode discriminator — FOUND, wired.** Two BSS words read 1 in every
Konquest capture (opening cinematic, dialogue, tutorial bouts) and 0
on the menus and in arcade fights, across two independent boots:
0x08AF69C8 and 0x08B123DC. The adapter's InKonquest() requires both.
Every front-end and fight gate now excludes Konquest on this flag —
necessary, because front-end pool state goes STALE in Konquest: the
Kombat submenu's holder (0x09118348) keeps the Arcade object from an
earlier visit through a whole Konquest session (a cold boot that
never opened the submenu reads 0 there), and during the opening
cinematic the main menu's holder still names the Konquest item, so
both gates false-fired before the exclusion. The character-select
gate is shut in Konquest on its own evidence (its holder reads the
0xFFFFFFCD sentinel there). Live symptom before the fix: WhereAmI in
the tutorial said "Kombat menu. Arcade, 1 of 3."

**Dialogue buffer — boot-stable, signed, wired.** The current line is
rewritten into a heap buffer at 0x09178568 — the SAME address in a
savestate lineage and in an independent cold boot (the deterministic
path through profile activation into Konquest allocates it
identically). The text-box struct's twelve words before the string
are a fixed signature: [0x30, 0xE9, 0x24, 0x180, vary, vary,
0xFFFFFFFF, color, 0x6, 0, 0x08AFD1C0, 0]; an offline signature scan
(~/mku-scan/analyze-dialog.py) finds exactly one match per dump,
holding the current line, across four lines and both boots. The box
is UP only while a scene-state pointer at buffer+0x7C8 (0x09178D30)
is non-null AND the color word (buffer-0x14) reads 0xC8C8C8FF (it
reads 0xFF4848FF when down); when the box closes the buffer keeps the
last line as stale text, so speech requires both signals. No static
pointer to the buffer exists (checked); if allocation ever shifts,
the adapter's signature check fails closed and this scan is the
relocation route. Adapter behavior: a line auto-speaks once it has
been stable for 30 frames (category "dialogue", Normal priority);
WhereAmI in Konquest speaks the current line; the last-spoken record
clears when the box closes so a re-shown line speaks again. Host
tests cover the lifecycle including the fail-closed case.
**Exercise 1 (movement) is now playable by ear through the first
lines.** Live-verified 2026-10-17 (mku-live6, cold boot, profile
activated from the bench memory stick): the adapter stayed silent
through boot, menus and the opening cinematic, then auto-spoke
Bo' Rai Cho's first line as the box came up ("Ah, Shujinko! You have
arrived! Let us begin...") and the second after a Cross advance
("You have a long way to go, but every step is a victory."). Host
tests: 56 passed.

**Objective counters — found (this exercise).** The tutorial's
step counters are two u32s 0x10 apart in the scene-state cluster
(~0x10DC above the dialogue buffer): toward-steps at 0x09179644,
away-steps at 0x09179654; the on-screen "N / 6" is their sum against
quotas of 3+3. The displayed total does NOT live in any single word
(split-phase storage — a direct pattern hunt for [0,0,1,2,5] found
nothing; the split patterns each hit exactly once). A step counts
only when it produces displacement: stepping into the opponent or
the arena wall does not increment. Counters are per-scene heap —
not yet wired; they belong with the Konquest HUD pass.

**Bout HP — VERIFIED in Konquest (2026-10-17, hunts 18-22).** The
tutorial was played through on the bench: exercise 1 (steps, above),
exercise 2 (three Backward Dashes — a dash is a TIGHT double-tap,
2 frames on / 2 off; 3-on/3-off is too slow and silently does
nothing), then the strikes lesson ("strike me once with four
different basic attacks"). There the shared fight struct's HP
floats are live and true: P2 HP at 0x08D92A24 went 1.000 → 0.912
(Square) → 0.888 (Cross) → 0.904 after the between-phase refill →
0.000 when the fourth distinct attack (Circle) landed, each change
screenshot-confirmed (bar, blood, counter). P1 HP (0x08D929B8)
stayed 1.000 — Bo' Rai Cho never attacks in the tutorial. So what
differs in Konquest is NOT health: it is the fighter-id fields
(2/0 — Konquest ids, not charsel indices) and round/timer
semantics (round read 3 through the strikes lesson; the timer
started at 25 and ticked in bursts — a per-phase clock, not the
arcade 99-second round clock). A Konquest fight line can therefore
speak real percentages ("Shujinko 100 percent, opponent 91
percent") once a bout gate is proven against free-roam negatives;
names for P2 are the open half (id 0 is Bo' Rai Cho here).

**Tutorial objective block.** The exercises keep their progress in
one scene-state block (this lineage): per-attack landed flags at
0x09179574/84/94/A4 (Square/Triangle/Cross/Circle, stride 0x10 —
the on-screen "N / 4" is their popcount, stored nowhere), exercise
1's step counters at 0x09179644 (toward) / 0x09179654 (away),
exercise 2's dash counter at 0x091796D4. All per-scene heap; the
block sits ~0x1000 above the dialogue buffer. Also observed: the
tutorial's instruction text reuses the dialogue text-box struct
with the red tint (color 0xFF4848FF) while an exercise runs — the
same buffer, a second speakable surface (the current objective).

## Free-roam reached — the 3D movement part (2026-10-17, hunts 23-33)

The Bo' Rai Cho tutorial was played to completion on the bench
(lesson order after the strikes lesson: named high-punch moves with
on-screen banners — LEANING FINGERS = plain Triangle, STONE COLUMN
= up+Triangle at range; attack variations — STING TAP = back+Cross;
then "two high attacks and two low attacks", the lows being
down+attack with a real pre-hold, ~10 frames, at point blank). Two
bench lessons worth keeping: (1) the lessons reset the fighters to
a WIDE spacing, so strikes need a ~28-frame walk-in, not 10; (2)
the tutorial banner names the exact next move and its input
glyphs — a drawn prompt, not text in the dialogue buffer.
Completing the last lesson ends the training and drops Shujinko
into **Earthrealm free-roam** — the 3D movement part of Konquest:

- Third-person 3D movement on the ANALOG stick (psp_set_analog;
  the D-pad is not the mover here). Movement is camera-relative;
  the camera swings in behind him.
- HUD: a compass dial (top-left), the map SECTOR under it
  ("B - 6" at spawn; crossing west flipped it to "A - 6" — letters
  run east-west, numbers north-south), the game clock top-right
  ("SUNDAY 01.01.01 7:03 AM"; time runs ~1 game-minute per 33
  frames, a full day ≈ 13 real minutes), and the realm name
  bottom-right. NPCs wander the courtyard; a red "!" marks an
  interactable ahead of spawn.
- The mode flag, dialogue machinery and text buffer from the
  tutorial all remain live in free-roam. INTERACTION PROVEN
  (2026-10-17, hunts 35-36, by accident): the red "!" at spawn is
  Bo' Rai Cho / his hall trigger — roaming back into him restarts
  the training (the lesson state re-initialized: round 1, fresh
  timer, and the opening dialogue "Ah, Shujinko! You have
  arrived!" replayed through the wired dialogue path, box-up pair
  and all). Two consequences: NPC/master interactions in free-roam
  fire through the same dialogue machinery the adapter already
  speaks, and a roaming player can land back in tutorial states at
  any time — the adapter sees them as ordinary Konquest dialogue,
  which is the correct behaviour.

- **Free-roam buttons — VERIFIED 2026-10-17 (hunts 38-41, at the
  user's prompting: they had it right).** SQUARE is the free-roam
  attack: frame-stepped captures show a full punch animation
  (~20 frames in, arm extended; ~36, follow-through) where CROSS
  in the same open spot leaves him neutral. CROSS is the interact
  button: in the open it does nothing at all (no attack, no menu),
  and it is the button that advances/opens dialogue everywhere
  else in the mode. (Do not conflate contexts: inside Konquest
  BOUTS the face buttons are the four basic attacks — Cross
  included, per the strikes lesson. Roam and bout mappings
  differ.) The master trigger above appears to be proximity, not
  the button.
- **Marked-interactable sweep — NEGATIVE beyond Bo (hunts
  42-50).** Roamed the whole spawn neighbourhood looking for
  another red "!" carrier: the courtyard, the east street (houses,
  stone lantern, a patrolling man in black shirt/shorts), the west
  fence line (a static crossed-arms man in black with a red sash;
  a large ring-handled wooden door in the wall), south into A-6,
  and the fountain approach. NPC census now: the old woman, the
  corner youth, the robed man and an orange monk (both in the
  fenced garden strip, unreachable from the street), the
  crossed-arms man, the street patrols, a red-clad figure near
  the gate. NONE carries a "!" in any capture, at 7-8 AM Sunday.
  Marked quest NPCs are presumably story-/time-gated; Bo' Rai Cho
  remains the only marked interactable found. Also mapped en
  route: START in free-roam opens a 3-item pause (RESUME / SAVE
  AND QUIT — greyed in this state / QUIT GAME), a different menu
  from the fight pause; SELECT does nothing.
- **Navigation lesson (bench, hard-won):** free-roam movement is
  camera-relative and the camera keeps its yaw through short
  settles, so multi-leg scripted drives quietly rotate — a "north"
  leg after an east leg runs west. Single long legs from a settled
  state are the only reliable bench drives; anything finer needs
  the position/heading words (decomp route). This is also exactly
  the blind player's problem in the real game, and the strongest
  argument yet for spoken heading/sector callouts. The spawn
  gateway additionally has a wide collision sliver: full-tilt
  north from spawn pins on the left sign's base; the gap threads
  slowly with a slight east drift.
- **What we're actually supposed to do (guides + decomp,
  2026-10-17).** The main-mission marker in Konquest is a GREEN
  PILLAR OF LIGHT over the objective NPC (plus the compass's green
  arrow), not the red "!" — the sweep was censusing the wrong
  marker. The village arc (GameSpy/GameFAQs village guide,
  mirrored in ~/workspace/games/mk-unchained/konquest-village-route.md):
  (1) turn around at spawn and talk to APEP, the boy with the
  green pillar — he leads Shujinko to the remaining classrooms;
  (2) classrooms at B-5, D-5, D-4 (225 koins each); (3) a guard
  blocks at D-4 — fetch HAM from the BUTCHER at C-6 (7 AM-8 PM)
  and give it to him; (4) final test = a two-round fight vs
  Bo' Rai Cho; (5) leave via the north gate (talk to Bo' Rai Cho)
  — ONE-WAY, no return to the village. Side: PUNCH the old woman
  in B-6 — she drops a KEY to a hut in B-5 full of koins (she is
  an attack-interactable, which is why Cross never worked on her).
  Decomp corroboration from the EBOOT's own file table:
  scr_konquest.sec is the master Konquest script;
  konquest_monk_1..5.sec are the five monk trainings;
  konq_nis_1_apep_0..9.at3 are Apep's voiced scene clips;
  shk_ham.mko is the ham item; and a debug string ("No navigation
  data for this konquest level") shows the engine keeps per-level
  NAVIGATION DATA — a named target for the position/heading route.
- **APEP FOUND — but his talk trigger will not fire on this
  lineage (hunts 52-65, konq38-51).** Apep stands at a post on
  the grass strip by the WEST ringed gate (B-6), ~15 m west of
  spawn. The green "pillar" renders at conversational distance
  as a bright GREEN POOL on the ground — and the pool marks his
  POST, not his body: when he wanders up the house steps beside
  the post, the pool stays behind at the post (hunt65). At his
  post, point-blank (~1 m), facing him, the talk was attempted
  with Cross, Circle, Triangle and Square, plus pure proximity
  walk-ins and 400-frame idle waits: NOTHING fires — no dialogue
  box, no NIS, no reaction (konq48-50). The old woman was also
  PUNCHED on her pass line at the A/B sign (konq53, hunt67): a
  connecting frame at point blank, but no key drop, no popup,
  no reaction over the following 160 frames. Two live hypotheses
  remain: (a) this lineage's quest state is wedged — the bench
  tutorial was completed across ~15 savestate hops, and an
  interaction-arming flag may never have been set the way a
  continuous playthrough sets it; (b) the PSP build arms these
  interactions from a trigger the console guides don't describe.
  Decisive next routes, in order: extract scr_konquest.sec from
  the ISO data and read Apep's actual trigger condition; or
  replay the tutorial in ONE continuous cold-boot lineage and
  retest at his post.
- **Script-data pass (2026-10-17, hunts 65-70).** ARCHIVE.IMP
  (577 MB, the game's only big archive) extracted to
  ~/mku-scan/imp/; its entries are NOT stored by plain filename
  (no "scr_konquest" string anywhere) — full IMP unpacking is
  still open. BUT the loaded village script is fully resident
  in the free-roam dumps: the complete Earthrealm-village
  dialogue set sits at 0x09D60958+ (Apep's intro and tour lines,
  the ham/guard exchange, the old woman's favour lines — she
  HAS a quest: "Will you do an old woman a favor?"), with its
  relocation pointer table (134 entries) at 0x09D69718. The
  data side is complete; nothing is missing. A world-block diff
  (mid-tutorial cold lineage vs post-tutorial free-roam,
  0x08AEC000-0x08AF0000) shows progress state ON and no single
  obviously-unset arming flag. The script VM's thread state is
  NOT in the string blob — locating it is decomp work (script
  engine writers), or the question is decided empirically by a
  continuous cold-boot tutorial replay with no savestate hops.
- **The spawn-area villagers do NOT talk — tested to exhaustion
  (hunts 38-41, ~25 Cross attempts).** The wandering old woman
  (point-blank: in front, beside, behind, faced after a turn), the
  young man at the street corner SE of spawn (~2 m, faced), a
  robed man across the fence east of spawn (unreachable side), and
  a standing figure by the fountain: no dialogue box, no prompt.
  None of them carries the red "!" marker that flags a real
  interactable (Bo' Rai Cho had it). Verdict: at this story point
  these are ambient set dressing; the first talkable encounter is
  the scripted master. When a marked/scripted villager is met
  later, the wired dialogue path is already proven ready for it.
- Dialogue text contains embedded glyph codes for buttons and
  directions (seen as "{" and "@_{" in raw reads of two lines) —
  speech will need a glyph-translation pass when Konquest dialogue
  gets polished, or lines will read punctuation aloud.

**Sector column — FOUND (static).** The word at 0x08AED5B4 reads
1 in sector B and 0 in sector A across seven dumps (three B-6
samples including an independent lineage's spawn dump, three A-6
samples, idle-stable): the map column as a small int in the
Konquest world-state block (0x08AEDxxx; a float at 0x08AED480 in
the same block twitches with movement). The neighbouring word
0x08AED5B8 flips with it — role unknown, it is not the row (the
row stayed 6 and it never read 6). The ROW word still needs a
north-south boundary crossing. (An automated scan briefly
reported the H3 control reading 0 at this address; a direct read
proved that a scanner bug — the word reads 1. Trust, but verify
the verifier.)
⚠️ **Caveat added 2026-10-23:** two fresh A-6 samples disagree at
this word (1 and 0 at the same x, opposite z) — see "Free-roam
position — CROWNED" below before trusting it as the HUD column.

**Position — narrowed, NOT yet crowned.** Six movement dumps
(J0-J5, hunt32-33: four stick legs + an idle control) cut the
float-pair field to ~20 survivors and a prime cluster.
(Superseded 2026-10-23: the position was crowned in the static
world-state block at 0x08AED478 — see "Free-roam position —
CROWNED" below. The cluster below was a heap shadow that drifts.) The
entity array around 0x08CC5530 holds ground-plane vec3s (Y ≈ 0)
that move rigidly and idle-stabilize — one of them is Shujinko,
the neighbours are NPCs and possibly the camera; a second value at
0x09148134 moves on a single axis only (camera/world-scroll, not
the player). The game-clock encoding is unresolved (not
minutes-of-day, minute-of-hour, or seconds-of-day as u32; not
float hours-of-day). Decisive tests status (2026-10-17, hunts 34-37): the rotation-only
leg RAN (L/R buttons rotate the camera in free-roam) — and the
0x08CC5xxx cluster's members CONVERGED/snap under rotation, which
supports cell-local coordinates there and kills them as simple
world-position candidates; a separate single-axis value at
0x09148134 kept climbing during rotation (camera register). The
north-south row crossing was attempted with a 660-frame
stick-down run: Shujinko crossed B→A in the first leg (spawn sits
just east of that boundary; "down/back" from spawn is westward)
and then pinned against the gate-sign structure for five legs —
blind waypoint navigation is the very problem the adapter exists
to solve, so the row, clock and position hunts now route to the
DECOMP pass: the sector word's struct (vaddr 0x29D5B4) has writers
in the image, and its neighbours' names will come from the code
that writes them, not from more driving in circles. The sector
column is solid enough to wire with a realm+row check beside it;
position stays out of the adapter until it is proven, per the
standing rule.

### Next

1. Free-roam position/sector/clock: position is DONE (2026-10-23,
   spoken; facing tracked movement-derived). Sector needs re-proof
   (see the caveat on the sector word) and the clock is still
   unencoded; then speak sector + clock ("Earthrealm, B 6, Sunday
   7:03 AM") and build toward navigation callouts; NPC interaction
   test (dialogue should fire through the wired machinery).
2. Konquest pause/map screens as met in free-roam.
2. Konquest bout speech: percentages are verified speakable; the
   work is a bout gate proven against free-roam/dialogue negatives
   plus P2 naming (Konquest fighter ids ≠ charsel indices).
3. Objective speech: the red instruction text (same buffer as
   dialogue) and the objective counters ("2 of 4") — the tutorial
   shows both are readable; wire alongside the free-roam pass.
4. Title-screen presence checks for the gates (still unsampled).
5. Decomp route to the state writers (ghidra-allegrex re-import
   first) — replaces heap anchors with static ones where they exist,
   and is the remaining road to the quit dialog's selection.


## Free-roam position — CROWNED; facing movement-derived (2026-10-23, pos hunts)

**Position: the float vec3 at 0x08AED478** (x, y at +4, z at +8) in the
Konquest world-state block — the same static block as the sector word.
The "float at 0x08AED480 that twitches with movement" from the
2026-10-17 hunt was this vec3's z all along. Found by diffing
controlled legs (probes ~/mku-scan/mku-pos{,2,3}.cpp; dump sets
pos2/pos3/pos4, all from hunt79/ck-atapep.ppst) and cross-reading
every .ram already on file. Evidence:

- **Idle:** two dumps 300 frames apart in one boot agree to 0.001;
  a second boot's idle pair agrees to 0.003.
- **Legs track quantitatively:** stick-up 180f Δ(+0.01, −0.33) (the
  fence cut it short — the trajectory probe below shows the hold);
  stick-right 180f Δ(−0.00, −2.22); stick-up 360f Δ(−10.25, +11.69),
  crossing B→A; stick-down 480f Δ(−10.34, −22.65).
- **Rotation purity:** L held 240f moves it 0.08; R held 240f, 0.20
  (collision settle). It is the player's feet, not the camera.
- **Continuity:** a trajectory probe sampling every 30 frames shows
  a smooth path — ~2 s diagonal, wall contact at x = −63.469, then a
  pure −z slide decelerating to rest. No steps, no teleports.
- **Cross-lineage:** hunt32's J0 (spawn) reads (−55.99, 0.87,
  154.19) and J1–J5 move it walk-sized steps; hunt79's
  atapep-clean.ram matches pos2's fresh boot of the same checkpoint
  to 0.01; the dojo bout lineage reads (−58.00, 0.88, 170.00),
  frozen with the fight; hunt80's corner spot reads (−58.03, 0.88,
  155.22). One static address, every boot.
- **The heap shadow records are NOT it:** records near 0x08CE5000
  (this lineage's twin of the old 0x08CC5530 cluster) sit ~0.5 from
  the vec3 and drift under camera rotation and sector streaming.
  That drift is what killed the cluster in 2026-10-17; the static
  vec3 does neither.

Axes: **x is east-west** (two B→A crossings, x dropping ~10 both
times while z moved in opposite directions). Which way z runs
cardinally is **unproven** (no row-boundary crossing on file), and
y sits ≈ 0.87 at village ground level in every sample.

**Facing — native hunt NEGATIVE.** Three dump sets (20 dumps) with
displacement-measured ground-truth headings, including two pure
camera rotations and an A-B-A heading cycle (−117° / +77° / −84°),
were tested against every angle encoding — wrapped and unwrapped
radians, degrees, turns, sin/cos pairs. No survivor. The HUD
compass rose is too small to calibrate from screenshots (two reads
contradicted each other), so no compass words are spoken either.

**Wired** (Core/mku_adapter.cpp): `InFreeRoam()` = flag A set, flag
B clear, vec3 plausible (finite, |x| and |z| < 1000, −50 < y < 100).
WhereAmI in free-roam — after the bout, objective and dialogue
channels, which keep priority — speaks **"Konquest. Position -54,
163."** (the rounded ground pair, Birth by Sleep style).
`RoamFacing()` is movement-derived: a 121-frame (2 s) window of
positions; ≥ 0.5 units across the window makes the facing that
displacement. It is tracked and DumpState-visible ("[mku] konquest
pos=(…) facing=(…) (movement-derived)"), **not spoken** — a clock
bearing needs a landmark to point at, and a compass word needs the
unproven z calibration. Live proof (mku-live10, real adapter):
"Position -54, 163." at Apep's post; after the street leg
"Position -63, 158." with facing (0.00, −1.00) — the measured wall
slide, not the leg's average; after the return leg "Position -54,
160."; in the strikes bout (B = 1) WhereAmI speaks the fight line
and no position, though the vec3 reads the dojo spot. Host tests:
5 new checks (99 passing); test-strength floor 95 → 100.

**Sector-word caveat (corrects the 2026-10-17 "FOUND" note):** the
word at 0x08AED5B4 read **1** at pos2's last dump, whose HUD says
A-6 (position (−64.18, 172.24)), and **0** at pos3's u4, whose HUD
also says A-6 (position (−64.18, 140.84)). Same HUD letter, same x,
opposite z excursions — so whatever it tracks, it is not simply the
HUD column (a streamed-cell index is the standing guess). It needs
re-proof before anything speaks it; sector and clock stay unwired.

**Still open here:** the z axis's cardinal direction (one
row-boundary crossing would settle it); the compass needle's
source word (decomp route); position in the other realms (the
block is Konquest-global, but only the village and the dojo are
sampled). The village landmark table this section called for is
built and spoken — see "Village landmark guide" next.

## Village landmark guide — SPOKEN (2026-10-23, guide hunts)

The landmark table the free-roam section above was waiting for.
The Guide query (Command::Guide, newly exposed for mku in
AdapterCommand.swift — the Android generated set regenerated to
match) speaks the Earthrealm village's named spots, one line each,
in fixed table order — the Birth by Sleep Hub Guide's presentation,
formula and fail-silent gating mirrored line for line.

**The table** (ground coordinates in the position vec3's frame):

| Landmark | x | z | Source |
|---|---|---|---|
| Apep's post | −53.49 | 162.96 | live-verified — the placed post entity's own record (apep-decomp: entity at 0x0916BBA0); the guide run stood at the player checkpoint beside it, (−53.85, 162.96), and read "right here" |
| Village spawn | −55.99 | 154.19 | from records — hunt32's J0 spawn dump on an independent lineage; the guide run navigated to the spot and read "right here" on arrival (stopped 1.1 short, inside the 1.5 radius) |
| Bo' Rai Cho's hall | −58.00 | 170.00 | from records — the dojo bout lineage's frozen vec3 and the dojoprobe's navigation target; the guide run navigated to the yard and read "right here" on arrival (stopped 1.1 short) |
| The west fence | −63.47 | 158.06 | live-verified — the trajectory probe's wall contact and live10's street-leg endpoint; the guide run's street leg stopped on the exact pair and read "right here" |

**Speech.** Per landmark: "Apep's post, right here." inside 1.5
units; otherwise distance rounded to the nearest unit ("Village
spawn, 9 away."); with a tracked facing, a clock bearing joins it —
"Bo' Rai Cho's hall, 8 away, 11 o'clock." — 12 spoken as "straight
ahead". With NO tracked facing (the player has not moved since
entering free-roam) the lines are distance-only: never a guessed
direction. The bearing is the signed angle from the
movement-derived facing (RoamFacing — direction of travel over the
last 2 s) to the landmark.

**Calibration (the left/right sign).** The bearing formula was
copied from the Hub Guide (sinA = fdz·tx̂ − fdx·t̂z, positive =
right), but this game's ground frame is not Birth by Sleep's: x
east-west is proven, z's cardinal run is not, so the sign could
have been mirrored here. Decisive sample (mku-guide2): one
straight stick push up the street from (−55.12, 155.79) — travel
measured (0.15, 0.99), essentially pure +z — with the hall at
direction (−0.31, +0.95) from the mid-walk position (−54.59,
159.37). The formula reads that as 11 o'clock, ahead-left. The
mid-walk screenshot (guide2/g2-east-mid.png), camera trailing the
walk with the street receding in the direction of travel, shows
the hall's long tiled-roof building filling the LEFT of the
frame — its street face ~3.4 units to the −x side of the walk
line, matching the geometry — with open street and grass on the
right. The mirrored sign would have put the building on the
right; the Hub Guide's sign stands, no flip. The other frames in
the two runs are consistent (no landmark ever appeared on the
side opposite its spoken bearing) but mostly had the camera
swung or the landmark out of view — this one frame is the
load-bearing sample, which is why it is described in full.

**Gating.** Free-roam only — flag A set, flag B clear, position
plausible — the position inside a generous bounds box around the
village (x −75…−40, z 130…190), and the guide yields to the
channels that outrank it: a latched bout, a dialogue box, or an
objective card all silence it. Everywhere else (menus, pause,
bouts, other realms) it fails silent, like the Hub Guide outside
its room. Plain WhereAmI is unchanged.

**Proof.** Host tests: 19 new checks (bearing quadrants — ahead =
straight ahead, right = 3, behind = 6, left = 9 — distance
rounding, the right-here radius, the no-facing fallback, gating
against flag B / leaving Konquest / an up dialogue box / leaving
the village bounds, and a WhereAmI regression); 118 passing;
test-strength floor 100 → 119.
Live (mku-guide1, real adapter on the hunt79 roam state, final
build): five guide asks — idle at the post, at the fence after
the street leg, back up the street, at the spawn, at the hall
yard — and every one of the 20 spoken lines matches the line
recomputed offline from the same phase's dumped position and
tracked facing exactly (~/mku-scan/guide1/verify_guide.py: ALL
PHASES MATCH); the two end-of-walk asks in guide2 verify the
same way (28 lines in all). Example,
standing at the post before moving: "Apep's post, right here.
Village spawn, 9 away. Bo' Rai Cho's hall, 8 away. The west
fence, 11 away." Example with facing, at the fence after the
street leg (facing = the measured (0, −1) wall slide): "Apep's
post, 11 away, 8 o'clock. Village spawn, 8 away, 10 o'clock.
Bo' Rai Cho's hall, 13 away, 7 o'clock. The west fence, right
here."

**Limits.** Bearings exist only once the player has moved (the
facing is movement-derived and persists while standing still
afterwards; it clears on leaving free-roam). The table is
village-only — other realms have no measured spots, and the
bounds box keeps the guide silent on their coordinates rather
than reading village directions there (a realm whose map
numerically overlapped the village box would defeat that, but
the realms are separate maps and no other-realm roam state
exists on the bench to test with). Bearings
describe the direction of last travel, so right after a wall
slide they describe the slide (as live10's facing did), not the
leg that led there. The z axis's cardinal direction is still
unproven — clock bearings are relative to travel, so they don't
need it. Sector/clock speech stays unwired (see the caveat
above).

## Options screen — the shared child slot and leftover fight words (2026-10-18)

Measured live in the same PPSSPP session, after booting from the title screen:
main menu reached with Start, eight Down presses to item 9, Cross to open the Options
screen. The screen shows three tabs read off the capture: GAMEPLAY, AUDIO, CONTROLS,
with GAMEPLAY focused.

State at that moment (absolute PSP addresses):

- `kMainMenuCursorAddr` 0x09E61750 = **9** (Options).
- `kMainMenuHolderAddr` 0x0911F868 = **0x09119000** — exactly
  `kMainMenuPoolTop - (9-1) * kMenuObjStride`, magic `0x4F424A20` at +4. The main
  menu's own state stays live under the child screen.
- `kKombatSubHolderAddr` 0x09118348 = **0x091181E0**, with magic at +4 and class word
  `0x08BAF3C0` at +0x0C. That is one of the three objects the Kombat list uses, and it
  is **a live object on the Options screen too** — the child-screen cell is shared by
  submenus, so a non-null, magic-valid holder does not identify the Kombat list.
- Fight words, left over from the attract demo, still read **round 1, timer 72, P1 HP
  1.0, P2 HP 1.0, both fighter ids 0**. Every clause of the old fight gate accepted
  that.

Two adapter lies followed, both now fixed:

1. `OnKombatSub()` spoke "Kombat menu. Arcade, 1 of 3." over the Options tabs, because
   the holder named a live object and the parent index (9) was only range-checked.
   The Kombat list is opened only by the KOMBAT item, and the measured parent index
   reads 1 while it is up, so the gate now requires **cursor == 1**.
2. `InFight()` spoke a fight over the same screen, because the leftover round/timer/HP
   words passed. A live front-end row (`FrontEndRow()`: index 1..9, holder naming the
   implied pool object, magic intact) now **outranks** those words.

Both fixes carry host checks against synthetic RAM planted with exactly the values
above (mku adapter: 59 passed, 0 failed). What is still unmapped on this screen: the
three tab objects, the settings rows under each tab, and every value. It stays on the
honest "this screen is not mapped yet" line until those are measured.


## Options screen — tabs, and the GAMEPLAY and AUDIO lists (2026-10-18, live)

Second live pass on the same build (PPSSPP 1.20.4, ULUS10102 v1.03). The Options
screen is a three-item category list, then a settings list per category.

**Tabs.** Measured by stepping and by screenshot:

| index | tab | child-cell value |
|---|---|---|
| 1 | GAMEPLAY | `0x091181E0` |
| 2 | AUDIO | `0x09117500` |
| 3 | CONTROLS | `0x09116820` |

The child cell is `kKombatSubHolderAddr` (0x09118348) — the *same* cell and the *same*
three objects the Kombat submenu uses, reused. What separates the two screens is the
parent index: 9 for Options, 1 for Kombat. The list wraps at both ends.

**Rows.** Inside a tab, the selection is a current-item pointer at
`kOptionsRowHolderAddr` 0x09119CE8, stepping DOWN through that tab's own object run,
each object carrying `0x4F424A20` (" OBJ") at +4:

- GAMEPLAY: top `0x09106040`, stride `0x1160`, **8 rows**, wraps:
  KOMBAT CPU, CHESS CPU, PUZZLE CPU, ROUNDS TO WIN, PUZZLE ROUNDS, ROUND TIME,
  DEATH TRAPS, VIOLENCE.
- AUDIO: top `0x0910B400`, stride `0x10B0`, **5 rows**, wraps:
  GAME MUSIC, ENVIRONMENT, ANNOUNCER, EFFECTS, SPEECH (all sliders read full in the
  capture).

Both tops are heap addresses on this build, like the main-menu pool: the adapter gates
fail closed when a later boot puts the run elsewhere instead of speaking a wrong row.

**CONTROLS is a different screen** — a button-mapping page drawn around a PSP diagram
with prompts like ATTACK 1 and ATTACK 2. Its selection is not the row widget (the row
cell read 0 there), so the tab is mapped but its rows are not, and the adapter says so.

**Not mapped:** every value (volumes, difficulty words, round counts). The adapter
speaks the tab and the row name, nothing else.

## Konquest is blocked by profile creation (2026-10-18)

There is **no ULUS10102 save data** on this memstick (`Documents/PPSSPP/PSP/SAVEDATA`
holds only ULUS10234, ULUS10437 and ULUS10537). With no profile:

- Konquest cannot be entered from the main menu: Cross and Start on the KONQUEST row
  change nothing, and the screen stays the main menu.
- PROFILES offers only **CREATE PROFILE**; VIEW and DELETE are greyed out, which
  confirms the missing profile is the cause.

The creation flow was driven as far as: name entry (default PLAYER 1) → "ARE YOU SURE?"
→ icon choice → **ENTER YOUR KODE** → **CONFIRM YOUR KODE**. Replaying the same
8 button presses did not settle the confirmation, and no save file appeared, so the
profile was not created and Konquest stays unreached. Next attempt should use a short,
even-length Kode (the entry row draws six slots) and confirm it exactly, or a save
created once by hand and kept outside the repository.



## Konquest entry research and the profile attempt (2026-10-19)

Community sources agree on the mechanism. Deception and Unchained require a **player
profile** before the modes unlock, and profile creation asks for a **Kombat Kode**
(a button-sequence passcode). GameFAQs' answer for Deception: create the profile under
Profiles (name, picture, code), save it to the memory card, then at the main menu
**press L and enter the profile code**, after which "all the modes should be
accessible". A locked profile is not loaded automatically.

Attempted live on ULUS10102 v1.03:

1. Press Start to the title, then the main menu (index 1).
2. Down to item 8 (Profiles), Cross: only CREATE PROFILE is enabled (VIEW and DELETE
   greyed), confirming no profile is loaded.
3. Create Profile → name entry (default PLAYER 1) → "ARE YOU SURE?" → icon choice
   (Sub-Zero) → ENTER YOUR KODE.
4. Six-symbol Kode: Cross, Circle, Triangle, Square, Cross, Circle; the same six on
   CONFIRM YOUR KODE. The confirm was accepted.
5. "SAVE PROFILE?" appeared with four EMPTY slots; Square creates the data, Cross on
   slot 1 writes it, and the game showed the "REMEMBER YOUR KODE" tip.

Result on disk: `Documents/PPSSPP/PSP/SAVEDATA/ULUS10102MKU0000/` with `DATA.BIN`
262,696 bytes, `ICON0.PNG` and `PARAM.SFO`, written 2026-10-19 00:14.

**But the game still does not see the profile.** After a cold boot the Profiles screen
again offers only CREATE PROFILE (VIEW/DELETE greyed), and KONQUEST on the main menu
still does nothing on Cross or Start.

The missing step is the **L login**. The PPSSPP debugger accepts only `l2`/`r2` as
shoulder buttons; `l1`, `l`, `shoulder_l` and the other spellings are rejected, and
pressing `l2` on the main menu or the Profiles screen produces no Kode prompt. So the
login was never performed and the profile stays locked.

Next attempt, in order: (a) verify which debugger name reaches the PSP L shoulder, or
press it on a real keyboard binding; (b) if that works, enter the six-symbol Kode at the
main menu to load the profile; (c) only then expect Konquest to open. Do not delete the
save: it is the profile that will be loaded once the login path is found.



## Getting into Konquest: the working recipe (2026-10-19)

Konquest is now reachable and was entered live. Two things had to be right.

**1. A profile must exist and be LOGGED IN.** Creating it is not enough: the save sits
on the card locked by its Kombat Kode, and the game will not use it until the kode is
entered at the main menu. The mode list then behaves normally.

**2. The login is the L shoulder, and the debugger's name for it is `ltrigger`.**
This was the whole blocker. PPSSPP's WebSocket input lookup
(`Core/Debugger/WebSocket/InputSubscriber.cpp`) accepts these names and no others:

```
cross circle triangle square up down left right start select
home screen note ltrigger rtrigger hold wlan remote_hold
vol_up vol_down disc memstick forward back playpause
```

`l`, `l1`, `l2`, `shoulder_l`, `L1` and every other spelling are rejected with
`Unsupported button value`. **`ltrigger` is L; `rtrigger` is R.**

The recipe, measured end to end:

1. Main menu (front-end index 1).
2. `ltrigger` → an ENTER KODE prompt opens in the lower right with six bead slots.
3. Enter the six-symbol kode (Cross, Circle, Triangle, Square, Cross, Circle — the one
   set at profile creation). The prompt closes.
4. Select KONQUEST (Down x3) and Cross. The mode loads.
5. `konquestA` and `konquestB` (0x08AF69C8 / 0x08B123DC) both read 1 from that point,
   which is the adapter's Konquest gate.

**First measured Konquest state.** The mode opens on its title card ("Mortal Kombat /
Konquest"), then the opening training with Bo' Rai Cho. The dialogue buffer machinery
already wired in the adapter reads the tutorial lines live:

- "How you maneuver in kombat plays as much a part of your strategy as how you punch or
  kick."
- "Your first exercise will be to step toward me three times. Then, step away from me
  three times."

with `dlgColor` 0xC8C8C8FF (box up) and `dlgPtr` non-null, i.e. exactly the pair the
adapter's `DialogueUp()` requires. The training bout runs on the arcade engine (round 1,
timer visible) with the Konquest flag set, so the fight gate correctly stays shut.

Next in Konquest: the free-roam sector/clock callouts and the pause/map screens, using
this entry path so the mode can be reached in one pass.



## Konquest objectives are spoken now; the dojo tutorial gate is the next wall (2026-10-19)

Measured in the opening dojo tutorial with Bo' Rai Cho, after the profile login:

- The instruction text is the **same buffer and struct as dialogue**
  (0x09178568, signature at -48) but the colour word at 0x09178554 reads
  **0xFF4848FF** and no dialogue box is drawn — the line is red HUD text.
  The adapter used to call that "box down" and stayed silent, so the one line a
  blind player needs ("step toward me three times") was never spoken.
- Now `ObjectiveUp()` accepts that colour, objectives speak as their own channel with an
  "Objective." prefix (once per line, deduped), and `WhereAmI` reads the live objective
  before dialogue. Host checks: 69 passed, 0 failed.
- Dialogue lines still use 0xC8C8C8FF and keep the plain channel, so the two do not
  collide.

**The training bout itself.** During the exercise the HUD shows a health bar, a
countdown (40, counting down), and an objective counter reading **0 / 6**. Six steps
are needed for "step toward three times, then step away three times".

**Blocker: the counter did not move.** D-pad taps injected through the debugger
(`up` / `down`, 8-30 frames, with and without rests, repeated in both orders) left the
counter at 0 / 6 while the timer ran. The inputs are accepted by the emulator (the same
calls drive the menus), so either the tutorial wants something other than a d-pad tap
(analog? a double tap? facing-relative input?) or the bout's step counter ignores the
injected pad. Until this is solved the tutorial cannot be completed, and free-roam — the
village, the coordinate HUD, the sector/clock values, the pause/map screens and any
quest — stays unreached.

**Navigation: not shipped, deliberately.** The game does show its own locator — the
guide describes a compass with grid coordinates A1-H8 under it, and the objective line is
game-given. Objective text is now spoken. The coordinate and clock words are still
unmeasured, so no navigation callouts were invented; per the standing rule the adapter
stays quiet rather than guessing a direction.



## The dojo drills, passed: what the inputs really wanted (2026-10-19)

The drills are playable through the debugger. Two input paths matter, and using the wrong
one looks exactly like a broken game:

- **Double taps need `input.buttons.send`, not `input.buttons.press`.** `press` does not
  answer until the hold finishes, so a 6-frame press plus the round trip is about 200 ms —
  too slow for the engine to read as a double tap. Send explicit press/release pairs
  (45 ms down, 55 ms up, then again) and a Backward Dash registers at once.
- **Directions are camera-relative.** With the camera framing Shujinko on the left and
  Bo' Rai Cho on the right, "away from me" is **left**. Down-taps did nothing; three left
  dashes moved the drill counter from 0 to 2 / 3 immediately.
- **Walking in free-roam is the analog stick** (`input.analog.send`), held for a second or
  more. D-pad holds do not move the character there.

The drill objectives rotate through phases ("step toward me three times", "three Backward
Dashes", "four different basic attacks"), all on the red 0xFF4848FF channel, so the
adapter's new objective channel reads each phase as it appears.

## Free-roam: the game's own wayfinding

Konquest opens into the village with a HUD that is exactly what a blind player needs:

- **A compass** in the upper left. The dial's cardinal letters rotate with the camera
  (W at the top when facing west, N to the right, and so on) and a green arrow sits on
  the rim.
- **A grid coordinate under it** — measured `B - 6`, then `A - 6` after walking one sector,
  then `B - 6` again on the way back. It only changes when a sector boundary is crossed.
- **The pause menu** (`start`) reads PAUSED: RESUME / SAVE AND QUIT / QUIT GAME.

There is **no quest marker and no waypoint arrow**. The compass and the grid coordinate are
the game's wayfinding, which is what a navigation aid must be built on rather than
inventing waypoints.

## Open problem: the coordinate is drawn, not stored

The HUD coordinate was not located in RAM. A full 24 MB user-memory dump takes 0.4 s, so
diffing is cheap, but a paused A-6 versus B-6 diff changes 246k bytes (the entire visible
world). The one clean `(row, col)` candidate at 0x091519B4 / 0x091519B8 turned out to be a
step toggle: it flipped 0/1 with every walk and never followed the display.

The next attempt should hunt the **player transform** instead — paused diffs show float
triples around 0x08B97xxx that track world position (for example 13.85, -22.94), with the
companion yaw float driving the compass. Sector then comes from position divided by the
sector size, calibrated on one boundary crossing.

No navigation callout is shipped until that is proven against the on-screen readout at two
different sectors: a wrong direction is worse than silence.


## Decomp upgrades queued (from the 2026-10-16 community sweep)

Digest: ~/workspace/games/mk-unchained/decomp-ideas.md (r/decomps,
r/recomps, r/regames, r/ReverseEngineering + linked project sources).
The queue, in order:

1. **Re-import with ghidra-allegrex** (PSP-specific Ghidra extension):
   proper Allegrex relocations, PSP calling conventions, VFPU
   decompilation, PPSSPP `.sym` round-tripping — a straight upgrade
   over the stock MIPS:LE:32:default import behind MKU_ELF.
2. **Resolve PSP imports by NID** (psp-ghidra-scripts / PSPSDK headers):
   naming the import stubs turns the function index into a map —
   callers of the control-read stubs are the input path; callers of
   the dialog/text routines are where Konquest text gets consumed.
3. Keep the verifier-guided loop as doctrine (already the house rule):
   every adapter claim ships with an automated predicted-vs-actual
   check against the running game — RAM + screenshot — never vibes.

## Konquest bout speech (2026-10-23, bout hunts + live8/live9) — MAPPED AND SPOKEN

This closes the item the "Bout HP" note left open ("a bout gate proven
against free-roam negatives; names for P2 are the open half") and the
"Next" list's Konquest bout entry. Bench: ~/mku-scan/mku-bout.cpp (state
probe), mku-live8.cpp / mku-live9.cpp (live adapter runs), screenshots
and logs under ~/mku-scan/bout/.

**What a bout looks like.** The dojo lessons and Konquest's scripted
encounters run on the versus engine in an arena: both fighters' NAMES
sit in the health bars (SHUJINKO / BO' RAI CHO), the phase timer is
centered under them, an objective counter ("0 / 4") at the left, style
names at the bottom (MANTIS / SUMO). Free-roam has none of it — compass,
grid coordinate ("B - 6"), the day/clock readout and EARTHREALM instead.

**The gate — three signals and a latch.** Each signal was measured per
state on the running game this session:

| state | flag A | flag B | fight struct | bout speech |
|---|---|---|---|---|
| versus fight (hunt11/fight2) | 0 | 1 | live | no (versus owns it) |
| versus pause + quit dialog | 0/1* | 0 | frozen | no |
| Konquest opening cinematic (hunt14/konq) | 1 | 1 | sane, FROZEN (timer 60, 1500 frames, no change) | no |
| Konquest exercises/strikes (hunt15/16/20 states) | 1 | 1 | live, timer ticking | YES |
| Konquest free-roam, village (hunt79/ck-atapep) | 1 | 0 | STALE (foe HP 0.000 from the last lesson, timer frozen) | no |

*flag A is lineage-sticky in one bench lineage (see the wart below).

Flag A (0x08AF69C8) is the mode — InKonquest() as before. Flag B
(0x08B123DC) is "the fight engine is live": 1 in versus fights and in
every Konquest arena scene, 0 in menus, in versus pause, and in
free-roam. Struct sanity: both HP floats in [0, 1] and P1's id == 2
(Shujinko's Konquest id, read in every Konquest capture). But A + B +
sanity ALSO hold through the opening cinematic and the dojo's talk
phases, struct frozen at full health — so the bout **latches**: it
opens only when the engine moves (the phase timer word changes or an
HP word changes) while the raw gate holds, and closes when the raw
gate drops. A cinematic never trips it; a real bout trips it on its
first timer tick or first landed hit.

**What is spoken.** On the latch: "Fight. Shujinko 100 percent,
Bo' Rai Cho 100 percent." While latched, the health line re-speaks
whenever a percent has moved since the last announcement, at most once
per 90 frames so combinations coalesce. A side reaching zero speaks
its outcome once per exchange (re-armed when that side's health rises
— the tutorial refills between phases): "Bo' Rai Cho defeated.", and
for P1 "Shujinko defeated." **only with time left on the phase clock**
— measured this session: letting the strikes clock expire zeroes P1's
HP and sets the opponent's win word, which is a FAILED EXERCISE, not a
knockout (the game answers with its own "Try again" dialogue), so that
path stays silent. WhereAmI in a latched bout speaks the fight line
ahead of the objective/dialogue lines (which still auto-speak as they
appear); unlatched Konquest WhereAmI is unchanged.

**What is NOT spoken, and why.** Round, timer and round-wins from the
versus line are all phase machinery in Konquest: the round word read
3, 4, 6 across tutorial lessons (it counts phases — a failed exercise
moved it 3 -> 4), the win words hold lesson tallies (P1's read 2, 4, 6
in states whose screenshots show mid-lesson play), and the timer is a
per-phase clock (25 for the strikes lesson, 88 for exercise 1) whose
expiry fails the exercise. Names: P1 is always Shujinko in Konquest.
P2 names come from a Konquest-id table with exactly one proven entry,
0 = Bo' Rai Cho; any other id speaks as "opponent".

**Live proof (mku-live8, hunt20/after-dash3 — the strikes lesson).**
Restore: bars full, timer 25, counter 0/4 (screenshot). Speech against
the RAM log, in order: entry "Fight. Shujinko 100 percent,
Bo' Rai Cho 100 percent."; Square lands (HP 1.000 -> 0.912, the
documented hit) -> "Shujinko 100 percent, Bo' Rai Cho 91 percent.";
a phase refill -> the line again at 100; Triangle's hit (0.904) ->
WhereAmI answered "Fight. Shujinko 100 percent, Bo' Rai Cho 90
percent."; Circle lands last -> HP 0.000 -> "Bo' Rai Cho defeated."
(after-Circle screenshot: his bar empty, timer 22). The same run then
restored the village free-roam state: 600 frames, NO fight speech,
WhereAmI answered "Konquest." mku-live9 ran the opening cinematic for
1500 frames with the adapter attached — total silence — then a dojo
exercise state, where the entry line fired as the engine moved. Host
tests: 94 checks (11 new: frozen negative, timer-motion entry,
WhereAmI, the 89-percent change line, outcome, refill re-arm, second
outcome, P1 knockout vs P1 timeout, unknown-foe wording, free-roam
close); test-strength floor for mku raised 84 -> 95.

**Wart recorded (pre-existing, not from this work).** Flag A is stuck
at 1 in the hunt1 lineage — its labeled dumps read A = 1 even at a
versus pause, and restoring hunt1/end.ppst today makes WhereAmI answer
"Konquest." there (the pause/dialog speech is dead in that lineage).
The hunt-quit1 lineage reads A = 0 and the QUIT GAME dialog proof
stands in it. So A's stuck state is a property of a session's history
(possibly "Konquest was visited"), not of the current screen. The bout
gate inherits InKonquest()'s mode reading; its own B + P1-id + motion
requirements keep it shut in the versus states measured, but a full
mode-flag repair is its own hunt, not this one.

**Still unproven.** Real (non-tutorial) Konquest fights — the scripted
encounters and free-roam fights of the later realms, and the monk
trainings (konquest_monk_1..5) — were not driven live; they run on the
same engine, flags and struct, and the latch also trips on a first HP
change, so speech does not depend on their timer behaving like the
tutorial's, but their opponent ids are unmapped (they will speak as
"opponent" until named). The pause menu inside a Konquest bout is
still unmapped (unchanged from before). Bout speech is in the working
copy only, like the QUIT GAME dialog — not on GitHub main.

## Sound-first fight health — AUDITED + WIRED (2026-10-25)

The owner's rule (2026-10-24): never speak what the game already cues
with a sound; in high-action play prefer sounds over speech because
speech distracts mid-action. Birth by Sleep's pass (its RE doc,
2026-10-24/25) is the template; this is the same design on this game's
fight struct. Two things changed: the Konquest bout's mid-bout health
re-speak (the throttled line every 90 frames) is REMOVED, and both
versus fights and Konquest bouts now carry the beep + milestone
system. The bout entry line and the outcome lines were audited and
KEPT. Everything else in this doc (dialogue, objectives, free-roam
position, the Guide, pause/menus) is not high-action and is untouched.

### The audio audit (bench ~/mku-scan/snd1, snd2, snd2ko)

Method, same as BBS: bench fights with the game's audio captured per
frame (`psp_read_audio`), peaks measured in tight windows (first
0.12 s) after each HP-word change the driver itself witnesses, against
each room's own baseline.

Versus (snd1: hunt11/fight.ppst restored, Scorpion vs Goro, driven
strikes; 24 foe hits, 10 player hits, a KO, a round change, a second
round, player damage down to 48%):

| Event | Tight-window peak | Reading |
|---|---|---|
| room baseline | median 0.0000, p99 0.026, max 0.028 | near-silent (see caveat) |
| foe hit (n=24) | median 0.525, min 0.414 | sounded |
| player hit (n=10) | median 0.446, min 0.302 | sounded |
| KO blow | 0.594 — the fight's loudest event, ~2 s slow-motion fall tail | heavily sounded |
| round-2 intro (+1.5–2.0 s after the turn) | 0.454–0.488 over ~0.15 surroundings | sounded (announcer/sweep) |

Caveat, stated plainly: in this bench a savestate restore drops the
music bed, so the versus baseline is near silence and the peaks are
measured against that floor, not against playing music. The verdicts
do not lean on the floor alone: BBS's audit (real music bed present)
found the same impact family at full scale, and a 0.41-minimum impact
is not a subtle sound in any mix. No announcer line was measurable
after the KO in this capture, so nothing is claimed either way about
a winner announcement — the versus decision below does not need one.

Konquest bout (snd2: hunt20/after-dash3.ppst, the strikes lesson
against Bo' Rai Cho; snd2ko: the same lesson extended until his HP
was driven to zero between phases):

| Event | Tight-window peak | Reading |
|---|---|---|
| dojo ambient bed | median 0.057, p99 0.184, max 0.187 | the room's own level |
| bout entry latch | 0.107 | NO distinct entry sound (inside the bed) |
| strike hits | 0.257–0.452 | sounded |
| per-phase refills (×3) | 0.188 | silent (the bed itself) |

The lesson refills Bo' Rai Cho to 100% between strikes. Under the old
adapter the arranged-KO run spoke **11 bout lines for 5 identical
strikes** — every refill + re-drop pair re-spoke the health line.
That chatter is the concrete thing the removal kills. An arranged
debug-write of his HP to 0.05 was overridden by the lesson engine
within a phase, so a genuine Konquest KO could not be captured in
the tutorial; the outcome verdict rests on the versus KO measurement
(same engine, same impact family) plus the identification argument.

### Verdicts

- **Mid-bout health re-speak: REMOVED.** Hits are sounded; a spoken
  percent every 90 frames is exactly the mid-action distraction the
  rule retires. Replaced by the beep + milestone system below.
- **Bout entry line: KEPT.** It fires at the latch — before the
  action, not during it — names the foe (the one fact a new player
  needs and no sound carries), and its instant is provably unsounded
  (0.107 in a bed peaking 0.184).
- **Bout outcome lines: KEPT.** A KO thud names nobody; in Konquest
  the defeat IS the lesson signal (pass/fail), and Konquest's own
  dialogue is unvoiced text. Same precedent as BBS keeping
  "D-Link: <name>." — when the sound cannot carry the information,
  speech stays. (Training timeouts still speak nothing: the game
  zeroes Shujinko's HP itself there, unchanged.)
- **Versus: nothing removed, the system added.** Versus never had
  entry/outcome speech — its intros and wins are the game's own
  announced presentation (the round intro measured sounded) — and it
  gains none. WhereAmI's fight line keeps the exact numbers on query
  in both modes.

### The wired system (`WatchMkuHealthCues` in Core/mku_adapter.cpp)

One watch, called every frame after `BoutOnFrame`: versus state
(`InFight()`) tracks P1/P2 from the fight struct; a latched Konquest
bout tracks Shujinko/the foe from the same struct with mode-tagged
track identities, so the two modes never share a latch. HP here is a
0..1 float; the percent is the fight line's own rounding
(`frac*100 + 0.5`).

- A hit dropping a bar stashes a one-shot beep: foe RIGHT (+0.8),
  1200→300 Hz; player LEFT (−0.8), 800→200 Hz; pitch logarithmic in
  the new fraction — the BBS family's exact parameters.
- Spoken percent ONLY at the milestone crossings 75 / 50 / 25 /
  nearly gone at 10 ("Foe health N percent." / "Your health N
  percent." / "Foe nearly gone." / "Your health nearly gone." — the
  BBS wording verbatim; the milestone set is Mortal Kombat 1's
  official accessibility Health Meter set). Latched per fighter;
  priming silent with at-or-below milestones pre-latched; a plunge
  speaks only the lowest crossing; healing back to milestone + 5
  re-arms (the tutorial's per-phase refills exercise this and stay
  silent doing it).
- A drop to 0 raises nothing: the KO is the game's own loudest
  moment, and Konquest's outcome lines own the defeat. Death (and
  round changes, read as a fresh fighter) resets the side's track.
- Events ride the cue snapshot: `mku_cue_snapshot` (this adapter's
  cue_snapshot was `nullptr` until now) copies pending events out on
  the first poll inside a live fight/bout and clears them — each hit
  sounds once; a poll outside a fight is refused and consumes
  nothing. No lock-on exists here, so the beacon fields stay empty.
- No HUD-gauge split exists in this game, so there is no
  MkuHudGaugeCues-style seam; the beeps sit behind the app's
  battle-cues switch via the snapshot, like every adapter's.

### Proof

- Host tests: **145 passed, 0 failed** (was 118). The three bout
  checks that asserted the old re-speak now assert its absence (hit →
  beep via the snapshot, no speech; refill → silent re-arm; unproven
  foe id → named only by the entry/WhereAmI "opponent" wording), and
  a dedicated sound-first section walks the versus ladder (prime,
  beep pitch/pan both sides, consumed-once, 75/50/25/10 both sides,
  drift-no-repeat, heal +5 re-arm, killing blow silent, round-2
  re-arm, refusal outside a fight consumes nothing) plus the bout
  path (entry kept, crossing speaks, outcome kept).
- Live versus (snd3, same driven fight as the audit): CUE lines align
  1:1 with the driver's hit events, pitch falling 1098→317 Hz as the
  foe fell; milestone lines at exactly the crossing hits (75 @ the
  76→67 hit, 50, 25, "Foe nearly gone." @ the hit to 10%); the KO
  raised no beep and no line; round 2 re-primed and re-armed (75
  spoke again); the player's own crossing spoke "Your health 75
  percent." in the takedmg phase.
- Live bout (snd4, strikes lesson): entry line once at the latch;
  strikes beeped (1062 Hz at 91%, again after the silent refill,
  1027 Hz at 89%); zero mid-bout health speech.

### Limits and deviations

- Two-player versus stays P1-centric (P1 = "you"), the FightLine
  convention; there is no second reader side to serve.
- The BBS never-locked-foe limit does not arise: fights are 1-on-1
  and both fighters are in the one struct — the tracked foe is
  simply P2 (who is also the only foe there is).
- NOW AUDIBLE in the apps (2026-10-24, the shared app queue item):
  both apps poll this adapter's snapshot — the same BBS-shaped
  fields — and voice the beeps (iOS CueSynth's per-frame
  `poke_cue_health()` poll; Android's new PspCueSynth behind its
  Settings "Battle cue sounds" switch), gated by each platform's
  battle-cues switch, off by default. The core bridge's per-frame
  snapshot cache is what guarantees the poll the events reach is
  the frame's only snapshot (see docs/design/cue-synth.md,
  "App-side implementation", for the verification level: compile,
  contract and algorithm proofs — no device has sounded one yet).
  The milestone speech, being ordinary adapter speech, works today
  as it did.

## Apep's talk trigger — what it checks (2026-10-23 investigation)

**Question.** Apep (the martial-arts student who guides Shujinko to
Bo' Rai Cho's other schools) stands at his post in the Earthrealm
village — placed entity at (-53.49, 2.01, 162.96), RAM 0x0916BBA0 in
the hunt79 lineage — and has never once answered the interact button,
on any bench lineage, at point-blank, with any button. His dialogue
gates the whole Konquest tour (dojos 2-5, the bridges, Earthrealm
scenes E3-E7), so this trigger is the current critical path.

**What the trigger checks (from the game's own script data).** ER1's
script/state tables are resident in RAM (name blocks at
0x09DA3000-0x09DA5C00 and 0x09D90800-0x09D93000). Apep's state names
spell out his state machine in the authoring tool's own words:

- `apep_startup_script: I haven't talked to apep |AND| I have finished
  the first dojo` (0x09DA5678) — the state that arms him.
- `apep: I haven't talked to apep |AND| I haven't finished the first
  dojo` (0x09DA537C), `apep: default`, `apep: I have talked to apep`.
- His follow-on states are the tour: `Talk to apep after dojo 2/3/4/5`,
  `apep_at_dojo_2..5`, `apep_at_bridge_meat: apep arrives at bridge`.

So the talk event exists only in the startup state, and that state
requires a story flag the script calls "finished the first dojo" (plus
not-yet-talked). His lines are in the village dialog pool
(`er1_dialog_text`, pointer table at 0x09D6BCB8): entry at 0x09D62EF8
"Hi! You must be Shujinko, the one Master Bo' Rai Cho mentioned.",
0x09D62FB0 "My name is Apep. Master Bo' Rai Cho asked me to show you
the other schools where he teaches martial arts.", then "Follow me!"
etc. Villagers confirm the design: "You should find Apep. I heard he
was waiting for you." / "Go play with Apep. I see him down by the
dojo." Apep also has `apep_daytime_behavior`, `apep_nighttime_behavior`,
`apep_go_home`, `apep_at_school`, and an `apep_near_handler` — he is a
scheduled NPC, not a statue.

**What was ruled out live (all on the bench, this session).**

1. **Input mechanics.** On hunt79's ck-atapep.ppst — a genuine
   post-tutorial free-roam state (flag B=0, P1 HP 1.000, standing at
   the post; the lineage that completed the exercises through the
   final hilow phase) — an exhaustive input matrix fired nothing:
   3600 frames of idle, repeated Cross, Cross at 8 facings, every other
   face/shoulder button, Select/Start, and walk-in bumps
   (mku-talkprobe). Cross-to-talk is not the problem; the event is
   simply not armed in that state.
2. **The savestate-hop lineage theory.** A fresh cold-boot, no-loads
   run (mku-apep1, continuous solver) passed the steps, backward-dash
   and basic-strikes lessons cleanly, then FAILED OUT of the
   variations phase and was ejected to the village spawn at 0 HP —
   the `failed_boraicho_fight` / `move_hero_to_safe_loc_boraicho`
   path. So the "clean lineage" has still never genuinely finished
   the tutorial on this bench; the hop lineage (hunt79) remains the
   only genuine completion, and Apep is silent there too.
3. **Phase timeouts** (mku-timeoutprobe on the hilow state): when an
   exercise timer crosses zero, P1 HP is zeroed as bookkeeping, flag B
   stays 1, the player stays in the dojo, and a "Try again" box waits
   for dismissal. Timeout is not the ejection mechanism; a lost fight
   is.
4. **Just visiting Bo's yard.** In free-roam the school yard
   (-58, 170) holds only the gate and two student guards; Bo is not
   there, the gate does not open, and a sweep north to the building
   wall (z~176) triggers nothing (mku-dojoprobe, mku-sweepprobe).
   Apep stays silent after the visit.
5. **The level-reload route is blocked from savestates.** The
   Konquest pause menu is Resume / Save and Quit / Quit Game. From a
   savestate-restored session, Save and Quit never wrote the stick
   (DATA.BIN md5 unchanged) and the title threw "PROFILE NOT FOUND —
   Data on Memory Stick Duo has changed" (mku-reloadprobe). The
   emulator's stick-profile path is inconsistent with savestate
   restores — the same trap recorded for Konquest entry. A reload
   test therefore needs a full cold-boot lineage whose profile was
   loaded from the stick in the same session.

**What is still open — the two candidate mechanisms.**

- (A) A post-exercise test: Bo's script has `boraicho_showme`,
  `boraicho_not_pass`, `boraicho_brc_impressed`, and there is a level
  object `boraicho_trigger_after_last_dojo` (trigger record at
  0x091C85C0). "Finished the first dojo" may mean passing Bo's test
  fight, not merely finishing the exercise phases — and if the fight
  is lost, the ejection at 0 HP is exactly
  `move_hero_to_safe_loc_boraicho`. Its trigger volume was not found
  in the yard sweep; its geometry is not in the trigger record
  (+0x20 is null; the script-data pointer at +0x18 is 0x09DAE0DC).
- (B) Startup re-evaluation: the arming state is literally a
  *startup* script. If states are picked once at level load, Apep can
  only arm when ER1 loads with the flag already true — i.e. after a
  Save-and-Quit + Continue cycle on a stick-loaded profile (blocked
  from savestates per (5) above; needs the cold-boot lineage).

**Next experiments, in order.** (1) One cold-boot lineage: stick
profile, all tutorial phases in a single process (the continuous
solver now passes steps/dash/strikes; variations and hilow still need
a reliable clear), then talk to Apep directly — if silent, Save and
Quit, Continue, and talk again. That single run tests the clean
lineage and (B) together. (2) Find the `boraicho_trigger_after_last_dojo`
volume in the collision data (0x090Bxxxx geometry space) and walk
into it; if a test fight starts, win it and retest Apep (tests (A)).
(3) Ghidra: decode the script VM's state-condition evaluation for the
apep_startup_script entry to the concrete flag word, then read that
word in a post-tutorial RAM dump — the code-level verdict on which
event sets it.

Bench artifacts for this investigation: drivers and logs in
~/mku-scan/ (mku-apep1, mku-talkprobe, mku-dojoprobe, mku-sweepprobe,
mku-varprobe, mku-timeoutprobe, mku-pauseprobe, mku-reloadprobe;
apep1-apep12 and probe run folders; working notes in
~/mku-scan/apep-decomp/NOTES.md). Nothing was pushed.

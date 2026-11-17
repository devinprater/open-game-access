# Dissidia 012 Duodecim (PSP, USA) — adapter start

Source: Games/PSP/Dissidia 012 - Duodecim Final Fantasy (USA) (En,Fr,De,Es,It).cso

## Artifacts
- ISO: ~/dis012/d012.iso. Extracted + decrypted files: ~/dis012/out (EBOOT.dec via pspdecrypt, tag D91613F0 type 2).
- Ghidra project: C:\Users\Devin Prater\oga-ghidra-dissidia012 (D012): EBOOT.dec + 3 overlays, all analysed.
- Scripts: C:\Users\Public\d12\match.py (masked-opcode function matcher vs the original).

## Layout differs from the original
- EBOOT is a proper ELF: load 0x08804040, size 0x1d9380 (1.9 MB code) vs original 3.8 MB at 0x08804000.
- Three overlays, ET_SCE-like (type 0xffa8), ALL linked at 0x089dd4c0 (one shared slot, swapped at runtime):
  OVL_BATTLE_APP.ELF 1.6 MB, OVL_MENU_APP.ELF 1.56 MB, OVL_EXTRA_APP.ELF 24 KB.
  Resolve which overlay is live before trusting any address in 0x089dd4c0..~0x08b63000.
- PACKAGE.BIN 1.03 GB + PACKAGE_INFO.BIN (magic 'pack', 0x2009 0218 header; not the original's 'packm').
- New resource paths: dungeon/* (Labyrinth: dungeon_entry.due, piece_data.dpc, dungeon_reward.dur),
  worldmap.bin, menu_pk_world_map_*, colosseum.bin, party_battle (menu_pk_party_battle_*), original_quest_param.bin,
  win_lose_condition_param.bin, prologus_* (system, manual_param, bgm_entry, voice_load), DLC (dlc_%03d, aerith.bin),
  general_archive/field/ZQ/field_lang.bin. Still present: talkevent/%s.evex, general_archive/main.
- Movies: 012 cutscenes ship as .PMF on disc (DES*, M_*, UP_*, END, LOP) -> audio description target.

## Original -> 012 function carry-over (12-word masked-opcode match; immediates/jump targets masked)
Found in EBOOT: menu selection validator (orig 250538 -> 0x088d75f0), pad read (f68e0 -> 0x08871e24),
 converted-input sink (f6694 -> 0x08871bd0), menu input consumer (267cb8 -> 0x088ea070), event poster (1accb4 -> 0x088a9de8),
 SFX poster (9b930 -> 0x0882fa10), heap alloc (e5710 -> 0x0885c6cc), menu node lookup (25468c -> 0x088da828),
 menu event dispatcher (24932c -> 0x088cf550).
Found in OVL_BATTLE_APP: talk-event manager init (1c3654 -> 0x08abe9f0), EX pickup poster (ce824 -> 0x08a3abc4).
NO match (rewritten, or moved with edits): board constructor/avail-dir/tick, DP adjuster, battle event dispatcher,
 enemy-lock writers, fighter ctor, EX check, menu manager ctor, HUD/UI dispatcher, evex loader, XOR decoder.
=> Menu/input/engine plumbing carries over; board and battle code is reworked and must be re-found.

## Adapter skeleton (Core/dissidia012_adapter.cpp, id "dissidia012", ULUS10566)
- Registered in Core/adapters.cpp after the original; added to core-sources.sh and the host test link lines.
- Attach fingerprints instruction words 1 and 2 of three EBOOT functions (pad read, event poster, menu selection
  validator). RUNTIME-CONFIRMED: booted headless (psp-proof harness, 600 frames) and read live RAM; all three match
  the decrypted EBOOT at the same addresses, so the EBOOT loads at 0x08804040 as the ELF says.
- Word 0 is NOT usable: PPSSPP rewrites the first word of a function (pad read read 1744860755 live vs 666763184 static).
- It speaks nothing yet. Seven adapter suites still pass (dissidia, dbz, mku, dq9, khbbs, alttp, gba).
- Live-read tool: ~/fp-run <img> <savedir> <frames> <hexaddr>... (source scripts/dissidia012-fp-main.cpp).

## Menu flow measured headless (ULUS10566, single boots unless noted)
- Title cursor 0x096AE20C (two boots). Data Config cursor 0x096AEA70 (two boots). Setup screens share 0x096AEA70,
  screen id u32 at 0x096AEC1C: 10 Play Plan, 12 Bonus Day, 14 Battle Mode, 16 Story Mode, 18 confirm. Confirm YES/NO
  is driven by Left/Right on the same cursor (0 YES, 1 NO); Up/Down do nothing there.
- After YES: Subtitle Language screen, then the PSP Save utility (NEW DATA), then straight into a tutorial battle
  (Lightning vs a Terra manikin), not the main menu. The main menu and story mode are behind that battle: NOT reached yet.
- Nickname is PPSSPP by default; the OSK is PPSSPP's own. Not wired.

## Battle HUD values in RAM: OPEN
Tutorial battle, HUD read by screenshot: left fighter BRV 95 / HP 1000, right BRV 408 / HP 338, pool 133.
RAM region 0x0969D688: [95,95,338] then at 0x0969D6DC [408,408,1000]. Both BRV pairs change live (95->79, 408->424).
The HP next to each BRV is the OTHER fighter's HUD value, so either the records hold HP in a different slot or the
screenshot reading is swapped. Do not wire a fighter reader until a fight with changing HP settles it.
The original's layout (S+0x0E brv, S+8 max, S+2 dmg) did NOT hold: 012's fighter struct was reworked.

## Battle HUD pairing SETTLED (one boot, tutorial fight, three snapshots vs screenshots)
Two records 0x54 apart. Each holds its OWN Bravery twice (cur, cur) and the OPPONENT's HP:
- 0x0969D688 Lightning BRV (95, 115, 82); 0x0969D690 Manikin HP (338, 179, 0); 0x0969D694 flips 0 -> 2 at HP 0 (defeat flag, one sample)
- 0x0969D6DC Manikin BRV (408, 419, 407); 0x0969D6E4 Lightning HP (1000, 1000, 1000)
HUD matched RAM at the 115/179/419/1000 snapshot. NOT yet wired: heap addresses, one boot only; needs a second-boot check
and a chain from a static holder. The original's S+0x0E/S+8/S+2 layout does not apply.
Flow: after the confirm YES the game runs the tutorial fight; victory screen follows.

## Main menu and Story Mode entry (headless, from savestate ~/d12s/mainmenu.state; labels by screenshot)
- Reaching the main menu: the opening is a chain of tutorial fights (Moogle coaching between them). Scripted mashing of
  square/circle/cross clears them (scripts/dissidia012-tutorial-gen.py). A "Welcome" pop-up and an "Introduction" pop-up follow.
- Main menu rows: Story Mode, Battle Mode, Communications Mode, PP Catalog (greyed on a fresh save), Collection, Data Config, Options.
  Cursor 0x096AEBEC (mirror 0x096AEC14), ONE boot. Adapter speaks only rows that are showing: PP Catalog is silent until its
  enable flag is found. Circle on the main menu opens "Save and return to the title screen?" Yes/No (cursor not read yet).
- Triangle = Customization, Square = Calendar shortcuts. Story Mode opens a scenario banner "Main Scenario 012: Treachery of the Gods",
  then a chapter row. Fresh save shows ONE chapter: "Prologue - Path to Sanctuary", Lightning Lv 1, CHAPTER 0 card, synopsis line,
  then "Play this story?" Yes/No (Circle there opens Customization).
- Story board (world map) code still unfound; not reached yet.

## Story Mode is a 3D field in 012 (headless, 24-frame contact sheet, labels by screenshot)
Prologue flow: Yes at "Play this story?" -> narration cutscene -> in-engine character cutscene (Vaan, Laguna, Lightning, Kain)
-> field dialogue box (Lightning) -> tutorial prompt "attack these orbs with Square" -> free-roam 3D field. HUD on the field:
Lv, HP, Gil, KP, Skill (Square), Map (Select), Customization (Triangle). No pillar grid, no Destiny Points seen so far.
The grid board seen in the original appears to be gone from the prologue; this explains why the original's board functions found
no match. A grid may still return later via gateways (not confirmed). So the story-mode adapter must read a field: player position,
orbs, enemies, gateways, dialogue boxes, and the Select map. Savestate ~/d12s/prologue_end.state sits on the field.

## Field position hunt (headless, savestate prologue_end.state): CANDIDATES ONLY
Driver gained `analog X Y FRAMES` (stick push, then release). Pushing the stick up for ~90 frames twice walks Lightning
into a scripted trigger: the HUD disappears and a figure near a glowing purple portal appears. Use 25-frame pushes.
Float diff (up, up, rest, down) leaves two families, one boot, NOT yet matched to the player:
- small floats 58.0 -> 60.4 -> 63.6, rest 63.6, back 60.2 (many copies, e.g. 0x0976B280, 0x0991842C0, 0x0999C338)
- large floats 1158.9 -> 970.4 -> 774.0, rest 774.0, back 955.5 (0x0978FF0C, 0x097ADA2C)
Neither is proven to be position (the small family may be a camera or a copy that animation also writes).
Next: tie one to the player by walking left/right too (a second axis) and checking the HUD-visible position change.

## Field position: most likely a joint array (one boot, savestate; NOT yet a wired reader)
Two-axis walk (up, right, left, down, 20-frame pushes) leaves 26 float triples that move and reverse correctly on both axes.
They are records of (x, y, z, 1.0), stride 0x10, with x ~58, y 0..1, z ~20 at rest, clustered within about 0.5 of each other:
e.g. 0x0976D020, 0x0976D030, 0x0976D040, 0x0976D050 ... 0x0976D090. That is a skeleton of world-space joints for the player model,
not a single root position. Walking up moved x +1.8 and z -2.0; the camera is not world-aligned, so axes are camera-relative.
Node headers sit just before it (0x0976CFC0 points at 0x0976D010). Heap addresses: they will move per boot.
OPEN: the owning character object (root position, facing, HP) and a fixed holder that reaches it. Next step is to follow the
pointers that reference 0x0976CFC0/0x0976D010 back to a static holder, then confirm on a second boot.

## Field position verification (replay 2)
- Reaching the field reproducibly: mainmenu.state + circle, cross x3, then 22 chunks of (6 x cross, run 90) reaches the field by
  chunk ~20 (contact sheet frames 20-30 show Lv 1 / HP 1000 / Gil 0 / KP 0). A different cadence lands in a cutscene instead.
  A separate fresh process with a different cadence did NOT show the block (that run was in a cutscene), so that is not a failure.
- Replay 2 (same savestate, same script, separate process, fresh walk up/right/left/down): the 26-ish joint hits reappear at the
  SAME addresses (0x0976D020/30/50/90, 0x0976B360, 0x09FFECA0...). This is a deterministic replay, NOT an independent heap:
  it proves the addresses are stable for this input path, not across arbitrary play orders.
- Pointer walk back from 0x0976D020: reaches only its own header chain (0x0976CFC0, 0x0976D010...), no static holder. No fixed holder
  found, so a field position reader is NOT wired. Static 1000s near 0x0976C2E8 ([330, 95, 1000, 11, 12]) look like a stale battle
  record, not field HP.

## Field player object FOUND and verified on three input paths
- Reader of the joint block (read watch, classic interpreter): resident EBOOT helper FUN_089a3868, a 4-word vector copy with 100+ callers. Generic, so it names no owner.
- Pointer walk (span 0x400) with heap not counted as static: 0x0976D020 <- 0x0976B5E8 <- 0x08B83DE4 / 0x08BAEA04.
- HOLDER: u32 at 0x08B83DE4 (mirror 0x08BAEA04) -> player object P (0x0976B250 this run; heap). Reads 0 in cutscenes.
- Player position: float row (x, y, z, 1.0) at P+0x30 (x at +0x30, y +0x34, z +0x38). Same row repeats at P+0x80 and P+0x100 (joints).
  Walk up moved x 58.0 -> 59.7 and z 20.0 -> 17.8 (camera-relative axes).
- Verified on: savestate run, replay in a separate process, and a perturbed-timing run (different press hold/gaps). All three: holder -> P, row tracks the stick.
  Still all derived from the same mainmenu.state start, so it is a different heap timeline, not a cold boot from the title.
- Holder table nearby: 0x08B83E18/1C/20 -> three more objects with the same (x,y,z,1) row at +0x30: likely party members (other three warriors). Not yet checked.

## Orbs: investigation so far (NOT found)
Movement model (measured, field2.state): analog only. Pure-axis pushes move ~1.6 units per 12 frames; DIAGONAL stick input produces
ZERO movement; the D-pad does nothing at all. Axis directions are camera-relative and the mapping rotates as the camera follows
("up" read (1.61,-0.47) early, then the same vector appeared for "right" later), so closed-loop navigation needs per-step
recalibration. Blind walking to a target stalled at ~5 of 34 units.
- Select map screen: terrain plus a player arrow only. No orb/objective markers, no legend.
- worldmap.bin is a tagged container. Sections and consumers (EBOOT):
  gate->FUN_08910668, gtbg->FUN_089107f0, pint->FUN_089123dc, frtk->FUN_089103e0 (loader FUN_0890ecfc);
  wene->FUN_0891314c, wfri->FUN_08810dd8, frnd->FUN_0890f41c, wtlp->FUN_08912fc8, not_->FUN_0891094c (loader FUN_0890ef38).
- pint (interaction points, the best orb candidate) lands in DAT_089d83b0. Live value 0x08B8357C, an INLINE array of
  {ptr,ptr,1} records, 6 entries. Only two carry plausible coordinates: (75.0, 100.0) and (114.56, -24.22, -31.07).
  Watching reads of that array for 2 frames on the field gave ZERO hits, so it is not the per-frame point list on this map.
- Reading the player's position row (P+0x30) is done by matrix/render code (0x089A3EFC, sceVfpuMatrix4Transform), so P+0x30 is a
  render transform, not necessarily the gameplay root.
- Pressing Square at the field start (no orb in view) changed 16,646 words, all animation/camera; no clean orb or counter delta.
BLOCKER: no orb located, and no reliable way to walk to one. Next: find the gameplay root position with a WRITE watch while moving
(the read watch only shows render users), then find the proximity check that consumes it; or re-derive the pint consumer FUN_08911fec.

## pint point list: STRUCTURE FOUND by decompiling (not yet proven to be the orbs)
Accessors (EBOOT): FUN_08912164 find-by-id, FUN_089120a4 find-by-id from an offset, FUN_08912060 clear,
FUN_08911fec fill/copy, wrappers FUN_089121a8 / FUN_089121d0 / FUN_08912424. All take DAT_089d83b0.
Layout: *DAT_089d83b0 -> a vector of lists; each element is {list*, list*, count}. Live on the field:
  [0] 0x095D2B20 count 160   [1] 0x095D2A20 count 11   [2] 0x095D1E80 count 147
  [3] 0x095CF700 count 120   [4] 0x095CD0C0 count 74
List header: +4 count, records start at +8, stride 0x2C, signed 16-bit id at +0 (so +0 is a u32 with the id in its low half),
a second small id/kind in the low half of +4, floats at +0x10 (x) and +0x14 (z), a third float at +0x18.
344 records carry coordinates. Player (58.0, 20.0): nearest record is id 1173 at (59.0, 19.4), 1.2 units away.
⛔ NOT CONFIRMED: pressing Square next to that record left every field unchanged, and watching the vector for 2 frames gave
ZERO reads. So this list is not the field's live interaction set; it is most likely per-map data for a different map, or
menu/world-map layer data. Do NOT wire it as orbs.
Next: the field's live set is probably the gate/wene lists (DAT_089d4788 for wfri read live as a list too). Watch each list
header for reads with the player standing on orbs, and confirm by a real change on pickup.

## Field navigation now works (adaptive axis steering)
scripts/dissidia012-nav.py: probes the stick ("up" 8 frames, "right" 8 frames), derives the camera-relative basis, picks the single
axis whose measured direction best matches the bearing to the target, pushes 12 frames, and recalibrates when the actual movement
misaligns. Measured: reached (68.0, 17.0) from (58.0, 18.95) in 5 iterations, arriving at (65.98, 17.0). Straight-line closed-loop
steering with a FIXED basis stalls because the camera rotates; recalibration is what fixes it.
⛔ Movement constraints (measured, re-confirmed): analog only; diagonals produce ZERO movement; D-pad ignored.

## Field objects as actually seen (screenshots at ~(67.6, 17.3))
Visible on this map: a distant bright white glowing pillar/portal standing on a peninsula, a purple-and-white figure beside it,
and a stone archway ruin behind. In another frame a four-pointed white sparkle sits next to the distant figure. No chests.
Standing exactly on pint record 821's coordinates (68, 17) and pressing Square four times changed ZERO fields in the pint list,
so that list is still not the interactable set. The nearest candidates for the next probe are the glowing pillar and the figure.

## Reaching the orb: camera coupling blocks dead-reckoning
Measured on the field: pushing the stick LEFT/RIGHT rotates the camera rather than walking (the character stays put while the view
orbits); walking FORWARD also swings the view, so a heading cannot be held open-loop. A visually-guided walk therefore loses the
target: the glowing orb was seen at the upper right on a peninsula, then left frame within two bursts.
The orb itself IS visible in renders (a white four-pointed sparkle beside a purple-and-white figure on the peninsula), so it exists
as a rendered world entity; it has not been matched to a RAM record yet.
Field controller found: FUN_08919A14 (1760 B) is the player state machine for this map — movement, camera yaw (it adds a delta to
the yaw via the object's vtable +0x3c), and the button branches. It queries input through the SAME function-pointer API shape as the
original game (obj+0x34 -> vtable +0x14), which is the seam to hook to find the Square/skill action.
Next: read the Square branch of FUN_08919A14 (or its callees FUN_0891600C / FUN_08916380 / FUN_08914B64) to find the skill action,
which must reference the orb it collects.

## Framebuffer steering probe (new direction, partially working)
The driver now has `bright`: it scans the 480x272 framebuffer for near-white pixels (channel sum > 720) and prints count, centroid and
bounding box. That gives a vision-free way to find bright world objects.
Result: it DOES see the orb — at one camera angle it reported n=19, box 32x40 at cx=237,cy=181, exactly where the sparkle sits.
But two things defeat the simple steer: the bright cluster is also LIGHTNING HERSELF (centre screen, ~n=20, box 38x36 at cx=225)
and the sky/clouds (n in the thousands, box covering the screen). The first auto-steer loop oscillated on the character.
Refinement needed: mask the player's screen region (centre-bottom) and the HUD, require the cluster to GROW as distance closes, and
use the growth as the approach test instead of a fixed pixel count.
scripts/dissidia012-autonav.py is the current loop; scripts/dissidia012-nav.py (coordinate steering) still works for known targets.

## Brightness probe, iteration 2: masking added, still no approach
Driver `blob`: brightest 12x12 tile with near-white pixels, masking the player (centre-bottom box) and the HUD. It reports n, x, y.
Finding: the locked blob sits at y ~ 118-130, i.e. ABOVE the horizon line. The orb sparkle measured earlier sat at cy ~ 181, below it.
So the probe is locking onto a bright cloud edge or distant light, NOT the orb; that is why "n" never grows when she walks.
Also measured: walking forward swings the camera hard (a blob at x=278 jumped to x=86 in one 30-frame walk), so an approach loop has to
re-acquire after every step. Smaller walks (8 frames) plus re-centre still did not converge in 60 iterations.
CONCLUSION: screen-space brightness is the wrong instrument for this. The orb has to be found in RAM, not by eye.

## DECOMP BREAKTHROUGH: the pint list IS the world-object list (drawn), layout known
Draw registration: FUN_089121f4 (add) and FUN_08912304 (remove) walk the pint list with the by-id iterator FUN_089121a8 and, for
records whose KIND (s16 at record+6) is 1 or 5, add/remove a model in the render list DAT_089d4960 (model created via FUN_0884417c
with tag 0x264000; kind 1 resolves its model through FUN_088bbd8c(*(record+0x1c)) and FUN_08847108, kind 5 uses the s16 at record+0x1e).
So pint records are world objects with 3D models. Their zero read-hit count earlier only means the add/remove pass runs on map load,
not per frame.
RECORD LAYOUT (stride 0x2C, list header +4 = count, records at +8):
  +0x00 s16 id        +0x02 s16 sub       +0x04 s16 kind2/id      +0x06 s16 KIND (1 and 5 are drawn)
  +0x08 flags         +0x10 float X       +0x18 float Z          +0x1c u32 model/object pointer   +0x1e s16 model id (kind 5)
Live census near the field: kind 0 x123, 2 x52, 3 x30, 5 x29, 6 x12, 1 x10, -1 x21 (plus junk rows from other lists).
Nearest DRAWN (kind 5) records: (74,45) x4 at 29.7 units, (66,-22) at 42.8, (110,0) x4 at 55.7, (73,95) at 76.5.
GATE HANDLER: FUN_08913e10 resolves the gate the player occupies via an imported call and reads the gate record (FUN_089105d8 by
index) whose s16 CELL COORDS sit at +0x10/+0x12; the controller then stores them at +0x38/+0x40 and switches state 5 (enter) or 6.
So gate records are CELL-based, not float world positions — the map is a cell grid with gates, like the original game's board.
⛔ Navigation limit measured: the nearest drawn object sits across water. The coordinate navigator oscillated at (59,19.6), never
closer than 28 units, because the terrain is a path network. World-object work needs PATH data (gate/cell grid), not straight lines.

## CORRECTION to the pint record layout (and the consequence for every distance I quoted)
Records of list 0 read as (X at +0x10, SECOND COORD at +0x14, HEIGHT/OFFSET at +0x18), all floats holding whole numbers:
  948 (75,100,0)   949 (243,78,0)   1084 (66,129,-22)   1085 (108,-14,-154)   1086 (171,114,0)
Every distance figure quoted earlier in this file used +0x18 as the second coordinate, which is WRONG: +0x18 is 0 for most records.
Recomputed against the player at (58,20), the nearest list-0 record is ~20 map units away (id 19 at (59,0), id 841 at (62,0)),
NOT 1.2 units. So no pint record is adjacent to the player anywhere measured, and the "record 1.2 units from the player" claim
(and the Square test built on it) was measuring the wrong field.
Consequence: the pint/point list is a PER-MAP table; the player's own (x,y) lives in the character object, and the two are not in the
same coordinate space until matched to the current map id. That match has not been done.
Interaction attempt at the field start: pressing Square three times beside the tutorial spot changed NOTHING in the point lists
(0 changed records, no count change, checked field-by-field for every record in all five lists).

## MATCH FOUND — and a retraction of the previous "correction"
The previous section claimed +0x18 was the second coordinate. That was WRONG. Verified layout for pint records:
  +0x10 float X, +0x14 float Y (second map coordinate), +0x18 float height/offset (0 for most records).
Evidence: list 0 record id 1173 reads (59.0, 19.4) while the player object reads (58.0, 20.0) — 1.2 units apart, same space.
No other list has any record within 30 units of the player (they are other maps' tables, mostly all-zero).
=> THE MATCH IS: list 0 (0x095D2B20, 160 records) is the CURRENT map's point table. Distances computed with +0x10/+0x14 are valid.
Record id 1173 sits at the player's feet and has KIND 0, which is NOT one of the drawn kinds (1 and 5 are drawn by FUN_089121f4),
so it is an invisible trigger/region marker, not a visible orb.

## Square on the world map = the SKILL action, and it is inert at Lv 1
Button ids recovered from the constants FUN_08919A14 queries with: DAT_089ce5d8 = 0x1000 TRIANGLE, DAT_089ce5e8 = 0x8000 SQUARE,
0x080000 (L-ish), 0x010000 / 0x020000 = camera yaw left/right. That confirms the earlier model: stick left/right rotates the camera,
only forward moves the character.
The SQUARE branch calls the skill UI (imports at 0x08B49E80 / 0x08B49CA4 with controller+0x570) and, when nothing is available,
falls through to FUN_08916380 (state 0xE + a sound). Screenshot test at the field start: pressing Square changes NOTHING on screen
(no menu, no effect), and Cross hides the HUD while she steps forward. So at Lv 1 with 0 KP there is no skill to use, and that is why
no orb has reacted: the world-map orb interaction needs an unlocked skill.

## STATIC CHAIN FOR THE FIELD (better than the earlier entity-table holder)
0x089D83D8 (static global) -> CONTROLLER at FIXED 0x08B83714 -> +0x6d0 = player/character object.
Verified on four RAM snapshots (field start, field after walk, battle-free fields, cutscene):
  sq0 / b0 / b4 all give controller 0x08B83714 and player 0x0976B250 with position rows (58.0,-0.7,20.0);
  in the cutscene snapshot (s0) the controller still exists but +0x6d0 = 0, which is exactly the "no player" state a reader
  should refuse to speak on.
Controller offsets: +0x38 / +0x40 = current CELL coords (8,-1 on this map), +0x570 = skill UI object (0x0975CBB0), +0x6d0 = player.
FUN_08916fa0 is the accessor: `return *(DAT_089d83d8 + 0x6d0)`.
=> A field reader can be built with NO scan: read 0x089D83D8, take +0x6d0, and speak position/cell; refuse when +0x6d0 == 0.

## Skill list: empty at Lv 1, not a usable data table yet
The Square branch asks the skill UI object (controller+0x570) whether a skill is available (import at 0x08B49E80) and falls through to
FUN_08916380 (state 0xE + sound) when it is not. Dumping the skill UI object at 0x0975CBB0 shows three child pointers
(0x099FB5C0, 0x09AD0918, 0x09ACF69C) and a block of static UI/child slots; no skill entries are present because the save has none.
So there is nothing to read as a "skill list" in this save. Orb interaction stays gated behind unlocking a world-map skill.

## Field reader shipped; two retractions from the portal attempt
Adapter now speaks the field from the static chain: 0x089D83D8 -> controller -> +0x6D0 player, position at player+0x30/+0x38.
WhereAmI on the field says "On the field. Position X, Z." and refuses ("Not on the field.") when +0x6D0 == 0 (cutscene/menu).
Quiet per frame: no menu watch applies while a player object exists. 40 host checks cover the chain, refusal and silence.
RETRACTED 1: controller+0x38/+0x40 is NOT a map cell. Two roam runs (field start and field2) moved the player from (58,20) to
(83,13) and (83,5) while BOTH words stayed (8,-1). The adapter no longer calls them cells.
RETRACTED 2: no gate/portal transition was reached. Controller+0x68 stayed 2 (normal walk) for 22 bursts of forward motion on both
maps, and no HUD change or prompt appeared. The earlier "HUD disappeared near a purple portal" was almost certainly the camera pan,
not a gate. The portal is still unreached by locomotion.

## Gate table and gate model table (decomp + RAM)
GATE RECORD: stride 0x14, id s16 at +0, s16 at +4 (branch-B trigger; >0 means "enterable"), s16 at +6 (type, passed to FUN_089107cc),
u32 flags at +8 (bits 2, 4, 8 drive UI), u16 at +0xe (sequential index), s16 CELL X at +0x10, s16 CELL Y at +0x12.
Accessor FUN_089105d8(list, id) walks id at +0 with stride 10 shorts. Gate holder 0x089D8390 -> vector -> list 0x095D1E80 (147 records).
Live cells in that list: (103,0) (101,0) (107,0) (105,0) (105,1) (109,0/1) (111,0) (112,0) (115,0/1) (117,0) (118,0) (122,0/1)
 (124,0) (125,0) (10,1) (37,0) (49..59,1), plus many (-1,-1) = unused. There is NO (8,-1) record, so controller+0x38 is not a gate cell.
GATE MODELS on the current map: RAM holds 70 name strings "DES_N_gate<map><nn>_<part>", i.e. gate0100_01 ... gate0106_04, so this map is
 map 01 and it has gates 00..06 in up to four parts. They sit in a model-name table: record 0x28 bytes = name[32] at +0, u32 model id at +0x20
 (e.g. gate0100_01 = 16, gate0100_04 = 29, gate0101_01 = 39). No RAM word points AT those names (the table is walked by offset), so the
 name table alone does not give positions.
STILL OPEN: the link from a gate record (cell) to a world position, and therefore the walk to the portal. Walking the field never changed the
 controller state (stayed 2) across 22 bursts on two maps, so gate entry is not simple proximity to the visible pillar.

## Portal hunt, round 3 (both approaches failed, recorded so they are not retried blindly)
GATE MANAGER: controller+0x88 -> object 0x095D75C0 with u32 count 8 at +0, then 8 entries of 0x14 bytes
{every 0x04: ptr A (0x095D20E0..), ptr B (0x095D2A28.. stride 0x14), 0, small int, float}. Ptr B walks list 0x095D2A20 (count 11),
whose records read as packed s16 pairs, NOT the gate-record layout (id/enterable/type/flags/cell), so this structure is gateway
PROGRESS, not the gate table. Not usable for positions.
PILLAR DETECTOR: new driver command `pillar` finds the longest vertical run of near-white pixels per column. On the field the longest
run is 4-7 px, so the visible portal is NOT a tall white column and shape detection cannot lock onto it. Vision steering is dead for
this target.
Runtime import probe: the address Ghidra renders as func_0x08b43a3c holds 0x68031b07 at runtime, which is not a valid stub, so the
gate-collision call is not resolvable that way either.
CONCLUSION: the one place the game decides "the player is on a gate" is that collision import; to reach the portal we need either a
save state taken while standing on it, or the overlay that implements the import.

## FIELD EVENT SYSTEM found by decomp (the missing layer between the map and the player)
FUN_0890f668 (big world-map function) walks an EVENT linked list with the iterator FUN_0890f544/FUN_089103bc and collects, into a
32-slot array, every event whose test passes. EVENT RECORD: id u16 +0, id2 u16 +2, KIND byte +4, +5/+6/+7 sub-ids (filtered against
controller+0x54), u16 +8, u16 +0xa, u16 +0xc (bit-index into a seen-table at obj+0x130), FLAGS u16 +0xe (bit 0 = active), then per-kind
fields. Asset/name strings follow some records ("upper_w_2d/T_UP0...", i.e. 2D UI prompts).
KIND SWITCH (so this list IS the map's interaction set):
  1 = party character by id; 2/3 = party proximity (radius s16 at +0x12); 4/5 = object via func_0x08b4571c(id) then radius test;
  6 = object, radius from a call; 7/8 = a PINT record by id (FUN_089121d0(s16 at +0x10)) compared against controller+0x768;
  9 = enemy by id against the 4 party slots; 10 = RAW COORDINATE test: player (x,z) minus (s16 at +0x10, s16 at +0x12), compared
  to radius s16 at +0x14.
KIND-10 EVENTS LIVE ON THE PROLOGUE MAP (integer coords, all with flags bit0 set):
  (100,25) r10   (98,-6) r10   (86,4) r10   (88,14) r15   (108,-14) r10
The last one found carries a 2D "upper_w_2d/T_UP0" asset, so kind 10 is a PROMPT/MESSAGE trigger, not a gateway.
Measured: the party walked through the radii of (86,4), (88,14) and (98,-6) during the island sweep and the controller state stayed 2;
Cross inside them did nothing. So these are not the portal either.
STATUS: the portal is still unreached. Three layers are now mapped (pint objects, gate records, field events) and none of them fires
when the party walks over the visible pillar. The remaining unknown is the game's own gate-collision import, which is the only code
that can say "the player is standing on a gate".

## FIXED: the field can now be navigated (shoulder buttons rotate the camera)
L and R rotate the CAMERA without moving the character, in about 90 degrees per 30 frames (1 frame ~ 3 degrees). Measured: press L for 30
frames and the forward vector goes from (0.98,-0.17) to (-0.05,-1.00) with 0.00 units of movement; R rotates it back to (0.95,-0.32).
That is the missing control: the stick's up/down move along the camera's forward axis, left/right strafe, and diagonals do nothing, so a
heading can only be held by aiming the camera with L/R. scripts/dissidia012-nav3.py does exactly that: probe forward (6 frames), compute
the signed error to the bearing, press L or R for |error|/3 frames, else walk 22 frames.
MEASURED: reached (88.2,9.2) from (58.0,19.0), a 33-unit run, in ~22 iterations. Reached (99.1,5.4) heading for (103,0).

## The portal is across water
The island's walkable extent measured: x 58..100, z ~5..21. Gate cells from the gate list include (101,0) (103,0) (105,0) (105,1) (107,0)
(109,0/1) (111,0) (112,0) (115,0/1) (117,0) (118,0) (122,0/1) (124,0) (125,0) -- all east of x=100, i.e. across the water, which matches the
glowing pillar seen on a distant peninsula. Pushing east stops at x ~100/101. Also tried (58,1) and (59,1): the walk stalls at z ~9, so those
cells are not walkable either.
Cross is a dodge, not an interact: pressing it moved the party from (99.1,5.4) to (92.4,1.1) then (86.0,-4.2) with no state change.
CONCLUSION: crossing to the pillar needs the world-map TRAVEL mechanic (or the gate-collision import), not locomotion.

## REACHED AN ORB (first time) — and it does not react to Cross or Square
Navigator with L/R aiming (scripts/dissidia012-nav3.py) now walks the map properly. Route that works east from the start:
 waypoint (95,16) then (105,17): the party crosses the water barrier that stopped straight-line walks at x~100 (earlier attempts stalled
 because they aimed at (103,0), which is in the water; the corridor is at z~16-17).
Standing at (107.7,23.5) the party is beside a hovering white orb (screenshot s_orb0.png): the orb is directly in front, no prompt.
 The orb is pint record id 9, kind 1, at (107,24), record 0x095D3100. Nearby drawn records: id 10 (97,30) d=15.6, id 8 (97,-4) d=22.8.
MEASURED: 4x Cross at the orb -> record unchanged, list counts unchanged (party just dashes away to (114.6,28.2) etc.).
 MEASURED: 4x Square at the orb (party stays put) -> record unchanged, counts unchanged, and a whole-RAM diff shows only animation
 counters moving (0x0918FCxx decreasing byte counts). So neither button interacts with the orb.
CONCLUSION: the orb is real, located, and reachable, but interaction needs the world-map SKILL that the tutorial refers to; this save has
 none (Square opens an empty skill UI, Lv 1, 0 KP). To interact, the save needs a skill first, or the orb's interaction path must be found
 in the overlay code rather than by button probing.

## PORTAL REACHED (point-blank) — and it is LOCKED
With L/R aiming plus Cross dashes for the blocked stretches, the party walked from the prologue start to the stone archway:
 route (95,16) -> (105,17) -> (118,27) -> (128,36), then the dash-assisted walk to the arch. Final position (146.0,46.2).
Screenshot s_portal_go.png: Lightning stands INSIDE the arch's front walls, facing the closed gate; the arch is barred by a dark iron
 portcullis with a solid panel in the curve, plus a thin translucent blue arc beside her (the gateway's aura). No prompt appears.
Records at that spot: kind 13 id 71 at (152,54) ~9.9 units beyond the gate, kind 12 id 70 at (128,37) behind, kind 3 id 17 at (112,49),
 kind 1 id 9 (the orb) at (107,24).
MEASURED: at the arch, Cross / Circle / Square all leave the position unchanged (146.0,46.2) and the controller state at 2. The gate does
 not open on a button press. So the gateway is LOCKED and something else unlocks it.
NEXT HYPOTHESIS (untested): the purple-and-gold manikin standing on the map near the second orb is a world enemy; defeating it is the
 obvious candidate for opening the first gateway.

## CHESTS WORK BY CONTACT (state 16) — first interactive object reached
Walking into the object at (98,46) (pint record kind 2, id 12) raised the controller state from 2 to 16 and produced the banner
"Obtained 50 gil from chest!" (screenshot s_enc2.png). So:
  - KIND 2 in the point list = treasure chest, and contact is the interaction (no button needed).
  - CONTROLLER STATE 16 = the item-notification state. Cross dismisses it back to 2.
  - State captures saved: ~/d12s/encounter.state (standing at the chest, notification up) and ~/d12s/bat.ppm/bat2.ppm.
Screenshot also shows, beyond the chest: a purple-armoured manikin on the path and the stone tower with a glowing pink-and-white portal
behind it -- i.e. the chest sits directly on the approach to the gateway.
BLOCKED: from the chest the party cannot advance north past (98.4,45.8) (repeated walks move <1 unit and oscillate). The barrier is on
the narrow path between the chest and the manikin; dashes and all four stick directions were tried.

## Gate reachable, not enterable (final state of this line of work)
Reached the archway again and pushed into the portcullis for 12 long walks: the party simply circles the arch —
 (146.4,43.8) (147.4,39.8) (145.4,36.2) (141.4,34.9) (137.7,36.7) (136.1,40.5) (137.6,43.4) (141.3,43.3) (145.2,42.1) (147.3,38.5)
 (146.4,34.5) (143.0,32.2) — with the controller state at 2 the whole time. Contact, Cross, Circle, Square and Select were all tried at
 point-blank range; none changes the state or opens the gate.
The northern route to the manikin at (112,49) is impassable from the corridor too: staged at (118.9,27.6), twelve aimed walks never
 got past z~29, and from the chest side the party is boxed into (96..98, 41..50).
CONCLUSION for this save: the prologue's world map has one closed gateway. The point list, the event list, the gate list and the
controller state are all mapped, and the chest proves contact-interaction works, so the reader side is ready. Entering a gateway needs
the story flag that unlocks it, which this save does not have.

## WHY THE GATE WILL NOT OPEN (decomp answer)
The gate handler FUN_08913e10 only runs its two entry branches when the game's own collision call returns a gate index. That index is
stored in controller+0x7B0. Branch A fires when (gate_record.flags & 4) != 0 AND s16 at +0x10 >= 0 -> controller state 6 (enter);
branch B when s16 at +4 >= 0 -> state 5.
MEASURED LIVE: controller+0x7B0 = 0xFFFFFFFF (no gate) at the stone archway at (146,46) AND while standing exactly on the gate cell
coords, walking to (104.5,0.2) which is gate (105,0)/(103,0). So the game never considers the party to be at a gate, and neither entry
branch can run. That is the reason the portcullis never lifts: it is not a button or flag we failed to press, the party is not inside a
gate volume at all.
=> The archway at (146,46) is scenery, not one of the gate records: its gate id would otherwise have been non -1.
=> Gate RECORDS that would open (flags bit 2 set, 0x5) are ids 933..1019 with cells (101,0) (103,0) (105,0) (105,1) (107,0) (109,0/1)
   (111,0) (112,0) (115,0/1) (117,0) (118,0) (122,0/1) (124,0) (125,0), plus (10,1) (37,0) (49,1) (50,1) (51,1) (56,1) (57,1) (58,1)
   (59,1). Records with flags 0x1 or 0x3 lack the bit and would take branch B instead.
The gate VOLUME is 3D collision implemented behind an import (the same call the handler makes), so the volume belongs to the overlay /
collision layer, not to the record data. Standing on the cell coordinates does not put the party in it.
NEXT: find the collision volume for the current map (the import's implementation in the resident/battle overlay), i.e. the same class
of work as the earlier import-resolution attempts, or load a save where a gate is already open.

## Gate search, last round: no gate volume found anywhere walkable
Route through the east side then west (the walkable corridor plus dashes):
 (94,17) (102.7,17.4) (119.1,27.3) (129.2,32.7) (140.5,41.7) (133.3,43.7) (117.4,47.6) (113.2,49.4).
At EVERY stop controller+0x7B0 (the gate index the whole enter path depends on) read 0xFFFFFFFF. So the party never entered a gate volume
anywhere on this map, including at the manikin's own coordinates (112,49) and inside the stone archway at (146,46).
Map screen check: opened Select at two different positions; the map shows terrain plus the blue player arrow only, with NO gate or
objective icons (the two circular features are terrain), so the map cannot be used to locate a gateway.
Gate-entity search by model id was inconclusive: the model-name table yields 35 gate model strings and 36 ids, but an id-based scan
matched 1,330 structs (far too many to distinguish real gate entities), so that route found nothing trustworthy.
CONCLUSION: within this save there is no reachable gate volume. The unlock path needs either a later save state or the collision import's
implementation. Everything else in the story-mode chain (objects, events, gates, chests, states, navigation) is mapped and documented.

## Controls as Devin described them, and what they measured
Devin's mapping: Cross = jump/dash, Square or Circle = punch/attack, and orbs are absorbed by punching them.
MEASURED headless:
- CROSS dashes/lunges: from (99.1,5.4) it moved her to (92.4,1.1) then (86.0,-4.2), about 6-7 units per press.
- CIRCLE also dashes/lunges: (111.3,26.0) -> (104.0,23.0), then (104.5,22.4) -> (108.9,25.1). So both face buttons move her; neither
  produced an attack on its own.
- SQUARE opens the skill UI and is inert at Lv 1 with 0 KP (already established).
Dash-through test: a dash aimed at the point record at (107,24) (pint id 9, kind 1) passed straight through its coordinates
((111.3,26.0) -> (104.0,23.0)) and changed NOTHING in any point record and no list count. So that record is not the visible orb.
What the screenshots show at (105.4,17.2): one glowing white orb about 10-15 paces to the left, and a second orb far up the valley pass
beside a dark gateway with a red manikin standing next to it. The visible orbs are therefore NOT adjacent to the records tested so far.
Not yet done: land a dash on a VISIBLE orb (needs visual aiming between dashes), and find the KP/Gil HUD block in memory so absorption
can be confirmed by a counter change instead of by eye (a search for Gil 50 with HP 1000 nearby in the chest dump found nothing, so the
status block is still unlocated).

## Chord attempts and the current blocker
Added a `chord` command to the driver (two buttons held together) and tried L+Circle, L+Square, R+Circle and L+Cross on the field.
Result: none changed any point record and all list counts stayed [160, 11, 147, 120, 74].
Caveat, and it matters: those chords were pressed at the field start (58,20), where no orb is within reach, so this is not a fair
test of the attack button. It only proves the chords do not disturb the point data on their own.
BLOCKER (needs Devin): the visible orbs cannot be located in memory. Every nearby point record was dashed through or stood on with no
change, and the dash-through at (107,24) went straight through that record's own coordinates. The status block holding Gil/KP is also
unfound (Gil 50 with HP 1000 nearby does not exist anywhere in the chest dump), so absorption cannot be detected by a counter yet.
WHAT WOULD UNBLOCK IT: a PPSSPP save state taken with the party standing against a glowing orb. From that state the orb's record can be
identified by diffing around one absorption, and the same state settles the gate question. Alternatively, the exact world-map attack
input (button or chord) removes the guessing.

## ORBS ABSORBED — confirmed, and the guides explain the world map
SOURCE (the game's own script, GameFAQs "Game Script" by Squall_of_SeeD, and finalfantasy.fandom Light Orb):
- The in-game tutorial text is "You can attack these orbs with Circle. It seems that we gain powers from them." So the orb button is
  CIRCLE, not Square. That is why every earlier Square press did nothing.
- Breaking an orb splits it into fragments that converge on the character and are absorbed; an aura appears. Four orbs give a Chain
  Skill, and the aura colour decides which: blue Round, green Jump, yellow Straight, red Cross, white Multichain.
- Prologue: the area has one chest (50 gil) and one gateway, with the Light Orbs decorating the path.
- GATEWAYS: a gateway "may be entered from the world map by pressing when standing next to it"; red fog gateways are mandatory, blue are
  optional. Inside, the Stigma of Chaos unlocks the exit.
- BOULDERS: new on this world map, black with red cracks; they take up to three hits depending on the character's world-map attack combo
  (Lightning's combos once, so two hits). Breaking one opens a quickmove line of light across a gap, usually to a chest or manikin.
MEASURED HERE, and it works: walking the prologue path from the field start while mashing CIRCLE produced the absorption — the screenshot
shows a bright pink-white aura around Lightning with energy fragments streaming into her, and Yuna standing beside the orb exactly as the
guide describes ("Lightning comes across Yuna standing beside a bright sphere of light").
⛔ The light orbs are NOT in the point list: diffing every record before and after the absorption run changed ZERO records and no list
count (still 160/11/147/120/74). They are runtime entities, which is why every record-based search for them failed.
Boulder test at the blocked spot north of the chest: eight Circle presses moved her only 2.5 units (45.1 -> 47.6) and walking pushed her
back; no boulder broke there. The barrier is either a different obstacle or needs the attack while facing it from the right angle.

## Gate hunt after the guides: the prologue path ends at water, the gateway is across it
Walked the prologue path from the field start with Circle mashing (orbs absorbed on the way) to the end: the promontory at
(112,-33). A 9-frame contact sheet of that walk shows a dead end there: cliff drop to the sea ahead, a raised grassy ledge to the
right, and, in two frames, a bright glowing white portal on a distant sandy peninsula ACROSS THE WATER with a figure standing next to
it. That portal is the prologue's gateway, and it is not reachable on foot from this promontory.
No black red-cracked boulder is visible at the dead end (asked directly of the frame), so the boulder-launched crossing the guide
describes is not this spot. The guide places boulders in the chapter that follows Laguna, not the prologue.
Gate-id sweep: 70 aimed walks with random turns while polling controller+0x7B0 and +0x68 stayed inside the starting basin
(58..80, 8..19) and never saw a gate id or a state change, so nothing on that side of the map is a gate volume either.

## OFFICIAL MANUAL: how a gateway is actually entered
Square Enix's Dissidia 012 manual (support.na.square-enix.com, upp_manual PDF, p.12 "Enter a Gateway"):
- "Stand in front of an entry to see the gateway's name and conditions." So standing in the gateway's trigger zone raises a PANEL with the
  gateway name and its conditions (KP Chance, Original Rules, enabled/disabled skills).
- "Press the [button] button after reviewing the conditions to enter the gateway."
- Two kinds: red entries MUST be entered to progress the story, blue ones do not affect story progression (and can be re-entered).
- Inside: move the piece to the Stigma of Chaos to clear it; move next to a chest to take it; move adjacent to a battle piece to fight.
  START then "Exit Gateway" leaves at any time.
=> THE DETECTOR IS THE CONDITIONS PANEL. I have never seen it on screen in any of the walks, which means the party has never stood inside
   a gateway trigger zone, so no button press could ever have worked at the archway.
WHY I AM BLOCKED: the map is fully connected (the Select map shows one continuous landmass with narrow paths; no water crossing is
needed), but my steering cannot reliably walk to a specific visible object, so I cannot park the party in a trigger zone. The gate id
(controller+0x7B0) and the controller state stayed at -1 / 2 through every sweep, confirming no gateway zone was ever entered.

## Map-based navigation attempt: marker detection failed
Added a `blue` command (centroid of strongly blue framebuffer pixels) to locate the Select-map player marker for calibration.
Measured: at world (58.0,20.0) the blue centroid was (249,135) n=63; after walking to (69.3,17.6) it was (251,134) n=53. Eleven world
units moved the detected cluster by TWO pixels, which cannot be the player marker on a map spanning ~100 units in 480 px. The simple
blue filter is locking onto a static UI element (a border flourish), so the calibration yields no usable pixel-to-world transform.
That closes the map-navigation route as attempted here: it needs a better marker signature (the arrow is a faceted cyan diamond drawn
inside the map, and the map palette itself carries blue), not a coarse colour threshold.
STATUS: the gateway is still not entered. Everything else about gateways is now documented (trigger zone -> conditions panel -> press to
enter; red mandatory, blue optional), so the moment the party stands in a zone the panel will be the confirmation. Blocked on precisely
placing the party in that zone.

## 100% SAVE FILE INSTALLED AND WORKING (this is the unlock for gateway testing)
Source: bucanero/apollo-saves on GitHub, path PSP/ULUS10566/00000145.zip (the zfreeman 100% save: story, labyrinth, catalog, museum,
summons, all characters Lv100/SSS, all equipment). No login needed; downloaded with `gh api ... --jq .content | base64 -d`.
INSTALL PATH THAT WORKS: the driver's save argument is a PPSSPP root, and the game looks in <root>/ppsspp-memstick/PSP/SAVEDATA/.
Putting the folder at <root>/PSP/SAVEDATA made the load screen say "There is no data"; at the ppsspp-memstick level it loads.
VERIFIED: title -> LOAD GAME (cursor value at 0x096AE20C, 1 = Load Game, and the title list WRAPS so blind presses misland) ->
save slot "DISSIDIA 012 FINAL FANTASY, PLAYER Cerulean, 304:51:41, 276 KB" -> "Load completed" -> MAIN MENU with PLAY TIME 304:51:52,
Story Mode highlighted, all seven rows present -> STORY MODE -> scenario banner "Main Scenario 012: Treachery of the Gods" -> chapter
carousel with CHAPTER 0 "Prologue - Path to Sanctuary", Lightning Lv 100 and a bonus line of Lv 2 -> "Play this story?" YES.
Saved states for continuing: ~/d12s/save100.state (main menu), save100b.state (chapter confirm), map100.state (prologue opening).
STATUS: the prologue then plays a long chain of in-engine cutscenes (narration, then Tifa/Vaan, then Laguna/Lightning/Kain, then
Vaan/Laguna/Yuna/Tifa...). Cross advances them (the scene changes), but 150 presses at 45-frame runs did not reach the world map, and
Start does not skip. The world map, and therefore the gateway, is past that chain.

## 100% SAVE REACHES THE PROLOGUE WORLD MAP (and the same wall)
Skipping the prologue cutscene chain: CROSS x243 at 25-frame runs reached the field. Saved as ~/d12s/field100.state.
The save is live and rich: Lv 100, HP 10644, Gil 1,569,231, KP 9938, and the Skill list now has five entries
(Assist Charge, EX Charge, BRV Charge, BRV Zero, Soundwave) — so the Square skill menu is no longer empty on this save.
SCENE: the party stands under a large stone archway whose cut-through reveals a distant stone gateway with a glowing purple-and-white
portal, with two manikins wandering between. Walking forward-left from (114.6,-24.2) reached (113.0,-32.9) and stalled at the same
coastal edge as the old save; controller+0x7B0 stayed -1 and the state stayed 2 the whole way, and no conditions panel appeared.
So the richer save does not change the geometry: the prologue's gateway sits across water from the reachable strip, and blind navigation
still cannot park the party in its trigger zone.

## CONTROLS CONFIRMED (Meristation guide, Spanish): X enters portals, Circle attacks
The guide's control list for the world map: Select opens the map, the stick moves, **Circle attacks orbs, enemies and rocks**, and
**X (Cross) enters portals and opens chests**. That matches the manual's "press the [s] button to enter" once the button glyph is restored,
and it explains the chest opening on contact earlier.
MEASURED with the 100% save at (114.6,-24.2): pressing Cross raised controller state 2 -> 3, and state 3 turned out to be Yuna's ORB
DIALOGUE ("This light... I feel a gentle strength from it"), i.e. Cross is also the dialogue advance, and it held at 3 for 900 frames
until Cross was pressed again.
SEARCH: a 20-stop camera-and-walk sweep produced a contact sheet with NO gateway conditions panel in any frame. Lightning stands under an
EMPTY stone archway on a cliff plateau; every glowing gateway portal is across the water to the left, and there is no light bridge or
walkable connection to them from this plateau.
An 80-step random walk with Cross pressed at every stop covered (59,14) to (130,-30) and never produced a state other than 2 (walking)
or 3 (dialogue), so no gateway trigger zone was ever entered.
CONCLUSION: on the prologue map the reachable plateau holds no active portal. Either the prologue's gateway is across the gap (and this
save has already cleared/broken whatever crosses it), or the entry needs the conditions panel that only appears in the trigger zone —
which this geometry does not let me reach.

## GATEWAY ENTERED (goal reached)
Reproduction that works, all headless with the 100% save (fp-save-100):
 title -> LOAD GAME (cursor 0x096AE20C == 1) -> save slot -> main menu -> Story Mode (Cross) -> chapter carousel
 -> press RIGHT to Chapter 1 "A New Threat" -> Cross -> Cross -> then Cross x250 at 25-frame runs.
That lands INSIDE the gateway board: ~/d12s/board.state, screenshot s_board.png.
WHAT THE BOARD IS: floating stone pillars over a void joined by glowing grid lines, Lightning on the leftmost pillar, three blue
manikin pieces on pillars to the right, a gold chest on a lower-right pillar, HUD showing gateway KP -98, total KP 9938, Gil 1569231,
Lv 100 / HP 10644, and the five world-map skills listed top-right. This is the Crescent Lake Gateway from Chapter 1.
STATE: on the board the controller state reads 21 and controller+0x6D0 is 0 (the board piece is NOT the field player object), which is
why "state 2 + player pointer" is the wrong liveness test inside a gateway.
BOARD CONTROLS, measured: the ANALOG STICK moves the CAMERA only (no piece movement); the D-PAD moves the selection cursor between
adjacent pillars; moving the cursor onto an enemy piece raises "Win battle", the enemy name/level ("Fleeting Flash Lv 7"), the battle
stage ("Old Chaos Shrine") and "Engage [X] / Cancel [O]"; CROSS engages.

## Gateway board controls, measured (goal: enter a portal — done)
- STAR opens the PAUSE menu: Return to Game / Quicksave / Exit Gateway / Help Manual. Exit Gateway warns "*KP gained in gateway will be
  lost" and then shows "Exit gateway? YES / NO" with NO preselected; choosing YES drops to the chapter's story cutscenes.
- SELECT toggles SCAN MODE (HUD changes to "SCAN MODE" top-centre and "Normal Mode [SELECT]" bottom-right). Scan Mode moves a glowing
  blue-white square CURSOR freely over the board and shows tile contents, including the red-orange highlighted Stigma tile at the far
  side. ⛔ Scan Mode is INSPECTION ONLY: pressing Cross with the cursor over another tile does not move the piece.
- TRIANGLE opens Customization from the board: Abilities / Equipment / Accessories / Summons / Assist / Adjust Level (greyed) / Shop,
  with HP 10644/10644, CP 450/450, BRV 1036, ATK 177, LUK 60, DEF 184, EXP total 1964655, plus the LUCKY! bonus panel.
- The analog stick on the board moves only the camera.
- Normal-mode D-pad moves a selection cursor onto an ADJACENT tile; an enemy tile raises "Win battle", the enemy and stage names, and
  "Engage [X] / Cancel [O]". Empty-tile dashing was not demonstrated: repeated (D-pad right, Cross) left the piece on its starting
  pillar, so the exact piece-move input is still open.
STATE NOTE: on the board controller+0x68 reads 21 and controller+0x6D0 is 0, so field-style liveness tests fail inside a gateway.

## ROUTE CONFIRMED FROM A PLAYTHROUGH VIDEO (and how far it got)
Video: "Final Fantasy Dissidia Duodecim 012 Prologue Path to Sanctuary" (YouTube hH74q5elghI), frames pulled with yt-dlp + ffmpeg and
read as contact sheets. The video shows the exact world-map route:
 1. Cutscene at the cliff (Lightning, Kain, Laguna) facing the dark rift.
 2. Control resumes UNDER the large stone archway. Dialogue: "Everyone made it out alright... I can see Sanctuary in the distance."
 3. Walk FORWARD down the path away from the camera.
 4. A glowing white star-shaped ORB sits on the path with a figure beside it. Dialogue ("This light... I feel a gentle strength from
    it...") then the orb is struck: white flash, party stands, lights ascend, narration "The three powers became a frail radiance."
 5. Then the player runs toward a glowing purple-and-blue swirling GATEWAY PORTAL on the LEFT side of the terrain and steps in; the
    screen flashes blue-white. That is the gateway entry.
REPRODUCED with the 100% save, headless: seven Cross presses cleared the arch dialogue, two forward bursts reached the orb, three Circle
presses struck it (flash, aura, "A frail power of the dead dwells.", and a Cross prompt appeared over Yuna), sixty Cross presses played
out the Yuna/Lightning conversation, and free movement returned with the full HUD at (130.7,-36.9). State saved as ~/d12s/free.state.
STILL NOT ENTERED: from free.state, a 16-step walk LEFT and a 30-step rotating sweep with Cross pressed at every step moved the party
across (98..130, -37..-17) with controller+0x7B0 at -1 and state at 2 throughout. No gateway zone was touched, and no conditions panel
ever appeared. The portal is on this plateau somewhere; the search simply has not crossed it.

## Where the walk-into-a-portal attempt actually stands (blocked, not done)
From the post-dialogue state (~/d12s/free.state):
- The 360 turn shows a glowing purple GATEWAY across the bay to the north-east, a tall white spire (the map's Teleport Stone) beside it,
  and a ruined stone archway inland on this side.
- Movement works: 25-frame stick pushes move 8-9 units (fwd (130.7,-36.9) -> (129.0,-28.6), back 7.3, right 9.4, left 8.9).
- BUT the party is boxed into a pocket: 24 northward pushes with Cross cycling only between (130.7,-36.9) and (131.0,-33.3). Ten Circle
  hits on the large grey boulder beside the ledge did nothing (it is not a breakable red-cracked boulder).
- The SELECT map confirms it: the blue player arrow sits at the tip of a peninsula pointing north-east, walkable land continues only
  SOUTH-WEST behind the arrow, and there is an EMPTY GAP straight ahead between this spur and the upper-right clearing. The gateway is
  across that gap, reached by the long way round: south-west into the central network, east/south-east around the lower loop, then north
  up the eastern spine and back to the upper-right clearing.
So entering the portal requires following that long path, not a straight line, and blind forward-walking cannot do it.

## The pocket is real: the party cannot leave it by stick input
From free.state (130.7,-36.9): ten backward pushes moved her 1.1 units TOTAL ((130.7,-36.9) -> (130.0,-35.8)) and then stopped; eight left
pushes cycled between (129.5,-31.8) and (130.9,-36.2) and came back to the start. Every direction returns to the same two or three points,
so the character is clamped to a small ledge. Earlier "8.5 units in one push" readings were taken while the camera was at a different
angle and are the exception, not the rule.
Verdict for this save state: no stick input leaves the ledge, so the gateway across the bay cannot be walked to from here. The walk-in
needs a different starting point or a scripted path, not more input attempts.

## Map-as-compass and map-scale calibration: tried, both dead ends
- The Select map is FIXED north-up and does NOT rotate with the camera: three shots with 90-degree camera turns between them were pixel
  identical, and the blue player arrow stayed pointing west-south-west the whole time. So the map shows the character's WORLD facing, not
  the camera's, and cannot be used to aim the stick.
- Calibrating map pixels to world units needs two map shots at two known positions. From own_free.state the 8 walks moved only 3.8 units
  (-3.04, +2.32) because that spot is clamped, so no usable scale came out of it. A calibration run has to start from a state where
  movement actually works (e.g. the old save's field, or right after the orb event before wandering).
- The map DOES show a small white arched marker on the upper-right plateau edge, which is worth treating as a candidate gateway marker once
  a scale exists.
STATUS: the walk-in is still not done. Every attempt founders on the same thing: the character's heading drifts with the camera and
several key spots clamp movement to a few units, so a long walk curves and reverses instead of crossing the map.

## Map scale measured (partial calibration)
Two map screenshots at known world positions, arrow centres read from the images:
  N1 at world (155.00, 32.85): arrow centre (284, 141)
  N2 at world (149.31, 40.79): arrow centre (281, 144)
World delta (-5.69, +7.94); pixel delta (-3, +3). So per-axis scale is 0.527 px per world X and 0.378 px per world Z, i.e. roughly
0.45 px per world unit, and the map is north-up with +X east and +Z south on screen.
CONSEQUENCE: one map pixel is about 2.2 world units and the arrow icon itself is ~12 px wide, so a map reading locates the party to only
about plus or minus 5 world units. That is too coarse to place a character inside a gateway trigger zone, and the map carries no
readable gateway marker of its own.
The one use left for the map is as a FACING compass (the arrow shows the character's world facing, and the camera does not affect it):
open map, read the arrow's bearing, close, rotate with L or R, reopen and check. That is a slow closed loop but it is the only heading
reference the game exposes.

## Last attempt on our own save: reached the archway approach, gate still inert
Aim-navigator run from own_free.state: (155.0,32.8) -> (136.3,39.5) -> ARRIVED (123.5,45.7), the far end of the pass where the stone
archway stands. Five Cross presses there did NOT enter anything; each one dashed the party away ((118.7,47.4) (113.7,53.3) (108.6,55.9)
(103.2,58.7) (98.2,63.2)) with controller+0x68 at 2 and +0x7B0 at -1 throughout.
So Cross is a dash unless the party is standing inside a gateway trigger volume, and this spot is not one.
BEST REMAINING EXPLANATION: the archway seen in every approach has been a BARRED gatehouse (dark portcullis with a solid panel), and the
world-map enemies (purple/red manikin figures) stand right beside the portal in several of these shots. The gateway is most likely locked
until those manikins are fought, which is the one interaction this save has never performed. Walking into a manikin is the next thing to
try: it should start a battle, and clearing it may unlock the portal.

## World-map manikins cannot be engaged by walking into them or hitting them
Nearest records around the party at own_free.state (155.0,32.8): kind 13 id 71 (152,54) d=21.4; kind 12 id 70 (128,37) d=27.3;
kind 15 id 823 (154,70) d=37.2; kind 3 id 17 (112,49) d=45.9; kind 1 id 9 (107,24) d=48.8; kind 2 id 12 (98,46) d=58.5 (the chest).
Walked to the kind-3 record at (112,49) and stood on it: (112.5,49.7), state stayed 2, no battle. Four Circle attacks there only dashed the
party ((110.5,54.5) then (107.8,56.0)), state 2, gate -1. Cross behaves the same way.
So the manikin-looking objects do not start battles by contact or by attack, and the gateway is still not entered.
FINAL STATE OF THIS LINE: portals are not entered by any input I can produce at any position I can reach on this map. Everything about
the map, the party, the point records and the gateway records is documented above; the missing piece is the game's own trigger volume,
which never registers (controller+0x7B0 stays -1 everywhere the party has stood).

## ROOT CAUSE FOUND (after consulting Claude Opus on the evidence)
Asked Claude Code (opus, high effort) for hypotheses; it proposed checking whether 0x7B0 is the right signal at all, checking the
harness input, and teleporting instead of walking. All three paid off.

1. WRITERS OF 0x7B0 (static scan for stores with imm 0x7B0): the real struct stores are `sw $s1, 0x7B0($s0)` at 0x08878B30, 0x08915080
   and 0x089180E0 (the rest are stack frames). FUN_08915048 is the world-map one:
     iVar2 = FUN_0891705C(-1);              // current "point"/area object
     sVar1 = *(short*)(iVar2 + 0x240);      // the gate id for that area
     *(u32*)(param_1 + 0x7B0) = 0xFFFFFFFF; // reset every call
     if (sVar1 >= 0) { ... if ((uVar3 & DAT_089ce354) || (*(u32*)(iVar2+0x260) & DAT_089ce350)) {
         *(int*)(param_1 + 0x7B0) = sVar1; ... } }
   DAT_089ce354 = 0x00000000, DAT_089ce350 = 0x00000008, DAT_089ce35C = 0x0, DAT_089ce358 = 0x00000010. So the only way the gate id is
   stored is: the current area's +0x240 is a valid gate id AND that area's +0x260 has bit 3 (0x8) set.
2. FUN_0891705C:
     if (param_1 < 0) param_1 = *(int*)(DAT_089d83d8 + 0x58);
     return DAT_089d712c + param_1 * 0x280 + 0x3E8C8;
   So the current area index lives at controller+0x58, and the area table base is DAT_089d712c.
3. LIVE READ at own_free.state: controller+0x58 = 1, area object = 0x08BED880, its +0x240 = -1, +0x260 = 0x00000001 (bit 3 clear).
   => The party is in an area with NO gateway. That is why 0x7B0 is -1 and why no button does anything at the archway: the archway is
   scenery in a non-gateway area, and the enter path never even gets a candidate.
4. TELEPORTING (new capability, after Claude's suggestion): writing the position to all FIVE copies in the player object (offsets 0x30,
   0x80, 0x100, 0x2E0, 0x360) makes it stick; writing only +0x30 is undone within 1 frame.
5. GRID SWEEP with teleporting (x 90..170 step 10, z -20..80 step 10): controller+0x58 stayed 1 at EVERY position and the area's gate
   id stayed -1. So no walkable spot on this world-map instance makes a gateway area current.
CONCLUSION: this map has no gateway registered at all. The gateway seen in playthroughs exists only after the preceding story beats are
completed. That is consistent with every failed attempt: the trigger was never missing an input, it was absent.

## Following the beat: the prologue map has ONE area and it has no gateway
Clean replay from prologue_end.state, walking into triggers instead of mashing Circle: the party moves 59.9 -> 72.4 on the path and clamps
at (72,17.5) with controller+0x58 = 1 the whole way. Reading that area object's +0x240 gives -1 and +0x260 bit 3 clear, i.e. no gateway.
Earlier runs reached the archway approach at (122.6,46.6) by other routes and the SAME area index 1 with gate id -1 was reported there.
A 45-step walk over (139..155, 21..40) never changed the area index either.
So on this prologue map instance there is exactly one area, and its gateway id is -1 everywhere reachable, including at the archway.
That is the mechanical reason nothing ever entered: the gateway does not exist in this map state, so there is no trigger to stand in and
nothing for the 0x7B0 writer to store.
CONTRAST, and it matters: a gateway IS enterable in this same harness. With the 100% save, choosing Chapter 1 loads a gateway board
(state 21) with pillars, pieces, Scan Mode and Customization, which only exists after a successful entry. So the entry path and the
controls are fine; it is the prologue's map state that lacks a gateway.

## Area check across every saved state: the same negative
Reading controller+0x58, DAT_089d712c and the area record's +0x240 from five separate RAM dumps (the 100% save post-orb, our own save
post-orb, the chapter-1 map after exiting a gateway, and the gateway board) gives IDENTICAL results every time:
  base 0x08BAED38, index 1, object 0x08BED880, gate id -1, flags260 0x00000001 (bit 3 clear).
Even the board dump reports index 1 with -1, which means this "current point" structure is not per-map: the pointer and index are fixed and
the world map never advances it while walking. So +0x58 is not the map area index; FUN_0891705C(-1) is returning a default/point object
that is only meaningful when the game itself sets it on entering a gateway.
That closes the "walk to an area that has a gateway" plan: there is no reachable position that changes the index, in any save, because
the index is not driven by position at all.

## Why following the beat to the portal has stopped here
The last route was: complete the Chapter 1 gateway board (which the 100% save can enter), leave via the Stigma, and then the world map has
live gateways. That route requires winning a battle.
Measured: on the board the D-pad can only select INTERACTABLE tiles (enemy pieces, chests), never empty pillars, so the player piece is
moved some other way; selecting the enemy shows "KP Win battle / Fleeting Flash Lv 7 / BATTLE STAGE Old Chaos Shrine / Engage X Cancel O"
and Cross starts the battle transition. My engagement attempt ran the transition but returned to the board rather than into a fight, and
45 rounds of Square+Circle produced no battle, no HP change and no victory screen.
So the remaining path needs autonomous combat (a fighting-game input loop that can actually win), which is a separate project from the
world-map work, not another walking attempt.

## Complete gate chain, from static analysis (second Claude consult)
Claude's second read reframed the 0x280 table: it is an ACTOR table, controller+0x58 is the controlled actor's slot (hence always 1, on the
board too), and +0x240/+0x260 are that actor's momentary gate-contact state. That predicts no snapshot would ever show a gate id, which
matches every measurement. It also suggested a static search for the writers, which paid off immediately.
STATIC SEARCH: `sh rt,0x240(base)` appears exactly THREE times in the EBOOT (0x08912548, 0x08912680, 0x089127D4) — all world-map code:
 - FUN_08912498 is the struct reset (fills 5 shorts with 0xffff).
 - FUN_089127C4 resets +0x240/+0x242 to 0xffff.
 - FUN_08912674(actor, gateId) is the assignment: it writes s16 +0x240 = gateId, copies the gate's index to +0x242, copies two vectors
   from FUN_08897DEC/FUN_08897E48 into +0x244/+0x250, and sets/clears flag bits from the gate record's flags (&2, &8).
FUN_08912674 has EXACTLY ONE CALLER: FUN_08913E10, the gateway handler.
DISASSEMBLED CHAIN in FUN_08913E10 (0x08913E10):
 - returns immediately unless controller+0x7D8, +0x7E1, +0x7E2 and +0x7E3 are all zero
 - calls FUN_08920244(DAT_089d712c, 0x3F548); a non-zero result short-circuits the whole handler
 - calls the import at 0x08B43A3C with controller+0x88; its return in $18 is the GATE INDEX
 - FUN_08910644(index) fetches the gate record; flags bit 2 and s16 +0x10 (or s16 +4 for the second branch) then set controller state 5/6
 - FUN_08912674(actor, index) writes the actor's +0x240, which FUN_08915048 later turns into controller+0x7B0
So the ONLY source of a gate index is that one import call, and it returns -1 in every map state we have.
The import stubs at runtime (0x08B43A3C = 0x68001BC0, 0x08B4511C = 0x680019BF) decode to opcode 26, i.e. not MIPS code, so they are
relocated/HLE entries rather than plain trampolines; resolving them needs the module table, not a raw read.
TALKING TO THE PARTY (as instructed): all three party objects were found through the actor table — (111.9,21.0), (92.8,47.0), (93.0,-2.0).
Walking to each and pressing Cross started a conversation every time (state 3 each), dialogues played to the end, and the actor's gate id
returned to -1 afterwards in every case. Conversations are flavour at this point and do not create the gateway.

## Correction: only TWO of the three party members were talked to
The earlier note said all three party conversations ran. That was wrong. Re-running the third target, reaching (93.0,-2.0) is blocked: a
45-step aimed walk stopped 21 units short and oscillated around (111.7..114.6, 6..11) no matter the approach angle. Standing at
(114.6,6.2) and pressing Cross eight times gave state 2 every time, so no conversation ran and the actor's gate id stayed -1.
So: party member at (111.9,21.0) talked, party member at (92.8,47.0) talked, party member at (93.0,-2.0) unreachable from this side and
never talked to.
Also recorded: the first Claude call in this round exited 1 with "Reached max turns (4)" because it tried to use tools. Re-running it with
--tools "" --max-turns 1 returned the actor-table analysis that led to the full gate chain, so the failure cost nothing but is worth
remembering as the working invocation for pure-reasoning questions.

## All three party conversations done; talks are ONE-TIME; still no gateway
Claude's suggested tests settled it:
- Teleporting 1 unit from the third member at (93,-2) and pressing Cross from the WEST side (92,-2) started a conversation (state 2 -> 3).
  From the east, north and south sides it stayed 2. So that member does talk, and the approach side matters.
- Revisiting the FIRST member at (111.9,21) after its conversation had run gave states [2,2,2]: no second conversation. Party talks on this
  map are one-time events, which is why the third member seemed silent at first: its talk had already fired earlier in the run.
- At the third member, Square and Circle give state 2 (nothing) and Triangle gives state 11 (the customization menu), so Cross is the only
  talk button and the others are accounted for.
- After all three conversations, the actor's gate id (+0x240) is still -1: conversations do not create the gateway.
TECHNIQUE THAT WORKED, worth reusing: to talk to an otherwise unreachable character, teleport to a cell within about 1 unit on the side
that works, then press Cross. Talking is a distance-and-side check, not an area trigger, so no walking is needed.

## Third Claude consult: how to find the story flag, if there is one
Claude's plan, in priority order, now that conversations are exhausted:
1. START WITH THE -1 ITSELF (its Q3): break at the import call inside FUN_08913E10 (jal 0x08B43A3C at 0x08913E84), record $a0-$a3,
   resolve the stub, then in the callee find the branch that returns -1 and trace every load feeding it. Two shapes are expected:
   a flag test (`lbu`/`lw` from a fixed address plus a bit mask -> that address and bit are the flag) or a table search that fails.
   If an argument is a position/cell, -1 may simply mean "not standing on a gateway cell", not "locked", and an edge-triggered walk onto
   the archway is the fix.
2. FIND THE SAVE BLOCK by breaking on sceUtilitySavedataInitStart and reading dataBuf/dataBufSize out of SceUtilitySavedataParam, then
   searching RAM for a distinctive 32-64 byte run of that buffer; confirm the live copy with a read watch during a save.
3. FLAG DIFF: mask noise first (two idle snapshots), then use the one-time conversations as known flag writes -- snapshot, talk, snapshot,
   and the single-bit change locates the event-flag array and its bit order. The gateway bit is likely in the same array.
4. SAFE ORDER: load savestate, set the bit in RAM, make the game re-evaluate WITHOUT loading a savestate (loading one restores all RAM and
   wipes the edit), check the archway, and only then save to the copied memory stick and reload from the title to confirm.
5. FLAG OR MAP VARIANT: if the lookup reads a heap region filled at map load rather than a fixed array, the gateway is data-driven and the
   flag picks a different map variant; sceIoOpen logging on map entry tells them apart.
Pitfalls noted: bit order may be 1<<(n&7) or 0x80>>(n&7), PSP is little-endian so bit 9 lives in byte 1, some "flags" are counters rather
than bits, a flag may be mirrored in a cache, and a gateway can open while later setup is missing and softlock progress.
NEXT CONCRETE ACTION for a future session: add an execute-breakpoint with register dump to the harness driver (the core already builds
against PPSSPP's debugger) and break at 0x08913E84 to see what the import returns and why.

## Sarah's session (2026-11-16, picking up from Hermes): the "import" is plain code, the scan is a state machine, the prologue gates are dormant

1. BREAKPOINTS NOW EXIST in the host driver. Core/psp_core.{h,cpp} gained host-trace exec-breakpoint APIs in the same
   scripts-only family as psp_watch_read: psp_break_set plants a PAUSE breakpoint; the glue's WebSocketNotifyBreakpointHit
   shim snapshots the GPR file pre-instruction into a ring; psp_break_poll drains it; psp_break_resume calls Core_Resume.
   scripts/dissidia012-drv.cpp gained bp / runbp / bppoll commands (and keeps the wr/wrf RAM-write commands Hermes had
   just added). Build: scripts/dissidia012-drv-build.sh -> ~/drv-bp (fresh glue + driver main linked against the existing
   PPSSPP object set, same shape as psp-memtrace-build.sh). Run with CLASSIC=1 so the classic interpreter's breakpoint
   checks fire.
2. THE "IMPORT" IS ORDINARY MIPS. Breakpoint proof on free.state over 300 frames: the jal at 0x08913E84 fires about
   every 4 frames, always with a0 = 0x095D75C0 (the gate manager), and the return site at 0x08913E8C always sees
   $v0 = -1. Read under the classic interpreter, the callee at 0x08B43A3C is a plain prologue (addiu sp,-0x20;
   sw s0..s4,ra; move s1,a0) -- a real function in the live overlay, FUN_08B43A3C. The 0x68xxxxxx words recorded earlier
   at that address appear only under the JIT (emuhack words); there is no HLE stub and no module table to resolve.
   Calling convention, from a second caller at 0x089143B0 whose delay slot loads a1: scan(gateMgr, playerObj), with
   a1 = controller+0x6D0; the tester reads the position rows at playerObj+0x80 and +0x90.
3. THE SCAN IS A PER-GATE STATE MACHINE. FUN_08B43A3C walks up to 32 records of 0x14 bytes in the gate manager
   (11 populated here; gate ids 11..21 in the ptrA table) and calls the tester FUN_08B43ED4(&rec+4, playerObj, gateMgr)
   on each whose ptrA word is nonzero. The tester builds two anchor vectors from the record's ptrB ids
   (FUN_08897DEC / FUN_08897E48), then runs whichever position tests the record qualifies for: an object-volume
   containment (FUN_08870C94) for records with ptrB+0xE >= 0 (gates 18-21), a facing-cone test for the one record
   with ptrA+4 >= 0 (gate 12, ptrA+4 = 2), a transform test needing ptrA+0x10 >= 0 (none here). It ends in a
   lifecycle switch on the record's state word at rec+0x10: state 1 spawns the portal object (FUN_088A9DE8,
   obj+0xDC |= 0x100), state 2 runs a 120-unit timer, state 3 arms the trigger via FUN_08B43B8C, state 4 waits,
   state 5 is finished. State 0 is read but never advanced by this code -- something outside the scan must write
   the 1. In free.state every gate's state is 0 except gate 14's, which is 5, its timer float past the threshold
   at 122.0: a gate that already lived its lifecycle before this map state.
4. EXHAUSTIVE POSITION CHECK: teleporting the player (all five position copies, readback-verified at +0x30/+0x80)
   over a 4-unit grid, x 56..168, z -44..76 (899 points, 10 frames each), plus nine heights at the archway (146,46),
   the manikin (112,49) and the gate cell (103,0), never moved the actor's gate id (actor+0x240) or controller+0x7B0
   off -1. Control reads: own_gate.state stands at the chest (98.3,46.2) with -1; gate100.state and chapmap.state are
   board states (controller state 21, no field player), so they say nothing about field gates.
CONCLUSION: the prologue gateway is not hidden and not a steering problem. Its record sits in the table with the
volume unarmed: in this map state no gate is switched on. Entry waits on whatever external event writes state 1
into a gate record -- Claude's story-flag hunt, now with a precise target (writers of gateMgr rec+0x10, e.g. under a
write watch during the orb/talk events from an earlier state). The other open path is unchanged: win a Chapter 1
board battle (autonomous combat), clear the gateway, and let the story advance the map itself.


## Sarah's session 3 (2026-11-16 evening): write-watch tooling built; the chapter-start capture is walled off by the cores

Devin's word for this session: find the story switch -- "look at the decomp if you can't, or gamefaq guides."

0. GUIDES FIRST (per the standing rule). Squall_of_SeeD's Game Script (GameFAQs faqs/62288), Chapter 0: the
   prologue field sequence is only: walk the path -> Yuna + orb tutorial (Circle, up to 4 orbs -> Chain Skill)
   -> optional talks with Yuna and Laguna -> Kain in front of the gateway entrance ("Enter the gateway ahead
   of us. We shall follow shortly.") + the message "Red gateways covered in thick fog must be passed to head
   towards your destination." -> Lightning enters -> Gateway to Departure board. NO battle and NO other story
   event sits between the conversations and the gateway. (Earlier work already showed all three party talks
   are one-time flavour and do not create the gateway.)
1. GATE ANCHOR DUMP (new drv `vec` variant planting a bp at 0x08B43F88, just after the tester builds its two
   anchor vectors; vecA = the gate's world position): gate 11 (66.98,0.05,126.21), 12 (76.50,0,72.00),
   13 (145.50,0,48.00) = the stone archway = Gateway to Departure, 14 (113.43,0.20,-22.21), 15 (207.00,0,37.50),
   16 (249.11,-1.03,79.25), 17 (270.65,0.82,93.78), 18 (0.35,1.11,31.41), 19 (307.50,0,-9.00),
   20 (220.44,-0.49,110.99), 21 (331.46,2.23,125.99). The gate manager's region is far bigger than the
   prologue plateau (gates out to x 331, z 126). vecB was ~(0, small y, 0) in every row -- a direction/offset,
   not a second corner. Gate states are IDENTICAL in prologue_end.state, own_free.state and field100.state:
   all 0 except gate 14 = 5, already finished at field start.
2. WRITE-WATCH TOOLING (committed this session). Glue: psp_watch_write / _onchange (MEMCHECK_WRITE,
   BREAK_ACTION_NONE), _log variants, and _pause variants; the breakpoint-hit shim now also records
   BreakpointKind::Memory hits (pc, addr, size, write, full GPRs) into a second 512-entry ring drained by
   psp_memwatch_poll. Driver: wwatch / wwatchc / wwatchl / wwatchcl / wwatchp / wwatchcp / mpoll.
   Two dead ends worth remembering: (a) psp_watch_poll's pc/addr are ALWAYS 0 -- PPSSPP's MemCheck::Apply
   only bumps numHits and never fills lastPC/lastAddr; (b) BREAK_ACTION_LOG prints nothing in this harness
   even though MemCheck::Action -> Log() should emit NOTICE_LOG lines. The PAUSE + shim-ring route is the
   one that works, and it is exact for exec breakpoints. Caution: for STORE memchecks under the classic
   interpreter the reported pc/regs are not instruction-exact (see 4).
3. THE WALL: memchecks change which core effectively runs, and the interpreter path cannot finish this
   game's chapter start. Isolation runs from map100.state, identical input (press cross + run 25) x243:
   - pure JIT, no watches: chain completes -- ctrl+0x68 = 3 (field dialogue) by f14640.
   - CLASSIC=1, no watches: ctrl+0x68 pinned at 21 for all 14,640 frames.
   - JIT + wwatchcp planted (minimal other traffic): pinned at 21, byte-identical values to the classic run.
   So planting any memcheck reproduces the stall; live write-capture during a fresh chapter start is not
   possible with this harness. (First version of the watch script also deadlocked outright: the drv's
   `press` prints no response line, so a blind readline after it blocked forever at 0% CPU -- the scripts
   now use select-based reads with timeouts throughout.)
4. BRACKET + REPLAY. JIT run savestating every 10 chunks: the gate table AND gate 14's arming all happen in
   the final window, chunks 229 -> 239 (f14030 -> f14640). At c229 the gate addresses still hold cutscene
   heap garbage; at c239 ptrA = 0x095D20E0 (valid), g13 = 0, g14 = 2 (state 1 was written and the tester
   already incremented it). States kept: ~/d12s/brk_c*.state, brk_f0..f2.state. Replaying that window from
   brk_c229 under CLASSIC + pause watches DIVERGES: one write to gate 13's word is captured at f463
   (reported pc 0x08B43700), then ctrl+0x68 falls to 0 and the load aborts -- a JIT savestate's load tail
   does not survive the interpreter. The captured write is NOT the gate writer: a RAM dump taken at the
   pause shows the code there is a generic field-object updater (the function at ~0x08B43680 adds a constant
   to a float at obj+0x20, gated on ctrl+0x68 == 2 and an object-type check); before the table exists, the
   gate addresses are ordinary heap and this updater walks over them. RAM dumps: ~/d12s/ram_f0.bin (field),
   ram_hit0.bin (at the pause).
CONCLUSION: gate 13's state word is only ever heap noise or 0 in every reachable state, and its record has
no position test configured, so the scan can never enter it; the "arm gate 13" framing is now very likely
wrong -- the Gateway to Departure is probably not a gate-manager gate in the prologue at all, and entry is
a map/script transition the field's event system fires (or a different map variant loads). The story-switch
hunt should move to Claude's plan items 2/3, which need no chapter replay: find the save block via
sceUtilitySavedataInitStart, and FLAG-DIFF around the one-time conversations (idle-masked snapshot, talk,
snapshot) to locate the event-flag array; the gateway's bit is likely a neighbour. Tooling for both exists
now (exec bp with GPR snapshots for the savedata break; ram dumps + diffing for the flag hunt).


## Sarah's session 4 (2026-11-16 night): flag-diff works -- the talk-flag byte is found; the gateway switch is not in it

Devin's word: run Claude's flag-diff (plan item 3). Protocol, all from prologue_end.state under CLASSIC
(field states are classic-safe): RAM dump, 600 idle frames, dump (noise pair); teleport next to a party
member, Cross, advance the dialogue to its end; dump; 600 idle frames, dump (post pair). Persistent =
changed across the event and stable afterwards, minus anything that moved in either idle pair.

1. TALK MECHANICS, refined: the first attempt (all 4 sides x 3 members at the PLAYER's height) fired
   nothing. The members stand at their own heights (found by scanning the idle dump for their documented
   (x,z) as float pairs: member at (111.9,21.0) has y=-1.62, member at (93.0,-2.0) has y=-2.54). Winning
   recipe: teleport to ~1 unit WEST of the member AT THE MEMBER'S Y, tap the stick (-1,0) for 6 frames,
   Cross. Member 1's dialogue ends after 15 Cross advances. Member 3's dialogue did NOT settle back to
   state 2 within 150 Cross advances (longer chain, or it holds a choice/tutorial) -- its post dumps are
   mid-dialogue, which still served the diff.
2. THE DIFF IS LOUD: one conversation persistently changes ~217K bytes (dialogue/script machinery, the
   0x08B88xxx record tables with a bit-3 flag byte per 8-byte record, entity poses, stack). Two talks
   intersect at ~184K bytes -- the shared machinery, not the flags. The discriminator that worked:
   single-bit flips in BOTH talks with a >=85%-zero neighbourhood, looking for DIFFERENT bits in the
   SAME byte.
3. FOUND: byte 0x08BED9B2 (controlled actor +0x132; actor table base 0x08BAED38, self slot 1) is a
   prologue event-flag byte. Idle value 0x04 (bit 2 pre-set before field start). Talk with the member at
   (111.9,21.0) sets bit 3 (0x04 -> 0x0C). Talk with the member at (93.0,-2.0) sets bit 0 (0x04 -> 0x05).
   The byte sits in an otherwise all-zero span (97% zero neighbourhood) and the bits persist after the
   dialogue ends. In ram_free.bin (own save, post-orb) the byte is 0x04 -- that lineage never ran these
   two talks. NOT yet block-tested (set the bit, confirm the talk no longer fires) -- that is the clean
   final proof and the first step next session.
4. NEGATIVE: the gateway switch is NOT in this byte. Fresh load, byte forced to 0xFF, 120 frames, then
   teleports stepping through the archway (140,47) -> (152,49): gate 13 state stays 0, actor gate id and
   ctrl+0x7B0 stay -1, state stays 2. So the talks tracked here are the optional ones; the gateway's own
   flag lives elsewhere.
NEXT, in order: (a) block-test 0x08BED9B2 bits 0 and 3 to seal the finding; (b) talk to the third member
(92.8,47.0 -- its exact position needs a looser float scan; possibly Kain, whose dialogue is the story
event) and diff for ITS flag byte/bit; (c) orb-event flag: same protocol around the orb tutorial (script
order is orbs first, then talks, then Kain/gateway -- entry may require the orb flag too); (d) if a
candidate gateway bit emerges, Claude plan item 4: set it in RAM in a live field state (no savestate
reload after the edit) and probe the archway.
SCRATCH: ~/d12s/flagdiff.py (v1, height bug), flagdiff2.py (member 1, works), flagdiff3.py (member 3),
diff.py/diff2.py (raw + single-bit), diff3.py (two-talk intersection), diff4.py (sparse single-bit --
the one that found the byte), verify.py (0xFF archway probe). Dumps: fd_*.bin (member 1), fd3_*.bin
(member 3).


## Sarah's session 5 (2026-11-16 late): the flag byte is a mirror, not the gate; third member found; all-talks combo still no gateway

Devin's word: continue. Four experiments, all from prologue_end.state under CLASSIC.

1. BLOCK TEST -- the session-4 byte is NOT the one-time gate. Fresh load, byte 0x08BED9B2 forced to 0x0C
   (bit 3 pre-set): the (111.9,21) talk STILL fired. Fresh load, byte forced to 0x05 (bit 0 pre-set):
   the (93,-2) talk STILL fired. But the talks ARE one-time: refire test -- complete the (111.9,21) talk
   naturally (byte goes to 0x0C), try again: no fire; the (93,-2) control talk fires fine. Conclusion:
   0x08BED9B2 is a downstream record the game writes when a talk completes (a mirror/cache), while the
   gate that blocks refiring lives in a master flag block elsewhere. Pre-setting the mirror fools
   nothing. The master block is now the target (Claude plan item 2, the save block, is the principled
   route: the mirror proves a master exists).
2. THIRD MEMBER FOUND at (95.0, -0.14, 31.0) -- an entity-heap position cluster (x8 copies), NOT at
   Hermes's documented (92.8, 47.0), where no entity stands in this state. Its talk is short (5-6 Cross
   advances) and sets BIT 6 of the same mirror byte. Byte map now: bit 0 = talk (93,-2), bit 3 = talk
   (111.9,21), bit 6 = talk (95,31), bit 2 pre-set at field start. Talks script-reposition the player
   afterwards; the (95,31) talk leaves Lightning standing at (98.37, 0.06, 44.49), right beside the
   object below.
3. THE (93,-2) TALK IS VERY LONG. It does not end within 150 Cross advances (session 4), nor within
   1800 idle frames + 80 advances; in the combo run it finally ended during a later advance loop
   (~85+ total advances). Budget 300 advances to complete it in-protocol. Likely the long tutorial talk.
4. COMBINATION RUN: all three talks in one session (mirror byte 0x04 -> 0x44 -> 0x4C -> 0x4D), then the
   archway probe (teleport steps (140,47)->(152,49) + walk-in): gate 13 state stays 0, gate id stays -1,
   no entry. ALL TALKS TOGETHER DO NOT OPEN THE GATEWAY.
5. OBJECT AT (98.0, 0.03, 46.0) (entity-heap cluster x6): Cross approached from the west gives state 16
   for ~180 frames, self-resolving, no flag changes (a popup/examine). Standing exactly on it: nothing.
   Circle pressed while standing on it: nothing. The (95,31) talk deliberately leaves the player beside
   it, so it is meant to be used after that talk -- probably the orb. Still owed: an in-range Circle/Cross
   test from the post-talk position without teleporting (teleporting on top seems to push the player out
   of interaction range over the following frames).
NEXT, in order: (a) free.state combination -- that lineage is post-orb with mirror byte 0x04 (talks never
done there); post-orb + all three talks + archway has NEVER been tested in one lineage. (b) In-range orb
interaction after the (95,31) talk. (c) The master flag block via the save-block route (plan item 2).
(d) Complete the (93,-2) talk in-protocol (300-advance budget) for a clean post-talk dump and diff.
SCRATCH: ~/d12s/blocktest.py, refire.py, diff5.py (cluster analysis), diff6.py (per-slot/entity diffs --
slot 1 has exactly ONE persistent changed byte per talk: +0x132), flagdiff_m2.py, probe16.py, m2b.py,
combo.py, orb2.py. Dumps: fd2_idle0/idle1/talk1/idle2 (the (95,31) talk), fd2_ev0/ev1 (state-16 popup),
fd2_combo.bin (all-talks end state, still in/after the long talk), fd2_orbclean.bin.


## Sarah's session 6 (2026-11-16 night): every field-event combination tried -- the gateway answers to none of them

Devin's word: "When you have a next step, try it." All remaining combinations were run.

1. THE LONG TALK'S SECRET: the (93,-2) dialogue does not end by Cross advances alone -- not in 150
   (session 4), not in 300 (orb6 run), not in 120 (freecombo run). It ends within ~3-5 advances AFTER
   the player is moved mid-dialogue to the (98,46) area (proven three times: combo, orb6, freecombo).
   The scene is a walk-to-the-orb scene: its progression is tied to the player reaching that spot, and
   teleporting there completes it. Whether that completion is the "real" one or an abort is still
   unknown -- the mirror byte cannot distinguish (its bit is set at talk start).
2. THE (98.0, 0.03, 46.0) OBJECT: Cross at contact = state 16 popup (~180 frames, no flags), Circle at
   contact = nothing, standing on it = nothing, walking through it = nothing. AND: the object is ABSENT
   from ram_free.bin (0 position hits) while all three members are present at their usual spots. In the
   post-orb lineage the object is gone -- consistent with it being the orb, consumed in that lineage.
   How it is consumed from prologue_end is still not reproduced: neither button does it, before or after
   the long talk's scene completes.
3. FREE.STATE COMBINATION (the last untested one): post-orb lineage + talks. (111.9,21) fired (bit 3);
   the long talk fired and completed via the teleport recipe (bit 0); the (95,31) talk would NOT fire
   in this lineage (nor in orb6 after the long talk -- it only fires in a fresh prologue_end; possibly
   consumed by the orb event in post-orb lineage, or the member relocates). Archway probe afterwards:
   gate 13 state 0, gate id -1, no entry. NOTHING.
4. HARNESS LESSON: the analog stick PERSISTS after an analog command until re-centered -- several
   "mystery movements" (players wandering after presses, the orb2 'push-out') were the leftover stick.
   All later scripts center the stick (analog 0,0) after every move. Stick directions are camera-relative
   and the camera zone at (98,46) maps (-1,0) to +x (toward the object from the west).
CONCLUSION: across sessions 4-6, every field-event combination reachable in these states has been tried
-- talks alone (mirror byte 0x4D), talks + orb-scene completion, post-orb lineage + talks -- and the
archway never responds. Combined with session 3 (gate 13 has no position test and is never armed), the
prologue gateway is not a field-event consequence in this map state at all. The remaining route is
Claude plan item 2: find the master event-flag block via the save block (break on
sceUtilitySavedataInitStart), read the story flags directly, and identify/set the one the field script
checks for the gateway transition. The mirror byte (0x08BED9B2) proves the master block exists and gives
its bit semantics for the talks (bit 0/3/6), which is the cross-check for whatever block the save route
finds.
SCRATCH: ~/d12s/orb3.py, orb4.py (wrong stick sign for that camera zone -- invalid), orb5.py, orb6.py,
freecombo.py. Dumps: fd2_orb3.bin, fd2_orb4_pre/post.bin, fd2_orb5_pre/post.bin, fd2_orb6.bin,
fd2_freecombo.bin.

## Session 7 — the save-block route (2026-11-17)

Plan: find the master event-flag block via the savedata buffer (Claude plan item 2), map the talk/orb bits in it, and set-test a gateway-flag candidate at the archway.

### Savedata route — executed; the buffer is not a live field block

- Stub found by ELF import parse: sceUtilitySavedataInitStart (NID 0x50C4CD57) stub at 0x089C5858; five EBOOT call sites (0x088AC100, 0x088AE4C4, 0x088AF1F0, 0x088AF964, 0x088B0214). An exec breakpoint on the stub fires 4x during boot: two EBOOT callers with param a0=0x08BAEF18, two menu-overlay callers with a0=0x096AE23C.
- SceUtilitySavedataParam layout in this game is shifted +0x10 vs the pspsdk header: mode at +0x30 (8 for the EBOOT param, 0 for the menu param), gameName at +0x3C ("ULUS10566"), saveName at +0x4C ("GameData00"; the EBOOT param uses the "GameData*" list pattern), fileName at +0x64 ("DISSIDIA.BIN"), dataBuf/dataBufSize/dataSize at +0x74/+0x78/+0x7C = 257,224 bytes (the disk DISSIDIA.BIN is 257,240 = buffer + 16). Buffers: EBOOT param 0x08BAFC40, menu param 0x096AEF90; they differ in 21,926 bytes (extracted to ~/d12s/s7_savebuf.bin and s7_savebuf2.bin; boot RAM dump ~/d12s/s7_load.bin).
- Chunk-matching the buffer's dense region against field RAM dumps matches only repeated record templates shared with live tables: **the game does not keep the save data as a contiguous live block in the field** — it parses it into structures at load. There is no live master block at a single address to set-test.

### Talk flags — exhaustive negative: no master flag block for the talks exists in RAM

- Re-ran the session-4 persistent-flip protocol over the session-4/5 dumps with numpy (per-talk flip sets saved to ~/d12s/s7_flips.json). Each talk leaves 13k–22k persistent single-bit flips (dialogue/text/graphics churn). **The only byte in the entire 32 MB flipped by all three talks, each with a different bit, is the known mirror 0x08BED9B2** (bit 3 = talk at (111.9,-1.62,21.0), bit 0 = long talk at (93.0,-2.54,-2.0), bit 6 = talk at (95.0,-0.14,31.0)). Restricting to cold neighbourhoods (0x08B80000–0x08C20000, 0x09760000–0x09780000) gives the same answer. The talks' one-time gating therefore does not live in a RAM flag block — it lives in the script entities' own state, which byte-diffing cannot see.
- The orb likewise has no flag byte: its only trace is the orb object's absence from the actor table in the post-orb lineage.
- NEW EVENT: a fourth one-time talk at (76.5, 0.0, 70.0) — Kain, standing 2 units south of gate 12's anchor — sets mirror bit 5 (0x04 → 0x24), completes in 3 advances, and fires in BOTH lineages regardless of the other talks or the orb. (In the prologue_end lineage the three member talks do NOT fire at their free-lineage spots.)

### Gate system — corrected mechanics; gate 13 is inert by construction

- Gate record layout corrected: gateMgr = *(ctrl+0x88) = 0x095D75C0 in every state; records at gateMgr+4+k*0x14 for gates 11–21, layout {ptrA, ptrB, handle, STATE, float timer}. ptrA → array at 0x095D20E0+(N−11)*0x14, ptrB → array at 0x095D2A28+(N−11)*0x14; read both as little-endian s16s. (Gate 13's state word is at 0x095D75F8; gate 12's at 0x095D75E4; gate 14's at 0x095D760C.)
- The scan FUN_08B43A3C is a pure finder (this corrects session 3's reading): it returns the first record id whose tester passes, else −1, and has no state machine of its own. In a 600-frame window after a state load the handler fired only in the first ~120 frames — it is not called per frame.
- The tester FUN_08B43ED4 has three position phases plus a state machine:
  - Phase 1: if the B s16 at +0xE >= 0, an ellipsoid-containment test (FUN_08870C94) against that volume of the volume table (16 records x 0x60 at 0x095E66F8 in this state; named objects OBJ_jump_tag_*, OBJ_mes_gate_*, OBJ_cpu_*; dump ~/d12s/s7_ctx.bin). Only gates 18–21 qualify, via jump-tag volumes 0/3/1/2 centred at (−3.76, 0.00, 31.66), (306.07, 0.34, −15.04), (220.24, 0.34, 114.45), (330.67, 3.16, 130.43).
  - Phase 2: if the A s16 at +4 >= 0, an anchor distance/angle test (radius constant 4.0 at 0x08B444EC) against the point vectors from FUN_08897DEC/FUN_08897E48(ctx, B s16 at +2). Only gate 12 qualifies; its anchor is (76.50, 0, 72.00).
  - Phase 3: requires (A word at +8) & 4 AND the A s16 at +0x10 >= 0. Gates 11/12/14 have the bit but A+0x10 = −1 for every gate, so phase 3 never passes on this map.
  - The state machine (record STATE 0–5) never returns "found" and never advances state 0: state 1 spawns the portal visual and zeroes the timer, state 2 is a 120-unit timer, state 3 binds the object, state 4 finalises, state 5 is terminal. Arming (0→1) is done by FUN_08B43AE8(mgr, idx), which finds the record whose B[0] == idx and sets its state to 1 — that is the story script's lever.
- Gate 13's data: A s16 at +4 = −1 (no phase 2), B s16 at +0xE = −1 (no phase 1), A word at +8 = 1 (bit 2 clear, no phase 3). **Every tester phase fails by construction: the stone archway at (145.50, 0, 48.00) is a scenery node on the world point graph (its OBJ_cpu_CtoD marker volume is a 0.75-unit cube at the anchor), and no flag, state, or position can make it respond.** Sessions 3–6 were probing a trigger that does not exist.
- The handler FUN_08913E10, decoded: if the scan returns an id, it fetches the record via FUN_08910644; if (A+8)&4 and A+0x10 >= 0 it sets ctrl state 6, else if A+4 >= 0 it calls FUN_08912674(actor, id) and sets ctrl state 5 (the conditions panel). Gate 12 is therefore the only handler-reachable gateway on this map; the jump-tag ids (18–21) are returned by the scan but ignored by the handler.
- Gate 14 (anchor (113.43, 0.20, −22.21), on the spawn plateau) sits at state 5 in both lineages — its lifecycle already ran — but standing, walking, and Cross at its anchor in prologue_end changes nothing, consistent with it having no passable tester phase.

### Live tests this session

- Genuine open-loop walking (free.state, dump ~/d12s/s7_freewalk.bin): cannot steer — the stick is camera-relative with no compass. Walking off the plateau edge respawns the player at the field entry point (130.66, 0.98, −36.88); the fall/respawn triggered around (128, −27). The archway was never reached on foot, so the walk hypothesis is inconclusive, not falsified.
- Gate 12's anchor: coarse grid (±6 at 2-unit steps), fine grid (±3 at 1-unit steps, 49 spots, after Kain's talk), facing taps, and walk-throughs — no FUN_08912674 hit, no panel, gate 12's state stayed 0. The phase-2 test needs more than standing in the disc (a facing/movement vector through the anchor segment, or the opaque virtual-call condition at 0x8B440B8). Kain's talk neither arms gate 12 nor opens the panel by itself.

### Verdict

- The master-block hypothesis is dead in both forms: the save buffer is not mirrored live in the field, and the talks demonstrably write no per-talk flag anywhere in RAM besides the mirror byte. Field-event progress lives in the script entities' state.
- The archway question is settled a stronger way: gate 13 has no trigger to enable. The prologue's real gateway entry, when the story arms it, is gate 12 at (76.50, 0, 72.00) via the handler's state-5 branch — or a pure script transition at the Kain beat (the entity system behind FUN_089767FC), which the gate data cannot rule out.
- Next untried routes: (1) the arm path — find who calls FUN_08B43AE8 / FUN_08B43704 from the field script system, and play the opening cutscene chain end-to-end in the harness with exec breakpoints only (memchecks forbidden) to capture a genuine post-Kain state and re-read the gate manager there; (2) the field script system itself — the Kain entity at (76.5, 70) is now a concrete anchor for the entity behind FUN_089767FC; (3) closed-loop walking (steer by camera yaw) to physically walk through gate 12's anchor segment and into the vol-0 jump tag at (−3.76, 0, 31.66) and watch the tester/handler fire.

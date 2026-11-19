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

## Session 8 — the genuine-state route (2026-11-17)

Devin's directive for this session: continue until the gate is ENTERED, through whatever routes the evidence favors.

Plan: play the opening chain genuinely (no state surgery, no flag writes) with exec breakpoints only — bp on the arming function FUN_08B43AE8 and on the panel-open call FUN_08912674 — then re-read the gate manager in the genuine post-Kain state and test gate 12 there.

### The genuine chain (fresh save, title → field)

- Harness IO fix first: the Python client must use blocking readline (the freecombo pattern). select()+readline deadlocks — Python's buffered reader holds lines the fd-level select cannot see. Two calibration runs died on this before it was diagnosed.
- Fresh-boot flow: autosave notice (Cross) → copyright (Cross) → title. Title cursor defaults to NEW GAME with no save, to LOAD GAME when a save exists (one Down from LOAD GAME lands on EXTRAS — that cost one run). NEW GAME → PSP name-entry OSK (Start = Finish; the default name is the emulator nickname) → Data Config creates GameData00 (276 KB) → "Save completed" (Circle = Back).
- From LOAD GAME on that fresh data, the genuine opening chain is: intro movie → Moogle tutorial ("Have you ever fought DISSIDIA battles?" — "I have played before.") → two tutorial battles (won by a dumb battle block: Square, Square, Cross, Triangle on rotation; enemy HP 338 → 114 → dead) → Customization/Abilities menus (escaped Circle-heavy) → Moogle farewell → chapter intro text cards (ctrl state 21 throughout the intro shell) → parapet cutscene (Lightning meets Kain) → field loads.
- Menu-navigation lessons: bright-screen heuristic alone misfires (the Moogle's snow scene reads as "menu-bright"); Cross advances dialogue, Circle escapes menus, and the ratio between them has to match the situation. Milestone savestates every 60 iterations made each fix cheap to retry.

### The arming function fired — once, for gate 14

- Exactly one FUN_08B43AE8 hit in the whole chain (frame ~11820 of the intro stage, during the parapet-cutscene segment): a0 = 0x095D75C0 (gateMgr), a1 = 3, ra = 0x08913D68 — the call site is the jal at 0x08913D60, one of only two static call sites in the field RAM dump (the other is 0x0891518C). The caller is in the handler's own module, not a distant story script.
- a1 = 3 is the record whose B[0] == 3 = gate 14. In the genuine field-start table gate 14 is already STATE 5 with timer 122.0: it was armed during the intro and its whole lifecycle (spawn → 120-timer → bind → finalise) ran before field control began. This matches the hand-built lineages, where gate 14 was also 5 — that part was never a lineage artifact.
- Gate 12's B[0] is 1: arming gate 12 means a call FUN_08B43AE8(mgr, 1) that has not happened yet at field start. Gate ids by B[0]: gate 11→0, 12→1, 13→2, 14→3, 15→4, 16→5, 17→6, 18→7, 19→8, 20→9, 21→10.

### Genuine field start vs the hand-built lineages

- Genuine spawn: field loads in dialogue (ctrl state 3) at (114.56, −0.29, −24.22), beside gate 14's anchor; after the 12-advance opening dialogue Lightning stands at (92.00, −2.45, −2.60), next to the long-talk member at (93, −2). The hand-built lineage spawned at (130.66, 0.98, −36.88) — a respawn point, not the genuine entry.
- Gate table at genuine field start (gateMgr 0x095D75C0, records at gateMgr+4+k*0x14): every gate STATE 0 except gate 14 = 5 (timer 122.0). Gate 12: A = [(12,1),(2,1),(11,0),(−1,31),(−1,−1)], B = [(1,6),(28,−1),(29,30),(31,−1),(−1,0)] — same A/B data as the hand-built states; only the lifecycle history differs.
- Mirror byte 0x08BED9B2 = 0x00 at genuine field start. The hand-built lineage had bit 2 pre-set (0x04) — a lineage artifact, not a story fact.
- Genuine states saved: ~/d12s/genuine_field.state (field start, post-dialogue), ~/d12s/genuine_board.state (chapter-intro text card; misnamed, kept), ~/d12s/s8_title.state, ~/d12s/s8_mile.state (rolling milestone).

### Session 8, part 2 — the field events, the tester decoded, and GATE ENTRY

Field events in the genuine lineage (bps on arm + panel throughout): the long talk/orb scene completed (mirror 0x00 → 0x01), a contact event near (95, 22) set mirror bit 2, Kain's talk fired at (76.5, 0, 70) and ended after 4 advances (mirror → 0x25, bit 5 as in session 7). **The arming function never fired during the field segment, and gate 12's record stayed STATE 0 after Kain.** Arming is not a field-event consequence. Genuine states saved: genuine_field.state, genuine_prekain.state/.bin, genuine_postkain.state/.bin.

Corrections to the session-7 model, from full disassembly of the tester/scan/handler cluster:

- FUN_08913E10 is not a gate handler — it is the field mode's per-frame tick (fires constantly; bp'ing it floods the hit log and wrecks input timing — do not bp it).
- FUN_08912674 is not the panel call — it is the actor↔data-record bind/initialiser (sets actor+0x240 = id, +0x242, copies the name strings to +0x244/+0x250, seeds the flag words +0x260/+0x264 from the data record).
- The scan FUN_08B43A3C calls the tester FUN_08B43ED4 for EVERY record with ptrA != 0, regardless of the record's state word, and returns the first passing record's B[0]. The record state word only drives the lifecycle tail (states 1–5) at the end of the tester itself. **Gate entry never required arming** — session 7's premise was wrong; its grid tests failed on the tester's real conditions, not on state.
- The arming function's two call sites: (1) 0x08913D60 in the tick — arms the record whose B[0] equals byte+7 of the map descriptor *(ctrl+0x54) (= 3 on this map → gate 14 at field load, gated by ctrl+0x7E6); (2) 0x0891518C in the consumer FUN_0891515C, driven by the interaction-registration function FUN_08915048 (its only caller is 0x08918420), which sets ctrl+0x7DF = 1 and, when the actor's flag words pass two global mask tests, ctrl+0x7B0 = the actor's bound id; the consumer then arms the record indexed by the data record's +6. Neither path fired for gate 12 anywhere in the prologue.

The tester FUN_08B43ED4, phase 2 (only gate 12 has A s16+4 >= 0), in order:
1. Distance from the player to the gate's interaction point (world point-table entry B+2 = 6, via FUN_08897DEC/FUN_08897E48 on the world object from FUN_08899300) must be < 4.0 (constant at 0x08B444EC).
2. FUN_089170F8() != 0: ctrl state == 2 and the byte at *(ctrl+0x8C)+5 is not > 0 (it is 0 in the genuine state).
3. A facing-angle test (FUN_08867FEC) between the player→point vector and the player's facing vector (player+0x90), threshold 90 degrees.
4. The virtual call at 0x8B440D8: object = *(0x89D6008)+0xC0 (0x08C15C50 here), vtable 0x089C96E8, fn 0x0887279C — returns (obj+8 & 4) != 0, unless the global busy bit (*(0x89D6008)+0x264 & 2) is set. This flag word is the gateway object's own state and cycles independently of the record state.

The gateway's collision wall stops the player at z ≈ 70.17 in front of gate 12's anchor (76.50, 0, 72.00) — the interaction pose is at that threshold, ~1.8 units south of the anchor, not on the anchor point (standing exactly on the anchor fails the geometry).

### GATE ENTERED (twice, identical trajectories)

From genuine_postkain.state, the working sequence was: teleport to (76.5, 0, 70); stick-up tap, settle, Cross (ends at (77.33, −0.11, 65.36)); teleport to (76.5, 0, 70) again; stick-right tap — she steps to (76.00, 0, 70.17), the threshold. The tester then passed phase 2 end-to-end (24 virtual-call hits logged across the tap's frames), ctrl state went to **5** (the conditions-panel/white transition), and within ~150 frames to **state 20: the board**. Screenshot-verified: the Gateway to Departure board — a grid of floating stone discs over a cloudy void, Lightning's piece materialising on the entry disc in a pillar of light. Board HUD: KP 0 (snowflake sigil), Gil 50, Lightning Lv 1 HP 1000, prompts "Skill □", "Scan Mode SELECT", "Customization △". Visible pieces: a gold treasure chest and an enemy manikin on discs to the right. Board states saved: genuine_panel.state/.bin (at entry), genuine_board3.state/.bin, genuine_board4.state.

Reproduction note (honest caveat): the identical input sequence WITHOUT the virtual-call breakpoint planted did not open the gate, and the gateway object's flag word (obj+8) read 0 at every sampled frame boundary in that run. The flag appears to be real-time-cycled: the harness runs frames as fast as possible, and the breakpoint pauses were what let the tester's evaluations land inside the flag's open window (the successful runs' trajectories were bit-identical, including the hit pattern at frames 210–230). At real play speed a player standing at the threshold gets that window naturally — nothing about the entry needs frame tricks, but a fast-running test harness can starve it. Worth remembering for adapter testing: gateway interactions are real-time-sensitive in this engine.

Adapter notes from the entry: the announceable events are (a) reaching the threshold wall in front of a gateway (z ≈ 70.2 here — the "you are at the gate" moment), (b) the state-5 transition (conditions/entry), (c) board entry with KP/Gil/level readout, and (d) board contents (chest and enemy pieces visible from the entry disc). The board's cursor/move system (D-pad + Cross, KP-gated) is the next mapping target. Gate 12's record state stayed 0 throughout — the adapter should key gateway detection on the tester/threshold geometry, not on record state.
## Session 9 — the Gateway to Departure board, mapped end to end (2026-11-17)

Goal: map the board (ctrl state 20, entered in session 8) as a playable, announceable space — movement, pieces, menus, and the exit. Method: systematic exploration from genuine_board4.state with a position instrument built from Scan Mode (below): enter scan, screenshot, and locate the glowing selection square by visual grounding — the square always frames Lightning's current disc, which made every move verifiable. All movement/piece claims below were traversal-verified this way, not inferred from single screenshots. Board states saved: ~/d12s/s9_chest.state (at the chest prompt), ~/d12s/s9_postkain_board.state (chest consumed, on the chest disc), ~/d12s/s9_kain2.state (chest done + Kain recruited, Lightning on I1), ~/d12s/s9_postscene.state (on G, after the approach scene).

### The board graph — 14 discs

```
            [T2] empty
              |
            [T1] empty
              |
[A]—[B]—[C]—[D]—[E]—[F]—[G]—[H]
empty START empty CHEST ENEMY empty empty STIGMA (exit)
              | |
             [I1] [J] = Kain, assist
              |
             [I2] empty
              |
             [I3] empty
```

- Links (all bidirectional, cursor- and traversal-verified): A-B, B-C, C-D, D-E, E-F, F-G, G-H along the main row; B-T1, T1-T2 (a two-disc spur north of B); D-I1, I1-I2, I2-I3 (a three-disc spur south of D); I1-J and E-J (J hangs between I1 and E). Confirmed non-links (pressing that way is a silent no-op): Right from H, Down from H, Up from A, Right from T2, Left from I1, Right from J, Down from J, Right from I3, Down from C.
- Scan-view disc centres (480x272 screenshot px, ±10 px render jitter): A(124,122), B(164,127), C(213,124), D(249,123), E(287,124), F(324,124), G(361,123), H(393,121); T1 ≈(149,97), T2 ≈(157,75); J ≈(252,152); the I-column renders at x ≈237 with I1/I2/I3 at y ≈171/205/242 in enlarged crops. Lightning enters the board on B (the second disc from the west), not on A.
- The T-spur and I-spur discs are all empty in this gateway — they exist for board geometry (and, in later gateways, for chains/skills), not for content here.

### Movement and KP

- Both the d-pad and the analog stick move the piece (the game's Help Manual confirms: "You can move around using either the directional buttons or the analog stick"). One input = one hop to a linked disc. There is no free roaming and no diagonal/off-link movement.
- **Movement is FREE.** KP read 0 at board entry and stayed 0 through every hop, engagement, and the exit. This corrects session 8's closing guess that board movement might be KP-gated. KP is a battle currency only (see "KP economy" in part 2 of this section).
- Pressing toward a disc holding a prompt piece (chest, assist, Stigma) does not move Lightning: it raises that piece's Engage prompt (below). Pressing toward an empty disc hops immediately. Pressing toward the manikin's disc hops onto it (see "The manikin").

### Piece interaction model — two kinds of pieces

- **Prompt pieces** (chest, assist, Stigma): pressing toward the piece from an adjacent disc opens a prompt box — piece name, one-line description, "Engage ⓧ / Cancel ◯" — the same text Scan Mode shows for that piece. Cross = Engage: the piece's event fires and **Lightning stays on her own disc** (oracle-verified for the chest and for Kain). Circle = Cancel: nothing happens, she stays. The piece does not move or animate toward her; engagement is entirely through the prompt.
- **The manikin** (E): no prompt, ever — see its own subsection.

### The chest (D) — a booby-trap joke and a Broadsword

- Engage from C fires a short on-board portrait-banner scene: Kain — "Kaboom!!!" / Lightning — "..." / Kain — "What if it exploded like that?" / Lightning — "...Okay, it won't... Sorry." (the chest was his idea of a trap demonstration). Then a banner: "**Obtained Broadsword from chest!**", then a tutorial tip box: "Items you obtain can be equipped within the customization menu."
- Flow cost: ~3 dialogue Cross advances + the obtained banner + tip dismissal. Afterwards board control returns with Lightning still on C. The chest piece is consumed (D becomes an ordinary empty disc; she can then hop onto it). KP 0, Gil 50 — unchanged. The chest grants an equippable weapon, not currency.

### Kain, assist character (J)

- Prompt from I1: "Kain / Assist Character / Engage ⓧ Cancel ◯". Engage fires a two-line scene on the board — Kain: "I'll trust you to lead our way, Lightning. Don't fret if you lose your way." / Lightning: "Don't lump me in with Laguna! Like I'll get lost that easily." Kain's piece is then consumed (J becomes empty) and Lightning stays on I1.
- Recruiting unlocks Kain as her assist; the Customization header's Assist slot still read empty immediately after the scene — the assist is set through the Customization menu's Assist entry, not automatically by the recruit.

### The manikin (E) — an exhaustive negative

- The piece is a full character model (a dark spearman, visually a Kain copy) — the same presentation as Kain's assist piece, not one of the small floating icons the manual lists for battle pieces. Scan Mode shows **no info box** for it (chest, Kain, and the Stigma all show one).
- Stepping onto its disc is allowed from every side and nothing fires: approached from D (west), F (east), and J (south), before and after the approach scene; stood on the disc for 900+ frames; Cross pressed on the disc with long waits; the piece never raised a prompt, never launched a battle, and ctrl state never left 20. Lightning can also simply pass through E to F.
- Best-supported reading, with the manual's taxonomy (part 2): this is a Chaos/event piece whose battle is not on offer in the prologue visit — the manual warns that "battle pieces triggering events or boss battles cannot be chained", and the only event this board's east side actually triggers is the scripted approach scene (below). Untried: approaching with the analog stick instead of the d-pad, shoulder buttons while on the disc, and re-entering this gateway after Chapter 1 is unlocked. For the adapter, treat E as a pass-through disc with a decorative piece in this gateway, and do not promise a battle here.

### The approach scene — stepping onto G the first time

- The first time Lightning steps onto G (the disc adjacent to the Stigma), a scripted party scene interrupts before the Stigma prompt can appear (~5 Cross advances; speakers Lightning, Kain, Laguna — the board's party is the trio, though only Lightning's piece is on the board). The mechanically important line is Lightning's: "Those "gateways" obstruct our path. We can unlock them by touching the insignia..." Laguna's opener confirms the board is otherwise cleared ("Looks like there's nothing else here. Let's unlock this "gateway" and go."). The rest is banter about Laguna having entered the locked gateways first.
- After the scene, the Stigma prompts normally. Adapter consequence: the first approach to any gateway's exit disc may be gated by a story scene; the exit prompt is only reachable once it has played.

### The exit — Stigma of Chaos (H): reached, and it cost nothing

- Prompt from G: "Stigma of Chaos / Engaging this piece finishes the level. / Engage ⓧ Cancel ◯" (identical in Scan Mode). Cross to engage: white flash (the same transition flash as gateway entry), black load, ctrl state 20 → **21** (the story shell), a post-gateway story scene (Laguna and Vaan), and then the **Story Mode chapter select with "Chapter 1 - A New Threat" unlocked (marked NEW) and a "Play this story? YES / NO" prompt** (△ opens Customization from there).
- **No battle, no KP cost, no threshold.** KP was 0 and Gil 50 at engagement and the level finished anyway. The manual's general rule — "you need to engage the Stigma of Chaos battle piece to unlock a gateway" — in this prologue gateway resolves to a touch-and-finish: the Stigma is a battle piece in later gateways, but here engaging it simply ends the level. **Session success condition met: the Gateway to Departure is completed end to end, and the landing (Story Mode, Chapter 1 unlocked) is identified.**

### KP economy (played + the manual's "Save Those KP!" and "Chains")

- KP is earned **only by defeating opponents in gateways**. Formula, verbatim from the manual: "(KP Chances Met) x (Enemies Defeated) = KP Gained". KP Chance conditions are displayed in the upper-left during a battle; meeting a condition in a won battle grants 1 KP, and 0 KP is paid if the conditions are unmet.
- Chains: defeating an enemy makes its allies on nearby tiles come after you in continuous battles; chained wins multiply KP by enemies defeated. Pressing Circle before a chained battle begins cancels the chain; fleeing after a battle has begun loses the KP accumulated up to that point. Weaker foes are engaged first in a chain.
- KP is spent at Moogle Shops — the Customization menu's Shop entry. KP gained inside a gateway is only banked in full once the gateway is unlocked (by engaging its Stigma of Chaos); leaving through Pause → Exit Gateway saves level and rewards but forfeits the unbanked KP.
- Inside a gateway, battle damage is **not healed** between battles and the EX Gauge carries over; a potion found in a gateway restores HP and EX Gauge to full. (Manual, "Progressing Through a Gateway".) None of this bites in the prologue gateway — there are no fights — but it shapes every later board.

### The four board surfaces

**Scan Mode (Select).** An overhead view of the whole board. The cursor starts framed on Lightning's own disc and hops disc-to-disc along the links, one disc per d-pad press (it is a disc cursor, not a free/grid cursor). Info boxes appear for prompt pieces only, with the exact text the Engage prompt uses: D = "Treasure Chest / Contains helpful items."; J = "Kain / Assist Character"; H = "Stigma of Chaos / Engaging this piece finishes the level." The manikin and every empty disc show no info box. Select toggles back to Normal Mode; Circle also exits. For the adapter, Scan Mode is the board's built-in "where is everything" view — its cursor position and info text are the announceables.

**Customization (Triangle).** Header: "Customization", character name + Lv (Lightning Lv 1), an Assist slot (empty until an assist is set), GIL (50), EXP NEXT (5000) / TOTAL (0), STYLE SET(△) / LOOK(—). Stat block: HP 1000/1000 TOTAL, CP 225/330, BRV 95, ATK 10, DEF 12, LUK 10. Menu list, fixed order, cursor starts on Abilities: **Abilities, Equipment, Accessories, Summons, Assist, Adjust Level, Shop**; Down moves one item; the footer describes the highlighted item ("Customize attacks and passive abilities." for Abilities). Also on screen: "LUCKY! 2%" and "BATTLES 13" readouts and a small board-minimap graphic. One Circle exits to the board.
- Gotcha: the **first** Customization open after obtaining the first item instead shows a Moogle tutorial dialog ("◇Equipping Items — Hmmm? Obtained an item, have you!? ... First, go to the Equipment menu! If you still have no clue, press START!") with a ◯ Close prompt. Circle closes it; every later Triangle open goes straight to the real menu. An adapter (or a player) must not mistake that first dialog for the menu itself.

**Skill (Square).** Inert in this gateway: pressed on a settled board at +60 and +300 frames, no menu, popup, or state change. The manual ("Let's Use Some Skills!") explains why: Square views/uses **held board skills**, and Lightning holds none here. Skills are gained from field orbs and chests (max 5 held; gaining a 6th forces a discard), in five families — Chain (increase chain radius), Recovery (recover HP / revive a KO'd character), Attack (weakens an enemy before battle), Charge (strengthens your character/party), Other. Attack/Charge effects apply to the next battle and persist through chains; fleeing deactivates them; a Chain skill triggers a battle immediately, so it is used last.

**Pause (Start).** Menu: **Return to Game / Quicksave / Exit Gateway / Help Manual** (cursor starts on Return to Game; footer "Close the pause menu."). Help Manual opens a "Gateways" topic list — About Saving; Progressing Through a Gateway; Chains; Let's Use Some Skills!; Save Those KP!; Can't Change Sets or Levels?; Battle Symbols; Skills — each a 3–4 page Moogle text (Down moves the topic cursor, Cross opens, Right/Left flip pages, Circle closes). This manual is the game's own rules documentation; the KP, chain, and skill rules above are quoted/paraphrased from it. Its "Battle Symbols" page gives the piece taxonomy for later gateways: Battle Piece (Normal — low levels, weak), (Hard — high levels, strong), (Strange — unusual stats), (Expert — all-around hard to beat), (Ultimate — extremely hard to beat); Party Piece (engages an enemy party); Chaos Piece (launches events or battles with characters sided with Chaos); Mirror Piece (launches a battle against a carbon-copy of your current character, mimicking stats and party status).

### Adapter announceables (sound-first: never speak what the game already cues)

- **Board entry:** gateway name, KP, Gil, and leader Lv/HP (the entry flash has a sound, but the numbers have no audio form).
- **Arrival:** current disc identity + which linked discs hold pieces (e.g. "On C. Right: chest on D. Down: nothing."). The game gives no positional speech; hop sounds alone don't tell a blind player where they are. This is the board's core announcement.
- **Engage prompts:** the prompt's own name + description text, verbatim ("Treasure Chest — Contains helpful items. Engage / Cancel"). The prompt is silent text; speaking it is the interaction.
- **Event results:** the obtained-item banner ("Obtained Broadsword from chest!"), "Kain joined as assist", and other one-line banners — all silent text in-game.
- **Story-scene trigger:** on the first approach to the exit disc, announce that a story scene is playing and that dialogue advances with Cross (banner dialogue is unvoiced text; OCR of the banner line is the long-term answer, out of scope here).
- **Exit:** Stigma engagement → "Level finished" + the landing: Story Mode chapter select, the newly unlocked chapter's name ("Chapter 1 - A New Threat"), and the Play-this-story YES/NO prompt with its cursor.
- **Menus:** Customization cursor item + its footer description; the first-open Moogle equip tutorial flagged as a tutorial, not the menu; Pause menu items; Scan Mode cursor disc + info text when the player scans.
- **Not to speak:** the hop/step sounds, the transition flash sound, and menu cursor blips — the game already cues those; doubling them adds noise.
- **Detection note for the reader:** board state is ctrl state 20; Lightning's current disc is most reliably read from the Scan Mode cursor frame (the selection square spawns on her disc) until a RAM address for the board cursor/piece position is found. RAM hunting for the disc index was tried briefly this session (full-RAM diffs across moves are swamped by render buffers — ~129K changed bytes; grid-int and float-triple searches found no clean state cell) and is the main open technical item for the adapter's board reader.

### Verdict

- The Gateway to Departure board is fully mapped and was completed end to end in the genuine lineage: 14 discs, free link movement, a joke chest (Broadsword), Kain recruited as assist, a decorative manikin, a story scene on first approach to the exit disc, and a Stigma engagement that finishes the level at zero cost and lands on the Story Mode chapter select with Chapter 1 unlocked.
- Unanswered, with boundaries stated: (1) the manikin's battle — exhaustively non-interactive in this visit (above); whether it activates on a later re-entry is untried; (2) a live battle's launch/return path from a board piece — no battle exists on this board, so battle transition states remain for the first gateway that has one (Chapter 1's); (3) the board cursor's RAM address for the adapter reader — the Scan Mode screenshot oracle works but is slow; a state cell is the follow-up.

## Session 10 — the board's RAM state cells (2026-11-17)

Goal: find stable RAM cells for the board state the adapter must announce, so the board reader needs no screenshot oracle. Method, fixing session 9's churn problem: OFFLINE diffing over labeled dump sets — (a) reloads of the session 8/9 states with known discs, two dumps 30–45 frames apart per state (a cell must be bit-stable within a state to count); (b) a fresh single-boot labeled walk from genuine_board4.state (stops B, C, B, T1, T2, T1, B, A, B, C, plus the chest prompt from C), Scan Mode screenshots confirming the stops the detector could read (B, T1, T2, A; the C stops are confirmed by construction — the chest prompt opened from them, which only happens from C). All addresses below are in the 0x08800000-based RAM image (dump offset = address − 0x08800000) and held at the SAME addresses across the session-8 boot, the session-9 boot, and this session's loads.

### (1) Lightning's current disc — FOUND, two encodings

- **Grid id (u32) at 0x08B82A24.** The value is the disc's grid id = 10*(row+2) + col on the board's virtual 10-wide grid (rows: T-spur −2/−1, main row 0, I-spur +1/+2/+3; cols: A=0 … H=7, T=1, I=3, J=4). It tracks her piece exactly: verified live across the walk (A=20, B=21, C=22, T1=11, T2=1), across reloads (D=23, I1=33, G=26), in session 9's own dumps (mvBase=21 → mvR1=22 after one Right hop; s9_board=21), and it does NOT move in Scan Mode or when a prompt opens. This is the adapter's primary cell.
- **Current-record pointer at 0x095D123C** (the board manager at 0x095D1230, +0x0C; the table object at 0x08B82A2C points back to the manager): holds 0x08B82A30 + 8*grid — the address of the current disc's record (below). 0 before the board loads. Manager +0x10 (0x095D1240) holds the PREVIOUS disc's record pointer (the disc she hopped from; reads A's record at entry).
- Caveat from the panel state: in genuine_panel.state (the entry transition) the grid byte already reads 20 (A's id — its init value) while the manager pointer is still 0 and the focus cell (below) is −1. Read the board cells only once ctrl state is 20 and the manager pointer is non-null.

### The disc table — full layout (adapter's board map for this gateway)

Record array at **0x08B82A30**: 60 records × 8 bytes, indexed by grid id: {u32 exists (1 for the 14 real discs, 0 for empty grid cells), u32 pointer to the disc object}. Disc objects at **0x095CD570 + 0x40*grid**, 0x40 bytes each: +0x00 = board X (float), +0x08 = board Z (float) — 4.0 units per disc step; +0x0C = 0xFFFF0000 | (0x100 + grid); +0x30/+0x34 = linked-list threading to the next/previous grid objects. The objects are STATIC: byte-identical before and after the chest was consumed and Kain recruited — piece presence/consumption state is NOT stored here (see (4)).

| grid | disc | record @ | object @ | X | Z |
|---|---|---|---|---|---|
| 1 | T2 | 0x08B82A38 | 0x095CD5B0 | −14 | −12 |
| 11 | T1 | 0x08B82A88 | 0x095CD830 | −14 | −8 |
| 20 | A | 0x08B82AD0 | 0x095CDA70 | −18 | −4 |
| 21 | B (entry) | 0x08B82AD8 | 0x095CDAB0 | −14 | −4 |
| 22 | C | 0x08B82AE0 | 0x095CDAF0 | −10 | −4 |
| 23 | D (chest) | 0x08B82AE8 | 0x095CDB30 | −6 | −4 |
| 24 | E (manikin) | 0x08B82AF0 | 0x095CDB70 | −2 | −4 |
| 25 | F | 0x08B82AF8 | 0x095CDBB0 | 2 | −4 |
| 26 | G | 0x08B82B00 | 0x095CDBF0 | 6 | −4 |
| 27 | H (Stigma) | 0x08B82B08 | 0x095CDC30 | 10 | −4 |
| 33 | I1 | 0x08B82B38 | 0x095CDDB0 | −6 | 0 |
| 34 | J (Kain) | 0x08B82B40 | 0x095CDDF0 | −2 | 0 |
| 43 | I2 | 0x08B82B88 | 0x095CE030 | −6 | 4 |
| 53 | I3 | 0x08B82BD8 | 0x095CE2B0 | −6 | 8 |

### (2) Scan Mode cursor — FOUND, and cleanly separate from the piece

- **Cursor record pointer at 0x095CD41C**: the address of the disc record under the Scan Mode cursor; **0xFFFFFFFF when not scanning**. Verified by a live scan walk (B→C→D→I1): it was the ONLY u32 cell in RAM matching the cursor's record pattern across the four dumps, while the grid byte, the manager pointer, and the focus pointer all stayed on B — the cursor is a separate cell from the piece position, as the adapter needs (announce the piece from (1); announce the scan cursor from here only while scanning).
- Neighbors in the same small struct (base 0x095CD410): +0x08 (0x095CD418) read 1 while the scan cursor sat on D (a prompt-piece disc, info box up) and 0 on empty discs — a candidate "cursor is on an info/piece disc" flag, single observation. +0x10 (0x095CD420) pointed at 0x08B82C60 only in the scan-on-D dump — a candidate pointer to the piece object under the scan cursor (the structure just past the record array, base 0x08B82C10+, is the piece-actor area); also single observation. Both are leads, not confirmed cells.

### (4) Engage prompt target — FOUND (the focus pointer)

- **Interaction-focus pointer at 0x095CD410** (the struct base above): at rest it holds her own disc's record; **while an Engage prompt is open it holds the TARGET disc's record** — the chest prompt from C read 0x08B82AE8 (D's record) in two independent datasets (this session's walk and the s9_chest.state reload). The adapter reads the prompt's target disc from this cell while a prompt is up and names the piece from the board's disc table.
- Caveat: it is the interaction system's "addressed disc", not a prompt-only flag — in the s9_kain2.state reload (just after the Kain engagement scene) it still pointed at J's record while Lightning stood on I1. Read it as the prompt target only while a prompt is actually up; at rest it can lag one engagement behind.
- The piece's own name/description text was NOT found in a cell (the prompt text is presumably rendered from the piece's data at draw time); the adapter's disc→piece table covers this gateway. Piece presence/consumption state (chest gone after looting, Kain gone after recruiting) is not in the disc objects and was not located; the piece-actor area past the record array (0x08B82C10+) is the lead for a future session.

### (3) KP and Gil — NOT FOUND

- KP read 0 and Gil 50 in every genuine-lineage state, so no genuine pair differs in either value. Cross-lineage diffing against the 100% save failed both ways tried: no u32/u16 cell holds 50 in all genuine field+board dumps and a different, plausible value in the 100% field dump (the few matches are texture/render data), and the narrow scan of the board-structures page (0x095C0000–0x095E0000) found no stable 50 at all. Two attempts to manufacture a 100%-lineage board state for a same-scene diff also failed: teleporting to Kain's spot in the 100% field opens his (long) dialogue, and after advancing it out (27 Crosses, ctrl state back to 2) analog taps from (76.5, 0, 70) produced no movement at all within the session-8 recipe — the post-dialogue 100% field does not answer the stick the way the genuine field did. Whether the 100% save's Gil is even nonzero is unverified (its Customization screen was never read), so the diff premise itself is shaky.
- Next routes for a future session: (a) two genuine-lineage board states that differ in Gil/KP by play (win a Chapter 1 gateway battle, then diff); (b) diff a Customization-menu-open dump against the settled board (the header draws GIL from somewhere); (c) read the 100% save's Gil off its Customization screen first to fix the diff target.

### (5) Board identity — NOT FOUND

- Only one gateway board is reachable in the genuine lineage, and no second board state exists in any lineage (the old board.state is a state-21 story-shell state, not a board), so there is nothing to diff an identity cell against. Constant-but-unverifiable leads in the board structures: table header words 0x08B82A1C = 1 and 0x08B82A28 = 31, manager +0x00 (0x095D1230) = 596. Chapter 1's first gateway board is the future test: if one of these words changes there while the structures sit at the same addresses, it's the identity cell.

### Corrections to earlier records (from this session's measurements)

- **s9_kain.state / s9_kain.bin are NOT "at the Kain prompt on I1"**: the grid byte reads 22 and the focus pointer C's record in both — Lightning is on **C** (the near-black screenshot fits the chest sequence's Kain banner scene). Session 9's Kain findings rest on its screenshots and stand; the state file must not be reused as an I1/Kain-prompt state.
- **Session 9's s9_sc0..sc5 dumps were not a scan-cursor walk**: grid byte, focus, and manager pointer all read B in all six dumps. Whatever they were capturing, the scan cursor never moved in them — which is why they couldn't separate cursor from piece.

### Verdict

- The board reader is unblocked: current disc (grid byte 0x08B82A24, or the manager's record pointer 0x095D123C), the full disc table with coordinates, the Scan Mode cursor (0x095CD41C, −1 when off), and the Engage prompt's target disc (focus pointer 0x095CD410 while a prompt is up) are all stable, address-fixed cells, verified live and across lineages. KP, Gil, and board identity remain open with their blockers and next routes stated above.
## Session 11 — the board reader ships in the adapter (2026-11-17)

Goal: implement session 9's board announceables in Core/dissidia012_adapter.cpp on the session-10 cells, and register the adapter in the app. The field reader and the menu watches are untouched; board mode is a new watch at the head of OnFrame that owns its frames while active.

### App registration

- Sources/OpenGameAccess/AdapterCommand.swift gains a "dissidia012" case in supported(adapterID:): [.whereAmI, .menuState] — exactly what the adapter's Command handler answers (the board branch answers both with the arrival line). Without this case the app returned an empty command list and hid the reader (the khbbs/mku lesson).

### What the board mode announces (announcement group "board", Normal priority; High when answering a query)

- Activation gate, fail-closed three ways: ctrl state (controller+0x68, same chain as the field) == 20 AND the board manager's current-record pointer (0x095D123C) equals the record address implied by the grid byte (0x08B82A24: pointer == 0x08B82A30 + 8*grid) AND the grid is one of the 14 known discs. The pointer/grid agreement is what keeps the entry transition (pointer still 0, grid byte at its init value) and any mid-hop disagreement silent instead of wrong; an unknown board (a later gateway) stays silent rather than wearing this gateway's names.
- Disc change — and board entry, which lands on B: the arrival line, current disc plus all four directions with what each linked disc holds, from a static session-9 table (grid, letter, board X/Z from the disc objects; neighbours are the coordinate-adjacent discs, which reproduces the verified link list exactly, including I1-J and E-J). E.g. "On C. Left: B, the entry disc. Right: chest on D. Up: nothing. Down: nothing." Standing on the manikin's disc adds "Enemy manikin here."
- Engage prompt: announced on the CHANGE of the focus pointer (0x095CD410) to a prompt disc's record — never level-triggered, because at rest the pointer can lag one engagement behind (session 10 caveat). The text is the prompt's own, verbatim: "Treasure Chest — Contains helpful items. Engage / Cancel" (likewise "Kain — Assist Character. Engage / Cancel" and the Stigma). A consumed piece raises no prompt, so it can never be prompt-announced.
- Scan Mode: while the cursor pointer (0x095CD41C) is not 0xFFFFFFFF, the disc under the cursor is announced as it moves, with the same name/description Scan Mode shows for prompt pieces ("Scan: D. Treasure Chest — Contains helpful items."); the manikin is named (the game shows no info box for it); an empty disc is just its letter ("Scan: C.").
- Board exit (state 20 -> 21): "Level finished. Story Mode chapter select." when the Stigma prompt was the last prompt announced and she has not hopped since, otherwise "Exited the gateway. Story Mode chapter select." (Pause -> Exit Gateway lands in the same shell without finishing the level). The newly unlocked chapter's name ("Chapter 1 - A New Threat") is NOT in a known cell, so the landing is announced generically — no invented text. The exit line keys on the direct 20 -> 21 edge session 9 measured.
- Where Am I / Menu State on the board answer with the arrival line at High priority.

### What it deliberately does not announce, and why

- KP, Gil, gateway name, leader Lv/HP at entry: session 10 found no cells for KP/Gil or board identity, so the entry line omits them rather than inventing values.
- Hop/step sounds, the transition flash sound, menu cursor blips: the game already cues those with sound (the sound-first rule, session 9).
- The board's Customization / Skill / Pause surfaces: not wired here — session 9 mapped their contents by screenshot only; no cursor cells were measured for them.
- Piece consumption: presence is not in a known cell (session 10; the piece-actor area past 0x08B82C10 is the lead). Arrival and Scan lines use the static table, so a looted chest or a recruited Kain is still named there until a presence cell is found. Prompt announcements are unaffected (above).
- Event-result banners (the obtained-item line, "Kain joined as assist") and the first-approach story scene: session 9 lists them as announceables, but no cell marks them; they are not detectable from the session-10 cells and are left for a future session rather than guessed at.

### Build

- scripts/dissidia012-adapter-test.sh (g++ -std=c++17 over the full adapter set + announce.cpp): builds clean — the only warnings are pre-existing ones in other adapters — and the synthetic-RAM suite passes 57/57, including 19 new board checks: entry-transition silence, entry on B, hop to C, board Where Am I, chest prompt open / no-repeat / re-open after cancel, Scan cursor lines (C, chest, manikin, off), standing on the manikin disc, the Stigma prompt, the level-finished exit with no repeat, and fail-closed silence for an unknown grid and for a grid/manager disagreement.
- The changed TU also compiles clean under the device toolchain with build-core.sh's own flags (clang++ -target arm64-apple-ios17.0, iPhoneOS SDK, -stdlib=libc++, -Werror=infinite-recursion).
- scripts/swift-check.sh PASSES: AdapterCommand.swift is really type-checked by swiftc on Linux, and the C ABI mirror (C++ Command enum vs the Swift copy) agrees case-for-case.
- The full device archive (scripts/build-core.sh) is BLOCKED on this machine before compiling anything: "no Genesis Plus GX source at /home/devin/src/genesis-plus-gx" — that source tree is not checked out here. Pre-existing environment gap, unrelated to this change; the archive build needs it restored (bootstrap-deps) before it can run end to end.

## Session 12 — full device build green; branch merged to main (2026-11-17)

Goal: clear session 11's only blocker — restore the missing Genesis Plus GX source tree the way the repo intends, run the full device archive build (scripts/build-core.sh) on this branch, and merge to main if green (Devin's standing merge posture, 2026-11-11).

### Environment restoration

- ~/src held every bootstrap dep except genesis-plus-gx. Restored exactly as scripts/bootstrap-deps.sh's fetch() does for this dep: cloned https://github.com/ekeeke/Genesis-Plus-GX.git into ~/src/genesis-plus-gx and checked out the pinned revision a5834b3df455c49bb97bfaa328c20845831f576b (detached HEAD, as the script leaves it). The full bootstrap script was NOT re-run: the other trees were already present, and its melonds-lua step tracks master — re-running it would have moved an existing checkout for no reason.
- Script modes checked against the standing exec-bit trap: scripts/bootstrap-deps.sh is 100755 in git and on disk. scripts/build-core.sh is 100644 in git, the same as its sourced siblings (core-sources.sh, build-sim.sh); it is invoked through bash / wsl.sh, so no mode repair was needed or made.

### Full device archive build — GREEN

- scripts/build-core.sh on dissidia-012-adapter @ d18c21f exited 0: 1,322 translation units compiled (the GPGX TUs among them, from the restored tree), Mesen 7-Zip symbol localisation, frontend-contract renames in 55 GPGX-side objects, 31 GPGX twin symbols localised against melonDS/Mesen/PPSSPP with no global defined twice, and the archive written: Vendor/libpokecore.a (664,576,000 bytes). The script's closing poke-symbol count reads 57. Full log: /home/devin/build-core-s12.log on the Legion.
- Session 11's TU-level and test evidence stands as recorded there (adapter suite 57/57, swift-check passes); this build adds the whole-archive proof that was blocked.

### Merge gates, as measured before merging

- Clone working tree clean on dissidia-012-adapter; nothing uncommitted to trample.
- Local main == origin/main (bdf8b0b), and main is a strict ancestor of this branch: rev-list main...dissidia-012-adapter = 0 / 76, so the merge is a fast-forward.
- The merge itself, the on-main adapter test run, and the push are recorded in the addendum commit that follows this section on main.

### Session 12 addendum — merge, on-main tests, push

- Merged the same day: main fast-forwarded bdf8b0b..52a8899 (the branch's 77 commits, sessions 1–12 of this document, the dissidia012 adapter, its tests, and the AdapterCommand registration). No merge commit, no conflicts, nothing else in the clone was touched.
- Adapter tests re-run on main after the merge (scripts/dissidia012-adapter-test.sh): 57 passed, 0 failed — the only compiler warnings are the pre-existing ones in other adapters (dq9, alttp, mku, khbbs). Log: /home/devin/dissidia-test-s12.log on the Legion.
- Pushed via the clone's git credentials: origin main verified at 52a889915020dbf2958f29e7aaa441e7e98ba399 by ls-remote. This addendum is the only commit after it.

## Session 13 — Chapter 1: the first real gateway battle (2026-11-17)

Goal: from session 9's exit, start Chapter 1, reach its first gateway board, trigger its first real battle, and map the launch and return; then take session 10's leftovers (KP, Gil, board identity) with the genuine pre/post-battle pairs a Chapter 1 battle finally provides. Work on branch dissidia-012-ch1 (cut from main bdc1164). Script consulted first (Squall_of_SeeD, faqs/62288, [6.1Ch]): Chapter 1 opens with the Cosmos CG and narration, then goes straight INSIDE the Crescent Lake Gateway — party banter, a Lightning manikin, a couple more manikins (chain tutorial), a chest guarded by a different-looking manikin, a Laguna talk, the Stigma; the world-map field segment and the Sage's Path Gateway (Kuja) come after. The play below matched that order beat for beat.

### Route in — the chapter opens on the board, not the field

- From s9_postscene.state (prologue board, on G): Right raises the Stigma prompt (focus = H's record), Cross engages, state 20 -> 21 after ~2,100 frames. Checkpoint ch1_postexit.state.
- The post-gateway scene and the chapter select were crossed with blind Cross advances; one of them answered YES at session 9's "Play this story?" prompt (no clean chapter-select savestate exists — ch1_adv.state is the Cosmos quote card, "You must defeat the god of discord...", inside Chapter 1's opening).
- The opening (CG + Sanctuary narration) runs long in the shell: ~25,000 frames of waits/advances before the board took input, and the hand-over is silent — the board renders with full HUD while the scene finishes. First proof of control was Start opening the Pause menu; d-pad movement worked from then on. Long-wait lesson restated: a rendered board is not yet a controllable board.
- **ctrl state anomaly (adapter-relevant): this board plays at state68 = 21, not 20.** The prologue board (entered from the field) was 20; Chapter 1's board, entered from the chapter shell, keeps 21 through board play, prompts, battles, and menus. The board cells (grid byte, manager pointer, focus, scan cursor) are all live at the session-10 addresses regardless. The shipped board reader gates on state 20 and so stays silent here — its fail-closed design doing its job; a chapter-board signature (state 21 + grid/manager agreement) is the follow-up if chapter boards should be read.

### The Crescent Lake Gateway board — 19 discs

Same addresses as the prologue board (record array 0x08B82A30, objects 0x095CD570 + 0x40*grid, manager 0x095D1230). Lightning enters on grid 20 (-18,-4). Discs (grid: X,Z): 2:(-10,-12), 12:(-10,-8), 16:(6,-8), 20:(-18,-4), 22:(-10,-4), 23:(-6,-4), 25:(2,-4), 26:(6,-4), 30:(-18,0), 31:(-14,0), 32:(-10,0), 33:(-6,0), 34:(-2,0), 35:(2,0), 36:(6,0), 42:(-10,4), 45:(2,4), 52:(-10,8), 55:(2,8). Links are the coordinate-adjacent pairs (the session-11 rule reproduces them exactly): a west column 20-30, the Z=0 row 30-31-32-33-34-35-36, north spurs 32-22-12-2 and 26-16, south spurs 32-42-52 and 35-45-55, and 22-23-33 / 25-35 / 26-36 closing the middle. Movement verified live (20->30->31->32 and back; 32->42 and back).

Pieces (Scan Mode info boxes, cursor cell 0x095CD41C tracking exactly as on the prologue board):
- **22: Capricious Reaper, Lv 2** (battle piece; BATTLE STAGE: Old Chaos Shrine) — the red-glowing piece.
- **31: Fleeting Flash, Lv 2** and **33: Fleeting Flash, Lv 2** (Lightning manikins; same stage).
- **36: Laguna, Assist Character** (prompt piece, not engaged this session).
- **52: Delusory Dragoon, Lv 6** (BATTLE STAGE: Order's Sanctuary) — the chest's guard; it appears on 52 during the chest scene (below). The chest shares its disc; no separate chest info shows while the guard stands.
- **55: Stigma of Chaos** ("Engaging this piece finishes the level.").
- 42 is an empty disc that is a scene trigger: stepping onto it (adjacent to the chest disc) fires the scripted chest scene (Vaan: "Is that...a treasure!? Come on, Light. Let's take it!" — the FAQ's beat verbatim), at the end of which the Dragoon stands on 52. Cross on 42 does nothing; the chest itself was not opened this session.
- Empty: 2, 12, 16, 20, 23, 25, 26, 30, 34, 35, 45 (no info box).

### The battle launch — prompt, tip card, VS card, arena

- Pressing toward a battle piece from an adjacent disc raises its Engage prompt (focus pointer = the piece's record; Lightning stays on her own disc). The prompt shows the piece's name, Lv, and BATTLE STAGE, and the board's top-left shows the KP chance for the battle ("KP: Win battle" for all three pieces tried) — the chance is readable BEFORE engaging.
- Cross (Engage): white flash, then a **tip card** ("Get close to opponents with Free Air Dash! Press R + Triangle..." — Cross = Next), then a **VS card** ("Lv 1 Lightning VS ? Lv 2 — STAGE Old Chaos Shrine — JUDGMENT (blank)" — Cross = Next). The enemy's NAME is hidden on the VS card ("?"); only its Lv and the stage are shown. The name is known from the board prompt/scan, and is revealed in the battle itself.
- state68 stayed 21 through prompt, cards, battle, results, and return — no ctrl-state edge marks the launch in this lineage. The reliable launch markers are the focus pointer leaving the board structs and the board heap being freed/rebuilt (the tutorial-boot HUD addresses at 0x0969D688+ are heap and were garbage in this boot — battle HP/BRV cells are still unbanked, same status as the doc's early "Battle HUD pairing" note).
- In-battle HUD (screenshot, Reaper fight): Lightning BRV 86 / HP ~0 at the killing blow; the Reaper BRV 92 / HP 399 (a Lv 2 manikin's HP pool).

### Battle 1 — Fleeting Flash (31), engaged from 30: WIN

- Fought with the session-8 dumb block (Square, Square, Cross, Triangle on rotation). Won in ~23 rounds of the block. (The block's later presses, after the return, wandered into Customization via the board's Triangle — a mashing artifact, backed out with Circle; the return itself is clean.)
- Return: Lightning back on disc 30 (the disc she engaged from — she never occupied 31), the defeated piece gone (31 is an ordinary hop-through disc afterwards, verified by stepping onto it).
- Deltas: HP 1000 -> 441, Gil 50 -> 64 (+14), KP gauge 0 -> 1 (the "Win battle" chance met: 1 chance x 1 enemy). No chain offered from 31 (the other Flash on 33 is two discs away).

### Battle 2 — Fleeting Flash (33), engaged from 32: WIN, and the chain

- Won in ~20 rounds of the same block. HP 441 -> 48, Gil 64 -> 78 (+14 again).
- **The win's KP was HELD, not paid**: the gauge stayed 1 while a chain was on offer. Sequence on return: the chain tutorial message ("Entering a battle with multiple enemies near you creates a chain leading to more battles.") — then the chained enemy presents itself: the Reaper's Engage prompt auto-opened (focus = 22's record) without any movement input.
- Cancelling: Circle at the chain prompt opens "**Cancel chain? YES / NO**" with the cursor on **NO** (the safe default — a Cross mash would have fought at 48 HP). Left + Cross (YES) closed the chain and **the held KP paid out: gauge 1 -> 2**. Model: a win's KP is held while a chain offer is open and banks when the chain closes (cancel or completion); the manual's (Chances Met) x (Enemies Defeated) is the chain-total formula, applied at close.

### The loss — Capricious Reaper (22), engaged from 32 at HP 48, idled: DEFEAT

- The Reaper killed her quickly (within ~2,600 idle frames). Landing: a defeat menu over her fallen figure — **Retry / Flee** (cursor starts on Retry).
  - Retry footer: "Start over from status at beginning of battle. Resets Summonstone status and EXP."
  - Flee footer: "Quit battle and return to gateway. Start over from status at beginning of battle except KO."
- Flee chosen: back on the board on disc 32 (the engagement disc), **HP restored to 48** (her HP at engagement — the KO is undone, damage from before the battle is kept), KP gauge still 2, Gil still 78, the Reaper still on 22. A loss in an unchained battle costs nothing banked; Retry (restart the battle at its opening state) was identified but not exercised.

### Session 10's leftovers — KP and Gil FOUND; board identity excluded

- **Gil: FOUND.** The battle-1 diff (pre pair on 30 vs post pair on 30) left exactly three u32 cells at 50 -> 64 in the whole image; all three tracked the second win in lockstep (64 -> 78, read live): **0x08BBFA00** and **0x08BBFA28** (0x28 apart, same record family in the static region — the adapter's cells) and a heap copy at 0x09E50D80.
- **KP (the gateway gauge): FOUND.** The four-state signature — pre-battle-1 = 0, post-battle-1 = 1, post-battle-2 = 1 (held), post-chain-cancel = 2 — leaves exactly three cells in the image: **0x0972B8B8, 0x0972B94C, 0x09AE3D64**, verified live at 2 after the cancel. Caveats: all three are heap addresses in this lineage (a static holder chain is still owed before the adapter can read KP on a fresh boot), and the two 0x0972Bxxx cells sit in a structure battles reuse (garbage mid-battle; they read correctly on the board). The earlier 0->1-only candidate set (0x08B82278 etc.) is a different counter that stayed 1 through the second win and the cancel — not the gauge. The bottom-right KP readout (banked KP, still 0) is a different value from the gauge and was not hunted; it only moves when a gateway is finished.
- **Board identity: NOT a cell in the board structures.** All three session-10 constant leads are identical on the second board (header 0x08B82A1C = 1, 0x08B82A28 = 31, manager +0x00 = 596), and the table header (0x08B82A00-0x08B82A30) has ZERO stable diffs between the two boards. Cross-board diffs in the manager/structs pages are the disc sets and per-board content (asset/sound tables), not an ID word. Boards are told apart by their disc-table fingerprint (the exists pattern + disc coordinates), which the adapter's per-board tables already amount to.
- Board HP: the 1000 -> 441 transition matched NO stable u32/u16 cell across the pairs — the board's HP readout is presumably recomputed from the character record at draw time. Not found; the Gil record's neighbourhood (0x08BBFA28's record also moved a word 0 -> 10 across battle 1 — an EXP-total candidate, single observation) is the lead.

### Adapter announceables (design notes; battle reading itself is a later session)

- **At the prompt** (the adapter's richest moment): enemy name + Lv + battle stage + the KP chance line, verbatim ("Fleeting Flash, Lv 2. Battle stage: Old Chaos Shrine. KP chance: Win battle. Engage / Cancel"). All silent text in-game.
- **At launch**: two Cross-gated cards — announce "tip card" then the VS card as "Unknown opponent, Lv 2. Stage: Old Chaos Shrine" (the name is genuinely hidden until the battle; do not invent it — the prompt already named it).
- **At return from a win**: the deltas (KP gauge, Gil, HP) — the results are otherwise sound-cued. When a chain offers, announce the chained enemy's name/Lv and that the KP is held until the chain closes; at the Cancel-chain dialog, announce that NO is the default.
- **At a loss**: the Retry/Flee menu with both footers verbatim, and on Flee the landing (disc, restored HP).
- **Not to speak**: the hop sounds, the white-flash transition sound, battle-internal cues (the sound-first rule).

### Verdict

- Chapter 1's first gateway (Crescent Lake) is reached and its board mapped to the first enemy and beyond: 19 discs, five pieces identified, the Stigma located. The battle launch (prompt -> tip card -> VS card -> arena) and both returns are mapped: a win (piece consumed, deltas paid, possible chain offer with held KP) and a loss (Retry/Flee; Flee restores the engagement state except the KO).
- Session 10's leftovers: Gil and the KP gauge are found (cells above); board identity is excluded from the board structures (use the disc-table fingerprint); board HP remains unfound with the Gil-record neighbourhood as its lead.
- Left in Chapter 1 (next session): the Delusory Dragoon (Lv 6) and its chest on 52, the Laguna assist on 36, the Stigma exit at 55, then the field segment and the Sage's Path Gateway with the Kuja battle (per the FAQ).
- States (all ~/d12s): ch1_postexit, ch1_adv, ch1_board_start, ch1_ctrl, ch1_scan, ch1_pre2 (prompt on 33), ch1_postbattle, ch1_postbattle2b, ch1_chaincancelled, ch1_chestscene, ch1_prereaper, ch1_lossbattle, ch1_loss (Retry/Flee menu), ch1_fled. Dump pairs: s13_prebattle_a/b, s13_postbattle_a/b, s13_post2c_a/b, s13_cc_a/b, plus s13_board_a/b and the prologue pair s13_pre_a/b used for the identity diff.

## Session 14 — state-21 boards and KP/Gil in the reader; Crescent Lake finished (2026-11-17)

### Part A — the KP holder chain is found (offline sweep + live proof)

Session 13 left the KP gateway gauge at three heap cells (0x0972B8B8, 0x0972B94C,
0x09AE3D64) with no static route to them. A sweep over all 124 RAM dumps in ~/d12s
settles it:

  KP CHAIN: base = u32 at 0x089D65A8 (a slot in the game's static pointer table);
  the gauge is the u32 at base + 0x260. In every dump the slot holds 0x09AE3B04,
  and base+0x260 IS session 13's third cell (0x09AE3D64). The twin slot 0x089D85BC
  holds the same base in every dump. (The struct at base+0x228 is a sub-struct
  whose +0x38 lands on the same cell — that is why session 13's chain candidates
  kept pointing near it.)
  Verified against the gauge signature everywhere: prologue boards 0; Chapter 1
  pre-battle 0, post-battle-1 1, post-battle-2 held 1, post-chain-cancel 2,
  post-Flee 2; the independent 100%-clear lineage dumps also resolve (read 0).
  Battle dumps read sanely too (0/1/2 as appropriate). Gil's pair 0x08BBFA00 /
  0x08BBFA28 agree in every board-live dump.
  REJECTED: *(0x08B41CC0)+0x38 (the slot holds garbage 0x90840005 in the 100%
  lineage — a one-lineage coincidence); *(0x08B8C2EC) -> 0x0972B89C (those slots
  hold float data in most dumps — transient).
  KNOWN GARBAGE CASE: the s10_boardstate dumps (a mid-rebuild snapshot) read
  KP 0xFFFFFF9E through the chain and disagreeing Gil cells, so the reader
  sanity-gates both: KP value <= 9999 with the twin slot agreeing (or zero),
  Gil only when both cells agree. Insane readings never move a baseline.
  LIVE (fresh drv, fp-save-s8): ch1_fled.state -> state 21, grid 32, manager
  agrees, 19 discs, Gil 78/78, KP via chain = 2. ch1_postexit.state (chapter
  shell) -> state 21 but manager 0xFFFFFFFF: the gates fail, reader silent.

### Part A — reader changes (Core/dissidia012_adapter.cpp, this branch)

- BOARD IDENTIFICATION BY FINGERPRINT (session 13's prescription): the set of
  existing disc records (u32 at 0x08B82A30 + 8*grid == 1) must equal a known
  board's disc set exactly, and the entry and Stigma discs' live object
  coordinates (objects at 0x095CD570 + 0x40*grid, X float +0x00, Z +0x08) must
  match the table — a same-shaped later board with different geometry cannot
  inherit piece names. Known boards: the prologue's Gateway to Departure (14
  discs, state 20) and Chapter 1's Crescent Lake Gateway (19 discs, state 21;
  table below). The current grid must also be one of the board's discs.
- UNKNOWN BOARDS GO GENERIC, never prologue-flavoured: arrival/scan lines name
  discs by grid id ("On disc 40. ... Right: disc 41, empty.") with neighbours
  computed from the live disc objects' coordinates; piece names and prompts are
  not spoken (piece presence is not in a cell, so a name would be invented).
- ACTIVATION is state 20 OR 21 with the same agreement gates as before
  (manager pointer == record implied by the grid byte). The chapter-select
  shell (state 21, manager 0xFFFFFFFF, stale grid byte) stays silent — proven
  below. Battles inside a state-21 board break the cells while the state holds
  at 21: the reader stays silent and KEEPS its baselines, so the battle's
  KP/Gil deltas announce when board play resumes.
- EXITS: a board that went live at 20 still exits on the 20 -> 21 edge with the
  shipped lines ("Level finished. Story Mode chapter select." / "Exited the
  gateway. Story Mode chapter select."). A board live at 21 exits when the
  state leaves 20/21 (measured landing sequence 21 -> 0 -> 3): "Level
  finished." if the Stigma prompt was last, else "Exited the gateway." —
  the landing (the chapter's field segment) is not named, as its name is not
  in a cell. Pause -> Exit Gateway from a state-21 board keeps state 21 in the
  shell and is NOT detected; it stays silent (documented limitation).
- KP/GIL ANNOUNCEMENTS: change-triggered only, Normal priority, group "board":
  "KP: <n>." / "Gil: <n>." Baselines are taken silently when a board is
  identified. Neither value has a game sound cue, so speaking changes follows
  the sound-first rule; nothing is spoken per-frame or on entry.
- Crescent Lake table (grid: coords, piece): 2:(-10,-12) -; 12:(-10,-8) -;
  16:(6,-8) -; 20:(-18,-4) ENTRY; 22:(-10,-4) Capricious Reaper Lv 2 (Old Chaos
  Shrine); 23:(-6,-4) -; 25:(2,-4) -; 26:(6,-4) -; 30:(-18,0) -; 31:(-14,0)
  Fleeting Flash Lv 2 (Old Chaos Shrine); 32:(-10,0) -; 33:(-6,0) Fleeting
  Flash Lv 2; 34:(-2,0) -; 35:(2,0) -; 36:(6,0) Laguna (Assist Character);
  42:(-10,4) - (the chest-scene trigger disc); 45:(2,4) -; 52:(-10,8) Delusory
  Dragoon Lv 6 (Order's Sanctuary — the piece appears only after the scene on
  42; the table names it because it is the piece the prompt raises there);
  55:(2,8) Stigma of Chaos. Battle prompts add the board's own KP-chance line
  ("KP chance: Win battle", shown at the prompt's top-left); scan lines carry
  name/Lv/stage only, as Scan Mode shows.

### Part A — tests and live proof

- scripts/dissidia012-adapter-test.sh: 82 passed, 0 failed (the 57 shipped
  checks all green + 25 new: KP/Gil changes and their garbage/mismatch
  silences, the state-21 Crescent Lake board end to end — entry, neighbours,
  Flash prompt, battle break with deltas on return, Laguna scan, Stigma exit
  to "Level finished." — and a generic unknown board). Two test-model
  corrections, both toward the measurements: the board section now sets the
  prologue disc-exists fingerprint the identifier requires, and the shell
  frame after the prologue exit now carries the shell's measured manager
  value 0xFFFFFFFF instead of leaving the board's cells agreeing.
- The changed TU compiles clean under the device toolchain with build-core.sh's
  own flags (clang++ -target arm64-apple-ios17.0, iPhoneOS SDK, -stdlib=libc++,
  -Werror=infinite-recursion). The full build-core was not run (not needed on
  this branch yet).
- LIVE PROOF over real RAM dumps (harness ~/d12s/oga-dissidia012-proof, source
  ~/d12s/dissidia012_live_proof_main.cpp; dumps fed in sequence, 3 frames each):
  s13_ctrl_a.bin (Crescent Lake entry, genuine lineage) -> "On disc 20, the
  entry disc. Left: nothing. Right: nothing. Up: nothing. Down: disc 30,
  empty." s13_cc_a.bin (post-chain, disc 32) -> the disc-32 arrival line, the
  Capricious Reaper prompt (its chain-offer prompt was up in that dump), then
  "KP: 2." and "Gil: 78." — the exact session-13 deltas, spoken on change.
  s14_shell.bin (chapter-select shell: state 21, manager 0xFFFFFFFF, grid byte
  stale at 26) -> 0 lines. genuine_board3.bin (prologue board, state 20) ->
  "On A. Left: nothing. Right: B, the entry disc. Up: nothing. Down:
  nothing."; feeding the Chapter 1 dump after it closes the state-20 visit
  with "Exited the gateway. Story Mode chapter select." and then announces
  the Crescent Lake entry — the cross-board sequence behaves as designed.

### Part B — Crescent Lake finished (genuine lineage, from ch1_fled.state)

Prep, measured on the Customization screens: the Moogle Shop is CLOSED in this
gateway ("The shop is not open yet."), so no healing exists here. Header: EXP
TOTAL 3789 / NEXT 1211, HP 48/1000, CP 225/330, BRV 95, ATK 10, DEF 12, LUK 10,
LUCKY! 5%, BATTLES 10. The prologue chest's Broadsword was equipped via
Equipment -> Optimize (ATK 10 -> 14, preview and result screenshot-verified).
The Assist was ALREADY Laguna Loire before any board engage (BRV: Machine Gun,
Shotgun; HP: Satellite Laser, Ragnarok Blade) — the game set him at the
chapter's start.

(a) THE CHEST AND ITS GUARD — fought three times, chest not opened. Stepping
on 42 fires the scripted chest scene; the Delusory Dragoon then stands on 52.
Its prompt (focus = 52's record): "Delusory Dragoon, Lv 6 — BATTLE STAGE:
Order's Sanctuary — Engage / Cancel", KP chance "Win battle". The launch has
THREE Cross-gated cards on this board: tip card, VS card, and an Opponent Info
card (new this session): HP 641, CP 336, BRV 158 (124+34), ATK 16, DEF 10
(17-7), LUK 13; equipment Cracked Shield / Charred Helm / Rusted Armor (Shield
DEF-8, Equip Level 1); "Bravery Bonus: Lv +5". At 48 HP with the session-8
dumb block rotation she was KO'd in ~8 rounds; a mashed Cross on the defeat
menu fired RETRY (the cursor starts on Retry). RETRY RESET, VERIFIED at the
next attempt's arena start: her HP back to 48 (the engagement value), BRV back
to base (hers 95, the Dragoon's 158), the Dragoon's HP full at 641. The Retry
footer reads verbatim: "Start over from status at beginning of battle. Resets
Summonstone status and EXP." Attempt 2 (block alternating in Circle for HP
attacks): KO in ~9 rounds. Attempt 3 (a Cross-free rotation so the menu could
not be mash-confirmed): KO within 2 rounds. Then a deliberate Flee (Down, Cross;
footer verbatim: "Quit battle and return to gateway. Start over from status at
beginning of battle except KO.") -> back on disc 42, HP 48, KP gauge 2, Gil 78,
the Dragoon still on 52. Verdict: at 48 HP, with no healing in this gateway,
the Lv 6 guard (641 HP; a single HP attack deals far more than 48) is not
beatable by blind block play, and the chest's contents were not recorded from
play. GUIDE DATA (not play): link3895's FAQ/Walkthrough (GameFAQs archive)
labels the board's chest "C- Buckler" — the chest holds a Buckler; the same
guide confirms the Dragoon appears only after stepping on the square above it
(disc 42), matching session 13.

(b) LAGUNA (36) — engaged; there is no recruitment event to record. The prompt
(from 35; focus = 36's record): "Laguna — Assist Character — Engage / Cancel".
Engaging plays a two-line banter scene — Laguna: "It's not that I have a bad
sense of direction. I'm just interested in all directions." / Lightning:
"Taking an interest is fine, but how do you explain getting lost after looking
around?" — and the piece is NOT consumed: he still stands on 36 and the prompt
reopens on re-approach. He was already the set assist before the engage (see
Prep), so the recruitment belongs to the chapter's story setup, not the board.

(c) STIGMA EXIT (55) — done. Prompt from 45 (focus = 55's record) -> Engage ->
measured transition: state 21 with the manager still on 45's record for ~300
frames, then state 0 with the manager at 0xFFFFFFFF (board unloaded), then
state 3 (field dialogue) with the player object live at (314.20, 0.19, 269.40)
— Chapter 1's field segment, in its opening dialogue. This sequence is what
the reader's state-21 exit rule keys on.

(d) THE FIELD SEGMENT — entered and surveyed; the Sage's Path Gateway was NOT
entered. The opening dialogue took 5 Cross advances to reach state 2; the
scene's tail auto-walked her to (323.54, -2.15, 280.03). Saved as
ch1_s14_field_start.state. OBSERVED: the field HUD shows HP restored to 1000
and KP 2 — leaving the gateway restored her HP and banked the gauge (a Dragoon
rematch at full HP would mean re-entering the finished gateway; not attempted —
the level was already finished and banked). The Map screen is ctrl state 14
(new value for the notes). This map's gate table (gateMgr 0x095D7110): gate 13
is STATE 5 with timer 122 — the Crescent Lake gate just exited, spent, the same
pattern as the prologue's gate 14; five gates carry interaction points
(B-word high half): 11 -> 7, 12 -> 13, 15 -> 4, 16 -> 5, 19 -> 10. Those point
indices do NOT resolve through the pint list (no records carry those ids on
this map; list 0 at 0x095D2F40 holds 196 records, the drawn ones are scenery —
the two stacked kind-5 clusters at (320,158) and (236,184) are mountain and
grassland dressing, screenshot-checked). The point accessor is decoded from
the binary: FUN_08897DEC(worldObj, i) returns the float at
*(worldObj+0x6C) + i*0x3C + 0x24 (X), FUN_08897E48 the one at +0x30 (Z), with
the count at *(*(worldObj+0x58)); worldObj = *(FUN_088992B0() + 0x210). But
the chain did not resolve live: the manager slot behind FUN_088992B0
(0x09AEF8D4) holds 1 in the field state, not a pointer — the function is a
lazy-init with one more hop than the decode followed. A teleport survey of the
ten most plausible drawn-record sites, four facing taps + Cross at each,
produced no gate reaction (state stayed 2 throughout). NEXT ROUTE for the
Sage's Path entry: resolve FUN_088992B0's return properly (or breakpoint the
scan's point fetch live) and read points 4/5/7/10/13 directly, then repeat the
session-8 threshold pose at the Sage's Path point. Pre-gateway and on-board
states for Sage's Path are therefore NOT saved this session; the field-start
state is.

States saved this session (~/d12s): ch1_s14_shop.state, ch1_s14_equip.state,
ch1_s14_opt.state, ch1_s14_equipdone.state, ch1_s14_assist.state,
ch1_s14_on42.state, ch1_s14_predragoon.state, ch1_s14_battle.state (at the VS
card), ch1_s14_fight.state, ch1_s14_fight2.state, ch1_s14_arena2.state (the
Retry-reset proof), ch1_s14_a2.state, ch1_s14_a3.state, ch1_s14_fled2.state
(post-Flee on 42), ch1_s14_prelaguna.state, ch1_s14_laguna.state,
ch1_s14_laguna2.state, ch1_s14_prestigma.state, ch1_s14_exit_land.state (field,
in the opening dialogue), ch1_s14_field_start.state (field control). RAM dump
s14_field.bin (field state) and s14_shell.bin (chapter shell) alongside.

Carried forward: piece-presence cells are still unknown (consumed pieces are
still named in arrival/scan lines on known boards); board HP is still not in a
cell (the field/board HUD HP was read from screenshots only); the Sage's Path
board is unmapped pending the point-table resolution above.

## Session 15 — Sage's Path entered; the entry recipe corrected; Crescent Lake re-entry refused (2026-11-17)

### The point-table chain, resolved (session 14's loose end)

- FUN_088992B0 disassembled: it returns the static manager struct at
  0x09AEF8D8. The word at 0x09AEF8D4 that session 14 read as a broken pointer
  slot is the lazy-init DONE flag (the function sets it to 1 and runs two
  init calls on first use). There is no missing hop — the manager was never
  behind a pointer.
- FUN_08899300 (the tester's world-object getter): idx = *(mgr+0x210); on
  this map idx = 0xFFFFFFFF (-1), and the branches resolve to worldObj = mgr
  itself (0x09AEF8D8). (idx == 0 would give mgr+0x108.)
- Accessors confirmed against the binary: FUN_08897DEC returns the float
  pointer *(worldObj+0x6C) + i*0x3C + 0x24; FUN_08897E48 the one at +0x30.
  But the tester loads the point's POSITION as the three floats at
  +0x24/+0x28/+0x2C (X, Y, Z) — the +0x30 triple is a second vector (mostly
  (0, small, 0); an aux/slope value), not the Z. Session 14's pairing was
  half wrong: X at +0x24 yes, Z at +0x2C (not +0x30).
- Live read on the Chapter 1 field (from ch1_s14_field_start.state): table
  at 0x095E61C0, count 18. Gate -> point (B[0] high half): 11->7, 12->13,
  13->14, 14->9, 15->4, 16->5, 17->6, 18->8, 19->10, 20->11, 21->12.
  Validation: gate 13's point 14 = (315.09, 0.68, 270.40), ~1.3 units from
  where the Crescent Lake Stigma exit deposited her (314.20, 0.19, 269.40)
  — the table is the gateway anchor table.
- The five tester-reachable gates on this map are the ones with
  lh(ptrA+4) >= 0: 11 (pt 7: 305.04, 0.81, 166.59), 12 (pt 13: 319.33, 0.94,
  193.64), 15 (pt 4: 220.44, -0.49, 110.99), 16 (pt 5: 210.56, 2.63, 152.92),
  19 (pt 10: 154.08, 6.72, 229.42). The other family (lh(A+4) = -1: gates
  13, 14, 17, 18, 20, 21) can only be tested via phase B, which additionally
  requires lh(ptrA+0x10) >= 0 — it is -1 for every record on this map, so
  that family is inert to the tester here.

### The entry recipe, corrected (the session 8/14 model revised)

- The tester (FUN_08B43ED4) phase A runs only while the player is near a
  candidate: evaluations arrive in bursts after movement/teleports, and a
  perfectly still player stops being evaluated within ~100 frames.
  Reaching the virtual call (bp 0x08B440D8, s0 = the gate's record) proves
  distance < 4.0, ctrl state 2, and the facing test all pass.
- The virtual call's predicate is (*(0x08C15C50+8) & 4) != 0 (masks at
  0x08B444F0/F4 = 4/0, via FUN_08871D18; busy bit via FUN_08873084). That
  flag word read 0 in EVERY sample this session — including throughout a
  faithful reproduction of session 8's prologue entry — yet the prologue
  gate still opened. The vcall-true path is therefore NOT how either entry
  happened; session 8's "real-time-cycled flag" reading is superseded.
- What actually fires entry: Cross, pressed while the tester has a live
  candidate. Prologue reproduction (genuine_postkain.state, session 8's
  sequence): teleport (76.5, 0, 70), stick-up tap -> threshold (76.53, 0,
  70.17), 17 evaluations, press Cross -> state 5 -> state 20. On the
  Chapter 1 field the same shape worked twice from ch1_s15_pregate.state:
  settle in pose (evaluations flowing), Cross, Cross -> state 5 -> board.
  The first Cross appears to be absorbed (candidate registration, or the
  press landing between evaluation bursts); the second fires. Budget two.
- Practical recipe: teleport to ~2-3.5 units from the anchor on the
  approach side, write facing toward the anchor (player+0x90/+0x98) or walk
  in head-on, let evaluations flow ~60-90 frames (the camera-settle wait
  doubles as this), then Cross up to a few times. Standing exactly on the
  anchor still fails (session 8); ~1.8-3.4 units out works (3.40 measured
  here — the teleport landed on the collision apron's edge and taps did not
  move her closer). Stick directions are camera-relative and the camera
  swings behind the written facing with a lag — write facing, wait, then
  tap "forward" if walking is needed.

### Sage's Path Gateway — ENTERED (gate 12, point 13)

- From ch1_s15_pregate.state (saved in pose at (319.49, 0.70, 197.04), 3.40
  units north of the anchor, facing written south): Cross x2 -> state 5 ->
  state 20, banner "Sage's Path Gateway" (screenshot-verified). Board
  entry: Lightning on disc 20, Lv 1 HP 1000, Gil 78, banked KP 2
  (bottom-right readout), gateway KP gauge 0.
- The board opens with an auto-played party scene a few hundred frames
  after the banner (Laguna: "This is really annoying! How ya doing,
  Light?" + a few more lines; ~5 Cross advances). It is NOT disc-triggered:
  it fired during a pure wait in one run and mid-walk in another (session
  15's first scan attempt walked into it blind — the walk's later
  screenshots are all the dialogue banner). Board control is live only
  after it.
- States: ch1_s15_pregate.state (field, in pose), ch1_s15_board.state
  (board entry, scene pending), ch1_s15_board_clear.state (scene done,
  control live). RAM dump s15_board.bin; scan screenshots s15_sc_<grid>.png.

### The Sage's Path board — 22 discs (full Scan Mode pass, every disc cursor-verified)

- 20: (-18,-4) ENTRY; 21: (-14,-4) -; 22: (-10,-4) -;
  32: (-10,0) Fleeting Flash Lv 6 (Order's Sanctuary);
  12: (-10,-8) Idle Sky Pirate Lv 6 (Order's Sanctuary);
  2: (-10,-12) Treasure Chest; 42: (-10,4) Treasure Chest;
  23: (-6,-4) -; 33: (-6,0) -;
  3: (-6,-12) Delusory Dragoon Lv 6 (Order's Sanctuary);
  13: (-6,-8) -; 24: (-2,-4) -; 34: (-2,0) -; 44: (-2,4) -;
  14: (-2,-8) -; 4: (-2,-12) -;
  25: (2,-4) Delusory Dragoon Lv 2 (Lunar Subterrane);
  35: (2,0) Tifa (Assist Character);
  26: (6,-4) Fleeting Flash Lv 2 (Lunar Subterrane);
  16: (6,-8) Treasure Chest; 6: (6,-12) -;
  7: (10,-12) ??? — "Engage this piece to find its true form."
- Links are the coordinate-adjacent pairs (the session-11 rule reproduces
  them); the scan cursor walked every link chain from the entry.
- NO Stigma of Chaos is on the board as entered. The ??? piece on 7 (the
  board's NE dead end) is the presumptive boss gate — the FAQ puts a Kuja
  battle in this gateway, and the Stigma presumably appears after it. Not
  engaged this session (brief: map only, no battles).

### Crescent Lake chest — re-entry refused by the tester (guide data stands)

- Gate 13 / pt 14 (315.09, 0.68, 270.40) with the proven recipe (pose 1.90
  units north, facing written, 5 Crosses): ZERO tester evaluations reached
  the virtual call. Gate 13's record belongs to the inert family
  (lh(A+4) = -1 closes phase A; lh(A+0x10) = -1 closes phase B), so a
  finished gateway does not re-open from the field in this lineage. Per
  the brief, not pursued further (a chapter-select route, if one exists,
  is a different mechanism). The chest's contents remain GUIDE DATA ONLY:
  a Buckler (link3895), still unverified in play.

### Adapter note

- Sage's Path is a third board fingerprint: 22 discs, state 20
  (field-entered), entry disc 20 at (-18,-4) — the same entry coords as
  Crescent Lake but a different disc set, so the session-14 fingerprint
  rule (exists-set + entry/Stigma live coords) needs care for boards whose
  Stigma is not yet present; the exists-set alone separates all three
  known boards (14 / 19 / 22 discs). Adding the Sage's Path table to the
  reader is the follow-up session's work; this session's Part B merges
  sessions 13-14's reader as-is.

### Part B — build and merge (session 15)

- scripts/build-core.sh on dissidia-012-ch1 @ 8ca7d02: exit 0 — 1,322 TUs
  compiled, Vendor/libpokecore.a written (664,598,240 bytes); log at
  /home/devin/build-core-s15.log on the Legion.
- scripts/dissidia012-adapter-test.sh: 82 passed, 0 failed on the branch,
  and re-run 82/82 on main after the merge.
- Merge gates (session 12's): working tree clean; local main == origin/main
  (bdc1164); merge-base == bdc1164 (main a strict ancestor, branch 3 ahead /
  0 behind). Pure fast-forward bdc1164..8ca7d02, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main = 8ca7d02.
  This addendum commit follows on main.

## Session 16 — the reader learns Sage's Path; Tifa recruited; the ??? is Kuja, Lv 8, and he holds (2026-11-17)

### Part A — Sage's Path in the reader (Core/dissidia012_adapter.cpp, this branch)

- THIRD FINGERPRINT: kSagesDiscs (22 discs, session 15's verified Scan map) and
  kSagesDef { entry 20, stigmaGrid -1 }. IdentifyBoard applies the Stigma
  coordinate check only when stigmaGrid >= 0 — this board has no Stigma at
  entry, so identification rests on the disc set plus the entry disc's
  coordinates, exactly session 15's care point. Unknown-board generic fallback
  and every fail-closed gate are unchanged.
- New piece entries: Delusory Dragoon Lv 2 and Fleeting Flash Lv 2 (Lunar
  Subterrane), Fleeting Flash Lv 6 and Idle Sky Pirate Lv 6 (Order's
  Sanctuary), Tifa (Assist Character), and ??? ("Engage this piece to find its
  true form"). The Lv 6 Dragoon entry is shared with Crescent Lake's.
- Tests: scripts/dissidia012-adapter-test.sh — 95 passed, 0 failed (the 82
  shipped checks all green + 13 new Sage's Path checks: entry, arrivals,
  prompts, scans). The TU compiles clean under the device flags (clang++
  arm64-apple-ios17.0, iPhoneOS SDK).
- LIVE PROOF (harness ~/d12s/oga-dissidia012-proof-s16 rebuilt from this
  branch; real dumps fed in sequence): s16_board_entry.bin -> the disc-20
  entry line; s16_prompt_pirate.bin -> disc-22 arrival + the Pirate prompt;
  s16_tifa_prompt.bin -> disc-34 arrival + "Tifa — Assist Character. Engage /
  Cancel"; s16_postflash.bin -> disc-25 arrival + "KP: 2." / "Gil: 106.";
  s16_chest_prompt.bin -> disc-26 arrival + the chest prompt;
  s16_unknown_prompt.bin -> disc-6 arrival + "??? — Engage this piece to find
  its true form. Engage / Cancel"; s16_unk_fled.bin (after the lost boss
  fight) -> disc-6 arrival, the board still identified. Session 15's
  s15_board.bin is a mid-load dump (manager 0, 59 exists flags set) and the
  reader correctly stays silent on it.
- Every Sage's Path prompt was also screenshot-verified against the game's
  own text during Part B: all six battle pieces show "KP: Win battle" at the
  prompt, as the reader's prompt lines say.

### The board's true shape — the NE wing hangs off the two Lv 2 manikins

- NO long links: pressing toward disc 16 from 14, and toward disc 6 from 4,
  does nothing at all (no prompt, no hop). The reader's ±4 coordinate
  adjacency is the game's own rule on this board.
- Consequence: discs 16, 6 and 7 connect to the rest of the board only
  through 26, and 26 only through 25 (or 16). The two Lv 2 manikins — the
  Dragoon on 25 and the Flash on 26 — are the gate to the ???; they are path
  fights, not optional ones. Chests 2 and 42 sit behind Lv 6 pieces (12/3
  guard 2 on both sides; 32 guards 42) and were not opened — the brief's "no
  unnecessary Lv 6 fights" — so their contents remain unmeasured.

### Part B — play (genuine lineage, from ch1_s15_board_clear.state: Lv 1, HP 1000, Gil 78, gauge KP 0, banked KP 2)

- ENTRY SCENE TIMING (harness lesson): the Laguna banter scene fires a few
  hundred frames after entry and eats d-pad input until advanced (~3–5
  Crosses). In the board_clear lineage it had NOT yet fired when session 15
  saved the state, so it ambushed this session's first walks mid-route.
  Burning Crosses until a test hop answers is the reliable clear.
- TIFA (35): engaging plays her warning ("I have a bad feeling, Light. That
  Chaos insignia...") and RECRUITS her — Customization → Assist afterwards
  shows Tifa Lockhart as the set assist (BRV: Beat Rush, Falcon's Dive; HP:
  Meteodrive, Rolling Blaze), replacing Laguna, who was the chapter-start
  default. The piece is not consumed; the prompt reopens on re-approach,
  exactly Laguna's pattern on Crescent Lake.
- DELUSORY DRAGOON Lv 2 (25): WON with the session-13 dumb block. Deltas:
  KP gauge 0 -> 1, Gil 78 -> 92 (+14), and Lightning LEVELED UP (Lv 1 -> 2;
  the Abilities screen then reads CP 225/331). A post-battle scene plays —
  Kuja, watching: "This isn't the time for pleasantries! If you don't dodge
  correctly, you're a goner!" No chain was offered after this win (the scene
  took precedence and the KP paid at once — contrast session 13's held KP).
  Her HP afterwards: 863.
- FLEETING FLASH Lv 2 (26): WON (payment at ~press 150: KP 1 -> 2, Gil
  92 -> 106, +14 again). HP afterwards: 778. No scene followed.
- CHEST (16), reached from 26 once the Flash fell: the contents are MEASURED
  from the game's own text — the **Soul of Destruction** accessory ("'Soul of
  Destruction' is a breakable accessory, but boosts your starting ATK";
  Lightning: "An accessory... Guess I'll wear it."). The chest is consumed;
  16 and 6 become ordinary hop-through discs.
- THE ??? (7) IS KUJA, Lv 8 (stage: Lunar Subterrane). A pre-battle scene
  has Kefka gloating ("You still have more manikins waiting for a chance to
  play!"), and the VS card NAMES him — "Lv 2 Lightning VS Kuja, Lv 8" —
  unlike the manikins, whose names the VS card hides. In battle he is a
  keep-away mage (Seraphic Star among his HP attacks). Attempt 1 (dumb
  block, 630 presses) ground his HP bar to roughly half while he built BRV
  past 1,700 and broke her repeatedly; the fight was then idled to the KO to
  map the loss cleanly. Defeat menu: Retry / Flee (footers as session 13);
  FLEE -> back on disc 6 with HP restored to 778 (the engagement value), KP
  and Gil untouched, the ??? still standing. Attempt 2 (a BRV-cycle rotation
  ○○○□) never entered the arena — the rotation has no Cross to advance the
  tip/VS cards (operator error, recorded). Attempt 3 entered with that
  rotation and was KO'd inside 90 presses. Verdict: at Lv 2, blind block
  play does not beat Kuja Lv 8 this session. The win, the Stigma's
  appearance, and the chapter exit are therefore UNMEASURED; the gateway
  stands open at ch1_s16_unk_fled.state (on disc 6, engagement HP 778) for a
  future session — a level grind or guard-aware play are the open routes.
- Reader note (unmeasured, deliberately not coded): if Kuja falls in a later
  session and a Stigma appears as a piece on an existing disc (the Crescent
  Lake Dragoon pattern), identification holds — the disc set is unchanged —
  but the arrival line would still name that disc's table piece, and with
  stigmaGrid -1 no "Level finished." would speak on the exit. Both are the
  known static-table limitation; they get fixed from that session's dumps,
  not guessed now.

States (~/d12s): ch1_s16_pretifa, ch1_s16_tifa, ch1_s16_predragoon2,
ch1_s16_clean_board, ch1_s16_preflash, ch1_s16_postflash, ch1_s16_prechest16,
ch1_s16_chest16, ch1_s16_pre_unknown, ch1_s16_pre_unknown_prompt,
ch1_s16_post_unknown_battle (mid-fight, attempt 1), ch1_s16_unk_fight /
ch1_s16_unk2_fight / ch1_s16_unk3_fight (attempt segments),
ch1_s16_unk_fled (post-Flee, on disc 6). RAM dumps: s16_board_entry,
s16_tifa_prompt, s16_prompt_pirate, s16_postflash, s16_chest_prompt,
s16_unknown_prompt, s16_unk_fled (all .bin).

### Session 16 addendum — build and merge

- scripts/build-core.sh on dissidia-012-sagespath @ b50a792: exit 0 — 1,322
  TUs compiled, Vendor/libpokecore.a written (664,599,608 bytes); log at
  /home/devin/build-core-s16.log on the Legion.
- scripts/dissidia012-adapter-test.sh: 95 passed, 0 failed on the branch,
  and re-run 95/95 on main after the merge.
- Merge gates (session 12's): working tree clean; local main == origin/main
  (e7b1a08); merge-base == e7b1a08 (main a strict ancestor, branch 1 ahead /
  0 behind). Pure fast-forward e7b1a08..b50a792, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main = b50a792.
  This addendum commit follows on main.

## Session 17 — battle cells found; Kuja holds through four engagements (2026-11-17)

### Recovery note

The first session-17 worker ended mid-task without a report: it
backgrounded engagement 1's driver and ended its turn waiting for a
delivery that had in fact already completed, and never picked the work
back up. This session recovered its trail from the worker's transcript,
its /tmp scripts (s17_*.py), and its ~/d12s dumps; every load-bearing
claim below was re-verified from the dumps directly before being
written here. The clone's tree was clean and the branch had no commits
— nothing of the first worker's work existed except the dumps, the
scripts, and ch1_s17_prekuja.state.

### Part A — battle instrumentation: block A found and live-verified

- BLOCK A: both fighters' live battle numbers sit in one heap struct,
  found by the offline diff protocol on the controlled instrumentation
  fight against the disc-3 Dragoon (dumps s17_drg_b0..b5: known BRV
  before/after a single BRV attack, known HP after a single HP attack).
  Layout (offsets from the struct base; most values are stored as
  adjacent duplicate pairs): +0x00/+0x04 Lightning BRV, +0x08 foe HP,
  +0x54/+0x58 foe BRV, +0x5C Lightning HP, +0xA8/+0xAC stage bravery
  pool. The doubled storage is the calibration signature: pattern-search
  a start-of-battle RAM dump for (BRV pair in 20..400, foe-BRV pair in
  0..3000, her HP in 100..1061, foe HP in 100..20000) and the struct is
  the first hit.
- THE BASE MOVES. It is a heap address and depends on the boot's
  allocation history: 0x0969D688 in boots that go straight from
  loadstate to a board engage, 0x096D5A38 in boots that pass through
  the Customization equip first (the prekuja lineage). Within one boot
  path it is deterministic — three separate prekuja boots reproduced
  0x096D5A38 exactly, and the direct-engage boots reproduced
  0x0969D688. A fixed battle address is wrong by construction; the
  reader design (below) has to locate the struct per battle.
- LIVE VERIFICATION: the cells were read live in five separate fights
  (validation + four Kuja engagements) and matched the game's own
  numbers at every battle start: Dragoon HP 1303, Kuja HP 1766 /
  BRV 274 / pool 282, Lightning HP 778 (the board engagement snapshot)
  / BRV 101. Max-HP fields were not separately identified — foe max is
  the start-of-battle foe-HP value (cross-checked against the Opponent
  Info card where one exists); Lightning's stat-screen max is 1061
  while battles snapshot her at the engagement value.
- ASSIST AND EX GAUGES: NOT FOUND — a recorded negative result. Three
  independent dump sequences (instrumentation b0–b5, the validation
  fight, Kuja engagement 1) were scanned for cells that start at 0 and
  rise monotonically outside block A: no credible candidate (the
  survivors are hit counters, coordinates/angles, and end-of-fight
  allocations that only move in the final dump). Block A's own
  neighborhood (±0x400) holds no gauge-shaped words. Both carried leads
  are dead: the 0x090B5E9C cluster (100 at instrumentation start, 0
  after one BRV attack, 110–140 in the validation end dump, unrelated
  pointer data at those addresses in the Kuja boot — a scratch/stat
  area, not a gauge) and 0x0969E10C (a float that sits at 1.0 early in
  a fight and 0.0 later — a flag or blend weight, not a rising gauge).
  Every fight this session ended within 12 action cycles, too short
  for gauges to move far; a future hunt wants a long fight with dense
  dumps. Until then the policies call the Tifa assist and attempt EX
  on a blind cadence, and no activation was confirmed in any
  engagement — the gauges' fill state is simply unknown.
- HARNESS LESSON (the expensive one): polling a FIXED block-A address
  to detect "fighters live" is a trap. When the base has moved, the
  poll never fires and the battle runs unobserved — engagement 1 lost
  566 of her 778 HP exactly this way (40 polls × 240 frames against
  the stale 0x0969D688 while Kuja fought; the policy finally started
  at 212 HP after a dump-based pattern search). The fix, used for
  engagements 2–4: poll with periodic RAM dumps + the pattern search
  (foe HP == 1766 as the signature). Block A was found at poll 3 in
  all three boots and the policy started at the full 778 HP every
  time.
- SECOND HARNESS LESSON: the card phase is live battle time. The
  validation fight's fixed card-crossing sequence (Crosses + ~1,800
  frames of waits) cost 414 HP before control — she entered the policy
  at 364 of 778 while the Dragoon attacked freely. Poll-and-start-at-
  once (above) is the fix.

### The policies and the validation fight

- v1/v2 (measured, cell-driven): BRV attacks until BRV ≥
  min(1.2 × foe HP, cap 700–999) or a kill/survival condition, then an
  HP attack; assist and EX on cadence chords. VALIDATION vs the
  Delusory Dragoon Lv 6 (disc 3, HP 1303): LOSS — entered at 364 HP
  (card-phase cost, above), her BRV peaked at 159, the foe's HP never
  moved (1303 in the end dump), KO at iteration 12; Flee returned her
  to disc 4 with KP 2 / Gil 106 intact. Per the brief, no second
  attempt on the Lv 6; the cells — the actual Part A deliverable —
  were verified live by this fight.

### Part B — Kuja: equipment, four engagements, no win

- EQUIPMENT: the Soul of Destruction was equipped in Customization →
  Accessories, slot 1, before engagement 1 — screenshot-verified twice
  (the highlighted item: "ATK=55, Accessory Breakability: 100%"; then
  the slot screen showing it equipped; her total ATK 15 → 55). The
  equip is baked into ch1_s17_prekuja.state, from which engagements
  2–4 all booted. Whether a loss can break it was not measured.
- All four engagements start from disc 6 with the prompt focus on disc
  7's record, at the full engagement snapshot (HP 778, BRV 101; Kuja
  HP 1766, BRV 274, pool 282), and every loss Fled back to disc 6 with
  KP 2 / Gil 106 untouched and the ??? still standing:
  1. (first worker) policy v2, started at 212 HP after the
     stale-address poll burned 566 HP pre-control; LOSS in 3
     iterations; Kuja 1766/1766 (start and end dumps agree).
  2. policy v2 at the full 778; LOSS in 9 iterations; Kuja 1766/1766
     (end dump). Her dash+BRV attacks never connected on the
     keep-away mage at this pacing (~110 frames per action cycle);
     Kuja HP-attacked every ~3 cycles for ~275 (his base BRV):
     778 → 499 → 225 → 0.
  3. rotation v4 (the session-13 dumb block □ □ × △ under cell
     surveillance, BRV-build rotations, cadence chords): LOSS at
     rotation 12 — the only engagement that damaged him. End dump:
     **Kuja 1564 of 1766 (202 dealt, ~two landed HP attacks at base
     BRV) — the session's best measured attempt.**
  4. aerial v5 (jump + air dash + aerial HP attacks): LOSS at
     rotation 8; Kuja 1766/1766. The decisive event is in the cells:
     at rotation 6 her BRV read 3, his 697, pool 0 — he BROKE her,
     took the 282 pool, and his next HP attack (~700) one-shot her
     from 499.
- KUJA'S CLOCK, measured: an HP attack roughly every 3 action cycles
  at ~275 damage (his base BRV), ~700 after a BREAK. 778 HP is three
  ordinary hits. Session 16's attempt-1 estimate (his bar at ~half
  over 630 presses) could not be reproduced under instrumentation —
  in four measured fights she died within 8–12 cycles each time. The
  discrepancy is recorded, not explained away: that driver pressed
  with different pacing and the bar reading was visual; the cell
  numbers here are exact.
- VERDICT: at Lv 2 with this loadout, blind play does not beat Kuja
  Lv 8 in four instrumented attempts. The win, the Stigma's
  appearance, and the chapter exit remain UNMEASURED. The gateway
  stands open at ch1_s17_prekuja.state (disc 6, Soul of Destruction
  equipped, engagement HP 778). Open routes unchanged from session
  16: a level grind (the board's Lv 6 pieces are the remaining EXP)
  or dodge-aware play — the cells now exist to build the latter: his
  BRV cell is the tell (a foe-BRV at or above base with hers falling
  is the loaded HP attack), and BREAK is pool-witnessed both ways.

### Battle-reader design notes (design only — no adapter code this session)

- Announceable from block A, all verified live this session: foe HP
  against its battle-start value gives the foe-health milestone rule
  directly (speak at three-quarters, half, a quarter, nearly gone;
  exact number on demand); Lightning's HP likewise for the own-health
  milestones (75/50/25 + "Low health."); BREAK events both ways (a
  fighter's BRV forced to 0 with the pool cell dropping to 0 and the
  breaker gaining it — the pool cell is the witness); foe BRV as a
  danger signal (at ~2× base, his next HP attack one-shots from mid
  HP); battle end (either HP cell hits 0), after which the board
  reader's existing gates take over on the return.
- Not announceable: Assist/EX gauge state (cells not found), foe
  intent beyond the BRV heuristic, positions/distances (no cells
  hunted).
- Integration shape: the reader arms on the board reader's
  battle-launch transition and disarms on the return, fail-closed
  like the rest; block A must be located per battle (the doubled-pair
  pattern search on a start-of-battle dump, or a pointer chase not
  yet found) — never a fixed address.

States (~/d12s): ch1_s17_prekuja.state (disc 6, Soul of Destruction
equipped — the session's Kuja doorstep). No post-Kuja or post-exit
states exist (no win). RAM dumps: s17_drg_b0..b5, s17_val_*,
s17_kuj_e1_* (first worker); s17r_e2/e3/e4 start+end dumps, prompts,
HUD and defeat screenshots (this session).

### Session 17 addendum — merge

- Doc-only session (no adapter code changed, so no build/test gates
  applied): the Session 17 section was committed on dissidia-012-kuja
  (efcb457) and merged here. Merge gates (session 12's): working tree
  clean; local main == origin/main (f01b93f); merge-base == f01b93f
  (main a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward f01b93f..efcb457, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main =
  efcb457. This addendum commit follows on main.

## Session 18 — the grind pays; dodge-aware policy v2 vs Kuja; he holds through five more (2026-11-17)

### Harness notes (drv client, learned the hard way)

- drv-bp block-buffers its stdout on a pipe; only runbp's bp_drain
  fflushes. A client that readlines before sending deadlocks: this
  session's first boot hung ~7 minutes with the driver idle in fgets
  (diagnosed via /proc — main thread in anon_pipe_read, ~0 CPU, while
  the CSO itself reads at 277 MB/s sequential / 0.2 ms random, so the
  disc was never the problem). The fix, in ~/d12s/s18lib.py: every
  reply-expecting command is followed by a "runbp 1" nudge before
  reading. Boot sync takes ~2 s afterwards.
- WSL /tmp does not persist across ssh sessions in this setup —
  screenshots and dumps live in ~/d12s, never /tmp.
- The battle is DETERMINISTIC (proven in Part C below): same savestate
  + same input stream = the same fight, cell for cell. Every claim in
  this section was measured under that rule.

### Part B — the grind: Delusory Dragoon Lv 6 (disc 3), one attempt, WON

- Route from disc 6: down, down, left, left, up, up to disc 4, then
  press left — the prompt opens (focus = disc 3's record 0x08B82A48).
  Doorstep saved as ch1_s18_predragoon6.state.
- Fixed entry (session 17's protocol): block A found at poll 11, base
  0x0969D688 (the loadstate-to-direct-engage lineage base); the policy
  started at the full 778 HP — the card phase cost nothing this time.
- WIN in 42 rotations of the plain session-13 rotation, with ZERO HP
  damage taken. The Dragoon's BRV never moved off 173 and the pool
  never off 236 — it never mounted an HP attack at all; it only
  BRV-attacked (her BRV dipped as low as 4, at rotation 18). Her HP
  attacks landed ~13 times in ~101-damage chunks (1303 -> 0; two
  partials at low BRV: 25 at rotation 18, 14 at rotation 32).
- MID-BATTLE LEVEL-UP: at rotation 19 her HP cell rose 778 -> 838
  (+60) and at rotation 21 her base BRV rose 101 -> 107 — the level-up
  applied inside the fight. The results screen confirms it verbatim:
  "Level increased by 1! (2->3)", "Basic stats up!", "Learned Auto
  Assist!", "Learned Launch!", "Received 10 PP."
- Rewards: Gil 106 -> 172 (+66), KP gauge 2 -> 3 (paid at once; no
  chain — no battle piece neighbours disc 4). ch1_s18_postgrind.state.
- CHEST 2 (disc 2; its path opened with the Dragoon's fall): prompt
  from disc 3 ("Treasure Chest — Contains helpful items"), contents
  measured by the Gil cell: +200 Gil (172 -> 372), chest consumed.
  (The banner text scrolled past between screenshots; the delta is
  exact.) ch1_s18_chest2.state.
- New Kuja doorstep: ch1_s18_doorstep.state — disc 6, Lightning Lv 3,
  HP 838, Gil 372, KP gauge 3. All Part C engagements boot from it
  (strictly better than session 17's 778 doorstep; the substitution is
  recorded here).

### Parts A + C — policy v2 and the five Kuja engagements

- Engagement values, identical in all five boots: her HP 838 / BRV
  107; Kuja HP 1766 / BRV 261, stage pool 288. (Session 17 read his
  opening BRV/pool as 274/282 against her Lv 2 — his opening numbers
  moved slightly with her level.) Block A at 0x096D5A38 (the prekuja
  lineage base), found at poll 11 in all five boots; the policy
  started at the full 838 HP every time.
- Policy v2 as briefed: the session-13 rotation (square, square,
  cross, triangle) under cell surveillance; Tifa assist (L+circle) on
  a 6-rotation cadence; survival — dodge chord (R+direction), guard
  hold, analog reposition — when foe BRV >= her current HP (a lethal
  HP attack is loaded), or foe BRV >= 450 and rising fast; press the
  offense when the pool transfers to her on a BREAK.
- All five engagements were lost; every loss Fled back to disc 6 with
  KP 3 / Gil 372 untouched and the ??? still standing:
  1. v2 as briefed — LOSS at rotation 20; Kuja 1323/1766 (443 dealt,
     the best measured attempt of either session). The cadence-assist
     window at rotation 6 coincided with the fight's only BREAK (pool
     288 -> her BRV 155 -> 443), and her one landed HP attack — at
     rotation 12, as he returned to cast at full BRV — dealt exactly
     443. Mid-fight level-up at rotation 13 (+61 HP, base BRV 107 ->
     113; reverted by the Flee, per its footer). Defense: the lethal
     trigger armed at 92 HP and the survival window nullified his
     spend (foe BRV 279 -> 2, 0 damage); she died inside the NEXT
     survival action to an unsigned hit (his BRV unmoved at 261 — an
     attack already in flight, or a spend whose BRV cost reset inside
     the sampling window).
  2. v2.1 + dash-in (R+triangle) whenever his HP had not moved — LOSS
     at rotation 9; Kuja 1748 (18 dealt). The dash is measured
     harmful: his hits landed at rotations 3/5/8 for 261/277/279.
     Deliberate assist-break attempts at foe BRV 0 and 2 produced no
     break (pool unmoved). Both additions were dropped.
  3. v2.1 + pacing (a 120-frame wait every 3rd rotation) — LOSS at
     rotation 12; Kuja 1586 (180 dealt: hits of 47 and 133 — the best
     landing rate). Level-up at ~rotation 11 (+61 again). Death by
     the break path: during rotation 12 he BRV-broke her (the pool
     went to him; his BRV read 594) and one-shot her from 340 — a
     kill the BRV tell cannot see coming, since his BRV sat at its
     261 base when the sequence started.
  4. v2 exactly as engagement 1 (control) — a frame-exact duplicate
     of engagement 1: every cell value at every action identical,
     same outcome. Determinism proven (above).
  5. Paced rotation, no dash, no assist attempts — LOSS at rotation
     10; Kuja 1728 (38 dealt). Survival nullified a signed spend
     again (305 -> 6, 0 damage); unsigned in-rotation hits (305 at
     rotation 6, 270 at rotation 10) killed her.
- What the cells say about the policy, in aggregate:
  - THE SURVIVAL SEQUENCE WORKS WHEN IT FIRES. Across the five
    engagements his HP attack spent inside a survival window twice
    (engagement 1's first survival, engagement 5's) and dealt 0 both
    times. The dodge/guard/reposition mix nullifies the signed spend.
  - THE TRIGGER IS THE DEFENSE-SIDE FAILURE. Kuja spends at base BRV
    (~261, up to ~305 with steals), so "foe BRV >= her HP" is false
    until she is already under ~305 HP, and the >= 450 rising trigger
    never fired in five fights. Three of the five deaths were
    unsigned: the spend's BRV cost resets inside a single rotation's
    sampling window (his BRV reads base on both sides of the rotation
    that killed her), and engagement 3's was a break combo launched
    from base BRV. Per-action BRV polling sees none of those coming.
  - THE OFFENSE IS THE PRIMARY FAILURE. In the best stream she
    landed 1 HP attack in 20 rotations. His teleport cycle makes the
    fixed-cadence rotation connect only as he returns to cast, and
    the one heavy hit needed the break's pool behind it. The
    deliberate low-BRV assist-break attempts never reproduced
    engagement 1's break — in that deterministic stream the break
    follows the exact cadence-chord timing, and disturbing the stream
    (dash, waits, extra chords) removes it.
  - His clock, updated: an HP attack every ~3–5 rotations for his
    current BRV (261–305), ~594 after he breaks her. 838 HP is still
    only ~3 ordinary hits.
- VERDICT: at Lv 3 with this loadout and policy family, Kuja holds
  through five more engagements (nine across sessions 17–18). The
  win, the Stigma's appearance, and the chapter exit remain
  UNMEASURED; the gateway stands open at ch1_s18_doorstep.state.
  Open routes, in the order the evidence supports: (1) a deeper
  grind — the board's two remaining Lv 6 pieces (Fleeting Flash on
  disc 32, Idle Sky Pirate on disc 12) are the recipe that just paid
  a level, +66 Gil and 10 PP for zero damage taken, and each level
  is ~+60 HP / ~+6 base BRV of headroom with in-fight level-up heals;
  (2) input-stream search on the proven engagement-1 backbone —
  determinism makes this a search problem: vary one input at a time
  around the stream that produced the break and the 443 hit, and
  keep what the cells reward; (3) identify the Assist/EX gauge cells
  (below) and weaponize EX mode.

### Gauge hunt (secondary — the long Dragoon fight's dump sequence)

- The 42-rotation Dragoon fight gave dumps at rotations 0/15/25/35
  and at the end (s18_drg_*). A scan for u32 cells outside block A
  that start at 0 and rise monotonically leaves 28 candidates; two
  families are identified arithmetically and are NOT gauges:
  0x09766D28 / 0x09766D40 (a duplicate pair tracing 0/404/845/1180/
  1394) is a cumulative damage-dealt counter — it matches the foe-HP
  deltas exactly, overkill included; 0x0977596C (0/888/1857/2592/
  6443, with a jump in the end dump) behaves as an EXP / win-bonus
  counter.
- One candidate is gauge-shaped: 0x0976288 traces 0 -> 433 -> 800 ->
  800 -> 800 — it filled by rotation 25 and HELD at 800 while she
  never called an assist or attempted EX. No other candidate
  plateaus. It is carried as a LEAD, not a finding: the address is
  heap (this boot's), and no spend test was possible — the Kuja
  fights ended before their own dump sequences could run (a two-point
  start/end scan of engagement 1 leaves 557 candidates, mostly
  results-screen allocations). A future long fight should re-find
  the plateau pattern in its own boot and spend it (triangle, or
  L+circle) under dump surveillance.

### States and dumps (~/d12s)

ch1_s18_predragoon6.state, ch1_s18_postgrind.state,
ch1_s18_chest2.state, ch1_s18_doorstep.state (the Kuja doorstep),
ch1_s18_kuj_e1_fled / e2_fled / e3_fled / e4_fled / e5_fled.state.
Dumps: s18_drg_poll0..11, s18_drg_start/r15/r25/r35/end;
s18_kuj_e{1..5}_poll*, _start, _end; screenshots s18_*.
Scripts: s18lib.py (the nudge-sync drv client) in ~/d12s.

### Session 18 addendum — merge

- Doc-only session (no adapter code changed, so no build/test gates
  applied): the Session 18 section was committed on dissidia-012-kuja2
  (bb8f860) and merged here. Merge gates (session 12's): working tree
  clean; local main == origin/main (62ff913); merge-base == 62ff913
  (main a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward 62ff913..bb8f860, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main =
  bb8f860. This addendum commit follows on main.

## Session 19 — the grind pays three levels; the deterministic search's best leaves Kuja at 761 (2026-11-17)

### Harness notes

- The session-18 policy was recovered verbatim from the session-18
  worker's transcript (its /tmp scripts did not survive): the Kuja
  runs below execute session 18's engagement-1 policy v2 exactly
  (same press/chord/read structure), with session-18's entry fix.
- drv poll dumps are 24 MB each; a 90-poll entry search is ~2 GB of
  dumps. Entry polls from failed attempts were deleted mid-session.
- Two infrastructure bites, both recovered: the Legion tunnel proxy
  dropped for ~10 minutes (a tee transfer died with it; the field
  state was already saved), and a VM/exec service restart killed a
  run's stdout pipe mid-fight (the Dragoon Lv 2 rematch's win state
  and end dump survived; its outcome was re-derived from the dumps).
  From then on every script redirected its own log to a file in
  ~/d12s — a broken pipe can no longer kill a fight or eat its log.

### Part A — the grind: both Lv 6 manikins beaten, chest 42 opened

From ch1_s18_doorstep.state (disc 6, Lv 3, HP 838, Gil 372, gauge 3).
Route to disc 22: down, down, left, left, left, left. Both fights use
the plain session-13 rotation at full-HP entry; block A found by the
HP-keyed pattern search (her HP exact), base 0x0969D688 both times
(the direct-engage lineage base).

- IDLE SKY PIRATE Lv 6 (disc 12; HP 1303, BRV 161, pool 242): WIN in
  59 rotations — nothing like the Dragoon's passivity. It landed
  161-damage HP attacks from rotation 29 on (five landed: 899 -> 738
  -> 566 -> 405 -> 229 -> 55, plus chip), and her HP attacks stopped
  landing entirely for rotations 44-58 with the Pirate at 35 HP; the
  killing blow landed at rotation 59 with her at 55 HP. Two in-fight
  level-ups (HP 838 -> 899 at rotation 5, 55 -> 115 at the kill).
  Results screens, verbatim: "Level increased by 2! (3->5)", "Basic
  stats up!", "Learned Auto Assist Lock On!", "Learned Auto
  Recovery!", "Received 1 AP.", "Obtained 66 gil.", "Received 10 PP."
  Gil 372 -> 438. The win's KP was HELD — a chain offered.
- THE CHAIN WAS THE OTHER TARGET. The results-crossing Crosses
  carried through the chain prompt and the cards, so the post-win
  savestate sat on the chained fight's VS card: Lv 5 Lightning vs the
  Fleeting Flash Lv 6 (disc 32), at her post-Pirate board HP of 115.
  Fought as it stood, plain rotation: WIN in 40 rotations with ZERO
  HP damage taken — the Flash's BRV fell to 92 early and it never
  mounted an HP attack; her HP attacks landed for 118 like a
  metronome. In-fight level-up at the kill (115 -> 176). Results:
  "Level increased by 1! (5->6)", "Learned Speed Boost!", "Learned
  Ruin!", "Obtained 66 gil.", "Received 10 PP." Gil 438 -> 504. The
  chain then closed and paid both wins' KP at once: gauge 3 -> 7
  (+4 = 2 chances x 2 enemies, the manual's chain formula).
- CHEST 42 (disc 42, guarded by the Flash): prompt from disc 32 after
  the Flash fell; contents from the game's own banner, caught on a
  15-frame screenshot cadence after two wider cadences scrolled it
  past: **"Obtained Bronze Helm from chest!"** (an item — the Gil
  cell correctly did not move).
- Board-clear state: ch1_s19_boardclear.state — disc 6, Lv 6,
  HP 176, Gil 504, KP gauge 7, banked KP 2.

### The heal problem — exit, re-enter, and the board resets

Part B needs a doorstep she can survive from; 176 HP is one Kuja
hit. No in-gateway heal exists, so: Pause -> Exit Gateway. The game
warns verbatim on both the menu footer and the confirm (cursor
starts on NO): "KP gained in gateway will be lost." The 7 gauge KP
were spent as the price. On the field she stood at the gateway with
its conditions panel already up and **HP fully restored — Lv 6 max
is 1303** (field HUD, screenshot). One Cross re-entered (state 5 ->
20, entry disc 20, gauge 0, banked KP 2 kept, Gil 504 kept).

The re-entry probe then proved the cost beyond KP: pressing toward
disc 12 from 22 reopened the Idle Sky Pirate's prompt (focus =
disc 12's record). **Leaving a gateway resets its board** — pieces
and chests restore; EXP, levels, Gil and items persist. (The first
probe presses were eaten by the re-entry banter scene and read as a
false negative; the scene-clear protocol — burn Crosses until a test
hop answers — settled it.)

Re-path on the reset board, plain rotation:
- Delusory Dragoon Lv 2 (25): WIN, zero damage taken. Its numbers at
  her Lv 6: HP 1061, BRV 73, pool 236 (session 16 read a Lv 2
  manikin at 399 HP against her Lv 1-2 — manikin HP scales with her
  level). +14 Gil (504 -> 518), gauge 0 -> 1. Block A 0x096D5A38.
- Fleeting Flash Lv 2 (26): WIN in 27 rotations, one 146 hit taken
  (1303 -> 1157). Same foe numbers (1061 / 73 / 236). +14 Gil
  (518 -> 532), gauge 1 -> 2.
- NEW DOORSTEP: **ch1_s19_doorstep.state — disc 6, Lightning Lv 6,
  HP 1157, Gil 532, KP gauge 2, banked KP 2** (board HUD verified).
  Her Lv 6 base BRV is 124 in the Kuja snapshot (the Lv 2 fights
  snapshotted 173 — the battle-start BRV varies by fight; both are
  cell readings, recorded as measured). This doorstep substitutes
  for the brief's post-grind doorstep exactly as session 18's did:
  the grind's own doorstep (boardclear, HP 176) could not host the
  search, and the substitution is recorded here.

### Part B — the deterministic search vs Kuja (7 runs)

Entry on the reset board replays the Kuja reveal cutscene, which
AUTO-PLAYS (probe screenshots changed lines with zero Crosses) and
runs ~16 poll-chunks (~10,000 frames) before the cards. The entry
protocol is now: engage, wait 600-frame chunks with a dump per chunk
(crossing every 300 frames after chunk 12), pattern-search each dump.
Block A was found at poll 16, base 0x096D5A38, in ALL SEVEN runs —
the entry stream is stable, and every run started the policy at the
full 1157 HP. Kuja at her Lv 6: **HP 1766 (unchanged), BRV 220
(session 17-18: 261-274), pool 306**; her BRV 124. Every loss Fled
back to disc 6 with Gil 532 / gauge 2 untouched.

- R0 — BACKBONE REPLAY (session-18 v2 unmodified): LOSS at rotation
  39; **Kuja 910/1766 (856 dealt)**. The shape changed completely at
  Lv 6: no BREAK either way in 39 rotations (pool never moved off
  306), but her plain HP attacks landed steadily (~110-155 each:
  1766 -> 1658 -> 1534 -> 1410 -> 1244 -> 1089 -> 910). In-fight
  level-up at rotation 29 (+61 HP, BRV 124 -> 130; reverted by the
  Flee). Death: at 233 HP the lethal trigger fired, three survival
  actions changed nothing (his BRV sat at 254), and the fourth
  survival window ended at 0 HP with his BRV still 254 — an attack
  already in flight when the trigger armed.
- V1 — +30 frames before every rotation: LOSS at rotation 32;
  **Kuja 761/1766 (1005 dealt) — the best attempt of either
  session.** She was BROKEN at rotation 19 (pool to him, his BRV
  567) yet the spend did not come until rotation 30 (588 -> 1, 592
  damage); level-up at rotation 25. Died at 181 HP inside the second
  survival action, his BRV unmoved at 250 — the in-flight pattern
  again.
- V2 — +60 frames before every rotation: LOSS at rotation 11; Kuja
  1456 (310 dealt). The bigger drift inverts the fight: his BRV
  attacks connected (her BRV pinned at 0, his up to 937), he broke
  her and one-shot her inside the first survival action.
- V3 — +15 frames before every rotation: LOSS at rotation 23; Kuja
  1169 (597 dealt). Broken during rotation 23 (his BRV 527); the
  survival action could not stop the 545 spend.
- V4 — +45 frames before only the Triangle (HP) beat: LOSS at
  rotation 10; Kuja 1748 (18 dealt). Delaying the HP beat desyncs
  the rotation entirely — her HP attacks whiffed for ten straight
  rotations while he spent four times. The axis is dead.
- V5 — pacing, +120 frames every 3rd rotation: LOSS at rotation 12;
  Kuja 1611 (155 dealt). Broken at rotation 6; a 407 spend followed;
  died in the survival loop at 230 HP.
- V6 — +30 frames every 2nd rotation: LOSS at rotation 27; Kuja
  1187 (579 dealt). One clean data point for the defense: at 26 HP
  the survival sequence NULLIFIED a signed spend (220 -> 1, 0
  damage) — the trigger works when it fires with the spend still
  incoming; the next window's unsigned hit killed her.

What the search says, in aggregate:
- THE MOST PROMISING AXIS IS THE UNIFORM +30-FRAME DRIFT (V1). It
  produced the best damage (1005), the best damage rate (~31 per
  rotation vs R0's ~22), and the deepest cut (761 left). The
  response to drift is jagged, not smooth (+15 and +60 are both
  much worse) — determinism makes each offset a different fight,
  so neighbouring offsets are new lottery tickets, not refinements.
- OFFENSE IS NO LONGER THE PRIMARY FAILURE. At Lv 6 the backbone
  lands steadily with no break needed; V1's rate wins in ~57
  rotations. She lives ~32. The gap is ~4 more survived spends
  (~900 HP of headroom), i.e. roughly two more levels, or a defense
  that sees the killing attack.
- THE DEFENSE FAILURE IS NOW FULLY CHARACTERIZED: in all seven runs
  she died either to a post-break spend (V2/V3/V5) or to an HP
  attack that landed inside a survival window with his BRV unmoved
  across it (R0/V1/V6) — the attack is already in flight when the
  foe-BRV trigger arms. Per-action BRV polling cannot see it
  coming (session 18's conclusion, now 7/7). The one nullification
  (V6) fired when the spend was still incoming at trigger time.
  A trigger on his BRV RISING THROUGH a low threshold (~200, his
  base) rather than >= her HP would arm a full cycle earlier; that
  is a policy change, outside this session's one-window search.
- VERDICT: Kuja holds through seven more engagements (sixteen
  across sessions 17-19). The win, the Stigma's appearance, and the
  chapter exit remain UNMEASURED; the gateway stands open at
  ch1_s19_doorstep.state. Open routes, in the order the evidence
  supports: (1) two more levels — the reset board's Lv 6 pair is
  re-fightable only via another exit/re-entry cycle, and each
  level is ~+61 HP / ~+6 BRV with in-fight level-up heals; a Lv 8
  attempt also re-rolls the entire deterministic stream; (2) the
  early-arming survival trigger (above) on the V1 stream; (3) the
  Assist/EX gauge cells (session 18's lead: 0x0976288 plateaued at
  800 in its boot) — EX mode remains the untested weapon.

### States and dumps (~/d12s)

ch1_s19_prepirate.state, ch1_s19_postpirate.state (on the chain's VS
card), ch1_s19_postflash.state, ch1_s19_chest42.state,
ch1_s19_boardclear.state (grind doorstep, HP 176), ch1_s19_exited.state
(the exit confirm, cursor on NO), ch1_s19_field.state,
ch1_s19_reentry.state, ch1_s19_reentry22.state, ch1_s19_re_predragoon2 /
re_preflash2.state, ch1_s19_rp_drg2_win / rp_fla2_win.state,
ch1_s19_doorstep.state (the search doorstep), ch1_s19_kuj_r0_fled /
kuj_v1..v6_fled.state. Dumps: s19_pir_*, s19_chf_*, s19_rp_*,
s19_kuj_<tag>_poll*/_start/_end. Screenshots s19_*. Scripts:
s19_grind1.py, s19_chain.py, s19_chest42*.py, s19_repath*.py,
s19_kuja.py (the parametrized v2 runner), s19_probe_kuja.py; run
logs s19_kuja_<tag>.log, s19_repath2.log.

### Session 19 addendum — merge

- Doc-only session (no adapter code changed, so no build/test gates
  applied): the Session 19 section was committed on dissidia-012-kuja3
  (1d6233d) and merged here. Merge gates (session 12's): working tree
  clean; local main == origin/main (1b60493); merge-base == 1b60493
  (main a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward 1b60493..1d6233d, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main =
  1d6233d. This addendum commit follows on main.

## Session 20 — the farm overshoots to Lv 10; the +15-frame stream kills Kuja (2026-11-17)

### Harness notes

- POST-WIN BOARD SCENES EAT THE D-PAD. After the Flash Lv 6 win the
  board sat in a pending scene: single direction presses did nothing
  (grid/focus frozen) until ~6 Crosses had burned through it. A
  savestate taken inside that window boots with dead input. Fix,
  used for all post-fight navigation this session: the fused go() —
  Cross, run 40, press direction, run 25, check the grid cell,
  repeat (the scene-clear protocol of session 15, mechanized). The
  older hop() failed three times this session by wandering during
  its opposite-direction fallback while input was still dead.
- A CUSTOMIZATION HEADER CAN HIDE A STACK. The pass-1 doorstep
  state was saved with the Customization menu still open, and
  behind it were queued notification banners ("New prizes creatable
  through Battlegen!", "The chocobo found a treasure chest!") plus
  a queued Battlegen tutorial dialog (Fang). Each banner eats a
  press; the stack took 24 mixed Circle/Cross presses to clear to
  the board (ch1_s20_dismiss_e.state). Verify menu-closed by
  screenshot before saving any doorstep state.
- THE KP READER READS THE GAUGE. The holder-chain cell
  (0x089D65A8 + 0x260) is the top-left gauge dial; the board/field
  HUD's "KP" number is the BANKED total. Both are reported as such
  below (session 19's doorstep was gauge 2 / banked 2; this
  session's win paid banked 2 -> 4).
- ONE CORRUPT STATE: ch1_s20_wp2_notlive.state, saved mid-scene
  transition, fails to load (loadstate 0; the emulator fell back to
  the title screen). Do not use it. Every other state this session
  loads. All scripts logged to files in ~/d12s per session 19's
  rule; no fight was lost to a broken pipe.

### Part A — the helm check, and the Soul of Destruction is gone

From ch1_s19_doorstep.state (disc 6, Lv 6, HP 1157/1303, Gil 532).

- BRONZE HELM: EQUIPPED. The head slot was empty; the helm's footer
  reads BRV+38, DEF+2 (verbatim), strictly better than nothing.
  Post-equip totals at Lv 6: BRV 162, DEF 19, ATK 19.
- THE SOUL OF DESTRUCTION IS GONE. The Accessories screen shows all
  three slots empty, and the accessory inventory holds only a
  "Level 1-9" booster x1 ("When your level is 9 or under, Effect:
  1.5 times") — not equipped, left as-is. Her total ATK is 19
  (base 15 + Broadsword 4) against session 18's verified 55 with
  the Soul equipped. The breakable Soul broke during the session
  18/19 fights; the break itself was not witnessed (session 17
  flagged its breakability as unmeasured — now measured, the hard
  way). Saved ch1_s20_start.state (disc 6, Lv 6, HP 1157, Gil 532,
  gauge 2, helm on).

### Part B — the farm: one pass, Lv 6 -> Lv 9 (the Lv 8 target overshot)

Exit the gateway (the 2 gauge KP forfeited), re-enter Sage's Path
(board reset, session 19), re-beat the manikins with the plain
session-13 rotation. Battle block A found per fight by the loose
pattern search (both known bases appeared: 0x0969D688 and
0x096D5A38).

- IDLE SKY PIRATE Lv 6 (disc 12; HP 1303, BRV 124, pool 266):
  first attempt LOSS in 70 rotations + idle (Pirate left at 118
  HP; it ground her down late, 744 -> 413 -> 140 -> 16 -> 0).
  In-fight level-up at ~rotation 50 (her BRV 162 -> 168). THE FLEE
  DID NOT REVERT THE LEVEL-UP: the retry engaged at HP 1364, the
  Lv 7 maximum. Session 18's "(reverted by the Flee)" was an
  inference from savestate boots; live measurement corrects it —
  level-ups persist through a Flee, and the Flee restores HP to
  the CURRENT (post-level) max. (The retry's first entry search,
  keyed on the stale 1303, found no block A; the loose-search
  diagnostic exposed the 1364.) Retry: WIN in 43 rotations at
  Lv 7, ending at 561 HP — the Pirate sat at 24 HP for 23
  rotations while her finishers whiffed. +66 Gil.
- FLEETING FLASH Lv 6 (disc 32): WIN in 60 rotations, engaged at
  561 HP, ended at 365 (the Flash sat at 1 HP for ~15 rotations
  before the kill). +66 Gil.
- DELUSORY DRAGOON Lv 6 (disc 3): WIN in 26 rotations with ZERO
  HP damage taken (session 18's passivity held; its BRV never
  moved off 124). In-fight level-up (HP 365 -> 425, the +60 heal)
  -> Lv 8. +66 Gil.
- DELUSORY DRAGOON Lv 2 (disc 25): WIN in 20 rotations, one
  100-damage hit taken (425 -> 325). Its numbers at her Lv 8:
  HP 1061, BRV 73, pool 248. +14 Gil.
- FLEETING FLASH Lv 2 (disc 26): WIN in 18 rotations, zero damage;
  in-fight level-up (325 -> 386, +61) -> Lv 9. +14 Gil.
- PASS TOTALS: EXP TOTAL 33496 -> 62098 (+28602); THREE level-ups
  (7 in the lost Pirate fight, 8 in Dragoon 6, 9 in Flash 2);
  Gil 532 -> 758; gauge KP 0 -> 7 across the pass. Header
  verified: Lv 9, HP 386/1485, BRV 179, ATK 22, DEF 22, LUK 14,
  CP 225/339, LUCKY! 20%, BATTLES 10. (Per-fight Gil/KP reads lag
  one fight behind the results screens; the pass totals above are
  the reconciled numbers.)
- NO SECOND PASS: the ~Lv 8 target was passed inside pass 1 —
  level-ups land mid-fight, and the pass's fights were already
  committed. Pass 1's doorstep: ch1_s20_pass1.state (disc 6,
  HP 386 — see the harness note: saved with the menu open).

### The Kuja doorstep — exit, toll, park (and one more level)

The pass-1 doorstep's 386 HP cannot host Kuja, so the heal cycle
ran once more: exit (gauge 7 forfeited), re-enter, pay the Lv 2
toll on the fresh board at full 1485 HP.

- Delusory Dragoon Lv 2: WIN in 11 rotations, zero damage taken.
- Fleeting Flash Lv 2: WIN in 13 rotations, zero damage, with
  ANOTHER in-fight level-up (1485 -> 1545, +60) -> Lv 10.
- Both +14 Gil (758 -> 786); gauge 0 -> 2.
- NEW DOORSTEP: **ch1_s20_doorstep.state — disc 6, Lightning
  Lv 10, HP 1545/1545 (full), Gil 786, KP gauge 2, banked KP 2.**
  Header: BRV 185, ATK 23, DEF 23, CP 225/341, EXP NEXT 6469 /
  TOTAL 67166, LUCKY! 22%.
- Kuja at her Lv 10 (r1 entry snapshot): **HP 1766 (unchanged at
  every one of her levels), BRV 193 — DOWN from 220 at her Lv 6 —
  pool 331 (up from 306)**; her battle-start BRV 214.

### Part C — Kuja: three engagements, and he falls on the third

Entry per session 19 (the reveal cutscene auto-plays; block A at
poll 16, base 0x096D5A38, in all three runs; the policy starts at
the full 1545 HP). Runner: s20_kuja.py (TAG WAIT WAIT_EVERY
THRESH STATE; THRESH >= 9000 disables the defense entirely).

- r1 — THE BRIEF'S POLICY (session-19 V1 backbone, +30 frames per
  rotation; dodge trigger foeBRV >= 200 OR >= 60% of her current
  HP, whichever arms first; survival streak cap 3): LOSS at
  21 rotations / 37 actions (16 of them survival); **Kuja
  1505/1766 — 261 dealt, the worst offense of the search.** The
  trigger design failed on this lineage's numbers, measured:
  (1) his resting BRV at her Lv 10 is 193, BELOW the 200 arm, so
  the threshold half was inert until he had already gained
  bravery; (2) the 60%-of-HP half became a death spiral — below
  ~320 HP it arms on nearly every poll (any foeBRV >= 13 at
  21 HP), starving offense 3-to-1 while protecting against
  nothing: both spends that landed during survival windows
  (453 post-break, 58) landed anyway; (3) the survival insertions
  desynced the drift stream — exactly ONE HP-damage event in 21
  rotations (261, on the rotation-6 assist-chord beat). Death at
  21 HP inside a survival action with his BRV unmoved: session
  19's in-flight pattern, again.
- r2 — DRIFT PROBE, WAIT=0, defense disabled: LOSS at 49
  rotations; **Kuja 425/1766 (1341 dealt, ~27 per rotation).**
  The clean backbone is strong in this lineage, and the offense
  movement itself dodged half his logged spends (3 of 6 spends
  lost 0 HP). She ran out of HP roughly 16 rotations short.
- r3 — DRIFT PROBE, WAIT=+15, defense disabled: **WIN in 42
  rotations; Lightning ends at 777 HP.** 1766 dealt at ~42 per
  rotation — the best rate of sessions 17-20 (session 19's best
  was ~31). Timeline: 1552 at rotation 6; she was BROKEN once
  early (his BRV to 575; the post-break spend whiffed for 0);
  1285 at rotation 10, 1085 at 19, 657 at 24, 443 at 29, one
  193 spend landed at ~rotation 34 (970 -> 777), 229 at 38, 0
  at 42. Only two logged spends landed all fight (193 total).
  Rewards, from the game's own text and the cells: "Obtained
  104 gil." (Gil 786 -> 890); win quote "That wasn't too bad of
  a fight."; banked KP 2 -> 4 (field HUD after the exit). No
  level-up (still Lv 10).
- The brief's THRESH=260 fallback was NOT run: r1's log made it
  moot before the win — four of his five damaging spends came at
  BRV 193-198, below any 200+ arm, and the survival sequence
  nullified 0 of the spends it faced in this lineage. The
  winning axis was the one session 19 identified — the drift
  stream — re-searched in the new lineage, and the response
  stayed jagged, exactly as session 19 warned: 0f -> ~27 per
  rotation, +15f -> ~42, +30f-with-defense -> ~12 polluted.
  Kuja's total across sessions 17-20: nineteen engagements; he
  fell on the nineteenth.

### The exit — no Stigma; the gateway completes itself

- The win returns to disc 6, and a story scene auto-plays (Kefka:
  "Oh, what kind of deliciously depraved memories should I fill
  you up with this time around?" — Lightning: "You'd let your
  friend die? You're not very different from the manikins.").
  **There is no Stigma piece on Sage's Path, before or after:
  beating the ??? WAS the gateway's completion condition.** The
  scene carries her off the board to the Chapter 1 field, where
  a Moogle-shop introduction scene plays ("...A shop?" —
  Moogle: "You're a warrior of Cosmos, Thunder Lady? I know
  everything about all of you warriors.").
- SCENE PROTOCOL: the post-win scenes need ~10,000 frames of
  Cross with LONG runs (240 frames). A 150-iteration run that
  test-pressed Down every 6 iterations stalled on the Moogle
  line for 60+ iterations; crosses-only at 240 frames cleared
  in 42 iterations (state flickering 2 <-> 3 at the end as
  control returned). Do not poke scenes while they run.
- LANDING: **the Chapter 1 field, under the stone arch by the
  Sage's Path entrance — Lv 10, HP 1545/1545 (fully restored by
  the completion), Gil 890, banked KP 4** — with the Moogle shop
  NPC at her elbow (talk prompt up) and the field running ahead
  through the arch. State: ch1_s20_after_scene.state. Chapter
  1's second gateway stands complete. The field segment toward
  the next gateway is unexplored; it is the next frontier.

### States and dumps (~/d12s)

ch1_s20_start.state (helm equipped, Lv 6), ch1_s20_pass1.state
(post-pass doorstep, HP 386 — saved with the Customization menu
open; see harness notes), ch1_s20_dismiss_e.state (the same
moment with the menu/dialog stack cleared), ch1_s20_doorstep.state
(the Kuja doorstep, Lv 10, full HP), ch1_s20_kuj_r1_fled.state,
ch1_s20_kuj_r2_fled.state, ch1_s20_kuj_win.state (on the win,
pre-scene), ch1_s20_after_scene.state (the field landing),
ch1_s20_wp2_notlive.state (CORRUPT — do not use). Per-fight
pre/win/fled states under ch1_s20_p1*/p1r/p1s/p1t/p1u/ds_*.
Dumps: s20_kuj_<tag>_start/_end.bin, farm *_start/_end.bin.
Screenshots s20_*. Scripts: s20_helm.py, s20_farm.py +
s20_farm_resume*.py + s20_farm_finish.py, s20_doorstep.py,
s20_kuja.py (the parametrized runner), s20_winproto*.py,
s20_wp4.py (the patient scene ride). Logs s20_*.log.

### Session 20 addendum — merge

- Doc-only session (no adapter code changed, so no build/test gates
  applied): the Session 20 section was committed on dissidia-012-kuja4
  (a3aad2c) and merged here. Merge gates (session 12's): working tree
  clean; local main == origin/main (138a62c); merge-base == 138a62c
  (main a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward 138a62c..a3aad2c, pushed via the clone's git
  credentials, git ls-remote verified in-session: origin/main =
  a3aad2c. This addendum commit follows on main.

## Session 21 — the Moogle shop (Gil, not KP); Leather Armor bought and worn; the Gateway of Fools and Hope censused (2026-11-17)

### Part A — the shop is the Customization menu's Shop entry, and it trades in Gil

- Boot from ch1_s20_after_scene.state: field state 2 at
  (319.08, 0.44, 195.18) by the Sage's Path arch; Gil 890, field
  HUD KP 4 (banked). The field KP-gauge cell (holder chain
  base+0x260) reads 2 here — the gauge, not the bank (session
  20's distinction, reconfirmed).
- THE FIELD MOOGLE IS NOT THE SHOP. Its talk prompt opens a
  single-page greeting ("You're a warrior of Cosmos, Thunder
  Lady? I know everything about all of you warriors.") — Cross
  opens (state 3), the page waits indefinitely, one more Cross
  closes it. Measured twice; no menu, no choice.
- THE SHOP IS Customization -> Shop (this doc's older paraphrase
  said KP; the live screen's header currency is GIL — the screen
  wins). Route: Triangle on the field -> Customization (state
  11) -> Shop (7th menu item: Abilities, Equipment, Accessories,
  Summons, Assist, Adjust Level, Shop; cursor starts on
  Abilities). First entry fires a Wakka "Shops" tutorial dialog
  (Circle/Cross closes it); a "New inventory in the shop!"
  banner also eats a press. The gateway completion is what
  stocked it.
- Shop header: "Lightning Lv 10 — GIL 890 — INVENT 3%". Menu
  Buy / Sell, cursor starts on Buy; cursor positions (menu,
  Buy/Sell, category, per-category item cursor) persist across
  visits. Categories cycle with R: Weapons -> Parrying ->
  Headgear -> Body -> Accessories -> etc -> (wrap) Weapons.
  Lists wrap. Sell prices are half of buy (Broadsword 250,
  Leather Armor 250).
- FULL INVENTORY (every item flagged NEW, all Equip Level 1;
  currency Gil except Accessories, which are TRADE):
  - Weapons (11): Broadsword 500 (Sword, ATK+4; EQUIPPED,
    Held 1); Knife 500 (Dagger, ATK+2 DEF+2); Longsword 500
    (Greatsword, ATK+6 DEF-2); Katana 450 (Katana, BRV+9 ATK+4
    DEF-2); Tanegashima 400 (Gun, HP-86 BRV+19 ATK+4 DEF-2);
    Spear 1140 (Spear, HP-86 ATK+8 DEF-2, Can't Equip); Axe
    1300 (Axe, BRV+20 ATK+8, Can't Equip); Rod 450 (Rod, HP+95
    BRV-20 ATK+4 DEF+2, Can't Equip); Staff 450 (Staff, BRV+9
    ATK+2, Can't Equip); Leather Gloves 400 (Grappling, HP-150
    BRV+19 ATK+4, Can't Equip); Oaken Pole 450 (Pole, HP-86
    BRV+19 ATK+2, Can't Equip). No weapon is a no-downside
    upgrade over the Broadsword — every equippable alternative
    trades stats.
  - Parrying / off-hand (4; her off-hand slot was EMPTY, so all
    four are pure gains): Kunai 400 (ATK+2 DEF+1); Buckler 450
    (Shield, DEF+3); Bronze Bangle 500 (Bangle, HP+85 DEF+1);
    Hide Armlet 450 (Gauntlet, BRV+9 DEF+1).
  - Headgear (3; she wears Bronze Helm, BRV+38 DEF+2): Leather
    Hat 500 (Hat, BRV+48 — net +10 BRV / -2 DEF, a trade);
    Bronze Helm 300 (same as worn); Headband 300 (HP+85 BRV+38
    — net +85 HP / -2 DEF, a trade).
  - Body (5; her body slot was EMPTY): Leather Clothing 950
    (Clothing, HP+428 BRV+9 DEF-2 — a trade, and unaffordable
    at 890); Leather Armor 500 (Light Armor, HP+428, NO other
    modifier — the one pure straight upgrade in the shop);
    Leather Chest Plate 500 (HP+342 BRV+19 DEF-2 — a trade,
    dominated by Leather Armor); Robe 400 (HP+342 BRV+9, Can't
    Equip); Bronze Armor 1100 (HP+513 BRV-10, Can't Equip,
    unaffordable).
  - Accessories (3; TRADE only — cost shown in place when the
    item is selected; no separate confirm screen on select):
    Power Ring (Damage +3%; Flash's Desire x3 [held 1] + 100
    Gil — not obtainable); Block Ring (Wall Rush Defense +10%;
    Sky Pirate's Hopes x3 [0], Flash's Desire x3 [1], Time
    Warp x3 [0] + 2600 Gil — not obtainable); Decoy (Wall Rush
    HP Defense +10%; Hero's Dream x3 [0], Flash's Desire x3
    [1], False Husk x3 [0] + 1500 Gil — not obtainable).
  - etc category: EMPTY. **There are no consumable or healing
    items in this shop at all.**
- PURCHASE: **Leather Armor, 500 Gil** — Buy -> Body -> Leather
  Armor -> Cross -> quantity prompt ("x01, TOTAL 500 GIL,
  REMAINING GIL 390") -> Cross. Gil 890 -> 390, verified in the
  Gil cell and the shop header. The Kunai (400) missed fitting
  the remaining 390 by 10 Gil — the near-miss, recorded.
  Nothing sold; no second purchase.
- EQUIPPED the same visit: Customization -> Equipment -> Equip
  -> body slot -> Leather Armor. Verified on the Equipment
  screen: HP total 1545 -> **1973 (1545 + 428)**, and her
  current HP filled to the new max (Customization header and
  field HUD both read 1973 afterward). Final worn set:
  Broadsword / (off-hand empty) / Bronze Helm / Leather Armor;
  totals BRV 185 (147+38), ATK 23 (19+4), DEF 23 (21+2),
  LUK 15, CP 225/341.
- HARNESS NOTES (menus): (1) an exploratory Down x5 in the
  Customization menu overshot into Adjust Level ("Change Lv
  010"); Up/Down do NOT change the value there (Left/Right
  do); backed out with Circle WITHOUT confirming — Lv 10 and
  all stats verified unchanged. (2) A blind Up x3 in the
  Equipment action list landed on **Remove All**, and Cross
  opened its "Remove all equipment?" confirm (cursor defaults
  to NO; Yes/No is horizontal, so Down does nothing); the
  dialog was cancelled with Circle and the equipment verified
  intact. Menu cursors persist between visits and wrap —
  verify the cursor by screenshot before any Cross in these
  menus. (3) Post-shop state saved at field control:
  ch1_s21_postshop.state (state 2, (319.08, 0.44, 195.18),
  HP 1973, Gil 390, KP 4).

### Part B — the road to the next gateway: 32 units, no scenes, no fights

- Gate survey at the post-shop boot (GateMgr via ctrl+0x88;
  heap base varies per boot — this boot 0x095D7110, count 10;
  point table re-read at 0x095E61C0, 18 points).
  Tester-reachable points (lh(A+4) >= 0): **7 (305.04, 0.81,
  166.59), 4 (220.44, -0.49, 110.99), 5 (210.56, 2.63, 152.92),
  10 (154.08, 6.72, 229.42)**. Point 13 (Sage's Path) now
  reads lh = -1 — **a finished gateway's gate goes inert**,
  matching Crescent Lake (14). Points 6, 8, 9, 11 sit in the
  inert family this boot. (lh values for the unspent four:
  pt 7 = 6; pts 4/5/10 = 238/239/240 — noted, meaning
  unproven.)
- The field Map (Select, state 14) shows the whole
  two-continent region with her position glowing on the
  southern continent's east side; it carries no destination
  marker and no place names, so it does not choose the gate.
  Point 7 is 32 units away, by far the closest unspent gate;
  the walk went there.
- THE WALK (closed-loop stick walker, s21_walk1.py; the stick
  PERSISTS after an analog command, so every push ends with
  analog 0 0 2): (319.08, 195.18) -> (307.95, 167.00) in 38
  pushes, state 2 throughout — **no scenes, no field
  encounters, no prompts**. She passed within 0.9 units of the
  spent Sage's Path anchor (point 13) without incident.
- ENTRY: posed 2.9 units from the point-7 anchor, **the FIRST
  Cross entered** (state 2 -> 5 -> 20). Banner: **"Gateway of
  Fools and Hope"** — Chapter 1's third gateway. Entry disc
  20, Gil 390 carried in, board HUD HP 1973, banked KP 4,
  gauge 0. No entry scene fired in the first ~1,500 frames
  (the Sage's Path banter pattern did not repeat here).

### The Gateway of Fools and Hope — first census (no fights; nothing engaged)

- **13 discs**: {1, 2, 4, 12, 14, 15, 20, 21, 24, 32, 33, 34,
  43} (the disc-table fingerprint that identifies this board).
  Entry disc **20** (-18, -4). Board manager pointer agrees
  (0x095D123C -> record of grid 20).
- Full Scan pass (Select; DFS over the scan cursor, every hop
  verified by the cursor cell 0x095CD41C; all 13 discs
  visited, zero backtrack warnings). Pieces as the game
  presents them:
  - **21 (-14, -4): Fleeting Flash, Lv 2** — Battle Stage:
    World of Darkness. It stands on the ONLY disc linked to
    the entry (20-21): this board's toll piece — nothing else
    on the board is reachable without engaging it.
  - **1 (-14, -12): Treasure Chest** ("Contains helpful
    items.")
  - **24 (-2, -4): Treasure Chest** ("Contains helpful
    items.")
  - **15 (2, -8): "???"** — "Engage this piece to find its
    true form."
  - Empty transit discs: 2, 4, 12, 14, 32, 33, 34, 43.
  - **No Stigma of Chaos on the board as entered** — the
    Sage's Path pattern again: completion presumably rides on
    the ??? piece.
- Link graph (undirected, from the scan DFS): 20-21, 21-1,
  21-24, 1-2, 2-4, 2-12, 4-14, 12-14, 12-32, 14-15, 14-24,
  24-34, 34-33, 33-32, 33-43.
- OPEN QUESTION: a small blue figure stands on a middle disc
  (most consistent with disc 12 by the entry-view geometry).
  Its disc produced NO Scan info box, and the disc-object dump
  (0x095CD570 + 0x40 x grid, all 16 words) shows no field that
  cleanly separates piece discs from empty ones — Scan Mode
  presents nothing for it. Identity unresolved; traversal past
  the Flash (next session) should settle it. Harness note:
  pressing Right toward the Flash from disc 20 set the engage
  focus pointer (0x095CD410) to 21's record without hopping —
  battle-piece addressing as in sessions 13-19.
- Her position at census end: on disc 20 of the Gateway of
  Fools and Hope, Lv 10, HP 1973/1973, Gil 390, banked KP 4,
  gauge 0. State: ch1_s21_scanned.state.

### States and dumps (~/d12s)

ch1_s21_postshop.state (field, post-purchase, armor worn),
ch1_s21_walk1.state (posed at point 7), ch1_s21_enter1.state
(on the board, just entered), ch1_s21_board.state (board
settled), ch1_s21_scanned.state (post-census). Shop
intermediates ch1_s21_shop_open / inshop* / inbuy* / buy_a /
equip_*.state. Screenshots s21_*.png (shop series, map, walk,
entry v-series, board w-series, scan s21_sc_<grid>.png x13).
Scripts s21_*.py; logs s21_*.log.

### Session 21 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 21 section was committed on
  dissidia-012-ch1field (c8e4ae1) and merged here. Merge gates
  (session 12's): working tree clean; local main == origin/main
  (b29b163); merge-base == b29b163 (main a strict ancestor,
  branch 1 ahead / 0 behind). Pure fast-forward
  b29b163..c8e4ae1, pushed via the clone's git credentials,
  git ls-remote verified in-session: origin/main = c8e4ae1.
  This addendum commit follows on main.

## Session 22 — Gateway of Fools and Hope cleared; Kain; Chapter 1 complete; the Kunai bought

Branch dissidia-012-fools from main 18739e6. Start state
ch1_s21_scanned.state: Lightning Lv 10, HP 1973/1973,
Gil 390, banked KP 4, on the entry disc (20) of Chapter 1's
third gateway. All fight numbers below are battle-cell
measurements (session-17 block A pattern search per boot);
all rewards and contents are the game's own screens/text.

### Board geometry — corrections to the session-21 census

- The player steps cell-by-cell (grid ±1 / ±10) across
  PLATFORM cells, and the platform set is not the census's
  registered-disc set: **disc 22 IS a platform** (not in the
  census set); **discs 23 and 13 are NOT platforms**. Engage
  and chest prompts open only from a grid-adjacent disc.
- Session 21's scan-DFS "links" 21-1, 21-24 and 12-32 are
  scan-cursor jumps, not player traversals. Neutral engage
  focus on a disc is its own record; focus 0x000000FF = none.
- The Scan census also MISSED THREE PIECES (below): Scan
  Mode presentation is not exhaustive for this board.

### The toll fight — Fleeting Flash Lv 2 (disc 21)

Prompt verified on screen: "Fleeting Flash, Lv 2 — BATTLE
STAGE: World of Darkness — KP: Win battle". Cells: foe HP
1061 / BRV 101 / pool 303 (block A base this boot
0x096BBF98). **Won in 12 rotations** of the plain
session-13 rotation. She took one 109 hit (1973 -> 1864)
and **leveled 10 -> 11 mid-fight** (+61 heal -> ended
1925). Results text: "Level increased by 1! (10->11)",
"Basic stats up!", "Learned Lightning Strike!",
"Received 1 AP." **Gil 390 -> 404 (+14), KP gauge 0 -> 1,
no chain offered.** A post-win story fragment played
during the results crossing ("This is bad. Doubt they
could've gotten past this many." + a crystalline figure).
State: ch1_s22_toll_win.state.

### The blue figure — Idle Sky Pirate Lv 6 (disc 2)

Session 21's unidentified blue figure is the **Idle Sky
Pirate Lv 6 on disc 2** (prompt from disc 12: "Idle Sky
Pirate, Lv 6 — BATTLE STAGE: Old Chaos Shrine — KP: Win
battle"). Disc 2 is also chest 1's ONLY platform neighbor,
so the Pirate gated the chest. Fight: **won in 63
rotations** — foe HP 1303 / BRV 96 / pool 333 (block A
0x09691388); long passive stretches (its BRV never left 96
for most of the fight); one 267 hit taken (1925 -> 1658).
**Gil 404 -> 470 (+66), gauge 1 -> 2.** State:
ch1_s22_pirate_win.state.

**Chest 1** (disc 1, prompted from disc 2): **"Obtained
500 gil from chest!"** — **Gil 470 -> 970.** State:
ch1_s22_chest1.state.

### The southern wing — two more census misses

- **Delusory Dragoon Lv 2 on disc 33** (prompt from disc
  32; stage World of Darkness): **won in 9 rotations** —
  foe HP 1061 / BRV 101 / pool 309 (block A 0x096BBF98);
  one 127 hit taken; **leveled 11 -> 12 mid-fight** (+61
  -> 1592). Results: "Level increased by 1! (11->12)",
  "Received 1 AP." **Gil 970 -> 984 (+14), gauge 2 -> 3.**
  State: ch1_s22_dragoon_win.state.
- **A second Idle Sky Pirate Lv 6 on disc 34** (prompt
  from disc 33; stage Old Chaos Shrine) — the grey winged
  statue in the board screenshots is this manikin's board
  model. It blocks the last step to chest 24; the northern
  detour is impossible (13 not a platform; 4 reachable
  only via 14). Fight: **won in 30 rotations, ZERO HP
  damage taken** — foe HP 1303 / BRV 96 / pool 339 (block
  A 0x09691388); her HP attacks landed 292 x5. **Gil
  984 -> 1050 (+66), gauge 3 -> 4.** State:
  ch1_s22_pirate2_win.state.

**Chest 24** (prompted from disc 34): **"Obtained Red
Drop from chest!"** — no Gil change; the Red Drop is in
inventory, not auto-used. State: ch1_s22_chest24.state.

??? doorstep (ch1_s22_pre_unknown.state, disc 14):
Lightning Lv 12, HP 1592 (max 2095), Gil 1050, KP gauge
4, banked KP 4.

### The ??? piece — Kain Highwind, Lv 10

The disc-15 prompt reads "??? — Engage this piece to find
its true form." Reveal scene: Lightning kneeling over a
fallen Bartz ("Bartz, stay with us!"), Kain standing by in
dragoon armor. VS card: "Lv 12 Lightning VS Kain Highwind
Lv 10" (stage Old Chaos Shrine). Kain's cells: **HP 1459,
BRV 185, pool 355** (attempt-1 boot; pool read 361 in the
attempt 2/3 boots).

- **Attempt 1 (plain rotation, Lv 12, 1592 HP): LOSS at
  rotation 32 — Kain left at 139/1459 (1320 dealt).** Five
  clean 228 hits in the first 7 rotations (1459 -> 319),
  then her attacks whiffed for ~23 rotations while he
  chipped her down; she **leveled to 13 mid-fight** (+60
  heal at rotation 10 -> 1652), was BROKEN at the end
  (pool to him, his BRV 600) and killed. Fled cleanly to
  disc 14. **Fled-state HUD: Lv 13, HP 1652** — flee
  restored the engagement HP (1592) PLUS the level-up
  max-delta earned mid-fight (60), and the level
  persisted. (Refines the session-20 flee note: the
  restore is engagement snapshot + mid-fight level-up
  delta, not automatically the full new max.)
- **Attempt 2 (+15f drift, Lv 13, 1652 HP): LOSS at
  rotation 29 — Kain at 640 (819 dealt).** The drift hurt
  against Kain: he built BRV to 802 and landed a 1002
  spend (1652 -> 650 at rotation 13), then a 649 hit
  (-> 1 HP at rotation 27); she died at rotation 29.
  Fled; doorstep values unchanged (Lv 13, 1652, Gil
  1050, gauge 4).
- **Attempt 3 (plain rotation, same lineage): WIN in 27
  rotations — Lightning standing at 575 HP.** Her HP
  attacks landed steadily (252/192 chunks); she took
  hits of 245/185/231/231/185 but was never broken (the
  pool never moved off 361). Reward from game text:
  **"Obtained 150 gil." — Gil 1050 -> 1200.** KP gauge
  stayed 4.

### Completion — Report shell, Chapter 1 done

The post-win scene ride (50 x Cross with 240-frame runs,
the session-20 protocol) carried ctrl state 20 -> **21**,
landing in the **Story Mode Report shell**: "Report 01,
1/4 — World: A — Place: Town on the Hawk's Wing —
Inherited Memories -01-", progress bar 1 of ~21 squares
filled. **Beating the ??? completed the gateway — no
Stigma, no field landing** (the Sage's Path pattern).
With Crescent Lake and Sage's Path, **Chapter 1's gateway
chain is complete**; entering Customization afterwards
showed the accomplishment banner "Successfully
accomplished 012 - Chapter 1!". In the shell her HP reads
full again (2155/2155 at Lv 13 — the completion restored
her, as at Sage's Path).

Shell controls (measured; state68 stayed 21 throughout):
Start opens the Story Mode help menu ("What is Story
Mode? / Difficulty Settings / Skills [NEW] / Reports
[NEW]", o Close) — Start does NOT forward from the report
despite the "FORWARD START" corner label. Circle steps
back: help -> report detail -> Reports index ("Report /
Reports" over a coastal-town backdrop); Cross at the
index reopens the report detail; a third Circle from the
index exits to the game's **main menu** (Story Mode /
Battle Mode / Communications Mode / PP Catalog /
Collection / Data Config / Options; header offers
Triangle = Customization, R = Calendar; PLAY TIME
3:11:52). States: ch1_s22_boss_win.state,
ch1_s22_after_report{,2,3}.state, ch1_s22_shell.state
(main menu).

### Part C — the Kunai (bought and equipped)

The landing was the main menu, not the field; the brief's
skip conditions (Gil < 400, or the gateway not
completing) did not hold — Gil was 1200 — and
Customization is reachable from the main menu, so the
purchase was carried out there: Triangle -> Character
Select (Lightning Lv 13) -> "Check abilities and
equipment?" YES -> Customization -> Shop -> Buy ->
Parrying: **Kunai, 400 Gil** (Equip Level 1+, ATK+2
DEF+1). **Bought: Gil 1200 -> 800.** Equipped in the
off-hand (empty since the start): **ATK total 26 -> 28,
DEF total 26 -> 27** (stat panel format TOTAL (BASE +
GEAR): ATK 22 base, gear 4 -> 6; DEF 24 base, gear 2 ->
3; HP 2155 (1727 + 428), BRV 203 (165 + 38), LUK 16
unchanged; totals re-read from enlarged crops — the
panel's small font is easy to misread at native size).

Harness notes from the menu work: entering Customization
the first time after the chapter completion stacks
notification banners ("New accomplishments added!",
"Successfully accomplished 012 - Chapter 1!", "The
chocobo found a treasure chest!") that eat D-pad presses
— ~2,100 frames of plain waiting burns the stack before
navigating. The Customization top LIST is the only route
to Shop / Assist / Adjust Level; L/R cycle only the
Abilities -> Equipment -> Accessories -> Summons ring.
Circle steps focus back one layer at a time (rows ->
categories -> list); Start opens context help in menus;
leaving the shop took one Circle more than the screens
suggest (a press is absorbed by the buy-list -> front
transition). Battle block A bases this session:
0x096BBF98 / 0x09691388. Her battle-start BRV ran 267-328
at Lv 11-13.

### End state

Lightning **Lv 13, HP 2155/2155, Gil 800, banked KP 4,
KP gauge 4**; gear Broadsword / **Kunai** / Bronze Helm /
Leather Armor; Tifa assist; Red Drop in inventory.
Chapter 1 complete — the story continues from Story Mode
on the main menu. Final states: ch1_s22_kunai.state
(inside Customization > Equipment, Kunai equipped) and
ch1_s22_shell.state (clean main menu). Milestone states
this session (all ~/d12s): ch1_s22_start, toll_win, on12,
at2prompt, pirate_win, chest1, probe_end, nav2d_stop,
dragoon_win, at34prompt, pirate2_win, chest24,
pre_unknown, boss_fled, boss_win, after_report{,2,3},
shell, plus menu intermediates (incust, atshop*,
list_cursor, atkunai, kunai_bought, backtolist, atequip,
atslots, atitems, kunai). Scripts s22_*.py; logs
s22_*.log; screenshots s22_*.png. (One state file,
"ch1_s22 tabs.state", has a space in its name — avoid
loading it; the drv command line splits on spaces.)

### Session 22 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 22 section was committed on
  dissidia-012-fools (a0ae421) and merged here. Merge gates
  (session 12's): working tree clean; local main == origin/main
  (18739e6); merge-base == 18739e6 (main a strict ancestor,
  branch 1 ahead / 0 behind). Pure fast-forward
  18739e6..a0ae421, pushed via the clone's git credentials,
  git ls-remote verified in-session: origin/main = a0ae421.
  This addendum commit follows on main.

## Session 23 — Chapter 2 opened: Vaan, the fleeing gauntlet, Hidden Darkness Gateway censused

Branch dissidia-012-ch2 from main b9fff11. Start state
ch1_s22_kunai.state — the TRUE session-22 end (Gil 800,
Kunai worn). The s22 shell state predates the Kunai
purchase (Gil 1200 there) and was NOT used; the lineage
was re-based by backing out of Customization from the
kunai state to a clean main menu (ch2_s23_menu.state,
Gil 800 verified). All fight numbers below are
battle-cell measurements (session-17 block A pattern
search per boot); all rewards, names and lines are the
game's own screens/text.

### Entering Chapter 2 — menus and lead

- Main menu -> Story Mode -> scenario banner "Main
  Scenario 012: Treachery of the Gods — The Twelfth
  Cycle" -> chapter carousel. The ring holds the three
  unlocked chapters: Prologue (card "CHAPTER 0 —
  Prologue - Path to Sanctuary", header shows Lightning
  Lv 13), Chapter 1 (checked, complete), Chapter 2.
  Left from the Prologue wraps to Chapter 2.
- Chapter 2 card: "Chapter 2 - Stern Eyes", header lead
  **Vaan, Lv 1**; synopsis line: "Vaan successfully
  flees from the hordes of manikins..." Cross opens
  "Play this story?" (YES/NO; hint: Triangle opens
  Customization). YES starts the chapter.
- **Chapter 2's lead is Vaan, a fresh Lv 1** — Lightning
  does not carry over as lead. His full starting record
  (Customization, read on the board after entry): Lv 1;
  HP 1000/1000; CP 330 (abilities use 265); BRV 95;
  ATK 10; DEF 13; LUK 10; EXP NEXT 4194 / TOTAL 806;
  **no equipment in any slot** (every gear bonus +0);
  bravery abilities Spear (ground), Greatsword
  (ground), Spear (midair), Greatsword (midair), CP 30
  each; **Assist already set: Onion Knight** (BRV:
  Blizzard, Turbo-Hit; HP: Blade Torrent, Comet).
  Shared pools carry over: Gil 800, banked KP 8 (s22
  ended banked 4 + gauge 4 — the gauge paid out at
  chapter completion, the session-13/14 pattern).

### The opening and the field

- Opening: narration card over the world map ("While
  retreating after parting ways with Lightning, Vaan
  strays from his friends." — map marker in the
  southeast), then an in-engine solo scene (state 3):
  "And I've lost sight of the others, too. Guess I
  better find a way back..." Control on the field at
  (307.52, -2.13, 295.72), HUD Lv 1 / HP 1000 / Gil 800
  / KP 8.
- A glowing dark rock stands ~2.85 units away at point
  17 (304.79, -3.02, 296.54). No gate maps to point 17
  this boot and six Crosses around it drew no tester
  response — scenery/story dressing, not an enterable
  anchor. **Cross on this field is a forward DASH**
  (~4-7 units per press), not a neutral interact: it
  moved him during the opening ride and later dashed
  him into the gateway (below).
- Field survey (this boot): GateMgr via ctrl+0x88 =
  0x095D7110, count 12 (gates 11-22); point table
  unchanged at 0x095E61C0, 18 points, same coordinates
  as Chapter 1. **Gate->point is reshuffled for
  Chapter 2**: 11->10, 12->11, 13->4, 14->5, 15->6,
  16->7, 17->8, 18->9, 19->12, 20->13, 21->14, 22->15.
  Chapter 1's gateway anchors (pts 7, 13, 14) sit in
  the inert family now (finished). Tester-reachable
  (lh(A+4) >= 0): gate 11 (lh 8) -> pt 10 (154.08,
  6.72, 229.42); gate 12 (lh 7) -> pt 11 (222.00,
  3.75, 193.50); gate 15 (lh 11) -> pt 6 (253.93,
  2.72, 181.66).
- Terrain: ridge-and-canyon country — straight-line
  and cardinal steering wall constantly (a cliff over
  the sea bounds the start plateau's south edge at
  Z~272; an east-west wall runs at Z~234-235). The
  walker that worked: vector steering with an 8-way
  probe on stuck (probe from a savestate, best-gain
  direction becomes a temporary bias). Route actually
  walked: start -> north coast corridor -> AMBUSH 1 ->
  relocated east (344.75, 250.28) -> southwest along
  the cliff tops -> inner corridor via (318, 266) and
  (308, 276) -> north via (300, 262), (298, 244),
  (292, 228) -> AMBUSH 2 on the high shelf at (281.1,
  7.63, 219.7) -> relocated to (268.57, 1.67, 253.34)
  -> north through the canyon notches (probe-found at
  (277.4, 241.4) and (269.2, 217.9)) -> gateway
  approach scene at (257.55, 2.41, 189.18).

### The fleeing gauntlet — two scripted field ambushes

Both fired on plain movement (no Cross), state 2 -> 5
-> 20, landing on a VS card: "Lv 1 Vaan VS ? Lv N",
foe name hidden, "JUDGMENT" under the foe, "Next" on
Cross. The standard rotation fought both.

- **Ambush 1** at (305.61, 251.35) walking north:
  ? Lv 1, stage **Order's Sanctuary**. Cells (block A
  0x0969E588): foe HP 338 / BRV 95 / pool 133; Vaan
  BRV 95. **WIN in 16 rotations, zero HP damage** —
  his HP attacks landed in 95-chunks (338 -> 243 ->
  148 -> 53 -> 0). Reward (game text): "Obtained
  6 gil." Gil 800 -> 806. The win RELOCATED him to
  (344.75, 250.28), +39 X east across the ridge.
- **Ambush 2** at (281.07, 219.73): ? Lv 2, stage
  **Lunar Subterrane**. Cells (block A 0x096D6938):
  foe HP 399 / BRV 111 / pool 139. **WIN in 17
  rotations**; took two 111 hits (1000 -> 889 -> 778).
  Reward: "Obtained 14 gil." Gil 806 -> 820. Landing
  (268.57, 1.67, 253.34). Field HUD afterwards reads
  HP 1000 — **field-battle damage does not persist**
  to the field.
- Neither field battle showed an EXP/level screen;
  Vaan is still Lv 1. As measured, these pay Gil only.

### Hidden Darkness Gateway — entry and census

- Approach scene (state 3) in front of a stone arch
  with a glowing seal, ~8.6 units NE of the pt-6
  anchor: Vaan: "Hm? This gateway seems different
  from the others. What's the deal?" / "Think I'll
  take a little peek inside, then I wouldn't be
  taking a detour."
- Entry: the post-scene Cross dashed him (257.55,
  189.18) -> (257.85, 181.73), inside the tester
  radius; by the next sample the game was on the
  board loading screen (state 20). Banner: **"Hidden
  Darkness Gateway"** — gate 15 -> point 6, Chapter
  2's first gateway.
- Board entry: state 20, **entry disc 23** (board
  manager pointer 0x095D123C -> record of 23 agrees);
  Vaan Lv 1, HP 1000, Gil 820, banked KP 8, gauge 0.
  An entry banter plays as board-overlay dialogue
  (ctrl stays 20; it eats input until crossed
  through — the Sage's Path pattern): Vaan: "I wish
  we had an airship. It'd make getting to Chaos so
  much easier..." / Onion Knight: "How can you have
  a carefree daydream at a time like this?" / Vaan:
  "What the—! How long have you been there? Don't
  scare me like that!"
- **Census** (full Scan DFS from the entry disc,
  every hop cursor-verified, zero backtrack warnings;
  coordinates (x, z) = (4*col - 18, 4*row - 4)):
  **18 discs** {0, 3, 4, 10, 11, 12, 13, 14, 20, 21,
  22, 23, 24, 32, 33, 34, 35, 36}:
  - 23 (-6, -4): ENTRY (Vaan)
  - 0 (-18, -12): **Treasure Chest**
  - 3 (-6, -12): **Delusory Dragoon, Lv 5** (Lunar
    Subterrane)
  - 4 (-2, -12): no Scan info box
  - 10 (-18, -8): no Scan info box
  - 11 (-14, -8): **Onion Knight (Assist Character)**
  - 12 (-10, -8): **Phantasmal Harlequin, Lv 5**
    (Lunar Subterrane)
  - 13 (-6, -8): no Scan info box
  - 14 (-2, -8): **Stigma of Chaos** — "Engaging
    this piece finishes the level."
  - 20 (-18, -4): no Scan info box
  - 21 (-14, -4): **Delusory Dragoon, Lv 2** (World
    of Darkness)
  - 22 (-10, -4): no Scan info box
  - 24 (-2, -4): no Scan info box
  - 32 (-10, 0): **Idle Sky Pirate, Lv 2** (World of
    Darkness)
  - 33 (-6, 0): no Scan info box
  - 34 (-2, 0): no Scan info box
  - 35 (2, 0): **Fleeting Flash, Lv 5** (Lunar
    Subterrane)
  - 36 (6, 0): **Treasure Chest**
- Link graph (undirected, from the scan DFS): 23-13,
  23-22, 23-24, 23-33, 13-3, 13-12, 13-14, 3-0, 3-4,
  4-14, 14-24, 24-34, 34-33, 34-35, 35-36, 33-32,
  32-22, 22-12, 22-21, 12-11, 11-10, 11-21, 10-0,
  10-20, 20-21.
- Two structural notes: (1) **this board's Stigma is
  present from entry** — Chapter 1's gateways had
  none as entered (theirs rode on the hidden boss);
  Vaan's own line flags the difference ("seems
  different from the others"). (2) "No Scan info box"
  means empty AS PRESENTED — session 22 proved Scan
  can miss pieces (Fools and Hope missed three), so
  presence is not excluded for the no-box discs;
  disc 4 sits in the manikin row and is the prime
  suspect. Nothing was fought on the board this
  session (brief: census only).

### End state

Vaan stands on disc 23 of the Hidden Darkness
Gateway: **Lv 1, HP 1000/1000, Gil 820, banked KP 8,
gauge 0**, no equipment, Onion Knight assisting.
States (~/d12s): ch2_s23_menu (clean main menu,
correct lineage), ch2_s23_open (field control after
the opening), ch2_s23_transition / _post_transition
(ambush 1 trigger / VS card), ch2_s23_fight1_win,
ch2_s23_transition2 (ambush 2 VS card),
ch2_s23_fight2_win, ch2_s23_walk9_end (approach
scene), ch2_s23_scene_end (entry loading),
ch2_s23_board (banter pending), ch2_s23_board_clear
(banter done), ch2_s23_scanned, ch2_s23_final (on
the board, post-Customization read). Dumps:
s23_field.bin, s23_board.bin, s23_f_start.bin,
s23_f_end.bin, s23_f2_start.bin, s23_f2_end.bin.
Scripts s23_*.py; logs s23_*.log; screenshots
s23_*.png (scan set s23_sc2_<grid>.png x18).

### Session 23 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 23 section was committed on
  dissidia-012-ch2 (62f71cc) and merged here. Merge gates
  (session 12's): working tree clean except the doc; local
  main == origin/main (b9fff11); merge-base == b9fff11 (main
  a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward b9fff11..62f71cc, pushed via the clone's git
  credentials, git ls-remote verified in-session:
  origin/main = 62f71cc. This addendum commit follows on main.

## Session 24 — Hidden Darkness Gateway cleared: Vaan geared, three manikins beaten, Stigma engaged

Branch dissidia-012-darkness from main 2140699. Start
state ch2_s23_final.state (Vaan on disc 23 of the
Hidden Darkness Gateway, Lv 1, unequipped, Gil 820,
banked KP 8, gauge 0). All fight numbers below are
battle-cell measurements (session-17 block A pattern
search per boot); all rewards, names and lines are
the game's own screens/text.

### Part A — exit, shop, re-entry

- Exit flow (nothing fought yet, so the reset cost
  nothing): Start -> Pause menu; its footer reads
  "Save EXP/rewards gained and escape gateway. *KP
  gained in gateway will be lost." Down x2 -> Exit
  Gateway -> confirm "Exit gateway?" (cursor starts
  on NO) -> Left -> Cross (YES). Landed on the field
  at the gateway arch, state 2, conditions panel up,
  Gil 820 intact (gauge was 0, nothing to lose).
  State ch2_s24_field.state.
- Shop (field Triangle -> Customization, state 11 ->
  Shop, 7th menu item; header "Vaan Lv 1 — GIL 820 —
  INVENT 3%"; the shop trades in Gil, session 21).
  Stock read live for Vaan — Weapons: Broadsword 500
  (Sword, ATK+4), Knife 500 (Dagger, ATK+2 DEF+2),
  Longsword 500 (Greatsword, ATK+6 DEF-2), Katana
  450 (BRV+9 ATK+4 DEF-2), Spear 1140, Axe 1300,
  Rod 450, Staff 450, Leather Gloves 400, Oaken Pole
  450 (below the fold). Headgear: Leather Hat 500
  (BRV+48), Bronze Helm 300 (BRV+38 DEF+2), Headband
  300.
- Bought (weapon first, then headgear; each purchase
  runs Cross -> quantity prompt "TOTAL n GIL /
  REMAINING GIL m" -> Cross -> "Will you equip this
  now?" YES -> "Gear equipped."): **Longsword, 500
  Gil**, then **Bronze Helm, 300 Gil**. Gil 820 ->
  320 -> **20**. Verified worn on the Equipment
  screen (weapon + head slots; off-hand and body
  still empty). Vaan's geared Lv 1 totals: HP 1000,
  CP 330, **BRV 133** (95 + 38 helm), **ATK 16**
  (10 + 6 sword), **DEF 13** (13 - 2 sword + 2
  helm), LUK 10.
- Harness lesson: leaving a submenu resets the
  Customization list cursor to the top (Abilities).
  An Up x5 from an assumed Shop position overshot
  into **Adjust Level** ("Change Lv 001"); backed
  out with Circle without confirming — Lv 1 and all
  stats verified unchanged. Rule: screenshot-verify
  the menu cursor before any Cross.
- Re-entry: one Cross at the arch re-entered the
  gateway; the entry banter re-played and was
  cleared by the scene protocol. Clean doorstep
  saved: ch2_s24_entry.state (disc 23, gauge 0,
  Gil 20).

### Part B — the board clear

Route lesson first: **there is no 22-33 link.** A
nav loop pressing Down from 22 kept raising the
Idle Sky Pirate prompt (disc 32 is adjacent below
22) and its Crosses engaged that fight blind; Vaan
was found mid-battle at ~316 HP when the script
died unsaved — discarded, no harm. The prompt from
22 IS the route to the Pirate. A second discarded
attempt: fight 1's first run crossed blindly after
the win and engaged the auto-opened chain battle;
the fight was re-run from its VS state with the
stop-at-win runner and came out cell-for-cell
identical (the board fights are deterministic).
Rule reconfirmed: never blind-Cross a post-win
return — inch with waits and watch the focus cell.

- **Fight 1 — Delusory Dragoon Lv 2 (disc 21,**
  engaged from 22; stage World of Darkness; prompt
  line "KP: Win battle"): foe HP 1061 / BRV 83 /
  pool 206 (block A 0x096BBF98 this boot). **WIN in
  20 rotations**; Vaan 1000 -> 814 (two 83 hits, one
  20 hit). +14 Gil (20 -> 34). The win auto-opened
  a chain offer (Idle Sky Pirate, disc 32);
  identified by focus + screenshot and **cancelled**
  (Circle -> "Cancel chain?" cursor starts on NO ->
  Left -> Cross YES) — held KP paid into the gauge:
  **gauge 0 -> 1**. End state ch2_s24_f1_done.state
  (disc 22, HP 814, Gil 34).
- **Onion Knight piece (disc 11,** prompt from 21:
  "Onion Knight — Assist Character"): engaging
  played a one-line board-overlay scene — Onion
  Knight: **"Guess it can't be helped. I'll lend a
  hand. But I'll only help in this gateway."** One
  Cross ended it. Meaning: he is the active assist
  **within this gateway** (he was already the set
  Assist in Customization). State
  ch2_s24_ok_done.state.
- **Chest on disc 0** (route 21 -> 20 -> 10, prompt
  from 10): **"Obtained 200 gil from chest!"** Gil
  34 -> **234**. State ch2_s24_chest0_open.state.
  Discs 20 and 10 are EMPTY (plain hops, no prompt).
- **Fight 2 — Idle Sky Pirate Lv 2 (disc 32,**
  engaged from 22; World of Darkness): foe HP 1061
  / BRV 83 / pool 206 — both Lv 2 manikins on this
  board share the stat line at Vaan Lv 1. **WIN in
  21 rotations** at engagement HP 814; took one 116
  hit (814 -> 698); Vaan BROKE the Pirate at
  rotation 20 (pool 206 -> 0, his BRV climbed to
  319) and finished at rotation 21. +14 Gil
  (234 -> 248). Chain offer (**Phantasmal Harlequin
  Lv 5**, disc 12, stage Lunar Subterrane)
  identified and **cancelled -> gauge 1 -> 2**. End
  state ch2_s24_f2_done.state (disc 22, HP 698,
  Gil 248). Discs 33 and 34, traversed next, are
  both EMPTY; disc 13 was likewise landed on
  cleanly during the entry test hop.
- **Fight 3 — Fleeting Flash Lv 5 (disc 35,**
  engaged from 34; stage Lunar Subterrane; the
  prompt carries a red "hard" icon; the Flash is
  the only path to chest 36). This fight broke the
  standard runner twice before it was understood:
  (1) this boot's block A sits at the session-17
  alternate base **0x096D5A38** (the shop-heavy boot
  path), not 0x096BBF98; (2) a Lv 5 engagement
  inserts an **Opponent Info page** between the VS
  card and the arena; (3) during the foe's opening
  summon cinematic the foe-HP cell reads junk (1,
  then 17) while its BRV climbs; (4) left idle, the
  Flash kills Vaan in ~1,200 frames — a diagnostic
  run's final blow dealt **944 damage**. The
  Opponent Info page gives the foe's full record:
  **HP TOTAL 1** (BASE 1242 under a large negative
  modifier), CP 335, **BRV 251**, ATK 14, DEF 10,
  LUK 12; equipment Cracked Shield, Mythril Helm,
  Rusted Armor; **Bravery Bonus: Lv +4**. A glass
  cannon: 1 HP behind a truck of bravery. With the
  policy started the moment the battle commenced:
  **WIN in 1 rotation** (foe BRV had already
  powered up 251 -> 350 behind its summon "Aura";
  pool 158). Rewards: **+50 Gil** (248 -> 298),
  **+1 KP straight into the gauge (2 -> 3)** — no
  chain offer materialized this time — **10 PP**,
  and the victory screen's own lines: **"Level
  increased by 1! (1->2) / Basic stats up! /
  Learned Crossbow (ground)!"** The level-up
  delta-healed him on the board: HP 698 -> **759**
  (new max 1061). States ch2_s24_f3_vs /
  _f3_winraw / _f3_inch.
- **Chest on disc 36** (hop 35, prompt from 35):
  **"Obtained Blue Drop from chest!"** — an item,
  not Gil (Gil unchanged at 298); the end-of-session
  Customization banner "New prizes creatable
  through Battlegen!" hangs on it. State
  ch2_s24_chest36_open.state.
- **Disc 24** (34 -> up): landed on clean — EMPTY.
  With that, every no-Scan-info disc except disc 4
  (10, 13, 20, 24, 33, 34) has now been landed on
  and proved empty.
- **Disc 4 — verdict: empty, by pattern.** Disc 4
  showed no Scan info in the session-23 census and
  none in a dedicated Scan Mode pass this session
  (cursor walked the top row; no info panel). A
  focus-prompt probe is physically impossible: its
  only neighbors are disc 3 (the Lv 5 Dragoon's
  battle disc) and disc 14 (the Stigma — prompt
  only, cannot be stood on). It blocks nothing
  (both chests open, Stigma reachable) and matches
  the board's six other no-info discs, all proved
  empty on landing. Recorded as empty; the Lv 5
  Dragoon fight that alone could prove it directly
  was not bought for a disc that gates nothing.
- **Stigma of Chaos (disc 14)** — prompt from 24,
  the game's text: "Stigma of Chaos — Engaging
  this piece finishes the level." Engaged with
  Cross. No scene or report page held for input:
  the finish auto-flowed (state68 20 -> 2) and
  Vaan landed **on the field in front of the
  gateway arch**, HP fully restored. **Banked KP
  8 -> 11** — the gauge's 3 KP paid into the bank
  on completion, exactly. Gil unchanged (298).
  **Hidden Darkness Gateway complete.** Left
  standing, optional per the brief: Delusory
  Dragoon Lv 5 (disc 3) and Phantasmal Harlequin
  Lv 5 (disc 12).

### End state

Vaan stands on the Chapter 2 field at the Hidden
Darkness Gateway arch, gateway complete: **Lv 2,
HP 1061/1061, CP 265/331, BRV 139, ATK 17, DEF 14,
LUK 11; EXP NEXT 1087 / TOTAL 9218; Gil 298;
banked KP 11**; gear Longsword + Bronze Helm;
Crossbow (ground) learned; Blue Drop in inventory;
Onion Knight assist. Final Customization read also
shows BATTLES 10 and an Accessories menu entry now
present. States (~/d12s): ch2_s24_field,
ch2_s24_entry, ch2_s24_f1_vs / _f1_done,
ch2_s24_ok_done, ch2_s24_chest0_open, ch2_s24_f2_vs
/ _f2_done, ch2_s24_f3_prompt / _f3_vs / _f3_winraw
/ _f3_inch, ch2_s24_chest36_open,
ch2_s24_stigma_prompt, ch2_s24_stigma_inch,
ch2_s24_final (field, gateway complete). Scripts
s24_*.py; logs s24_*.log; screenshots s24_*.png.

### Session 24 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 24 section was committed on
  dissidia-012-darkness (753087b) and merged here. Merge gates
  (session 12's): working tree clean except the doc; local
  main == origin/main (2140699); merge-base == 2140699 (main
  a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward 2140699..753087b, pushed via the clone's git
  credentials, git ls-remote verified in-session:
  origin/main = 753087b (gh api agreed). This addendum commit
  follows on main.

## Session 25 — Encounters and Treason cleared: the drift search finds Terra's stream at +10 (2026-11-17)

Branch dissidia-012-encounters from main 59c07d9
(verified via gh api before the branch was cut;
ls-remote over the tunnel hung once this session,
gh api is the accepted fallback). Start state
ch2_s24_final.state (Vaan on the Chapter 2 field at
the Hidden Darkness arch, Lv 2, HP 1061/1061,
Gil 298, banked KP 11, Longsword + Bronze Helm,
Onion Knight assist). All fight numbers below are
battle-cell measurements (session-17 block A
pattern search per boot); all rewards, names and
lines are the game's own screens/text. Guide-derived
claims are labeled guide data.

Guides first (standing rule): the FF Wiki's
"Treachery of the Gods/Chapter 2" page, Suih's
GameFAQs guide, and the GameSpy/IGN walkthrough
(p.9). Chapter 2's intended gateway order (guide
data): **Hidden Darkness (session 24, done) ->
Encounters and Treason -> a field segment
(Phantasmal Harlequin Lv 8, False Stalwart Lv 6)
-> Gulg Gateway (boss: Kefka Lv 10) -> Snow of
Sorrow (optional, blue) -> Seasonless Gateway
(final).** GameSpy recommends Lv 5-7 before the
red (Encounters and Treason) gateway. The wiki on
that gateway (guide data): KP Chance "Win Battle";
its boss is a Cosmos piece, **Terra Branford,
Lv 7** — she starts in EX Mode (Trance), casts
Tornado repeatedly (Dualcast can double it), has
no bravery attacks and never uses EX Burst; there
is **no Stigma on the board — beating Terra is
the finish** ("Defeat to exit gateway").

### Part A — field walk, entry, census

- Gate census from the start state (script
  s25_census.py): **the gate->point map is
  UNCHANGED from session 23** — clearing Hidden
  Darkness did not reshuffle it. Live gates:
  11 -> pt 10 (154.08, 6.72, 229.42), lh 8;
  12 -> pt 11 (222.00, 3.75, 193.50), lh 7;
  15 -> pt 6 (253.93, 2.72, 181.66), lh 11
  (the blue/repeatable one). All other gates
  lh -1. Point table unchanged at 0x095E61C0
  (18 real points). Vaan's field position at
  start: (257.85, 2.19, 181.73).
- The walk to the pt-11 anchor took two walkers:
  the direct line west died in a walled bowl at
  (247.4, 183.2); a southern swing via (254, 192),
  (244, 200), (232, 198) arrived at (224.50, 3.75,
  193.48), ~2.5 units from the anchor, after 16
  stuck-probes — this terrain is rough and slidey.
  State ch2_s25_walk2.state.
- **Entry: the first Cross fired it** (state
  2 -> 5 -> 20, budget-two-Crosses rule kept but
  unneeded), entry disc **53**. The banner
  screenshot was missed (first shot caught the
  "Now Loading" screen); the gateway's identity
  rests on the census below, whose contents match
  the wiki's Encounters and Treason piece list
  one-for-one — **this is the Encounters and
  Treason gateway** (gate 12 -> pt 11).
- **Census** (full Scan DFS, zero backtrack
  warnings; s25_scan.py): **24 discs** — 0, 11,
  12, 13, 14, 15, 20, 21, 25, 30, 31, 33, 34, 35,
  36, 37, 40, 43, 50, 51, 52, 53, 63, 64. Entry
  53 (-6, 8). Contents, from Scan info boxes:
  - 51 (-14, 8): **Phantasmal Girl Lv 2**
    (Order's Sanctuary)
  - 50 (-18, 8): **Treasure Chest**
  - 34 (-2, 0): **Fleeting Flash Lv 2**
    (World of Darkness)
  - 36 (6, 0): **Delusory Dragoon Lv 2**
    (Order's Sanctuary)
  - 15 (2, -8): **Idle Sky Pirate Lv 2**
    (World of Darkness)
  - 30 (-18, 0): **Phantasmal Harlequin Lv 5**
    (Order's Sanctuary)
  - 31 (-14, 0): **"???" piece** — "Engage this
    piece to find its true form." (= Terra, per
    the guide data above.)
  - No info box (empty as presented): 0, 11, 12,
    13, 14, 20, 21, 25, 33, 35, 37, 40, 43, 52,
    63, 64.
  - **No Stigma and no assist piece on this
    board** — the finish is the Terra fight.
- **Movement lesson (measured, s25_diag52*.py):
  scan links are NOT gameplay links across long
  gaps.** The scan edge 52-up->12 spans four
  empty rows; in normal mode neither d-pad Up
  nor analog Up moves Vaan there (focus stays on
  his own record, grid never changes, long holds
  fail). The same failure hit 25-left->21 (four
  empty columns). Every one-disc-step link tried
  worked first try or nearly, and beaten pieces'
  discs are standable (the Pirate's disc 15 was
  stood on after its fight). Practical rule:
  route on one-step links; treat multi-row/column
  scan edges as scan-only until proven.

### Part B — the board clear

Vaan entered at Lv 2, HP 1061 (board HP), Gil 298,
gauge 0. All four Lv 2 manikins beaten; every
chain offer — none appeared this session.

- **Phantasmal Girl Lv 2 (disc 51**, prompt from
  52; prompt verified by screenshot: "KP: Win
  battle", stage Order's Sanctuary): block A at
  the alternate base 0x096D5A38 this boot; foe
  HP 1061 / BRV 73 / pool 218. **WIN in 65
  rotations** — far harder than Hidden Darkness's
  Lv 2s (20-21 rotations): her Tornado (midair)
  HP attack landed repeatedly, Vaan 1061 -> **479**.
  Rewards from the result screens: **+14 Gil
  (298 -> 312)**, gauge 0 -> 1 (no chain offer),
  **"Level increased by 1! (2->3) / Basic stats
  up! / Learned Auto Assist!"** No visible
  delta-heal this time (board HUD: Lv 3, HP 479).
- **Chest (disc 50**, prompt from 51): **"Obtained
  Orange Drop from chest!"** (game text; Gil
  unchanged at 312 — an item, matching the wiki's
  listed first contents for this gateway, guide
  data). State ch2_s25_chest_open.state.
- **Fleeting Flash Lv 2 (disc 34**, prompt from
  33 via the one-step route 51->52->53->43->33):
  **WIN in 16 rotations, zero damage taken**
  (479 -> 479). +14 Gil (312 -> 326), gauge
  1 -> 2, no level-up.
- **Delusory Dragoon Lv 2 (disc 36**, prompt from
  35): **WIN in 17 rotations**; the level-up
  landed mid-fight (HP 479 -> 540, the delta
  heal). Rewards: +14 Gil (326 -> 340), gauge
  2 -> 3, 10 PP, **"Level increased by 1! (3->4)
  / Basic stats up! / Learned Crossbow
  (midair)!"**
- **Idle Sky Pirate Lv 2 (disc 15**, prompt from
  25): **WIN in 21 rotations**, HP 540 -> 488.
  Rewards: +14 Gil (340 -> 354), gauge 3 -> 4,
  **"Level increased by 1! (4->5) / Basic stats
  up! / Learned Auto Assist Lock On! / Learned
  Auto Recovery!"**
- **Phantasmal Harlequin Lv 5 (disc 30**, prompt
  from 20; optional — it blocks nothing, Terra
  is reachable via 33/21): **LOSS in 21
  rotations.** Vaan dealt 436 of its 1242 HP
  before its bravery (Hyperdrive) climbed to
  432 and finished him. Fled clean: back on
  disc 20 at the full engagement HP 488, Gil
  and gauge untouched. Left standing.
- **Terra Branford, Lv 7 (disc 31, the "???"
  piece**; prompt from 21). Engaging opened a
  board scene first — Vaan: "Someone's over
  there—"; Terra, entranced: "Destroy... Must
  destroy all foes..." Measured fight cells
  (block A 0x096BBF98): **foe HP 702** (the
  wiki's figure exactly), starting foe BRV 137,
  pool 194; Vaan 488 HP / 156 BRV. Measured
  behaviors: Tornado is her only offense; her
  bravery resets after an HP attack and rebuilds
  fast in Trance; **she regenerates +10 HP per
  rotation while not being hit** (three +10
  ticks logged in both losing runs) — idle time
  refunds her; and the fight turns on break
  cycles (whoever breaks takes the stage pool).
  The standard rotation (drift 0) lost in 14
  rotations having dealt only 94. The session
  19-20 method — same VS state, vary one stream
  parameter — ran as a search (all losses fled
  clean to 488 HP):
  - drift +15: LOSS in 16 rotations, **430
    dealt** (Terra to 272) — he broke her once
    (a 324 HP attack) but died to her second
    break cycle at 161 HP.
  - drift +30: LOSS in 11, 0 dealt.
  - drift +15, assist every 4: LOSS in 13, 94
    dealt (the extra chords wreck the phase).
  - drift +20: LOSS in 16, 276 dealt.
  - **drift +10, assist every 6: WIN in 20
    rotations, Vaan standing at 319 HP.**
  Rewards (screens + cells): **+84 Gil
  (354 -> 438)**; Battlegen: **"Girl's Dream
  x1 created!"**; post-fight scene on the
  board (Vaan: "Stop it already. I don't plan
  to be beat—", then Terra flees — the wiki's
  story beat, guide data); the gauge paid out:
  **"Received 4 KP!" — banked KP 11 -> 15.**
  No level-up (still Lv 5).
- **Finish:** no Stigma — beating Terra was the
  finish condition, as the guide data said. A
  white fade took Vaan to a **new field segment**
  (snowy mountains, a shining spire in the
  distance) and a closing line from Vaan: "I
  think there's a gateway leading to the other
  side beyond that large snowy mountain." Field
  landing restored HP to full. **Encounters and
  Treason complete.** (Whether this gateway can
  be re-entered, e.g. for the Harlequin rematch,
  was not tested.)

### Part C — shop record (no purchase)

The Customization banner announced "New
inventory in the shop!" At the natural menu stop
after landing, the Weapons list was read live for
Vaan (Gil 438): Broadsword 500 (Sword, ATK+4 —
for Vaan ATK 20 -> 18, DEF 17 -> 19: a
sidegrade), Knife 500, Longsword 500 (owned,
Held 1), **Zweihander — TRADE** (marked NEW;
materials, not Gil), Katana 450, Spear 1140
(HP-86 ATK+8 DEF-2 — ATK 20 -> 22), Axe 1300,
Rod 450, Staff 450. **Nothing is a straight
upgrade he can afford; no purchase** (the
brief's rule). Menu lesson reconfirmed the hard
way once more: an unverified six-Down walk from
a post-submenu cursor landed on **Adjust Level**
— backed out with Circle unconfirmed, level and
stats verified unchanged; the successful walk
screenshot-verified the cursor at every step
(session 24's rule).

### End state

Vaan stands on the new Chapter 2 field segment
by the snowy mountain, Encounters and Treason
complete: **Lv 5, HP 1242/1242, CP 265/335,
BRV 156, ATK 20, DEF 17, LUK 12; EXP NEXT 648 /
TOTAL 27382; Gil 438; banked KP 15**; gear
Longsword + Bronze Helm (unchanged); assist
Onion Knight. Learned this session: Auto Assist,
Crossbow (midair), Auto Assist Lock On, Auto
Recovery. Items: Orange Drop (new, chest),
Blue Drop (session 24), Girl's Dream x1
(Battlegen-created). The status panel's BATTLES
counter reads 4. Left standing, optional:
Phantasmal Harlequin Lv 5 (disc 30). Next per
the guide order (guide data): the field segment
battles, then the Gulg Gateway and Kefka.
States (~/d12s): ch2_s25_walk2, ch2_s25_entered,
ch2_s25_board, ch2_s25_girl_prompt / _girl_vs /
_girl_winraw / _girl_inch, ch2_s25_chest_open,
ch2_s25_flash_prompt / _flash_vs / _flash_winraw
/ _flash_inch, ch2_s25_dragoon_prompt /
_dragoon_vs / _dragoon_winraw / _dragoon_inch,
ch2_s25_pirate_prompt / _pirate_vs /
_pirate_winraw / _pirate_inch,
ch2_s25_harlequin_prompt / _harlequin_vs /
_harlequin_fled, ch2_s25_terra_prompt /
_terra_vs, ch2_s25_terra10_winraw,
ch2_s25_terra_inch, ch2_s25_final (field, gateway
complete). Scripts s25_*.py (incl. the generic
s25_nav.py / s25_fight.py / s25_fight2.py /
s25_inch.py); logs s25_*.log; screenshots
s25_*.png.

### Session 25 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 25 section was committed on
  dissidia-012-encounters (d49d44f) and merged here. Merge gates
  (session 12's): working tree clean except the doc; local
  main == origin/main (59c07d9); merge-base == 59c07d9 (main
  a strict ancestor, branch 1 ahead / 0 behind). Pure
  fast-forward 59c07d9..d49d44f, pushed via the clone's git
  credentials and verified with gh api:
  origin/main = d49d44f6c0f005cdeacc173a95a29a4504bf48d0.
  This addendum commit follows on main.

## Session 26 — Gulg Gateway cleared: Ifrit gained and equipped, the Leather Clothing upgrade, and Kefka beaten at drift +10 (2026-11-17)

Start state ch2_s25_final.state: Vaan Lv 5, HP 1242/1242, Gil 438,
banked KP 15, Longsword + Bronze Helm, Onion Knight assist, Orange
Drop + Blue Drop + Girl's Dream in inventory.

### The field segment and entry

Field census from the save: GateMgr count 13 (gates 11-23); gate 11
was the only unspent live gate, pointing at point-table record 10
(154.08, 6.72, 229.42), lh 8. The field is a snowy mountain path
under a shining spire; flavor text on entry: "A frail power of the
dead dwells." Walked waypoints (200,214) -> (186,224) -> (168,230)
-> the gate anchor; rough terrain, ten stuck probes. NO field
ambush fired anywhere on this route — the guide's Phantasmal
Harlequin Lv 8 and False Stalwart Lv 6 ambushes (guide data) did
not occur on the path taken; recorded as a measured absence for
this route, not a claim they do not exist. At (159.2, 7.41, 231.2)
the approach scene triggered (ctrl state 2 -> 3); its dialogue was
not captured (the screenshot after it ended shows only the stone
arch with its glowing seal), so no lines are reconstructed here.
Entry recipe: Cross (dash to (155.90, 6.86, 229.23)), Cross again
-> states 5 -> 19 -> 20, entry disc 11. The banner screenshot
confirms "Gulg Gateway" (gate 11 -> pt 10, matching the guide
order). The field conditions panel for this gateway: KP Chance
"Win within 90 seconds"; Rules: Disabled; Skills: Enabled. No
entry banter on the board.

### Board census (full Scan DFS, zero backtrack warnings)

19 discs: 0, 2, 3, 4, 11, 12, 13, 20, 21, 23, 25, 30, 31, 33, 34,
35, 41, 43, 51. Contents from the Scan info boxes:

- disc 3: Idle Sky Pirate Lv 5 (stage Lunar Subterrane)
- disc 4: Summonstone — "An unknown summon is sealed within."
- disc 25: "???" piece — "Engage this piece to find its true form."
- disc 30: Delusory Dragoon Lv 5 (Lunar Subterrane)
- disc 31: Capricious Reaper Lv 15 (Order's Sanctuary)
- disc 34: Phantasmal Harlequin Lv 2 (World of Darkness)
- disc 43: Fleeting Flash Lv 2 (World of Darkness)
- disc 51: Treasure Chest — "Contains helpful items."
- No info box (empty as presented): 0, 2, 11 (entry), 12, 13, 20,
  21, 23, 33, 35.

No Stigma on the board and no assist piece: the finish condition
is beating the "???" piece (the Encounters and Treason pattern).
One-step gameplay graph (only delta-row/col = 1 links move; Scan
links across gaps are not gameplay links, session 25's lesson,
re-proven below): 11:12,21 · 12:11,2,13 · 2:12,3 · 3:2,4,13 ·
4:3 · 13:12,3,23 · 20:21,30 · 21:11,20,31 · 23:13,33 · 25:35 ·
30:20,31 · 31:21,30,41 · 33:23,34,43 · 34:33,35 · 35:34,25 ·
41:31,51 · 43:33 · 51:41 · 0: no one-step links. Scan-only
suspects: 0-2, 0-20, 4-34, 21-23, 23-25, 31-33, 41-43.

### First pass, the Ifrit scene, and the KP-chance calibration

Idle Sky Pirate Lv 5 beaten from disc 2's prompt (45 rotations,
drift 0, assist every 6; foe HP 1242; Vaan 1242 -> 196; +50 Gil;
leveled 5 -> 6 with NO delta-heal — HP stayed 196). The 45-rotation
win earned no gauge KP: this gateway's KP Chance is the 90-second
window, not the win itself.

Summonstone (disc 4, prompt from 3) opened a board scene, game
text: a voice — "Flames of hell? I've heard that before... I don't
know you, but will you help me?" -> "…I am Ifrit. I shall lend my
strength to champions of Cosmos." -> Vaan: "Hm... Okay. Well, nice
to meet ya, Ifrit!" -> tutorial: "Equip Summonstones to borrow an
Esper's power. Open the customization menu with △." -> banner:
"Obtained Ifrit AUTO!" Deliberately not equipped yet at this point
(unverified menus). Re-check on a later re-entry: the stone is a
one-time grant — disc 4 becomes inert and standable.

Phantasmal Harlequin Lv 2 at board HP 196: LOSS in 10 rotations
(foe HP 1061 measured; dealt 422; Vaan died at rotation 10 after
reaching 50 HP at rotation 7). Fled cleanly (engagement HP 196
restored, Gil/KP untouched), then exited the gateway at gauge 0 —
the exit costs nothing when the gauge is empty; levels and Gil
persist, the board resets. This became the session's working
pattern: clear what the current HP bar allows, exit, re-enter
full.

KP-chance calibration, measured across the whole session: wins in
12-30 rotations of the standard stream paid 1 KP each; wins in
33-59 rotations paid none. The 90-second boundary sits between 30
and 33 rotations of this input stream. After Harlequin wins a
chain offer repeatedly auto-opened offering the Fleeting Flash
(disc 43); declined every time (circle -> "Cancel chain?" -> left
-> cross YES), and the win's held KP then paid into the gauge.

### The Dragoon search and the farm loops

Delusory Dragoon Lv 5 (foe HP 1242) resisted at low HP; the
deterministic drift search from its VS state (Lv 7, 218 HP at
policy start on the winning line): drift +10 dealt 941 (died
rotation 17, foe at 301), +15 dealt 945 (died rotation 16), +5
WON in 17 rotations with 71 HP left. The Harlequin is the board's
HP toll: its cost swung 365-1314 damage depending on pass and
stream (two different drifts in one pass both ended at exactly
962 HP left — its big hits land on a schedule). Fleeting Flash
Lv 2 stayed cheap all session (12-19 rotations, ~30-190 damage).
Level curve across the session's loops: Lv 5 at entry; Lv 6
(Pirate), 7 (Harlequin), 8 (mid-fight level-up vs Flash, HP rose
157 -> 218 in-fight), 9 (Dragoon), 12 (read on the board HUD
after the next pass's Dragoon), 13 (Customization screen), 14
(mid-fight vs Pirate), 15 (field read), 17 (farm pass: Pirate,
Harlequin, Flash, Dragoon). Max HP by level (field/HUD reads):
Lv 6: 1303, Lv 7: 1364, Lv 9: 1485, Lv 12: 1667, Lv 13: 1727,
Lv 15 with Leather Clothing: 2375, Lv 17: 2058 was a partial-HP
field read, max not re-read.

### Shop and equipment (Customization opens from the board)

The Moogle shop is reachable mid-gateway: board △ ->
Customization -> Shop (a Moogle tutorial on equipping summons
intercepts the first visit after obtaining a stone; START in the
Summons menu shows summon details). Full stock with measured
stat previews against the Longsword loadout (ATK 28 at the time):
Broadsword 500 (ATK+4: net ATK -2, DEF +2), Knife 500 (ATK+2
DEF+2: net ATK -4), Longsword 500 (equipped; Equipment screen:
class "Greatsword", ATK+6), Zweihander TRADE, Katana 450 (BRV+9
ATK+4 DEF-2: net ATK -2), Spear 1140 (HP-86 ATK+8 DEF-2: net
ATK +2 only), Axe 1300 (BRV-20 ATK+6: net ATK +2), Rod 450,
Staff 450. Shields: Kunai 400 (equipped; "Parrying ATK+2 DEF+1"),
Buckler 450, Bronze Bangle 500, Hide Armlet 450. Hats: Leather
Hat 500 (BRV up, DEF -2 net), Bronze Helm 300 (equipped),
Headband 300. Clothing: Leather Clothing 950 (HP+428 BRV+9
DEF-2), Leather Armor 500 (equipped), Mythril Vest TRADE,
Leather Chest Plate 500, Robe 400, Bronze Armor 1100 (preview
not read). CONCLUSION: no Gil weapon is a straight upgrade —
the Longsword is already the best ATK weapon the shop sells;
the Spear/Axe net +2 ATK at high cost with real downsides. The
one straight upgrade was survivability: Leather Clothing
950 Gil for +428 max HP. Farmed to exactly 950 (886 on hand +
14 Flash + 50 Pirate across an exit/re-entry), bought it, and
equipped at the shop's "Will you equip this now?" prompt.
Measured effect: max HP 1788 -> 2216 at Lv 14; current HP does
NOT rise on equip (stayed 1788) — the gain lands at the next
full restore. Ifrit AUTO equipped in the same menu session
(Summons -> Equip): game text "Bravery boosted to 150%. Auto
when stage bravery gained.", 3 uses on the stone. Also noted:
a "The chocobo found a treasure chest!" banner fired once on
opening Customization (banner text only; contents not verified).
Vaan's measured Lv 14 stats (Equipment screen): HP 1788,
CP 345, BRV 203 (170 + 33), ATK 29 (23 + 6), DEF 26, LUK 17;
after the clothing: BRV 217, DEF 24.

Engaging a piece first shows an Opponent Info screen (foe
stats and equipment, cross = NEXT through its pages) — e.g. the
Harlequin: Lv 2, HP 1061, CP 351, BRV 101, ATK 13, DEF 12,
LUK 11; Cracked Shield, Charred Helm. A future reader surface:
foe stats are readable pre-battle from this screen.

### Harness lesson: the battle-block finders went blind at Lv 14

Both finders capped her BRV at 400 (s18lib find_block_a's
20 <= her_brv <= 400, and the loose structural finder's same
cap). Vaan's battle-start BRV passed the cap as he leveled:
421 measured at Lv 14, 451 at Lv 15 — every poll returned zero
candidates and two fights "could not be found". Widened both
caps to 900; the foe-HP-exact finder + widened cap is the
reliable combination. Two failure modes also recorded: (1) an
exact-current-HP anchor is too brittle — the true block appeared
exactly once (poll 2, base 0x096BCE98) and never again because
any poll-phase hit breaks the equality; (2) loosening the BRV
floor to 0 produced one false positive (base 0x096A9028, values
1024/768/0) whose cells degenerated into text bytes mid-fight —
a policy-start sanity read (her HP and foe HP must match the
known values) is the guard. Battle block A bases seen this
session: 0x096D5A38, 0x096BCE98, 0x096BBF98, 0x09692538 (boss),
plus the 0x096A9028 false positive. Separately, one exit attempt
from a just-fled defeat state failed silently (the pause menu
never opened — a UI overlay was still up); exiting from a
postwin-clean board state worked. Flee states are not clean
menu bases.

### The boss: Kefka

The "???" piece (disc 25) opens a story scene, not a bare VS
card. Captured lines: Kefka, to Terra: "Listen, you're a vicious
beast. With vicious power, meant for mayhem and destruction!"
and "Now tell me, how is a person like that going to decide she
'doesn't want to fight'?" Terra, earlier: "I didn't... I don't..."
Vaan (watching): "Oh, you crack me up! 'Don't want to fight...'
she says!" and "...beating the pulp out of people?" The fight
is against Kefka himself (visually confirmed in the arena and
on the final-blow frame; HP 2072 measured; Lv 10 is guide
data). His arena card shows an AP Chance: "First □ attack over
600: +1". Attempts (standard rotation, assist every 6):

- Lv 13, 306 HP, drift 0: dealt 252, died rotation 6 (stage
  pool ran 379 -> 758 for the boss).
- Lv 14, 397 HP, drift 0: dealt 46 in 20 rotations — a whiff
  stream; his bravery never built (285 start to finish).
- Lv 14, 397 HP, drift +10: dealt 650, died rotation 18.
- Lv 15, 962 HP, drift +10: dealt 754, died rotation 25.
- Lv 15, 962 HP, drift +5: dealt 684, died rotation 45 — all
  damage landed by rotation 31, then the stream stalled.
- Lv 15, 962 HP, drift +20: dealt 622, died rotation 18 (boss
  bravery snowballed to 1328).
- Lv 17, 1530 HP, drift +5: dealt 964, stalled at foe 1108
  from rotation 32, died rotation 40.
- Lv 17, 1530 HP, drift +10: WIN in 31 rotations, Vaan 714 HP
  left. The finish ran through Ifrit: final BRV 1876 (a pool
  capture boosted to 150%), and the killing blow was a BREAK
  (final-blow frame: foe HP 0000, "BREAK", Vaan BRV 1876).

Rewards: +150 Gil (206 -> 356); the gateway finish banked the
gauge (4 KP) into his total: banked KP 15 -> 19. Beating the
boss completes the gateway (no Stigma needed): the game
returned Vaan to a field — a new aurora-lit snowfield where
Terra now stands with him (screenshot-verified).

### The one unclaimed item: the disc-51 chest

The chest was never opened, measured unreachable at gateway
levels: disc 51's only one-step neighbor is 41, whose only
one-step neighbors are 31 and 51. The 43->41 hop was probed
directly (two presses: no movement, focus fell to his own
disc's record) and the 31<->33 link is likewise scan-only, so
the only route in is standing on disc 31 — and disc 31 is
blocked by the Capricious Reaper Lv 15's engage prompt
(screenshot-verified from disc 21: "Capricious Reaper Lv 15 /
Order's Sanctuary / Engage / Cancel"). Passing a piece's disc
requires beating it; at Lv 5-9 (the clear passes) a Lv 15 was
out of reach, and by the time Vaan outleveled it the gateway
was finished. Recorded as the board's single unclaimed item,
with the route fully mapped for any future revisit.

### End state

ch2_s26_final.state (field, gateway complete): Vaan Lv 17,
HP 2058 (field read; max not re-read, ~2497 by the level
curve), Gil 356, banked KP 19. Equipment: Longsword, Kunai,
Bronze Helm, Leather Clothing. Ifrit AUTO equipped (it fired
in the boss fight; remaining uses not re-read). Onion Knight
assist. Items: Orange Drop, Blue Drop, Girl's Dream. Terra
accompanies him on the new field segment. Next per the guide
order (guide data): Snow of Sorrow (optional) -> Seasonless
Gateway (Chapter 2 final).

States (~/d12s): ch2_s26_fwd / _walk1 (field), ch2_s26_entered
.. entered8 (per-pass entries), ch2_s26_scanned (post-census),
per-fight prompt/vs/winraw/fled/done states under pirate,
pirate2-6, pirate8, stone, harlequin, harlequin2-8, flash,
flash3-5, flash8, dragoon, dragoon3, dragoon4, dragoon6,
dragoon8, drag5/drag15 (Dragoon search), boss, boss2-4 and
bossd5/bossd10/bossd20/boss4d10 (boss attempts; boss4d10 is
the win), ch2_s26_field1..field7 (exit landings),
ch2_s26_boss4d10_done, ch2_s26_final. Scripts s26_*.py
(census/explore/walk/enter/scan/nav/fight/ffight/inch/open/
postwin/flee/exit/move/menu/diag/status/tar); finders patched
in-session (s18lib BRV cap 400 -> 900; s26_ffight loose finder
BRV cap widened, pool floor 50 added, single-sighting accept).
Logs s26_*.log; screenshots s26_*.png.

### Session 26 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 26 section was committed on
  dissidia-012-gulg (45b388b) and merged here. Note on gates:
  origin/main advanced past the branch base during the session
  (2b99d3f -> d4b75bf, PR #36 MKU proximity + the adapter build
  wiring), so this was a true merge, not a fast-forward. The
  research doc was verified untouched between 2b99d3f and
  d4b75bf (empty diff), the working tree was clean except the
  doc, and the merge (5f71c42) took only the doc change
  (+255 lines). Pushed via the clone's git credentials and
  verified with gh api:
  origin/main = 5f71c4280ee6c6bca83e79ec96228038dbb174ba.
  This addendum commit follows on main.

## Session 27 — Snow of Sorrow + Seasonless Gateway: Chapter 2 complete

Branch dissidia-012-seasonless from main fa3ae1a. Start state
ch2_s26_final.state: Vaan Lv 17, HP 2058, Gil 356, banked KP 19;
Longsword / Kunai / Bronze Helm / Leather Clothing; Ifrit AUTO;
Onion Knight assist; Orange Drop, Blue Drop, Girl's Dream.
Guide order (FF Wiki Ch. 2 / Suih): Snow of Sorrow (optional per
guide), then Seasonless Gateway, Chapter 2's last.

### Field: gate census and a scripted field battle

Gate census (GateMgr via ctrl+0x88) from the s26 final state:
count 14 (gates 11-24). Tester-reachable (lh(ptrA+4) >= 0):
gate 12 -> point 13 (-34.00, 0.42, 295.01) and gate 18 ->
point 12 (48.33, -0.81, 262.18). Gate 11 (Gulg) is inert/spent.
Gate 21 reads lh 238 with point 16 all-zeros (anomalous;
not pursued).

Walking west toward gate 18, a scripted JUDGMENT field battle
fired at (39.50, 235.55) (ctrl state 5 -> 20) vs Phantasmal
Harlequin Lv 10, stage Old Chaos Shrine. Opponent Info:
HP 1545, BRV 147. Won in 13 rotations of the standard stream,
zero HP attacks weathered; +150 Gil (356 -> 506); Vaan leveled
17 -> 18 mid-fight. Post-battle the game placed Vaan at
(-40.46, 0.40, 302.88) on the west cliffs — a teleport whose
mechanism is unexplained (recorded, not explained). Battle HP
(2398 at Lv 18) is not the field HUD HP (2058 at Lv 17):
the two pools are read at different moments; do not conflate.

Harness lessons (field fights): an exact-HP-keyed block finder
fails when the hero takes damage before the first dump; a
purely structural finder can lock onto a frozen template block
(HP/BRV values present but never moving). A candidate block is
only live if its values MOVE between dumps. This boot's battle
block base: 0x09692288.

Seasonless (gate 12) was scouted before Snow: entered via the
gentle-entry recipe (small analog nudges + Cross; entry fired
~2.5 units from the anchor), an entry scene with Terra played,
full Scan census taken (19 discs; see below), and the board was
exited cleanly via Start -> Exit Gateway with nothing fought.
The unfinished visit left no progress; on the later re-entry
the entry scene replayed in full.

### Snow of Sorrow (gate 18)

Entry disc 22; board live immediately (no entry scene).
Census (16 discs): 0 chest, 2 Phantasmal Harlequin Lv 7
(Lunar Subterrane), 3 chest, 11 Idle Sky Pirate Lv 11
(Old Chaos Shrine), 20 Idle Sky Pirate Lv 7 (World of
Darkness), 31 Fleeting Flash Lv 11 (Old Chaos Shrine),
42 Stigma of Chaos, 53 Terra (Assist Character); no-info
discs 1, 21, 22 (entry), 23, 24, 32, 33, 43 — matches
Suih's piece list for this gateway (guide data, confirmed
by the census). KP Chance shown on the prompt screen:
"Win within 90 seconds" (game text).

Movement, measured: scan links that span a missing disc
(2 -> 22 and 0 -> 20 here, a two-row gap) are NOT gameplay
links — 30 hop attempts and direction probes never moved;
the board is a loop and the south half is reached around the
sides. Gameplay graph: 0:1 · 1:0,2,11 · 2:1,3 · 3:2,23 ·
11:1,21 · 20:21 · 21:11,20,22,31 · 22:21,23,32 ·
23:3,22,24,33 · 24:23 · 31:21,32 · 32:22,31,33,42 ·
33:23,32,43 · 42:32,43 · 43:33,42,53 · 53:43.

Fights (standard stream, drift 0):
- Idle Sky Pirate Lv 7 (disc 20): HP 1364, BRV 102, ATK 16,
  DEF 12, LUK 13 (Cracked Shield, Charred Helm). WIN,
  26 rotations, zero damage. +84 Gil (506 -> 590); gauge
  0 -> 1 (26 rotations paid, inside the 90 s chance).
  A chain offer followed ("Chain 1: Idle Sky Pirate Lv 11,
  Bonus +2 KP") — declined per standing practice.
- Idle Sky Pirate Lv 11 (disc 11): HP 1606. WIN,
  30 rotations. +176 Gil (590 -> 766); gauge unchanged —
  so the sub-90-second payment boundary for this stream on
  this board sits between 26 and 30 rotations, or the held
  KP was lost in the chain-cancel (unresolved; gauge total
  was correct at the gateway finish either way).
- Phantasmal Harlequin Lv 7 (disc 2): HP 1364, BRV 102,
  ATK 18, DEF 10, LUK 13. WIN, 47 rotations, took 102
  damage. +84 Gil (766 -> 850); no KP (47 rotations).
- Fleeting Flash Lv 11 (disc 31): HP 1606, BRV 153,
  ATK 20, DEF 22, LUK 15 (Cursed Ring). WIN, 56 rotations;
  Vaan leveled 18 -> 19 mid-fight. +176 Gil (850 -> 1026).

Chests (game text): disc 0 = Iifa Leaf; disc 3 = Delicious
Fish Scale. Terra assist piece (disc 53) engaged: her line
"I'll help you..." (one-line overlay scene, the assist-piece
pattern). Stigma (disc 42) engaged last: finish flowed
straight to the field at gate 18's arch (50.04, -1.27,
262.72); HP restored to full (Lv 19 max 2619); the gauge's
1 KP banked — banked KP 19 -> 20. Gil 1026.

Postwin lesson for this board: at board idle the focus
pointer rests on piece records, so s27_postwin's chain guard
false-fired repeatedly (harmless circle/left/cross bumps; no
chain was ever accepted and held KP paid correctly). The
reliable decline is manual: circle -> left -> cross, then
verify by screenshot. Script copy kept at
~/workspace/s27_decline.py (VM side).

### Seasonless Gateway (gate 12) — Chapter 2's last

Re-entered from the field (walk west along the road, gentle
entry). The entry scene replayed; dialogue captured across
both entries (game text): Terra — "I don't want to hurt
people, or destroy things anymore. So please!"; Vaan —
"People are people aren't they? It doesn't matter what
'side' you're on." / "But that doesn't mean I'm just gonna
throw away what I believe in." (first entry also caught:
"I mean, that's gotta be better than the alternative." /
"Okay. Sure, we were summoned here to fight.")

Measured anomaly: Vaan entered at HP 2519 although he had
left Snow of Sorrow at full 2619 and fought nothing between
(field walk only). The -100 is unexplained.

Census (19 discs, entry 31): 0 chest, 10 Phantasmal Girl
Lv 15, 24 Terra (Assist Character), 33 Idle Sky Pirate
Lv 11, 43 Phantasmal Harlequin Lv 15, 50 Fleeting Flash
Lv 15, 54 Idle Sky Pirate Lv 15, 55 Stigma of Chaos,
60 chest; no-info discs 20, 23, 25, 30, 31 (entry), 32,
34, 35, 40, 53.

KP Chance here is untimed (guide says "Win battle"; measured
consistent: all five wins paid exactly 1 KP each, including
44- and 49-rotation wins; gauge 0 -> 5 across the board).

Fights:
- Phantasmal Girl Lv 15 (disc 10): HP 1848, BRV 176,
  ATK 26, DEF 25, LUK 17 (Cursed Ring). WIN, 44 rotations,
  zero damage. +300 Gil (1026 -> 1326).
- Fleeting Flash Lv 15 (disc 50): HP total 1 — a glass
  cannon (base 1848 - 962 gear; BRV 324, ATK 24, DEF 18;
  Cracked Shield, Beret, Rusted Armor). WIN, 23 rotations,
  but it converted its boosted bravery first: Vaan took
  1300 damage (2519 -> 1219). +300 Gil (1326 -> 1626).
  Same lesson as session 24's Lv 5 Flash: fight it the
  moment it is engaged; do not idle near it.
- Idle Sky Pirate Lv 11 (disc 33): HP 1606, BRV 125,
  ATK 20, DEF 16, LUK 15. WIN, 14 rotations; Vaan leveled
  19 -> 20 mid-fight. +176 Gil (1626 -> 1802).
- Phantasmal Harlequin Lv 15 (disc 43): HP 1848
  (Opponent Info page was skipped by the inch crosses;
  HP read live in the arena). WIN, 49 rotations, zero
  damage. +300 Gil (1802 -> 2102).
- Idle Sky Pirate Lv 15 (disc 54): HP 2481 (1848 + 633
  gear), BRV 265, ATK 44, DEF 33, LUK 17 (Mythril Sword,
  Wakizashi, Iron Helm, Bronze Chest Plate) — the
  strongest manikin of the chapter. Drift 0: LOSS in
  59 rotations with the foe at 261 HP (2220 dealt).
  Drift +5: WIN, 48 rotations; Vaan leveled 20 -> 21
  mid-fight. +300 Gil (2102 -> 2402).

Chests (game text): disc 0 = Pink Tail; disc 60 = Soul of
Destruction. (Both match Suih's claims for this gateway —
guide data until measured; now measured.)

Terra assist piece (disc 24) scene (game text): Vaan —
"Don't push yourself. I can handle these guys on my own.";
Terra — "I'm alright, I can fight, too..."

Stigma (disc 55) engaged last: the finish flow ran through
the chapter-end sequence to state 21 — Chapter 3 opens
directly on a new board (floating rock islands in rain),
a new lead character at Lv 1, HP 1000, with tutorial text:
"He also possesses many special attacks. Check his
abilities in the customization menu." Chapter 2 is
complete. The new board's HUD reads the shared pools:
Gil 2402, banked KP 25 (20 + the gauge's 5, which left
the gauge at the finish), gauge 0.

### Standing at session end

End state: ch2_s27_final.state — Chapter 3's opening
board, tutorial text dismissed, board idle.

Vaan's final Chapter 2 standing: Lv 21, Gil 2402, banked
KP 25. Gear unchanged: Longsword / Kunai / Bronze Helm /
Leather Clothing; Ifrit AUTO equipped; Onion Knight the
set assist. Inventory gains this session: Iifa Leaf,
Delicious Fish Scale, Pink Tail, Soul of Destruction
(plus Orange Drop, Blue Drop, Girl's Dream carried in).
No shop visit was needed: nothing on the route beat the
Longsword for Gil, and Leather Clothing's HP carried the
chapter. Session totals: Vaan Lv 17 -> 21, Gil 356 ->
2402, banked KP 19 -> 25; 10 battles won, 1 lost (the
Pirate Lv 15 drift-0 attempt, fled and re-won at +5).

States (~/d12s): ch2_s27_census_in, ch2_s27_g12scene_inch,
ch2_s27_exit (Seasonless scout), ch2_s27_g18in,
ch2_s27_snow_done, per-fight prompt/vs/info/winraw/done
states under harf, pir7, pir11, har7, flash, terra,
stigma (Snow) and sgirl, sflash, spir33, shar43, spir54,
sterra, sstigma (Seasonless), ch2_s27_final. Scripts
s27_*.py in ~/d12s (copies of the s26 set, prefixes
renamed); s27_enter2.py / s27_decline.py also kept on the
VM at ~/workspace. Note: the VM-side s27_enter2.py takes
argv in the order start, out, TAG, tx, tz and logs to
s27_enter2_<tag>.log. Two VM service restarts interrupted
tool calls mid-session; no progress was lost (all state
lives in WSL ~/d12s).

### Session 27 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 27 section was committed on
  dissidia-012-seasonless (6b3b0cd) and fast-forwarded here
  (main was still at the branch base fa3ae1a; the merge took
  only the doc change, +204 lines). Pushed via the clone's
  git credentials; origin/main verified with gh api after
  the push. This addendum commit follows on main.

## Session 28 — Chapter 3 opens: Laguna, first gateway cleared

Branch dissidia-012-ch3 from main dcb86f2. Start state
ch2_s27_final.state (Chapter 3 opening board). Scripts
s28_*.py in ~/d12s (copies of the s27 set, prefixes
renamed; their saved states carry a ch2_s28_ prefix from
the rename order — cosmetic only).

### The new lead — Laguna Loire (guide claim corrected)

The brief's guide guess named Bartz Klauser as Chapter 3's
lead. The GameSpy/IGN walkthrough (page 10, read this
session) instead says Chapter 3 opens playing as Laguna,
and the game itself confirms it: the VS card and the
Customization header both read "Laguna Loire". The Bartz
claim is wrong for the chapter opening (guide data,
superseded by measurement).

Starting standing (all from the game's Customization
screens at board entry):

- Laguna Loire, Lv 1. HP 1000/1000, CP 265/330 used,
  BRV 95, ATK 12, DEF 11, LUK 10. EXP total 0, next 5000.
  Gil 2402, banked KP 25, gauge 0 (shared pools carried
  over from Chapter 2).
- Bravery attacks: LAND Machine Gun (circle neutral),
  Grenade Bomb (circle + away); AIR Shotgun (circle
  neutral), Missile Barrage (midair) (circle + towards).
  HP attacks: LAND Satellite Laser (square neutral, wall
  rush) plus one empty land square+towards slot; AIR
  Ragnarok Blade (square neutral).
- Equipment: all four slots EMPTY. Accessories: 3 slots,
  all empty. Assist: NONE — the Assist menu item is
  disabled and the footer reads "No assist character
  selected." Summons: none set at start.
- Accessory pool (shared, from the Equip picker): Near
  Death, Near Opponent, Level 1-9, Red Drop, Orange Drop,
  Blue Drop, Iifa Leaf, Chocobo Down x5, Soul of
  Destruction. None equipped: Soul of Destruction is
  breakable (it broke on Lightning in sessions 18/19);
  saved for a boss that needs it.
- Equipment pool: Broadsword and Longsword both offered
  to Laguna; Kunai = "Can't Equip" (Parrying class);
  Bronze Helm x2 in pool.

### Gearing (pre-board natural stop; the shop trades in Gil)

From the pool, no Gil spent: Broadsword (ATK+4), Bronze
Helm (BRV+38, DEF+2), Leather Armor (HP+428, no penalties
— picked over Leather Clothing's HP+428 / BRV+9 / DEF-2).
Shop purchases, each a straight upgrade, each equipped on
the spot: Iron Sword 1300 (ATK+9), Iron Helm 620 (BRV+54,
DEF+2), Buckler 450 (fills the empty hand slot, DEF+3).
Gil 2402 -> 32. Ifrit AUTO summonstone equipped to Laguna
from the Summons page (clean equip, no transfer prompt;
3 uses left).

Geared standing: Lv 1, HP 1000 current / 1428 max,
BRV 149, ATK 21, DEF 16, LUK 10.

Shop stock noted and passed: Tanegashima 400 (Gun:
HP-86, BRV+19, ATK+4, DEF-2 — a sidegrade, not bought;
the board's chest later gave one free anyway), Leather
Hat 500 (BRV+48 but DEF-2 net vs the helm — a trade,
skipped), Chainmail 1000 (stats unchecked; Gil was
committed), Zweihander and Capella trade-only.

Menu-navigation lessons (measured): the Customization top
screen cycles notification banners and each Cross
dismisses one (about 9 crosses before a selection lands);
L/R switch pages in a ring (Abilities -> Equipment ->
Accessories -> Summons); in the Abilities list, left/right
are page up/down, not column switches — the category
column is two-level (Cross expands "Offensive Abilities"
into the Bravery/HP radios); the Equipment slot cursor
remembers the last-used slot; START and SELECT both open
the context manual; Circle exits to the board only from
an unpolluted state.

### Census — Chapter 3's first gateway (17 discs)

Full Scan pass from the entry disc. Discs: 4, 12, 13, 14,
21, 23, 30 (entry), 31, 32, 33, 41, 43, 44, 52, 53, 54,
62.

- 31: Treasure Chest. 23: Counterfeit Wraith Lv 1
  (Order's Sanctuary). 14: Fleeting Flash Lv 5 (World of
  Darkness). 4: Treasure Chest. 44: Stigma of Chaos.
  54: Fleeting Flash Lv 5 (World of Darkness).
  43: Ephemeral Vision Lv 1 (Order's Sanctuary).
  33: Transient Gunner Lv 1 (Order's Sanctuary).
  No-info discs: 30 (entry), 21, 13, 53, 32, 12, 52, 62,
  41 — all later traversed or stood beside, with no
  Scan-missed piece found on this board.
- The IGN/GameSpy piece list for this gateway matches the
  census one-for-one (guide data, now measured). Its
  chest claims (31 = Cross Chain skill, 4 = Tanegashima)
  were both confirmed from game text below.
- MOVEMENT LESSON (measured; cost two failed approaches):
  Scan links that jump a missing disc are NOT gameplay
  links — 21->23, 32->12, 32->52 and 14->44 are
  scan-only. Gameplay adjacency is single row/column
  steps only.

### Play — gateway cleared: five wins, no damage taken

Standard rotation policy, drift 0 throughout, no assist
(Laguna has none):

1. Chest 31 (prompted from 30): "Obtained Cross Chain
   from chest!" — the skill auto-set into the board's
   Skill slot. Laguna's line: "I'll use this chain skill
   to clean house of these spunky imposters!"
2. Transient Gunner Lv 1 (33, from 32): HP 1000, BRV 67.
   WIN in 5 rotations. +6 Gil, gauge 0->1. Laguna
   Lv 1->2.
3. Counterfeit Wraith Lv 1 (23, from 33): HP 1000,
   BRV 67. WIN in 4 rotations. +6 Gil, gauge 1->2. A
   chain offer toward the Vision (43) appeared in the
   result flow and was declined (focus rested on the
   piece's record — the session-27 false-fire pattern
   can't be ruled out; declined either way, per
   practice).
4. Fleeting Flash Lv 5 (14, from 13): info card —
   HP 1242, BRV 153 (118+35), ATK 14, DEF 8, Cracked
   Shield (DEF-8), Bravery Bonus Lv+3. WIN in 13
   rotations. +50 Gil, gauge 2->3. Laguna Lv 2->3.
   Harness note: the first attempt at this fight failed
   with BLOCK A NOT FOUND — the inch had overshot the
   info card into the live arena (foe already attacking)
   and the foe-HP key had been misread from the arena
   HUD as 1212 (the info card's 1242 is authoritative).
   Re-inching 2 steps from the VS state lands exactly on
   the Opponent Info card; fought from there, clean.
   Battle-block bases seen this session: 0x0969D688 and
   0x096BBF98.
5. Chest 4 (prompted from 14): "Obtained Tanegashima
   from chest!" Laguna: "Hey, what do you know? A 'Seed
   Shooter'! I've been wanting one of these bad." The
   Tanegashima was NOT equipped: measured against the
   Iron Sword it is ATK -5, DEF -2, HP -86 for BRV +19 —
   a downgrade for this build.
6. Ephemeral Vision Lv 1 (43, from 33): info card —
   HP 1000, BRV 67 (95-28), ATK 11, DEF 5, Cracked
   Shield + Charred Helm. WIN in 9 rotations. +6 Gil,
   gauge 3->4.
7. Fleeting Flash Lv 5 (54, from 53): info card —
   HP 1242, BRV 141 (118+23), ATK 14, DEF 8, Cracked
   Shield, Bravery Bonus Lv+2. WIN in 20 rotations (the
   foe's bravery reached 223 mid-fight; the plain
   rotation still shut it out). +50 Gil, gauge 4->5.
   Laguna Lv 3->4.
8. Stigma of Chaos (44, prompted from 54): engaged last.
   Finish flow: "Received 5 KP!" — banked KP 25 -> 30;
   out through a short dialogue state to a FIELD: a
   rocky mountain peak above the sea at dusk. Leaving
   the gateway restored Laguna to full HP (1610).

Every win paid exactly 1 KP to the gauge (the first
fight's VS card named the condition family JUDGMENT; no
timer was observed). Laguna took zero HP damage in all
five fights; each level-up healed +61 HP mid-fight.

Also learned on the way: Ricochet Shot (new ground
bravery attack, CP 30) appeared in Laguna's ability list
by Lv 4, marked NEW. It is learned but not slotted — the
land bravery list shows only two rows (Machine Gun,
Grenade Bomb) and the third slot's picker behavior was
not fully mapped; left as a micro-task for a later
session. It does not affect the fight policy (neutral
circle only).

### Standing at session end

End state: ch3_s28_final.state — Chapter 3 field segment
(mountain peak above the sea), first gateway complete,
state 2, idle.

Laguna Loire, Lv 4: HP 1610/1610, CP 265/333, BRV 167,
ATK 24, DEF 19, LUK 13 (the bottom stat rows were partly
covered by a rotating banner; values as read). EXP total
20030, next 1790; BATTLES 6; a chocobo "LUCKY! — next
battle EXP x2" bonus is active (84%). Gear: Iron Sword /
Buckler / Iron Helm / Leather Armor; Ifrit AUTO set;
Cross Chain board skill; no assist. Gil 150, banked
KP 30, gauge 0.

Session totals: Laguna Lv 1 -> 4; Gil 2402 -> 150 (2370
spent gearing up; +118 won and opened on the board);
banked KP 25 -> 30; 5 battles won, 0 lost, 0 HP damage
taken.

Next per the guide (guide data, unverified): a scene on
the mountain with a mysterious voice, then south and east
to the second gateway (Ephemeral Vision Lv 1, Transient
Gunner Lv 1, Transient Marionette Lv 5 among its pieces).

States (~/d12s): ch3_s28_board (pre-gear top menu),
ch3_s28_geared, ch3_s28_board2 (clean board, geared),
ch3_s28_census, per-piece prompt/vs/info/winraw/done
states under chest31, gunner, wraith2, flash14b, chest4,
vision, flash54, stigma (saved with the ch2_s28_ prefix
noted above), ch2_s28_stigma_open (the field landing),
ch3_s28_final. Scripts s28_*.py in ~/d12s.

### Session 28 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 28 section was committed on
  dissidia-012-ch3 (4dfb817) and fast-forwarded here
  (main was still at the branch base dcb86f2; the merge took
  only the doc change, +205 lines). Pushed via the clone's
  git credentials; origin/main verified with gh api after
  the push. This addendum commit follows on main.
## Session 29 — Chapter 3 continues: the mountain voice, Gateway To Decay

Branch dissidia-012-ch3b from main 78c28cd. Start state
ch3_s28_final.state (Laguna on the mountain peak, field).
Scripts s29_*.py in ~/d12s (copies of the s28 set,
prefixes renamed; saved states carry the ch3_s29_
prefix).

### Start standing (measured at boot)

- Laguna Loire, Lv 4: HP 1610/1610, Gil 150, banked
  KP 30 (field HUD readout). Gear: Iron Sword / Buckler /
  Iron Helm / Leather Armor; Ifrit AUTO set; Cross Chain
  board skill; no assist. (The s18lib gauge() cell read 5
  at field boot — it does not track the banked total;
  the HUD's 30 is authoritative.)

### Field census (measured, this chapter's population)

Gate records 11-20 populated (21-26 empty this boot);
gate -> landing point: 11->5, 12->7, 13->2, 14->3,
15->4, 16->6, 17->8, 18->9, 19->10, 20->11. Gate 14's
record showed state=5, timer=122 (the Omen of
Destruction arch Laguna had just exited; its point 3
anchor at (-85.81, 0.19, 241.49), ~37 units ESE of the
landing pose (-118.62, 0.30, 254.96)).

Point table (24 points): 0 (-12.10, 0.50, 32.00),
1 (-35.00, 0.93, 284.28), 2 (-34.00, 0.42, 295.01),
3 (-85.81, 0.19, 241.49), 4 (-36.05, 0.00, 257.95),
5 (-56.71, -0.43, 79.74), 6 (-24.44, 0.87, 9.07),
7 (0.35, 1.11, 31.41), 8 (-123.64, -1.57, 63.87),
9 (-243.05, -1.28, 134.92), 10 (-187.89, -1.57, 182.34),
11 (-274.40, -1.39, 165.23), 12 (-21.21, -2.47, 35.38),
13 (-132.36, -1.92, 97.51), 14 (-103.31, -1.50, 201.88),
15 (-31.29, -2.12, 213.59), 16 (-215.80, -4.00, 167.68),
17 (-100.82, 3.00, 34.45), 18 (-136.88, 3.00, 29.24),
19 (-121.69, 3.00, 58.70), 20 (-146.34, 3.00, 82.49),
21 (-142.84, 3.00, 111.91), 22 (-176.98, 3.00, 81.86),
23 (-171.11, 3.00, 116.96). Points 17-23 sit at y=3.00
(an elevated western shelf); the point set differs from
Chapter 2's population of the same map.

### Guide data for this segment (labeled, unverified)

- GameSpy/IGN walkthrough (page 10): from the peak,
  "head for the rock in front of you and break it to
  cross the bridge and grab an Assist Charge skill from
  the chest"; then "after the Reaper, browse the moogle
  shop", take a Rosetta Stone from "the chest in the
  grass in front of the gateway", trade it for an
  accessory slot in Customization, then enter the
  Gateway To Decay. Inside: "begin by taking down both
  Transient Gunners... There's no boss battle here, so
  once the enemies are defeated, engage the Stigma of
  Chaos to end the level."
- FF Wiki (Treachery of the Gods/Chapter 3): the second
  gateway is the Gateway To Decay. Its board: entry A2;
  Vaan assist piece at B1; Fleeting Flash Lv 5 (HP 1242)
  at B4; a Potion at B5; Transient Gunner Lv 1 (HP 1000)
  at C3 and E3; Counterfeit Wraith Lv 5 (HP 1242) at E1;
  Delusory Dragoon Lv 4 (HP 1, BRV 597) at E2; Fleeting
  Flash Lv 1 (HP 1000) at F4; chest at G2 (Power
  Armlet); Stigma of Chaos at G4. KP Chance: Win within
  90 seconds. Field wandering enemies for the chapter:
  Phantasmal Harlequin Lv 7, Capricious Reaper Lv 7,
  Counterfeit Wraith Lv 9, False Stalwart Lv 11.
- The session-28 doc's forward note named second-
  gateway pieces "Ephemeral Vision Lv 1, Transient
  Gunner Lv 1, Transient Marionette Lv 5" — that does
  not match the wiki's Gateway To Decay list; treated
  as unresolved guide disagreement, settled by the
  in-game census below.

### Field segment — the voice, the crystal, the Reaper

- Descent: the peak's east rim is walled; the walker
  descended the southern slope (probing along the
  ledge) to a valley floor at (-100.14, -0.13, 215.97).
  There, ANY step tripped ctrl state 3 — a scripted
  scene zone.
- THE VOICE (measured, dialogue from game text): the
  "mysterious voice" is Vaan. Scene lines, in order —
  Vaan: "Laguna! You're still wandering around here?
  You got lost again, huh?" / Laguna: "Hey now, you're
  in the same boat yourself. We both got lost, and here
  we are now." / Vaan: "I had a little errand to take
  care of. Anyway, do you know the way back?" / Laguna:
  "Of course I do! I won't get lost this time. Alright,
  Vaan, just follow me!" After the scene Vaan is
  visible standing with Laguna on the field, and the
  next battle's VS card showed an Assist portrait —
  Vaan is Laguna's assist from here (measured in the
  Reaper fight below; the assist chord produced his
  call-in).
- The crystal (kaname_03): point 14's anchor holds a
  giant dark crystal with orange fissures. Point-record
  names decoded from the census dump: points 2-11 are
  "POS_gate" anchors (gate_0306, gate_0400..0408);
  points 0-1 are "POS_jump_tag" anchors; points 12-16
  are "POS_kaname_01..05"; points 17-23 are
  "POS_fog_01..07" (y=3.00, a visual layer). The
  kaname_03 crystal did NOT respond to the gateway
  entry recipe (burst Crosses beside it), to dashing
  into it, or to contact; Square opened state 4 and
  Triangle state 11 (field menus) beside it. It is
  not this segment's breakable rock under any verb
  tried; left standing, nature unresolved. (The
  guide's "break the rock" step was never needed on
  the route the walker took down the slope.)
- FIELD BATTLE (measured): descending south-east from
  the crystal, a tester zone at (-87.46, -0.23, 172.01)
  fired state 5 and the entry flow landed on a VS
  card: Laguna Lv 4 vs "?" Lv 7, stage World of
  Darkness, condition JUDGMENT — the roaming
  Capricious Reaper (guide data: Lv 7, HP 1364, arena
  World of Darkness — all three matched measurement).
  Won in 18 rotations, drift 0, assist chord every 6
  (Vaan). Laguna took zero HP damage; he leveled
  4 -> 5 mid-fight (HP 1610 -> 1670; the chocobo
  LUCKY x2 EXP bonus from session 28 was still
  active). Battle block base this boot: 0x096BBF98.
  Rewards: +84 Gil (150 -> 234). No KP (field battle).
- The post-battle result flow self-advanced on frames;
  the postwin walker's field Crosses (dashes) carried
  Laguna SE to (-52.71, -0.90, 153.55) on a sandy point
  above the lake. HUD there: Lv 5, HP 1670, Gil 234,
  KP 30.

- Route south: from the Reaper site the walker hugged
  the lake shore south (16 stuck-probes on the shore
  path) to the arch at point 5 (gate_0402). The arch's
  info panel (game text): "Gateway to Decay — KP
  Chance: Win within 90 seconds; Rules: Disabled;
  Skills: Enabled". NOT found on this route: the
  guide's moogle shop and the Rosetta Stone chest
  ("in the grass in front of the gateway") — GameSpy's
  route approaches from the west across a bridge; our
  slope/shore route never passed them. Both remain
  unvisited (guide data only).

### Gateway To Decay — census (measured)

Entry banner (game text): "Gateway to Decay". Board
plays at ctrl state 20. Entry scene on entry (game
text): Vaan: "Hey Laguna, I'll give ya a hand, so lay
back like you're on an airship." / Laguna: "Thanks
Vaan, you're a great help. Don't go crashing and
burning, though."

18 discs: 11, 14, 20, 21, 22, 23, 24, 25, 26, 32, 33,
34, 41, 42, 44, 45, 46, 51. Entry disc 20. Contents,
matched one-for-one against the FF Wiki board (guide
letters in brackets): 11 = Vaan assist piece [B1];
14 = Counterfeit Wraith Lv 5 [E1]; 24 = Delusory
Dragoon Lv 4 (gold) [E2]; 26 = chest [G2]; 32 =
Transient Gunner Lv 1 [C3]; 34 = Transient Gunner
Lv 1 [E3]; 41 = Fleeting Flash Lv 5 [B4]; 45 =
Fleeting Flash Lv 1 [F4]; 46 = Stigma of Chaos [G4];
51 = Potion [B5]. Empty: 21, 22, 23, 25, 33, 42, 44.

REAL gameplay adjacency (single row/column steps;
Scan links spanning missing discs are not links —
the scan's 21-down->41, 42-right->44, 25-down->45 and
26-down->46 edges are scan-only): 20-21, 21-22,
21-11(engage only), 22-23, 22-32, 23-24, 23-33,
24-25, 24-14, 24-34, 25-26, 32-33, 32-42, 33-34,
34-44, 41-42, 41-51, 44-45, 45-46. The board is one
winding path; 25 and 26 (the chest) are reachable
only through 24, and 14 only from 24 or 11 — and 11
cannot be stood on even after Vaan's recruitment
(hop 21->11 failed 30/30 tries), so the Dragoon at
24 gates both the chest and the Wraith.

### Play — Gateway To Decay

All fights: standard rotation, drift 0, assist chord
(Vaan) every 6 rotations unless noted. KP Chance
(Win within 90 seconds) PAID on every win — gauge
rose exactly 1 per win (session-26 calibration holds:
wins up to ~21 rotations still paid).

- Vaan piece (11), engaged from 21: scene (game
  text) — Vaan: "What's it now? Get lost again?" /
  Laguna: "Ah! Th-that's not it! You should trust me
  some more, Vaan." Piece left the board; Laguna's
  assist inside the gateway set to Vaan (he was
  already the field assist from the mountain scene).
- Transient Gunner Lv 1 (32), from 22: WIN, 21
  rotations, 91 damage taken (1670 -> 1579). Foe HP
  1000 measured. +6 Gil. Gauge 0 -> 1.
- Transient Gunner Lv 1 (34), from 33: WIN, 7
  rotations, no damage. Laguna leveled 5 -> 6
  mid-fight (+61 HP heal; 1579 -> 1640). +6 Gil.
  Gauge 1 -> 2.
- Fleeting Flash Lv 5 (41), from 42: WIN, 15
  rotations, 147 damage taken (1640 -> 1493). Foe
  HP 1242 measured (matches guide). The Flash sat
  at 1 HP for 8 rotations before the killing hit.
  +50 Gil. Gauge 2 -> 3.
- Potion (51), from 41: engaged and consumed — HP
  restored to the Lv 6 full 1731 (and per the guide
  it also refills EX; see the Dragoon note — EX
  does not appear to carry into a later battle).
- Fleeting Flash Lv 1 (45), from 44: WIN, 10
  rotations, no damage. Foe HP 1000 measured.
  +6 Gil. Gauge 3 -> 4.
- Stigma of Chaos (46), from 45: engaged; exit
  banter (game text) — Laguna: "Every gateway's
  filled with those imposters. They just multiply..."
  / Vaan: "I've gotten used to their looks now,
  though. Their Laguna seems smarter than you." /
  Laguna: "Vaan... Well, I guess you're right. I
  don't think they'd get lost..." Payout banked:
  KP 30 -> 34. Landed on the lakeshore field south
  of the arch with Vaan beside him; HP full.

- LEFT STANDING: Delusory Dragoon Lv 4 (24) and,
  behind it, Counterfeit Wraith Lv 5 (14) and the
  chest at 26 (Power Armlet, guide data — contents
  unverified). The Dragoon is a gold glass cannon:
  HP 1, BRV 597 (both measured on its VS/arena
  cards), arena Order's Sanctuary, JUDGMENT. Its
  Jump opener deals ~746; later hits ~250. SIX
  attempts from the doorstep state, five policies —
  standard rotation (drift 0 and +10), cross-spam,
  forward-advance, spaced dodge-counter, and a
  potion-primed EX-mode opener (triangle + cross on
  rotation 1; no EX activation visible — EX gauge
  evidently does not carry from the potion into a
  new battle) — produced ZERO connecting attacks by
  Laguna in ~35 rotations: the gold piece's evasion
  defeats the blind rotation outright (normal pieces
  connect fine — see the other fights, same build).
  Every loss was fled cleanly (Retry/Flee), HP
  restored, nothing else lost. Known levers for a
  future session: a reset-board farm loop to Lv 9-10
  (more HP = more rotations of lottery tickets; the
  Flash above shows connections do land eventually),
  or a movement-aimed policy once foe position can
  be read. The gateway itself is complete without
  it (Stigma finish, as in session 24's precedent).

### Standing at session end

ch3_s29_final.state: field (lakeshore south of the
Gateway To Decay arch), ctrl state 2, pos
(-41.73, -2.83, 52.25), saved clean and idle.
Laguna Lv 6, HP 1731/1731 (Lv 6 max measured via
the potion refill; initial BRV 240 measured
in-fight), gear unchanged (Iron Sword / Buckler /
Iron Helm / Leather Armor), Ifrit AUTO, Cross Chain
skill, assist Vaan. Gil 302, banked KP 34, gauge
paid out. Session totals: 5 wins (1 field, 4 board),
6 losses (all the Dragoon, all fled), Laguna Lv 4 ->
6, Gil 150 -> 302, KP 30 -> 34. Ricochet Shot still
unslotted (no menu stop was needed; the rotation
never wanted it). Next per guide: through the empty
gateway north of the landing and left — a field
segment with a Counterfeit Wraith fight and four
chests (BRV Zero, EX Charge, Soul of Unrivaled
Valor, Pink Tail), then the chapter's final board,
the Southern Shrine Gateway (Cloud of Darkness
Lv 12; guides recommend Lv 12+).

States (~/d12s): ch3_s29_w2.state (valley floor,
scene trigger), ch3_s29_scene1.state (after the
Vaan scene), ch3_s29_gw2.state (Reaper VS),
ch3_s29_reaper_done.state, ch3_s29_decay_census
.state, ch3_s29_drag_vs.state / ch3_s29_drag2_vs
.state (Dragoon doorsteps), per-fight *_vs /
*_winraw / *_done states (gun1, gun2, flash5,
flash1), ch3_s29_potion_vs.state,
ch3_s29_stigma_vs.state, ch3_s29_final.state.
Variant fight scripts: s29_fightx.py (cross-spam),
s29_fightz.py (advance), s29_fightq.py (spaced
dodge-counter), s29_fighte.py (EX opener).

### Session 29 addendum — merge

- Doc-only session (no adapter code changed, so no build/test
  gates applied): the Session 29 section was committed on
  dissidia-012-ch3b (2231032) and fast-forwarded here
  (main was still at the branch base 78c28cd; the merge took
  only the doc change, +277 lines). Pushed via the clone's
  git credentials; origin/main verified with gh api after
  the push. This addendum commit follows on main.

## Session 30 — Chapter 3's final stretch: the Wraith, a Rosetta Stone, and the long shore (gateway NOT reached)

Branch dissidia-012-ch3c from main 5a6d8cf. Start state
ch3_s29_final.state (Laguna on the lakeshore, field).
Scripts s30_*.py in ~/d12s (copies of the s29 set,
prefixes renamed) plus new s30_field.py (field-battle
postwin walkback), s30_hop.py, s30_comb.py, s30_nav2/3/4.py,
s30_compass.py, s30_dash.py, s30_face.py, s30_look.py,
s30_inter.py, s30_map.py (navigation experiments, see
below).

### Start standing (measured at boot)

- Laguna Loire, Lv 6: HP 1731/1731, initial BRV 240,
  Gil 302, banked KP 34. Gear: Iron Sword / Buckler /
  Iron Helm / Leather Armor; Ifrit AUTO set; Cross Chain
  board skill; assist Vaan. Ricochet Shot still learned
  but unslotted (third-slot picker unmapped; no natural
  menu stop made it cheap this session either).

### Field battle (measured): Counterfeit Wraith Lv 9

- The Wraith roamed just west of the landing. A short
  west walk tripped ctrl state 5 at (-41.84, -2.85,
  51.89). Won with the field-fight script in 19
  rotations, drift 0, assist chord every 6: foe HP
  1,485 measured (matches the FF Wiki's field Wraith
  Lv 9). Battle block base this boot: 0x0969D688.
- Laguna leveled 6 -> 7 mid-fight (max HP 1731 ->
  1792); he took one 516-damage hit (arena HP ended
  1276). Postwin walkback: Gil 302 -> 428 (+126). On
  the field his HP reads full 1792/1792 — field-battle
  damage does not stick (consistent with Ch. 2/3
  findings).

### Field chest (measured): Rosetta Stone

- A golden chest at (-76.81, -0.11, 77.64), at the
  snowy massif's base on the north terrace west of the
  Gateway To Decay arch. Cross beside it fired the
  state-16 popup; game text: "Obtained Rosetta Stone
  from chest!" The chest stays open afterward.
- Correspondence (labeled): GameSpy's Ch. 3 route puts
  a Rosetta Stone chest "in the grass in front of the
  gateway" (Decay) with a moogle shop west across a
  bridge; this chest matches the "in front of the
  gateway" position. Suih's guide assigns the Rosetta
  Stone to the pre-Decay segment as well. A small
  red-pommed figure (moogle, presumably the guide's
  shop) was visible up-slope north-west of the chest
  but was not reached or spoken to.

### The final gateway was NOT reached (honest result)

The session's main objective — enter Chapter 3's last
gateway and fight the Cloud of Darkness (guide data) —
was not achieved. Both remaining arch areas proved
unreachable with this session's navigation tooling,
despite an extensive route survey (all measured):

- The landing cove exits only west (beach) and
  north-west (slope). South-east probes from the
  landing found no moving direction at all (water and
  rock); the eastern arches (gate04_03 at (-24.44,
  0.87, 9.07) and gate04_04 at (0.35, 1.11, 31.41),
  with kaname_01 at (-21.21, -2.47, 35.38)) sit on a
  south-eastern landmass cut off by the lake on every
  side tried.
- West: the beach strip ends at a choke near
  (-80, 58); the mid terrace (z ~59-62) ends at a
  promontory at (-75.24, -1.96, 62.07) with water
  north and west. gate04_05 at (-123.64, -1.57,
  63.87) is across that water; no ford or shallows
  found (no wading anywhere: deep water always blocks
  movement outright).
- The traversable network that WAS mapped end to end:
  landing -> west beach -> ledge (-52.16, -1.63,
  56.26) -> diagonal ramp (found by comb sweep) ->
  bluff (-44.70, -1.10, 60.56) -> tower-plateau
  pocket (-59.11, -0.47, 67.48) -> Decay terrace
  (z ~77-80, y ~-0.1..-0.5) running west to
  (-74.08, -0.11, 76.55) -> gap crossing to the rim
  (-82.40, -0.82, 85.00) -> the north-south strip /
  causeway (x ~-79..-88) past a reef bend at
  z ~124-131 -> the northern lake's south shore
  pocket (-88.75, -0.60, 149.37) and its east shore
  shelf (z ~151-154, x -94..-55, y ~-0.6) -> shore
  north end (-88.16, -1.35, 164.60). The western
  shore of the northern lake and the southern lake's
  east shore were never reached; a golden chest on a
  lake islet at about (-103, 155) was seen but is
  across open water. A beacon tower (light column)
  stands on the far western shore (bearings only).
- Session 29's doc explains the artery: its walker
  hugged this same shore south from the Reaper site
  (-87.46, -0.23, 172.01) to Decay. The strip is the
  chapter's west-side artery; it does not continue
  to either remaining arch area.

### Navigation findings (banked for the next session)

- Player facing vector is readable: player object
  (ptr at 0x08B83DE4) +0x08 = facing X, +0x00 =
  facing Z (unit pair with +0x20/+0x28/+0x90/+0x98
  copies). Stick-to-world response is REFLECTED in
  atan2(z, x) space: rotating the stick by +t turns
  the converged facing by about -t (measured from
  the s30_face.py turn experiment).
- DASH traversal (new): holding the stick and
  pressing Cross repeatedly moves Laguna ~10 units
  per dash cycle (about 6x walk speed) and the dash
  run follows terrain channels (it turned at a
  shelf's end and descended a valley unaided). It
  does not cross water or climb banks. Found late;
  the long treks next session should be dash-first.
- The walker's probe gives up ("probe found no
  moving direction") when every direction's gain
  toward the waypoint is negative — it cannot route
  around a bay whose first step loses ground. Its
  fixed-basis steering converges only while the
  camera basis stays put; recalibrating from a
  standstill against a wall poisons the basis.
- s30_nav4.py (aim-by-facing walker) has the sign
  fix applied but its aim phase drifts too much in
  tight terrain; not trusted yet.
- The world map (Select, ctrl state 14) is a fixed
  full-world view; arrow centroid calibration near
  the western lobe: px = 230.5 + 0.391 * wx,
  py = 131.6 + 0.345 * wz (fits four measured
  positions within ~2 px).

### Guide data for the unreached segment (labeled, unverified)

- FF Wiki Southern Shrine Gateway: entry D6; KP
  Chance "Win Battle"; A1 Counterfeit Wraith
  (Strange) Lv 12 HP 1; B1 Carbuncle (AUTO)
  summonstone; B2 Counterfeit Wraith (Hard) Lv 12
  HP 1,667; C2 Idle Sky Pirate (Hard) Lv 12 HP
  1,667; D4 Transient Gunner Lv 9 HP 1,485; E4 Idle
  Sky Pirate Lv 9 HP 1,485 (stepping on E4 triggers
  the scene where Vaan vanishes as Assist and the
  boss appears); E5 Transient Gunner (Hard) Lv 12
  HP 1,667; G4 Vaan assist piece; H4 Cloud of
  Darkness Lv 12, HP 2,446, arena World of Darkness,
  summon Iron Giant. Order implied: B2/C2 and the
  summonstone first (Vaan still available), then
  D4/E5, E4 last.
- Gateway to the Shrine (repeatable, guide): Fleeting
  Flash Lv 6, Delusory Dragoon Lv 6, Transient Witch
  Lv 12, Delusory Dragoon Lv 12 (HP 1); chest
  Gargoyle Pebble; KP Chance "Win within 90 seconds".
  Guides recommend Lv 12+ before the final board;
  Laguna is Lv 7 — farm loops in the repeatable
  gateway remain the plan once it is reached.
- Field segment (Suih, guide): False Stalwart Lv 11
  wanders west of the Wraith (not encountered; two
  distant red figures sighted, neither confirmed);
  segment chests Soul of Unrivaled Valor, Pink Tail,
  EX Charge, BRV Zero (none collected; the Rosetta
  Stone above is a different, pre-Decay chest).

### End standing (measured)

- Laguna Lv 7, HP 1792/1792 on the field, Gil 428,
  banked KP 34 (unchanged — no gateway entered).
  Rosetta Stone added to inventory. Chapter 3 NOT
  finished. End state: ~/d12s/ch3_s30_final.state
  (Laguna standing at the opened Rosetta Stone chest
  on the north terrace, ctrl state 2).
- Next session's first moves: (1) try the moogle
  up-slope from the end state (shop + maybe the
  bridge route west of Decay per GameSpy); (2)
  dash-first trek around the lake's east side via
  the highlands (points 14/15) toward gate04_04 /
  gate04_03; (3) if the eastern landmass is reached,
  its arches are the chapter's last two gateways in
  some order (blue repeatable first for farm loops
  to ~Lv 12, then the final board per the census
  plan above).

### Session 30 addendum - merge

- Session 30 doc committed on branch dissidia-012-ch3c
  (2a8e65d) and merged to main by fast-forward
  (5a6d8cf..2a8e65d plus this addendum). Doc-only
  session; no adapter code changed, so no new test
  gates applied.

## Session 31 — Chapter 3 route survey: the escarpment,
the sealed pocket, and the one-way ledge (gateway NOT
reached)

Branch dissidia-012-ch3d from main 065c049. Start state
~/d12s/ch3_s30_final.state (Laguna at the Rosetta Stone
chest, Lv 7, HP 1792/1792, Gil 428, banked KP 34).
Mission: the moogle lead, then the trek east to Chapter
3's last gateways. Verdict up front: the final gateway
was NOT reached and Chapter 3 is NOT finished — but the
map's remaining unknowns shrank a lot. No battles were
fought this session; Laguna's standing is unchanged
(Gil read 428 in every state check along the way).

### Guides read first (all labeled guide data)

- GameSpy/IGN walkthrough, Chapter 3 pages: post-Decay
  order is "go through the empty gateway and head to
  your left", beat the Counterfeit Wraith, ignore both
  gateways, detour for four chests (BRV Zero across a
  broken-rock bridge gap, EX Charge, Soul of Unrivaled
  Valor past the False Stalwart, Pink Tail around the
  bend), then farm the blue gateway to Lv 12+, then
  the Southern Shrine (engage the boss statue after
  the Stigma vanishes; use EX Charge first; dodge
  Cloud of Darkness's [Wide-Angle] Particle Beam by
  jumping; Break her, then spam Satellite Laser).
- link3895's GameFAQs FAQ: Chapter 3 has FIVE gateways
  — 1.3.1 Omen of Destruction, 1.3.A Great Forest
  Gateway (blue, repeatable), 1.3.2 Gateway to Decay,
  1.3.B Gateway to the Shrine (blue, repeatable),
  1.3.3 Southern Shrine Gateway. FAQ 1.3.B board:
  Fleeting Flash Lv 6, Transient Witch Lv 12, Delusory
  Dragoon Lv 12, Delusory Dragoon Lv 6; chest Gargoyle
  Pebble; KP Chance "Win within 90 seconds"; tip: use
  Jump Chain without moving after Laguna's opening
  line. FAQ 1.3.3: KP Chance "Win Battle", Bonus Line
  Lv 11; the [=] path square at F4 appears only after
  stepping where piece 5 (Idle Sky Pirate Lv 9, E4)
  stood AND after the dialogue there. FAQ 1.3.A board:
  Transient Gunner Lv 1, Fleeting Flash Lv 5, Transient
  Vision Lv 5, Delusory Dragoon Lv 4; chests Fish Scale
  (E7), Soul of Valor (E1). FAQ world-map notes: a
  Phantasmal Harlequin Lv 7 prances around the blue
  gateway in the Great Forest area; the Mount Duergar
  Mogshop floats "swimming in the air" just before the
  Decay (red) gateway; the Rosetta chest sits "to the
  left between the forest and mountain" before it.
  Mogshop stock (FAQ 9.2.4): accessories "BRV = 0"
  (20 KP), "No BRV Damage" (15 KP); summonstones Magus
  Sisters, Demon Wall, Magic Pot (trades), Calcabrina
  (120 KP) — nothing the finish depends on.

### Census re-run and gate liveness (measured)

- From the start state, the gate census reproduces
  session 29/30 exactly: gates 11–20 populated, 21+
  empty; gate→point and the 24-point table unchanged.
- NEW: the gate record's lh(A+4) field separates live
  gateways from spent ones. Gates 12, 15, 16 read 15,
  16, 17 (live); every other gate reads −1 — including
  Decay (11) and Omen (14), both completed. So Chapter
  3 has exactly THREE live gateways left: gate 15 →
  point 4, gate 16 → point 6, gate 12 → point 7.
- Anchor names decoded from the RAM dump's strings:
  point 0 = POS_jump_gate04_04, point 1 =
  POS_jump_gate03_06 — JUMP anchors bound to specific
  gateways (gate04_04 = gate 12's arch, gate03_06 =
  gate 13's arch). Points 2–11 = POS_gate03_06 /
  POS_gate04_00..08; points 12–16 = POS_kaname_01..05;
  points 17–23 = POS_fog_01..07 (the full fog set in
  the dump runs to POS_fog_12); POS_replay_camera%02d
  also present. Working identifications: point 4 =
  gate04_01 = Great Forest Gateway (blue, live);
  point 6 = gate04_03 and point 7 = gate04_04 = the
  south-east pair (Shrine blue + Southern Shrine;
  point 7 east of point 6 matches GameSpy's "head to
  the right" for the final gateway); point 2 =
  gate03_06, the FAQ's "barred-off gateway" with the
  KP Gambler chest (gate 13, inert this chapter).

### New harness tools

- s31_dashnav.py: greedy dash navigator — from a
  checkpoint loadstate, probes all 8 stick directions
  (dash cycles), commits the best-gain one, repeats;
  clears state-3 field dialogue with Cross presses
  until state 2. s31_wprobe.py: 8-direction walking
  probes (6 × 20-frame pushes) from a checkpoint.
- Upload lesson: a bare stdin data stream does NOT
  arrive through the ssh/wsl pipe (a script uploaded
  that way landed 0 bytes); embedding the script as
  base64 inside the executing program text works.

### The moogle lead (brief step 1): not found

- The chest pocket is walled west; its exits are north
  up the slope to the rim (−81.0, −0.8, 85.0). Every
  "small red-pommed figure" sighting from session 30
  and this one (terrace, rim, reef bend, strip north
  end) has bearings consistent with ONE object: the
  kaname_03 fissured crystal at (−103.31, −1.50,
  201.88), whose orange fissures glow at distance; it
  was identified up close this session (jagged dark
  crystal, orange cracks). No moogle was found
  anywhere on the surveyed shore. The FAQ's Mogshop
  (floating just before the Decay gateway) remains
  unlocated — possibly on the unreached landmasses;
  its stock is non-essential (above), so the hunt is
  deprioritized, not closed.

### The northern trek (measured, states in ~/d12s/)

- Rim → dashnav north up the causeway strip: (−77.7,
  94.4) → (−78.7, 104.2) → reef bend (−78.7, 123.2) →
  (−76.2, 129.3) → (−78.9, 138.9) → (−81.7, 148.6) →
  (−84.4, 158.2) → north end (−86.4, −1.1, 167.1).
- Ambient field banter fires on the strip around
  z≈122–124 (ctrl state 3; advance with Cross). Vaan,
  verbatim from game text: "Cosmos is a goddess,
  right? Couldn't she just make 'em disappear if we
  asked?" (Laguna's reply was not captured.)
- North end → crystal (−103.4, −0.1, 198.8) → valley
  (−98.6, −0.2, 214.2) (session 29's scene valley) →
  east along a traversable highland corridor at
  z≈198–216, y≈−2: (−94.2, 205.4) → (−88.0, 198.4) →
  (−79.7, 204.1) → (−71.5, 209.8) → (−63.2, −2.2,
  215.5) → (−54.0, 212.3) → promontory (−51.8, −1.8,
  205.7) overlooking the lake (lighthouse islet,
  golden-chest plateau, and arch ruins visible across
  the water).

### The escarpment (measured): the corridor is a dead end

- A north–south ridge at x≈−52 blocks eastward
  progress at EVERY latitude tried, z 195–218, by
  dashing and by walking. Walk probes at (−51.8,
  205.7), (−53.7, 213.0) and (−52.6, −2.75, 211.4):
  east gives zero or negative displacement; all
  moving directions slide back west into the corridor.
  The ridge's foot basin runs z≈195–212 and closes at
  both ends (probes at (−53.0, −2.8, 211.9): every
  direction ≤1.8 units). The corridor floor slopes
  DOWN eastward into the basin (y −1.6 west → −2.8 at
  the foot): a cul-de-sac against an escarpment.
- The land east of the ridge is a raised tableland:
  the SE anchors sit at y≈+0.5..1.1 (points 6, 7, 0)
  and the northern anchors at y≈0..0.9 (points 4, 2,
  1), versus y≈−2 in the corridor and the western
  lowlands. kaname_04 (point 15, (−31.29, −2.12,
  213.59)) is the odd one out — tableland latitude,
  valley-floor height; it may mark a pass or a low
  pocket on the far side. No ramp up the escarpment
  was found at z 195–218.

### The western pocket is sealed on the east (measured)

Returned via the rim checkpoint (same lineage) and
re-probed the post-Decay pocket's east side:

- Landing cove (−41.8, 51.8): all dashes toward the
  forest have negative gain (session 30's "exits only
  W and WNW" holds).
- Bluff (−43.65, −0.50, 63.23), 8-direction walk
  probes: no direction gains more than +1.8 x (the
  landing descent); east is walled.
- Decay terrace extended EAST beyond session 30's
  map: (−57.1, 80.4) → (−47.8, 76.7) → (−40.0, −0.5,
  75.0), a new segment. At its east end every dash and
  walk probe bumps (disp ≤2) or slides back west; the
  south face descends only ~6 units to the bluff
  shelf (a walker grind down it lands on the same
  sealed pocket). From the terrace's east end a
  BURNING RED GATEWAY ARCH is visible across the lake
  (screenshot s31_look_teast.png) — a SE-cluster
  gateway seen from afar, across open water.
- Rim → terrace on foot works (walker via (−75.7,
  79.1)); the rim → chest dash does not (too coarse
  for the gap crossing).

### The peak climb (measured): a one-way ledge

- From the valley state, the walker climbed the peak's
  southern slope to (−117.8, −0.46, 238.7), 16 units
  short of the Omen landing (−118.62, 254.96), then
  found no moving direction; dashnav from the same
  spot also made no progress. The ledge at z≈239,
  x≈−118 is descend-only on this line. Session 29's
  descent reached the valley at its EAST end (x≈−99),
  so the climbable line is east of the line tried;
  finding it is the next session's first search.
- World-map caution: the session-30 linear arrow
  calibration (px = 230.5 + 0.391·wx, py = 131.6 +
  0.345·wz) is a WESTERN-LOBE fit only. Projected
  east, it puts the SE cluster in open water — the
  map is schematic beyond the lobe and cannot be used
  to route the eastern landmasses.

### End standing (measured)

- Laguna Lv 7, HP 1792/1792, Gil 428, banked KP 34 —
  unchanged; no battles, no gateway entered, no items
  gained or lost. Ricochet Shot still unslotted (no
  natural menu stop occurred). Chapter 3 NOT
  finished. End state: ~/d12s/ch3_s31_final.state
  (copy of the valley state: Laguna in the highland
  valley at (−98.6, −0.2, 214.2), ctrl state 2 — the
  furthest-advanced lineage point; the Vaan banter
  flag on the strip is set in this line).
- Next session's first moves: (1) from the end state,
  find the climbable line up the peak's south slope —
  probe the slope foot east of x≈−118 (z 225–240,
  x −110..−95), where session 29's descent actually
  ran; (2) from the peak, cross the plateau east to
  points 4/1/2 (Great Forest Gateway + the barred
  gate03_06 + its jump anchor); (3) if the plateau
  joins the eastern tableland, ride it south to the
  SE pair (points 6/7), testing the POS_jump_gate04_04
  anchor (point 0) with Cross when reached; (4) then
  the standing plan: Shrine blue gateway farm loops
  to ~Lv 12, then Southern Shrine in the wiki order
  (B2/C2 + Carbuncle, D4/E5, E4 last).

### Session 31 addendum - merge

- Session 31 doc committed on branch dissidia-012-ch3d
  (d7ddb95) and merged to main by fast-forward
  (065c049..d7ddb95 plus this addendum). Doc-only
  session; no adapter code changed, so no new test
  gates applied.

## Session 32 — Chapter 3: the peak climbed, Great
Forest cleared, and the boulder mechanic (two
workers, reconstructed)

- Reconstruction note: session 32 ran as two workers
  (32, then recovery 32R); both ended without filing
  a report. This section was written by session 32c
  from their save states, logs, and screenshots in
  ~/d12s and committed before any further play, so
  the record can no longer be lost with a worker.
  Claims below were verified against those artifacts
  by 32R or 32c unless labeled guide data.

### The climb (worker 32, measured)

- From ch3_s31_final.state (valley at (−98.6, −0.2,
  214.2)), the climbable line up the peak's south
  slope is session 29's descent line at x≈−100
  (states ch3_s32_slope1/slope2), reaching the
  plateau (y≈0..0.9, z 240–295; states
  ch3_s32_plat1/plat2). Session 31's west line at
  x≈−118 stays descend-only.

### Great Forest entry (measured)

- Gate 15 → point 4 (gate04_01). Entry recipe
  refined: pose within ~0.5 units of the anchor and
  let ~600 frames settle for the tester burst; the
  earlier failures were distance (2.4 units), not
  flags. The board plays at ctrl state 20.
- Entry scene: Laguna — "Am I lost again? It can't
  be that I actually want to be alone, right?" —
  and Squall appears: "My...apologies..."; Laguna:
  "Squall! Perfect timing!..."

### Great Forest census (measured, Scan pass)

- 18 discs, entry disc 34. Pieces: 4 = chest;
  10 = Transient Gunner Lv 1; 14 = Delusory
  Dragoon Lv 4; 36 = Stigma of Chaos; 41 =
  Fleeting Flash Lv 5; 50 = Squall (Assist
  Character); 54 = Ephemeral Vision Lv 5 (the
  guide's "Transient Vision" name is wrong);
  64 = chest. Disc 24 showed figures in its Scan
  shot but no info box — unresolved; nothing was
  identified or engaged there. Discs 31, 32, 33,
  35 showed no info box (empty).
- Chest 4 = **Chocobo Down** (game text: the
  "Obtained Chocobo Down" banner read in the
  Customization menu by the playing worker; the
  guide's Soul of Valor for this square is
  superseded). Chest 64's contents were NOT
  captured — the banner expired before the menu
  read (guide lists Fish Scale for that square:
  guide data, unverified).
- Adjacency rule reconfirmed: moves and engages
  only between truly adjacent discs (grid ±1 or
  ±10 among existing discs); Scan links spanning
  gaps are not move links.

### Great Forest play (measured)

- Dragoon (14) WIN: +36 Gil (428→464), gauge 0→1.
  Gunner (10) WIN: +6 Gil (464→470), gauge 1→2.
  Squall's piece engaged from disc 40: a scene
  plays and he assists inside this gateway (the
  board's assist piece; HUD at his prompt: Lv 8,
  HP 1852/1852, gauge 3, Gil 520). Flash (41)
  WIN. Vision (54) WIN. Chest 64 opened (by then
  gauge 4; Laguna at 1238 HP after the fights).
  The board's Moogle Shop was browsed only — Gil
  and banked KP are unchanged across the visit
  (verified by 32R).
- Laguna leveled 7→8 on the board (EXP total
  38,768 at chest 4 → 44,305; one fight paid at
  the ×1.5 LUCKY rate); max HP 1792→1852.
- Stigma (36) finish: "Received 4 KP!" — banked
  KP 34→38. Pass totals: +142 Gil (428→570),
  +4 KP. Exit landing: field at (−10.92, −0.15,
  255.55) beside the Great Forest arch, HP full.
  Gate census after exit: gates 12/15/16 still
  live (lh(A+4) 15/16/17) — Great Forest is blue/
  repeatable and stays enterable after finishing,
  so it remains the farm board.

### After Great Forest (worker 32, measured)

- Dash navigation east across the plateau arrived
  at (−33.7, −0.2, 286.4), beside point 1
  (POS_jump_gate03_06 at (−35.00, 0.93, 284.28))
  — state ch3_s32_tableland1.state. The worker
  ended there without reporting.

### Session 32R verification (measured)

- 32R re-read the surviving prompt screenshots:
  "Squall / Assist Character" at his engage
  prompt; "Stigma of Chaos — Engaging this piece
  finishes the level." at the Stigma prompt; the
  HUD figures above check out against them.
  Piece names and levels verified against the
  guide list, with the Vision's name corrected
  to Ephemeral Vision.
- Jump-anchor test at point 1: Laguna stood 1.2
  units from POS_jump_gate03_06 — proximity alone
  fired nothing, and Cross on this field is a
  dash, not an interact. Walk-in tests at the
  barred gate03_06 arch also fired nothing; the
  barred gateway stays inert (gate 13 lh(A+4)
  =−1), consistent with session 31.
- Shop/inventory work from the tableland state:
  the Customization shop's Buy/Sell screens were
  read (Gil 570); nothing was bought or sold by
  either worker.

### Session 32R geography (measured)

- Plateau east channel: from (−13.0, 269.5) a
  channel runs southeast down to a stall at
  (−6.1, −0.1, 254.6) (states ch3_s32r_east1–4);
  probe navigation reads the stall as a wall in
  every direction.
- kaname_04 pocket: descending southeast from
  (−47.6, −1.1, 231.7) reaches a low basin at
  (−38.1, −1.5, 214.6), 4.3 units west of
  kaname_04 (−31.29, −2.12, 213.59) — the first
  ground at valley height east of the escarpment.
  The basin is a dead end: south wall at z≈210;
  its east side rises to a dead-end knoll at
  (−29.4, −0.7, 211.2).
- Decay terrace east views (screenshot study,
  s32r_look_te*): from the terrace's east edge a
  flat-topped mesa sits ~15–20 units east across
  water at terrace height; a red manikin figure
  stands on or just past its far side, and the
  burning gateway arch is visible beyond it. The
  floating X marker tracks the nearest wandering
  manikin (the same marker as the Wraith's); the
  red figure may be the False Stalwart standing
  on the far shore.
- Attack tests: at the beach choke (−75.8, −2.9,
  56.3), Square opens the field attack state
  (ctrl state 4) but there is no boulder there —
  the choke is simply the beach ending at water.
  At the plateau channel stall, one Square per
  direction (N/E/S/W) produced no break — an
  insufficient test against a boulder that may
  need up to three aimed hits (see below). 32R's
  last state: ch3_s32r_break_chan.state at
  (−6.7, −0.1, 255.5), mid-test.

### The boulder mechanic (guide data, banked by 32R)

- Suih's FAQ (L476–490): "Boulders are a new thing
  with the new World Map, depending on what
  character you're using, they can take up to
  three hits to destroy... Now when you destroy a
  boulder a little quickmove line of light will
  appear to take you over some sort of a gap,
  usually to an island, where a chest and/or
  manikin will be at." Hits vary by character
  (Kain 1, Lightning 2, Tifa 3 — combo counts).
  The FAQ elsewhere (L2020) describes them as a
  "black boulder with red cracks". GameSpy adds
  that the Pink Tail chest sits "across the
  boulder path" behind the Omen exit — boulders
  also BLOCK PATHS, and probe-based navigation
  reads a blocking boulder as a wall.
- Suspicion (32R): the kaname crystals — giant
  dark crystals with orange fissures (points
  12–16) — match the boulder description closely
  enough to test as boulders or kin. Session 29's
  single Square beside kaname_03 opened the
  attack state but may simply have been too few
  hits, or not connecting. Laguna's hit count is
  unknown; his machine-gun world-map attack may
  combo.
- Standing test protocol (from 32c): at any
  stall or gap edge, face the suspected boulder
  and press Square repeatedly (up to 4–5 times),
  re-aiming between presses; watch for a break
  and a light line; ride the line by moving or
  dashing into it.

### End standing (measured, end of 32R)

- Laguna Lv 8, HP 1852/1852 on the field, Gil
  570, banked KP 38. Gear unchanged (Iron Sword /
  Buckler / Iron Helm / Leather Armor, Ifrit
  AUTO). Inventory gained: Chocobo Down; chest
  64's item unrecorded. Vaan remains the field
  assist. Chapter 3 NOT finished. Furthest
  states: ch3_s32_tableland1.state (plateau east,
  by the jump anchor) and ch3_s32r_break_chan
  .state (plateau east channel stall).

## Session 32c — the attack button found; Chapter 3
route work continues

### Square is not the attack (measured, corrects
sessions 29/32/32R)

- Frame-stepped test from a clean field state
  (ch3_s32r_break_chan.state): one Square press
  moves ctrl state 2→4 and HOLDS it — a screenshot
  at +20 frames shows the equipped-skill
  description popup (Cross Chain) open over the
  field. State 4 on this field is the skill-popup
  state; Square opens and closes it. Every earlier
  "Square attack" test — session 29 beside
  kaname_03, 32R at the beach choke and the
  plateau channel stall, and 32c's own first
  40-press sweep — only toggled that popup and
  never fired a shot. Those negatives are void.
- **Circle is the world-map attack** (measured):
  one Circle press from the same spot produced a
  muzzle flash and an orange tracer streaking
  across the ground in the +6-frame screenshot
  (s32c_circ_c1_a.png); ctrl state stays 2, and
  Laguna steps ~1 unit per volley. This matches
  the series' control tables (Circle = bravery
  attack; Square = skill/HP attack). Boulder
  protocol from here: face the target, press
  Circle repeatedly (up to 4–5), screenshot after.

### Test (a): the plateau channel stall (measured
negative)

- At ch3_s32r_break_chan.state (−6.7, −0.1,
  255.5): 32 Circle presses across 8 facings, on
  top of the earlier 40 Square presses — no
  break, no light line, no state change.
  Screenshots show open grass slope against the
  rock face; there is no boulder at this stall.
  The plateau's east channel is a terrain dead
  end, not a boulder plug.

### Session 32c (continued) — kaname_03: a quickmove line
### ridden; an islet chest that will not open

Test (b) ran in the session-31 lineage state
(ch3_s31_final.state; Laguna Lv 7, Gil 428, KP 34) as a
test only. The walker took him down the valley to
(-101.5, -0.6, 204.7), then south to the crystal's face
at (-104.1, -0.1, 198.4), about 3.6 units north of the
kaname_03 anchor (-103.31, -1.50, 201.88).

- Circle sweep 1 (32 volleys, 8 facings, from the
  northern approach, positions z 203-206): no break,
  no state change.
- At the crystal's face a QUICKMOVE LIGHT LINE is
  present: a bright green-yellow beam running along
  the cliff edge with the floating triangle marker
  (screenshots s32c_csweep_kn03b_start.png and
  s32c_csweep_kn03b_S.png). Whether the line was
  already there or the first sweep's volleys broke an
  unseen boulder and spawned it is UNRESOLVED — the
  approach screenshots were not checked for the line
  before the sweep.
- Circle sweep 2 (32 more volleys at point-blank
  range around the crystal base): the crystal itself
  does not react as a boulder. The kaname crystals do
  not break under 64 aimed volleys total.
- One Triangle press at (-104.1, -0.1, 198.4) RODE
  THE LINE: in ~30 frames Laguna slid ~16 units SSW
  along the western cliff to (-109.9, -0.4, 183.5),
  a small rocky islet (state ch3_s32c_qm_kn03.state).
  The line's return end glows at the islet's north
  edge; Vaan waits on the far shore.
- The islet holds A GOLDEN CHEST (screenshot
  s32c_qm_kn03_tri1b.png), triangulated from three
  camera views to about (-111.8, 187.3), the islet's
  south-west side. IT HAS NOT BEEN OPENED. Ruled out
  by direct test: contact at ~1-unit grid resolution
  over the whole islet (s32c_mow.py, 11 targets,
  11-frame steps, state checked at every stop);
  contact on a tight ring around the triangulated
  spot ((-111.57, 187.02), (-111.06, 187.46),
  (-111.90, 186.74), (-110.80, 186.84)); Cross beside
  it after a 300-frame settle (the session-30 Rosetta
  recipe — Cross dashed him away instead); Circle
  beside it. Contents unknown. (Session 30's lake
  islet chest was bearing-estimated at about
  (-103, 155); this islet is at z about 183, so they
  may be different chests.)
- Point-table dump (from ch3_s32c_qm_kn03.state;
  manager 0x09AEF8D8, table at rd(mgr+0x6C), records
  0x3C bytes, name string at +0x00, position floats
  at +0x24/+0x28/+0x2C): 30 records. New information:
  jump anchors come in PAIRS — rec 0
  POS_jump_gate04_04 (-12.10, 0.50, 32.00) and rec 29
  (name prefix garbled in the dump, reads
  "..._jump_gate04_04") at (4.66, 0.00, 31.39), the
  two ends of one line inside the south-east cluster.
  Gate anchors: gate04_00 (-85.81, 0.19, 241.49),
  gate04_01 (-36.05, 0.00, 257.95), gate04_02
  (-56.71, -0.43, 79.74), gate04_03 (-24.44, 0.87,
  9.07), gate04_04 (0.35, 1.11, 31.41), gate04_05
  (-123.64, -1.57, 63.87), gate04_06 (-243.05, -1.28,
  134.92), gate04_07 (-187.89, -1.57, 182.34),
  gate04_08 (-274.40, -1.39, 165.23). Kaname points:
  01 (-21.21, -2.47, 35.38), 02 (-132.36, -1.92,
  97.51), 03 (-103.31, -1.50, 201.88), 04 (-31.29,
  -2.12, 213.59), 05 (-215.80, -4.00, 167.68). Twelve
  POS_fog points (01-12) at y=3.00 spread over
  x -100..-251, z 29..126 — a far-west region with
  gates 04_06/07/08 and kaname_02/05 that no session
  has visited; presumably a later chapter's ground on
  this shared world map. NO chest record exists in
  this table: field chests live in a different object
  list, still unlocated in RAM.

### Session 32c (continued) — the long way around the
### lake; the south-east landmass still unreached

The true lineage (Laguna Lv 8, Gil 570, KP 38) was
walked from ch3_s32r_break_chan.state (popup dismissed
first: the state had been saved in ctrl state 4;
Circle closed it — clean start saved as
ch3_s32c_start.state). Full route, all dashnav/walker
legs verified by log: plateau west to (-63.8, 247.0)
and (-91.2, 240.8); down the x=-100 climb line in
reverse to the valley (-99.7, 215.2); south past the
kaname_03 crystal shore to the causeway north end
(-86.5, 166.9); the causeway south to the rim
(-83.6, 85.0); east to the Decay terrace (-54.7,
79.3) and its east point (-40.3, 75.8).

- Terrace east point (test (c) area): look shots show
  a wide water gap east to a shore with a tall beacon
  tower; a Circle sweep (32 volleys) at the edge
  broke nothing, spawned no line. No boulder there.
- Beach choke (-80.3, 58.8): the beach's west wall.
  Two Circle sweeps from two positions (64+ volleys
  total) broke nothing. The choke is terrain, not a
  boulder — GameSpy's "break the rock" is not here,
  or not breakable from the beach side.
- Landing cove (-43.2, 51.2): due south across a
  narrow strait stands a burning gateway arch with a
  black orange-fissured rock at its foot (screenshot
  s32c_qm_cove_before.png) — that far shore is the
  south-east landmass (the shrine pair's anchors,
  gate04_03 (-24.44, 9.07) and gate04_04 (0.35,
  31.41), plot just east-south-east of the cove).
  The cove itself is a ~2-unit pocket: no line
  boards there (Triangle opens Customization), no
  boulder, and every dash/walk direction south or
  east fails at once (deep water).
- The world map (Select, state 14) was read at three
  positions (cove, north shore strip, bowl) to fix
  the geography. The map is stylized and NOT globally
  linear (session 30's calibration is a western-lobe
  fit only); trust it topologically, not metrically.
- North-east route: valley east along the highland
  corridor (z 198-216) to the promontory (-53.1,
  205.9). The promontory descends SOUTH to a small
  shore pocket at (-53.7, -2.8, 192.5) on the lake's
  east shore. Across a ~13-unit channel east of the
  pocket: a flat-topped islet with a golden chest
  (about (-42, 198)) and, just beyond, a shore point
  with a manikin (False Stalwart?) and open plain
  behind it — the eastern peninsula that runs south
  to the south-east landmass. THE CHANNEL DOES NOT
  CROSS by walking or dashing (fine-walker probes
  fail in all 8 directions), no line boards at the
  pocket (Triangle = Customization), and Circle
  sweeps at the pocket and at the promontory broke
  nothing. The crossing mechanism for this channel
  is still unfound.
- The pocket's shore runs WEST instead: a beach
  strip (z 181-192) to (-82.0, 181.7), then a valley
  SOUTH along x about -75 (z 156 -> 75) to a bowl at
  (-63.4, -0.4, 74.6) — a ruined stone gate stands
  just east of the bowl — and the bowl's shore runs
  EAST back to the Decay terrace east point
  (-40.0, 75.0). So the promontory pocket, the north
  shore strip, the x=-75 valley, the bowl, and the
  terrace form one connected loop around the lake's
  south-west; none of it touches the south-east
  landmass.
- RAM string search (full dump from the shore strip)
  found object families beyond the POS_ point table:
  OBB_jump_gate03_06, OBB_jump_gate04_04,
  OBB_mes_gate03_06, OBB_mes_gate04_04, N_gate04xx
  nodes, P_kaname_01..05, DES_N_ descriptions, and
  one cleanly-read OBB record: 'OBB_cpu_BtoC' at
  (-123.75, -2.05, 63.79), beside POS_gate04_05.
  The OBB array's stride is not the POS table's
  0x3C, so OBB_mes positions are not yet read.
  Working guess: the OBB_jump/OBB_mes objects are
  the quickmove-line triggers bound to gate04_04
  and the barred gate03_06.

End standing (true lineage, unchanged): Laguna
Lv 8, HP 1852/1852, Gil 570, banked KP 38. Latest
lineage states: ch3_s32c_s15.state (Decay terrace
east point) and the loop states ch3_s32c_n*/e*/s*
in ~/d12s. Chapter 3 is NOT finished: the two live
south-east gateways (gate 12 -> Gateway to the
Shrine, gate 16 -> Southern Shrine) are still
unreached, and the pocket-channel crossing is the
open problem.

### Session 32c — closing note and next first test

Chapter 3 did not finish this session. The record
above is complete through the lake loop and is
pushed on dissidia-012-ch3e (not merged — the
chapter record is incomplete).

Next session's first test (hypothesis, untried):
kaname_04 (-31.29, -2.12, 213.59). Session 32R
reached its basin from the plateau side (descend
south-east from (-47.6, -1.1, 231.7) to the basin
floor (-38.1, -1.5, 214.6)) and stopped 4.3 units
short of the crystal, testing only Square (void —
Square is the skill popup). The corridor side is
walled by the escarpment (walker stalls at
(-52.72, -2.71, 211.20)). At the crystal's face,
test Triangle for a quickmove line (kaname_03's
line was found exactly this way) and a Circle
sweep. If a line exists there it may jump the
escarpment's east side toward the tableland and
the eastern peninsula's north end. Second open
problem: the pocket channel at (-53.7, 192.5) —
the OBB_mes_gate04_04 object's position (array
stride still unread) may name its trigger.

## Session 33 — kaname_04's line ridden; the channel
crossed (Chapter 3 continues)

Branch dissidia-012-ch3f from main 89b5426 (the
session 32/32R/32c record was merged to main first:
ff 2427bb3..89b5426, push verified via gh api).
Scripts s33_*.py in ~/d12s.

### Start standing (measured at boot)

- Start state ch3_s32c_start.state (true lineage on
  the plateau at the east-channel stall): ctrl state
  2, pos (-6.68, -0.07, 255.52), Gil 570. The HUD on
  the landing screenshot below reads Laguna Lv 8,
  HP 1852, Gil 570, banked KP 38 — the true lineage
  confirmed on screen.

### Route to kaname_04 (measured)

- dashnav leg 1: plateau (-6.7, 255.5) -> (-50.4,
  -1.3, 233.9) in 6 cycles (state
  ch3_s33_platsw.state) — the plateau's south-west
  edge, at the head of 32R's descent line.
- Leg 2: -> (-38.6, -1.9, 210.7) in 3 cycles (state
  ch3_s33_basin.state), the basin's south lip.
  Probes from (-37.5, 219.4) moved east freely past
  the crystal's x — the basin's east side is open
  ground, not walled.
- Leg 3: one dash cycle -> (-31.4, -0.7, 211.0),
  2.6 units south of the kaname_04 anchor (-31.29,
  -2.12, 213.59), on the knoll 32R mapped (state
  ch3_s33_kn04near.state).

### kaname_04 face test (measured): a line, ridden
### after a Circle sweep

- First Triangle at the knoll (from (-30.9, -0.6,
  211.0) after the look shots): ctrl state 11
  (Customization) — no line boarded.
- Circle sweep (8 facings x 4 volleys, the csweep
  pattern): no break observed; Laguna drifted about
  a unit per volley east across the knoll to
  (-29.1, -0.8, 211.5). (The first volley likely
  closed the state-11 menu.)
- Second Triangle at (-29.1, -0.8, 211.5): RODE A
  QUICKMOVE LINE. Laguna slid ~19 units SSW over
  the water — the mid-ride screenshot
  (s33_kn04_a_tri2.png) shows the light line
  streaming behind him — and landed at (-38.2,
  -1.0, 194.6), ctrl state 2 (state
  ch3_s33_kn04_end.state).
- Whether the line pre-existed at the knoll's edge
  or the sweep broke an unseen boulder and spawned
  it is UNRESOLVED (the same ambiguity as
  kaname_03). What is measured: from 2 units west,
  Triangle before the sweep did not board; from the
  knoll's east side, Triangle after the sweep did.
- The landing is a sandy point with a GOLDEN CHEST
  beside the landing spot (screenshot
  s33_kn04_a_tri2b.png) — the chest 32c
  bearing-estimated at about (-42, 198) from the
  east-shore pocket, across the channel that would
  not cross on foot. The channel crossing is the
  kaname_04 line.

### The landing islet and its chest (measured):
### Pink Tail

- The landing point (-38.2, -1.0, 194.6) is a small
  flat-topped islet: walkable extent about x
  -41..-36, z 190..197 (probe-walked; its south
  shore stalls at (-36.7, -1.3, 191.7)). It is NOT
  the peninsula — open water still separates it
  from the shore to the east.
- The golden chest stands at the islet's north
  side, about (-38.8, 197.4) (opened from that
  spot). Two earlier attempts missed it because
  they searched the islet's south half and because
  Cross only interacts when the tester has the
  chest as a live candidate: after a 200-frame
  settle at (-38.8, -0.7, 197.4), ONE Cross fired
  the state-16 popup: "Obtained Pink Tail from
  chest!" (screenshot s33_nchest_a_x0.png). This is
  the Pink Tail the guides put across the boulder
  path / around the bend. Gil unchanged (570).
- Lineage state: ch3_s33_nchest_end.state (on the
  islet, Pink Tail in inventory). Laguna Lv 8,
  HP 1852/1852, Gil 570, banked KP 38.

### OBB array read (measured from the 32c RAM dump)

- OBB records share the POS layout (name at +0x00,
  position floats at +0x24). Full OBB map from
  s32c_field.bin: OBB_jump_gate04_04 (4.66, 0.00,
  31.39); OBB_mes_gate04_04 (5.38, 0.00, 33.00);
  OBB_cpu_AtoB (0.38, 0.83, 31.47);
  OBB_jump_gate03_06 (-33.48, 0.00, 299.17);
  OBB_mes_gate03_06 (-33.20, 0.00, 302.18); plus
  OBB_cpu pathing nodes at the other gate anchors.
- So a jump line's two ends are the POS_jump record
  and the OBB_jump record: gate04_04's line runs
  (-12.10, 0.50, 32.00) <-> (4.66, 0.00, 31.39) —
  BOTH ends on the south-east landmass. No jump
  line crosses from the western side; the OBB lead
  names no near-side trigger. The crossing to the
  SE landmass must be a kaname line or nothing.

### East of the basin (measured negative)

- The Triangle on the islet (at the chest spot)
  rides BACK to the knoll: (-38.8, 197.4) ->
  (-29.9, -0.6, 215.4). At the islet's east edge
  (-36.3, 194.2), Triangle opens Customization —
  the islet has exactly one line.
- From the knoll return point, dashnav south
  stalls at the z~211 wall (best southward probe
  slides east along it). The basin's east shelf
  (x -36..-14, z 211..225) is walkable but walled
  on the south at z~211-212 along its whole length
  (probed to x=-14): a closed highland shelf, no
  descent to the peninsula. Lineage state on the
  shelf: ch3_s33_ese1.state (-18.1, -0.4, 212.0).

### The crossing hunt, second half (all measured negatives)

Protocol for the spot tests below: arrive, Circle
sweep (8 facings x 4 volleys), then Triangle at 6
spots; a ride = ctrl state leaves 2/11 or a move
of more than 3 units. Test forks ran from
ch3_s32c_s15.state (terrace east point, Lv 8, no
Pink Tail) unless noted; the true lineage was
not risked on forks.

- Landing cove (-44.6..-46.1, 50.4..51.8): all 6
  Triangle spots opened Customization. The cove
  screenshot shows the far shore across a strait:
  a burning gateway arch with a black
  orange-fissured rock at its foot (kaname_01's
  shore, ~27 units SE).
- Beach choke (-79.5, -2.8, 58.3): all 6 spots
  Customization. Screenshot: a sealed ravine
  ford, shallow stream between rock walls; no
  boulder. (With 32c's 64+ volleys there, the
  choke boulder theory is dead.)
- Plateau east channel stall (-6.7, 255.5): all
  6 spots Customization. No line boards at the
  stall; east of it is walled (walks toward
  (-3.5, 256.5) and (-5.0, 253.0) both fail).
- Decay terrace east point (-40.3, 75.8): sweep
  + 6 spots along the east edge, all
  Customization.
- kaname_04 islet, second line: a full sweep ON
  the islet broke nothing. Triangle at the NE
  shore (-38.0, -0.8, 196.3) boards the SAME
  return line to the knoll (lands (-30.8, 0.6,
  213.8)). The islet has exactly one line.
- East-shore pocket (-53.7, -2.8, 192.5), run
  from its own loop state ch3_s32c_s1.state:
  sweep + Triangle at 6 spots, all
  Customization. The pocket's east edge is
  x ~ -53.7; the channel east of it does not
  cross on foot and boards no line.
- Cove strait: dashnav from the cove toward
  kaname_01 stalls at (-41.9, -2.8, 51.8) — the
  strait is deep water, no reef path.
- Basin shelf east: dashnav from ch3_s33_ese1
  toward (5, 195) makes NO progress in any of
  the 8 probe directions — the shelf's east end
  is walled; no land link to any eastern strip.
- Plateau north-east: from the tableland point
  (-33.4, -0.2, 287.4, beside POS_gate03_06),
  dashnav toward (0, 295) stalls — east is
  walled along the plateau's north edge at
  z ~ 293.

### World map and guides, re-checked

- The Select world map (ctrl state 14) was
  screenshotted and studied (s33_map_a.png). It
  is schematic and NOT to scale: a land strip
  seems to run down the map's east side to the
  SE landmass, but both candidate land links
  (plateau NE, shelf east) measured walled.
  Treat the map as suggestive only.
- link3895 FAQ, this segment: the chest list is
  Soul of Unrivaled Valor, Pink Tail, (EX
  Charge), (BRV Zero); "Heading back towards
  the next gateways, theres a chest next to a
  forest and small mountain with EX Charge, and
  the BRV Zero is across the boulder gap." The
  FAQ's mechanics section adds that red
  gateways "unlock further areas in the
  chapter". Both red gateways before the SE
  pair (Omen of Destruction, Gateway to Decay)
  are cleared; no measured crossing opened.
- GameSpy page 10: "break the rock in front of
  you to jump across the bridge and take a BRV
  Zero skill from chest number one... head to
  the right and hop into the Southern Shrine
  Gateway." The rock's location is still not
  identified; it is NOT at the choke, cove,
  stall, terrace, pocket, or islet.

### Session 33 end standing

- Chapter 3 is NOT finished. Gates 12
  (gate04_04) and 16 (gate04_03) on the SE
  landmass remain unreached; the crossing is
  the open problem, now with a complete
  negative map of every candidate shore.
- True lineage: Laguna Lv 8, HP 1852/1852,
  Gil 570, banked KP 38, Ifrit AUTO, Vaan
  assist, Pink Tail held (opened this session).
  Latest lineage state: ch3_s33_ese1.state
  (basin east shelf, -18.1, -0.4, 212.0).
- Session 34 leads, ranked: (1) the mid-lake
  islet chest ~(-103, 155) — unreached; test
  sweep + Triangle grid at the northern lake's
  south-shore pocket (-88.75, -0.60, 149.37)
  for its line base. (2) kaname_02's face
  (-132.36, -1.92, 97.51) — never tested with
  the correct buttons. (3) Find the False
  Stalwart's actor in RAM: its patrol ground
  identifies the landmass it walks. (4) The
  unexplored quadrant x -20..0, z 60..160 (the
  lake's east arm, the tower shore) — no shore
  of ours touches it; a line base for it is
  unlocated. (5) kaname_05 (-215.80, -4.00,
  167.68) — far west, likely a later segment.
- Harness note: the VM's /tmp was wiped
  mid-session (restart). Durable copies of this
  session's scripts live in ~/d12s on WSL
  (s33_pock.py, s33_isl.py, s33_terr.py,
  s33_map.py, s33_findpocket.py added).

## Session 34 — gate liveness re-run: nothing woke;
the crossing hunt continues

Branch dissidia-012-ch3f (continued). Scripts
s34_*.py in ~/d12s.

### Gate liveness re-run (measured, current lineage)

- Session 31's liveness scan was taken BEFORE
  Great Forest was cleared. Re-run from the true
  lineage state ch3_s33_ese1.state (Laguna Lv 8,
  HP 1852/1852, Gil 570, banked KP 38, Pink Tail
  held; pos (-18.05, -0.38, 212.01), ctrl state 2):
  gate 12 lh(A+4)=15 -> point 7; gate 15
  lh(A+4)=16 -> point 4; gate 16 lh(A+4)=17 ->
  point 6; every other gate -1 or empty — IDENTICAL
  to session 31's scan. No previously inert gate
  has woken now that both red gateways (Omen,
  Decay) and Great Forest are cleared. Note gate
  15 (Great Forest, cleared) still reads 16, so
  lh is a segment-membership marker, not a
  cleared/uncleared flag. The guide line "red
  gateways unlock further areas" does not show up
  as a gate-liveness change; the crossing to the
  SE landmass is still a line/boulder problem.
  Checkpoint state: ch3_s34_start.state.

### The pocket grid, properly probed (measured
### negative)

- Session 33's spot protocol used a fixed-basis
  walker whose steering is camera-relative; this
  session caught it failing (it drifted 23 units
  east during one "sweep"). All spot tests were
  re-done with an empirical goto (snapshot-probe
  navigation, s34_navprobe.py). Northern lake
  south-shore pocket (-88.2, 147.5), west shore
  (-91.1, 151.3), (-93.1, 152.5), shore west end
  (-94.6, 155.0), north pocket shore (-89.8,
  155.2): Circle sweep + Triangle at every spot —
  all Customization, no line, no break. The
  mid-lake islet chest ~(-103, 155) has NO line
  base on this shore. Lead (1) is closed.
- The lake's south-shore shelf (z ~146-155) IS
  walkable east from the pocket to x ~-52, where
  the shore turns north to a headland at
  (-47, 162-167) — a dead end (dashnav probes:
  water east/north, land only back west/south).
  No line or boulder found on the shelf either
  (sweep + Triangle at x -52..-66 all negative).

### The field object table (measured from RAM)

- A scene-object list at 0x097d1410+ (linked
  list, stride ~0x330, each entry carrying two
  transform matrices) holds exactly 18 unique
  positions, IDENTICAL across two dumps taken at
  different field positions — the list is global
  and static (no wandering figure in it; the
  False Stalwart is not here). Members: the five
  kaname crystals (01 (-21.21, 0.93, 35.38),
  02 (-132.36, 1.30, 97.51), 03 (-103.31, 1.66,
  201.88), 04 (-31.29, 1.21, 213.59), 05
  (-215.80, -0.74, 167.68)); a null at origin;
  and twelve others: (-68.00, -1.02, 56.00),
  (-70.00, 0.69, 24.00), (-54.00, 2.09, 18.00),
  (-19.00, 0.76, 26.00), (-75.00, -0.50, 101.00),
  (-40.00, -0.95, 197.00), (-35.00, -0.19,
  289.00), (-110.00, -0.41, 183.00), (-16.00,
  -0.68, 53.00), (-119.00, -0.70, 48.00),
  (-105.00, -0.34, 40.00), (-44.90, -0.59, 31.80).
  Opened chests leave the list (no entry at the
  Rosetta Stone chest (-76.81, 77.64), opened in
  session 30), so the twelve are unopened field
  objects: the two islet chests ((-40, 197) =
  Pink Tail islet in pre-33 state time; (-110,
  183) = kaname_03 islet chest) plus ten more
  whose nature was probed next.
- The beach object (-68.00, -1.02, 56.00) was
  stood on, swept, Triangle'd and Cross'd: it is
  a vertical white-blue light column with
  sparkles, hovering ~1 unit up (screenshots
  s34_look2_beachobj_*). It does not interact
  with Circle, Triangle, Cross or contact.
  Nature unresolved (a marker of some kind);
  the same kind of column presumably accounts
  for the other round-coordinate objects ringing
  the southern waters: (-70, 24), (-54, 18),
  (-44.90, 31.80), (-19, 26), (-16, 53),
  (-105, 40), (-119, 48), (-75, 101),
  (-35, 289).

### Guide route, re-read in full (GameSpy p.10)

- Post-Decay, GameSpy's order is: through the
  empty gateway, head left, Wraith, ignore both
  gateways, four-chest detour (BRV Zero across a
  rock-bridge; EX Charge to the left; False
  Stalwart guarding Soul of Unrivaled Valor;
  Pink Tail around the bend), backtrack, grind
  in the blue gateway, then "head to the right
  and hop into the Southern Shrine Gateway."
  No guide describes a crossing puzzle for the
  shrine itself — the only bridge in the segment
  is the BRV Zero rock-bridge. FF Wiki confirms
  the segment's map enemies (Harlequin Lv 7,
  Reaper Lv 7, Wraith Lv 9, False Stalwart
  Lv 11) and its two remaining boards: Gateway
  to the Shrine (repeatable; Dragoon Lv 6 +
  Lv 12 HP-1, Flash Lv 6, Witch Lv 12; chest
  Gargoyle Pebble; Jump Chain at entry chains
  all four) and Southern Shrine (entry D6;
  Carbuncle AUTO summonstone; Wraith Strange
  Lv 12 HP 1; Wraith Hard Lv 12; Pirate Hard
  Lv 12; Gunner Lv 9; Pirate Lv 9 at E4 —
  stepping there vanishes Vaan and raises the
  boss; Gunner Hard Lv 12; Cloud of Darkness
  Lv 12, HP 2,446, at H4). Guides recommend
  Lv 12+ for the final board; Laguna is Lv 8,
  so farm loops in the repeatable gateway are
  the plan once it is reached.

### The cove, aimed (measured negative); the OBB
### table (measured)

- A feedback facing tool (s34_cove.py) turns
  Laguna by pulse-walking and reading the facing
  vector (player struct +0x08/+0x00) until the
  dot product with the wanted direction exceeds
  0.92 — facing is now aimable, which the fixed
  face() helpers never guaranteed. From the cove
  (-43.7, -2.8, 50.9) and (-46.0, -2.8, 49.8),
  facing due south (fz -0.92..-0.97): 3 rounds
  of 5 volleys each per spot, Triangle after
  every round — nothing broke, no line boarded.
  The strait does not open by shooting south
  from the cove.
- The OBB array (stride 0x60, positions at
  +0x24 like the POS table) holds exactly ten
  records in this segment: the two jump lines'
  far ends (gate04_04 (4.66, 0.00, 31.39);
  gate03_06 (-33.48, 0.00, 299.17)), two "mes"
  sign objects beside them, and six OBB_cpu
  links sitting on gateway anchors (AtoB at
  gate04_04, BtoC at gate04_05, BtoD at
  gate04_02, DtoE at gate04_00, EtoF at the
  plateau, CtoH at gate04_06) — the wandering
  manikins' hop graph, most likely. There is no
  OBB record for any kaname line or light
  column: the only scripted crossings in data
  are the two jump lines, both internal to
  their own landmasses. The kaname lines are
  run by the map-unique objects themselves.
- Cautionary harness note: in the session-29
  fork state, volleys at the cove walked Laguna
  into the (there still living) Counterfeit
  Wraith's zone and a field battle loaded —
  ctrl state read 20 during the load, which is
  NOT exclusive to board entry.

### THE CROSSING (measured — the route problem
### is solved)

- The strait is crossed ON FOOT. Session 30's
  survey (run with the since-discredited fixed-
  basis walker) missed a walkable spit south of
  the beach. Measured route, true lineage, all
  in ctrl state 2, no battles: landing cove
  (-41.8, -2.8, 51.8) -> beach -> south at
  x ~-60 to (-59.3, -2.2, 41.0) -> west along
  the spit to (-69.8, -2.8, 41.1) -> south to
  (-70.7, -1.1, 31.1) -> (-70.8, -0.8, 26.8)
  -> east along the southern shore: (-61.5,
  -0.1, 23.3), (-53.9, 0.7, 20.9), (-47.5, 0.0,
  22.4), (-43.9, -0.5, 30.8), (-35.8, 0.0,
  25.5), (-27.8, -0.1, 20.1) — the south-east
  landmass, reached. The cove's own south edge
  is a wall (wade probes stall at (-46.1, 49.7));
  the crossing is the beach's southern spit,
  ~20 units west of the cove. No boulder, no
  line, no boat: the "crossing" was a shore the
  old walker never probed southward.
- The light-column objects at (-70, 24),
  (-54, 18), (-44.90, 31.80) stand along this
  shore (passed on the walk; not probed).

### Gateway to the Shrine ENTERED (measured)

- gate04_03 (gate 16, point 6, anchor (-24.44,
  0.87, 9.07)) is the GATEWAY TO THE SHRINE —
  banner screenshot s34_south_south1_
  gategate0403_board.png. Session 32's working
  identification was swapped: gate04_04 (gate
  12, point 7, anchor (0.35, 1.11, 31.41)) is
  therefore the Southern Shrine Gateway.
- Entry: the tester zone sits ~3 units NORTH
  of the anchor. Crosses at the anchor and at
  +3E/-3W spots only dashed (no candidate);
  at (-24.6, 0.2, 11.4) the first Cross fired
  the conditions transition (ctrl state 5) and
  the board settled at ctrl state 20, entry
  grid 13. Board state: ch3_s34_gate_
  gate0403.state (= ch3_s34_south_end.state).
  True lineage on the board: Laguna Lv 8,
  HP 1852/1852, Gil 570, banked KP 38.

### Gateway to the Shrine — first clear
### (measured)

- Census (17 discs; entry D2 = grid 13). Wiki
  squares verified one-for-one against the FF
  Wiki list by the grid decode id =
  10*(row-1)+(col-1): B1 Delusory Dragoon
  Lv 12 (Strange, HP 1, BRV 783), D1 Delusory
  Dragoon Lv 6 (HP 1,303), E2 Stigma, F3
  Transient Witch Lv 12 (Hard, HP 1,667), G3
  chest, B4 Vaan assist piece, E4 Fleeting
  Flash Lv 6 (HP 1,303). Entry banter plays in
  ctrl state 20 (advance with Cross; scan only
  engages afterwards).
- Board movement, corrected: a direction press
  alone highlights/prompts but does not reliably
  move her after a battle; the working verb is
  the (Cross, direction) pair repeated until
  the grid changes (first pairs are absorbed).
  Scan edges are a SUPERSET of movement links:
  13->33 (down) and 22->24 (right) are
  scan-only on this board; the real graph used:
  13-12-22-32-33-34, 12-2-3, 13-14 (Stigma).
- Vaan piece engaged at B4 (one-line scene;
  he is the active assist inside the gateway;
  the piece stays on the board).
- Delusory Dragoon Lv 6 (D1): WIN in 29
  rotations, drift 0, Laguna 1852 -> 1239 HP;
  +66 Gil, KP gauge 0 -> 1 (the "Win within
  90 seconds" chance paid).
- The chain accident: the reward walk's blind
  Crosses accepted the chain offer toward B1
  (the postwin chain guard compares the focus
  cell against record pointers, but after a
  battle the cell holds slot addresses/garbage
  — the guard cannot match; chain offers must
  be declined MANUALLY: Circle, Left, Cross).
  The HP-1 Dragoon Lv 12 ground Laguna from
  1239 to 24 HP against blind HP attacks
  (battle block this boot 0x096D01D8; foe
  bravery peaked 972) and a scripted duel from
  24 HP was lost by a hair. Recovered from the
  pre-chain winraw state; the chain was then
  fought properly with the standard rotation
  and WON (the monitoring locked onto a frozen
  template block — 0x0968DFF0, values never
  moved — while the real battle played out
  blind; result read from the board after):
  Laguna Lv 8 -> 9, +204 Gil (636 -> 840),
  gauge to 4.
- Skill prompts: after a win the game queues
  one prompt per equipped board skill (KP
  Gambler: Use Cross / Cancel Circle; Cross
  Chain: Cancel Circle). They block board
  input until all are answered. KP Gambler
  was spent somewhere in the chain/postwin
  handling: the next win paid TRIPLE KP
  (Fleeting Flash, gauge 4 -> 7) and the skill
  left the equipped list.
- Fleeting Flash Lv 6 (E4): WIN in 32
  rotations, drift 0 — but it cost 1233 HP
  (Laguna 1300 -> 67). +66 Gil (840 -> 906).
  At 67 HP the Witch (Lv 12, Hard) was not
  attempted; the board's Stigma was engaged
  instead: gauge 7 paid out, banked KP
  38 -> 45. Laguna on disc 13 for the finish;
  Witch and the G3 chest (Gargoyle Pebble,
  guide claim) remain for the farm loops.
- The finish flow's trailing Crosses re-entered
  a gateway from the exit landing: the SOUTHERN
  SHRINE GATEWAY (banner measured; entry disc
  grid 53 = D6, matching the wiki). Laguna
  stands on it at full HP 1913/1913, Lv 9,
  Gil 906, banked KP 45 — the final board,
  entered earlier than planned. Plan: census,
  exit unfought, farm Gateway to the Shrine
  loops to ~Lv 12 (guides' recommendation),
  then play the Shrine.

### Southern Shrine — first visit (measured)

- Census (24 discs; entry D6 = grid 53). Wiki
  squares verified: A1 Counterfeit Wraith
  (Strange) Lv 12 HP 1; B1 Carbuncle (AUTO)
  summonstone; B2 Counterfeit Wraith (Hard)
  Lv 12 HP 1,667; C2 Idle Sky Pirate (Hard)
  Lv 12 HP 1,667; D4 Transient Gunner Lv 9
  HP 1,485; E4 Idle Sky Pirate Lv 9 HP 1,485
  (the boss trigger); E5 Transient Gunner
  (Hard) Lv 12 HP 1,667; G4 Vaan assist piece;
  H4 Cloud of Darkness Lv 12 HP 2,446.
- Real movement links are again a subset of
  the scan graph (51->31, 22->20, 20->22,
  3->5, 42->44 are scan-only here). The
  {A1, B1, A2} sub-cluster is reachable only
  through B2's disc. Verified route in:
  53-52-42-32-31-30-20-10, prompt right to 11.
- Wraith (Hard): WIN in 68 rotations, zero
  damage; Laguna Lv 11 -> 12 mid-fight.
  Wraith (Strange): WIN in 1 rotation (the
  liveness-checked finder picked the live
  block at 0x096D01D8). Carbuncle scene
  played ("My name is Carbuncle, a stone
  harboring the rainbow light") — summonstone
  granted in this visit's chain. Pirate
  (Hard): WIN in 43 rotations, zero damage,
  Lv 12 -> 13. Gunner Lv 9: WIN in 7
  rotations, zero damage. Pirate Lv 9: WIN
  in 24 rotations, zero damage, Lv 13 -> 14.
  Stepping on E4 fired the boss scene (Vaan's
  assist vanishes; the Cloud of Darkness
  piece kindles at H4; the G4 assist piece
  vanishes with him — disc 36 is empty).
- Gunner (Hard): the visit's only real fight.
  Drift 0 LOSS at 53 rotations (foe at 304),
  drift +10 LOSS at 40 (foe at 166), drift
  +5 WIN at 62 rotations — Laguna survived
  at 286 HP (the foe sat at 1 HP from
  rotation 51 while its bravery peaked at
  1,204). Lv 14 -> 15 on the win.
- Cloud of Darkness attempt at 286 HP: LOSS
  in 6 rotations, foe untouched (her HP
  attacks land immediately; ~285 damage per
  hit). The board was exited via the Start
  menu (which needs a clean, prompt-free
  board state or the Start press is eaten).

### Southern Shrine — second visit and the
### end of Chapter 3 (measured)

- Re-entered at Lv 15, full HP. The Gunner
  (Hard) was SKIPPED this visit (it guards
  nothing; E5's disc is only a doorstep to
  E4, reachable from D4's side as well).
- Wraith (Hard): WIN in 27 rotations, zero
  damage, Lv 15 -> 16. Wraith (Strange): the
  finder locked onto the frozen template
  block again and the fight ran blind — WON
  (Gil/gauge tallies prove it) but the HP-1
  Wraith landed ~968 damage first (Laguna
  2337 -> 1369). Lesson: for HP-1 foes, key
  the candidate filter on her CURRENT HP —
  the template carries a stale one (1324).
  Carbuncle was not re-collected this visit
  (its scene queues skill prompts that block
  movement; see below).
- Skill-prompt queue, final form of the
  lesson: after battles AND scenes the game
  queues one prompt per equipped skill, and
  the queue can hold several at once. They
  block ALL board input and do not always
  render in a given screenshot. Working
  protocol: THREE Circle presses (150 frames
  apart) after every fight or scene before
  navigating; verify with the first hop.
- Pirate (Hard): WIN in 67 rotations, zero
  damage (it regenerates ~23 HP/rotation
  while out of reach — a mid-fight standoff
  — then connects normally). Gunner Lv 9:
  WIN in 7, zero damage, level tick.
  Pirate Lv 9: WIN in 21, zero damage,
  level tick. E4 trigger scene played again.
- CLOUD OF DARKNESS: WIN in 25 rotations,
  drift 0. She hit hard early (Laguna 1510
  -> 279 by rotation 16) but his offense
  outpaced her: 2446 -> 1881 -> 1738 ->
  1153 -> 978 -> 1 (rotation 24) -> 0.
  Laguna standing at 279 HP, +61 level tick
  on the kill (340 HP in the final read).
- The defeat scene plays out in ctrl state
  20 on the board (CoD's valediction on
  manikins and the balance; Laguna's
  ripostes), then the gauge pays out
  (7 -> 0; banked KP 51 -> 58) and the game
  presents the CHAPTER 4 opening board: a
  new lead (Lv 1, HP 1000 — the woman with
  long brown hair), Gil 3582 shared.
  CHAPTER 3 IS COMPLETE. Chapter-end state
  (which is the Chapter 4 opening board,
  unplayed): ch3_s34_chapter3_end.state.
- Laguna's end standing: Lv 19 by level-tick
  accounting (last direct HUD read Lv 16
  after the Wraith (Strange); +61/+60 HP
  level-up ticks — the measured level-up
  heal — in the Gunner Lv 9, Pirate Lv 9
  and Cloud of Darkness wins). Gil 570 ->
  3582 across the session; banked KP 38 ->
  58; Pink Tail, Rosetta Stone, Gargoyle
  Pebble and the Carbuncle summonstone
  (first visit) collected; Ifrit AUTO still
  equipped; Ricochet Shot still unslotted.

### Session 34 — what the crossing was

- No boulder, no boat, no line: a walkable
  sand spit south of the beach, missed since
  session 30 because the survey walker of
  that era steered by a fixed basis that
  could not hold a southward line. The two
  gateway identifications were swapped in
  the session-32 record (gate04_03 is the
  Gateway to the Shrine; gate04_04 is the
  Southern Shrine). The four-chest detour of
  the guides (BRV Zero / EX Charge / Soul of
  Unrivaled Valor / the False Stalwart) was
  NOT done — the light columns along the
  southern shore were never resolved, and
  the False Stalwart was never met in the
  field; they remain open colour for a
  future session, not chapter requirements.

## Session 35 — Chapter 4 opens: Yuna identified,
geared; West Melmond next

Branch dissidia-012-ch4 from main 9b2911b. Start state
ch3_s34_chapter3_end.state (the Chapter 4 opening
board, unplayed). Scripts s35_*.py in ~/d12s.

### The new lead — Yuna (measured from the game)

- The board boots at ctrl state 21, entry grid 20,
  Gil 3582, banked KP 58, gauge 0. A queued tutorial
  popup ("The character in your control has changed.
  Check equipment in the customization menu.") had to
  be dismissed before board input went live; Triangle
  then opens Customization.
- The Customization header reads "Yuna", Lv 1 — the
  woman with long brown hair of session 34's closing
  note. The guides agree (Suih; FF Wiki): Chapter 4
  is "Where Memories Wait", Yuna lead, travelling
  with Jecht.
- Starting standing (Customization, before gearing):
  HP 1000/1000, CP 265/330 used, BRV 95, ATK 11,
  DEF 11, LUK 10, EXP total 0, next 5000. Chocobo
  LUCKY 100% and NEXT BATTLE EXP x3 still active.
- Bravery attacks (aeon summons): LAND Meteor Strike
  (circle neutral; "[Close] Summon Ifrit. Sends foe
  flying. Wall rush"), Energy Blast (circle+towards);
  AIR Heavenly Strike (circle neutral), Impulse
  (circle+towards). HP attacks: LAND Energy Ray
  (square neutral; "[Long] Summon Valefor. Invokes
  vertically long columns of fire") plus one empty
  land slot; AIR Thor's Hammer (square neutral) plus
  one empty air slot.
- Equipment: all four slots EMPTY. Class checks from
  the pool pickers: swords Can't Equip; Bronze/Iron
  Helms Can't Equip; Kunai and Buckler (hand) Can't
  Equip; Hide/Iron Armlets are Gauntlets, Can't
  Equip. Her classes: Rod/Staff/Gun weapons, Bangle
  hand, Hat head, Clothing body.
- Assist: Jecht, set by the game (BRV: Jecht Rush,
  Jecht Stream; HP: Jecht Blade (ground), Triumphant
  Grasp).
- Summons: Ifrit AUTO remaining usage 0 (exhausted
  across Chapters 2–3); Carbuncle AUTO in the pool
  with 3 uses.

### Gearing (pool + shop; the shop trades in Gil)

- From the pool, free: Leather Clothing (body;
  HP+428, BRV+9, DEF-2). Leather Armor is Can't
  Equip for her.
- Shop purchases, each equipped on the spot (the
  equip prompt's cursor starts on YES): Power Armlet
  1300 (hand, Bangle; HP+122, DEF+6), Guard Stick
  1220 (weapon, Rod class; HP+122, BRV-28, ATK+9,
  DEF+2), Headband 300 (head, Hat; HP+85, BRV+38).
  Gil 3582 -> 762.
- Carbuncle AUTO equipped from the Summons page.
- Geared standing: Lv 1, HP 1000 current / 1757 max
  (equipping HP gear does not fill current HP),
  BRV 114, ATK 20, DEF 17, LUK 10, CP 265/330.
- Shop stock noted, not bought: Rod 450 (HP+85,
  BRV-20, ATK+4, DEF+2), Staff 450 (BRV+9, ATK+2),
  Oak Staff 1220 (stats not read; a North Melmond
  chest holds one per the guides), Tanegashima 400
  (already held; she can equip Guns but it stays a
  sidegrade), Capella gun trade-only (HP-231,
  BRV+51, ATK+22, DEF-2, Assist Gauge Duration+15%),
  Bronze Bangle 500 (HP+85, DEF+1), Leather Hat 500
  (BRV+48), Plumed Hat 700 (not read), Robe 400 and
  Cotton Robes 900 (body alternatives, not read).

### West Melmond Gateway — Scan census (full pass)

17 discs, matching Suih's count. Decoded grid id =
10*row + col on this board (row from z: -12, -8, -4,
0, 4, 8; col from x: -18, -14, -10, -6, -2, 2, 6).
Entry disc 20 (-18, -4). Contents by Scan panel:

- 1 (-14, -12): Ephemeral Phantom Lv 3, stage Old
  Chaos Shrine
- 2 (-10, -12): Treasure Chest
- 11 (-14, -8): empty
- 12 (-10, -8): Jecht, Assist Character
- 20 (-18, -4): ENTRY
- 21 (-14, -4): empty
- 22 (-10, -4): Imitation Despot Lv 1, stage Order's
  Sanctuary
- 23 (-6, -4): Treasure Chest
- 24 (-2, -4): Ephemeral Summoner Lv 1, stage
  Order's Sanctuary
- 25 (2, -4): empty
- 26 (6, -4): Stigma of Chaos
- 31 (-14, 0): empty
- 34 (-2, 0): empty
- 35 (2, 0): empty
- 41 (-14, 4): Delusory Dragoon Lv 3, stage Old
  Chaos Shrine
- 45 (2, 4): empty
- 51 (-14, 8): Treasure Chest

All four manikins, all three chests and the Jecht
assist piece match Suih's West Melmond list exactly
(his chest contents — Halbred, Guard Ring, Hairband
— get verified on opening). Movement edges were
walked and recorded in full (s35_census2.py DFS;
s35_census.log): the board is one connected web —
a west column (1-11-21-31-41-51) with the 12/2 pair
hanging off its top, the long row-2 chain
20-21-22-23-24-25-26, and a south-east spur
24-34-35-45 with 34-31 closing a loop. The gateway's
KP Chance (Suih): Win battle. Bonus Line Lv 2.

### West Melmond play (part 1)

Harness lesson first: the Scan cursor pointer
(0x095CD41C) holds 0x00000000 when Scan has never
been opened on this board visit, a record address
while scanning, 0xFFFFFFFF in some other states.
Gate any Select press on the value being a record
address (RECORDS..RECORDS+480); a Select pressed
"to exit" while the value is 0 ENTERS Scan Mode
instead, and board movement then silently stalls
(cost one nav run here).

- Board mechanics as before: engage prompts rise
  from the adjacent disc; after a win she stays on
  her own disc and the piece is consumed. The KP
  Chance "Win battle" pays +1 gauge KP per win.
- Imitation Despot Lv 1 (disc 22; info card: HP
  1000, BRV 95, ATK 11, DEF 5, LUK 10, Cracked
  Shield): standard rotation, WIN in 23 rotations,
  no damage taken. Rewards: Lv 1->2, 1 AP, +6 Gil
  (768), Despot's Desire x1 created (battlegen),
  gauge 0->1.
- Chest on 23: Guard Ring (accessory) — one of
  Suih's three contents for this board.
- Ephemeral Summoner Lv 1 (disc 24; HP 1000, BRV
  95, ATK 11, DEF 3, LUK 10; Cracked Shield and
  Cursed Ring x3): WIN in 18 rotations, no damage.
  Rewards: Lv 2->3, learned Auto Assist, 1 AP, +6
  Gil (774), gauge 1->2.
- Standing: Yuna Lv 3, board HP 1121, Gil 774,
  gauge 2 KP, banked 58.

### West Melmond play (part 2) — board cleared

- Jecht (disc 12): his prompt is a repeatable talk
  prompt, not a consumable recruit — Engage replays
  his line ("Let's take care of these pests quick!
  We still have a ways back to Sanctuary anyway.")
  and the piece stays. Disc 12 can never be stood
  on, so chest 2 is reachable only from disc 1.
- Ephemeral Phantom Lv 3 (disc 1; info card: HP
  1121, BRV 107, ATK 15, DEF 14, LUK 11, Cursed
  Ring x2; battlegen Phantom's Desire 17.5%): WIN
  in 31 rotations, one hit taken (HP 1121 -> 1010).
  Rewards: Phantom's Desire x1 created, 1 AP, +24
  Gil (798), gauge 2->3. No level-up (the EXP x3
  bonus was spent on the first battle).
- Chest on 2: Cotton Robes. (Suih's three contents
  for this board were Halbred, Guard Ring,
  Hairband; measured contents are Guard Ring,
  Cotton Robes, Hairband — two of three match.)
- Delusory Dragoon Lv 3 (disc 41; HP 1121, BRV
  107, ATK 13, DEF 14, LUK 11, Cursed Ring x2;
  battlegen Desire 17.5%, Dream 9.6%, Hopes 8.9%):
  WIN in 35 rotations, no damage. Rewards:
  Dragoon's Desire x1 and Dragoon's Hopes x1 both
  created, Lv 3->4, +24 Gil (822), gauge 3->4.
- Chest on 51: Hairband.
- Standing at the Stigma: Yuna Lv 4, board HP
  1071, Gil 822, gauge 4 KP, banked 58. All four
  manikins beaten, all three chests looted.

### S35 field segment 1 (post-West Melmond) — survey in progress

After the Stigma: cutscene (Yuna separated from Jecht, whistles for him,
meets Tidus), field control at spawn **(-242.96, -1.67, 128.24)**; HP
restored to full **1939** (Lv 4 max with gear); gauge paid out: banked KP
58 -> **62**; Gil **822**. States: `ch4_s35_field1_start` (mid-cutscene),
`ch4_s35_field1_b` (field control at spawn).

Survey so far (positions = player-object floats via ptr 0x08B83DE4
+0x30/+0x34/+0x38): the spawn shore is a peninsula. South coast runs ESE:
(-183, 110) -> (-157, 101) -> a dark lava-cracked Chaos-crystal islet,
dead end at (-130..-134, 98-100) (dashes wedge into its collision pocket;
walk steps extract; Cross at its base does nothing; no eastward progress
from it). NW from (-183, 110): beach (-209, 128) -> (-226, 121) loops SW
back to spawn. West of spawn a raised grassy plateau with a small ruin
stands across a cliff (shore base ~(-250, 130)); no quickmove-line ride
found at its base (8-direction dash probe, all deltas <= 10, y unchanged).
North from (-194, 105): muddy tidal flats to a land spit ending at
(-185, 76) facing deep sea — not a crossing. The in-game field Map
(Select) shows the whole world; Yuna's marker sits on the SW peninsula of
the southern continent, which sprawls EAST — the onward land route is
east along the continent (guide, link3895 1.4.1 World Map note: "go
through the now empty gateway and turn right to get into a big open area.
To the left is the Wraith and first chest. Further along you'll see a
moogle floating upside down and a boulder in the ruins of Melmond, the
Rosetta Stone is just beyond that." Segment contents per guides:
wandering Counterfeit Wraith Lv 4; chests EX Charge + Rosetta Stone;
Mogshop 9.2.5 in the Melmond ruins — buy a Round Chain there if none held
[we hold Cross Chain], optionally a Jump SKILL).

Field-nav lessons: dashnav (s33) costs ~90 s/cycle (state reload per
trial) and ssh `timeout` kills it unpredictably — verify by log + pgrep
before rerunning; single-cycle/short twalk hops (~15-25 s) are the
reliable mover. Stick directions are camera-relative; the heading
estimate in twalk corrupts against walls (oscillation at barriers is the
tell). DES_N_ table at 0x095DCBC8 (stride 0x28) is (name, id) pairs only
— gate0306_* = Ch3, gate04XX_* = Ch4 — no positions. GateMgr table
0x095D75C0 is a link/id registry, not positions.

### S35 North Melmond Gateway — census (measured, Scan pass)

Field route found: from the flats' north edge (z~76-81) a ford gap near
x~-133 lets her cross NE; the North Melmond disc sits at field
(-123.5, 67) — walking onto it fired the gateway banner directly (the
wandering Counterfeit Wraith Lv 4 was NOT encountered; it roams west of
the ford per GameSpy). Opening cutscene on the board: Tidus checks on
Yuna ("Are you travelling alone? You alright?"). Entry grid **63**;
gauge 0; Yuna Lv 4, HP 1939, Gil 822, banked KP 62. KP Chance: **Win
within 90 seconds**. Bonus Line 3. Board = 15 discs (id = 10*row + col):

- 63 ENTRY; 53 empty; 43 empty; 33 **False Hero Lv 1** (The Rift);
  23 empty; 13 **Ephemeral Summoner Lv 1** (The Rift); 3 **Stigma of
  Chaos** (entry column runs 63-53-43-33-23-13-3 straight up).
- West branch: 53 -left-> 52 **Imitation Despot Lv 3** (Old Chaos
  Shrine); 52 -left-> 51 **Treasure Chest** (guide: Oak Staff);
  51 -up-> 31 empty; 31 -right-> 33. (22 -down-> 52 is a drop edge.)
- 23 -left-> 22 **Fleeting Flash Lv 3** (Dream's End).
- East branch: 33 -right-> 34 **Ephemeral Phantom Lv 3** (Dream's End);
  34 -right-> 35 **Summonstone** (guide: Ramuh AUTO); 35 -down-> 45
  **Zidane (Assist Character)**; 34 -down-> 54 empty; 53 -right-> 54;
  43 -right-> 45.

Contents match link3895 1.4.2 exactly (5 manikins, Oak Staff chest,
Ramuh summonstone, Zidane assist). States: `ch4_s35_nm_board` (board
control), `ch4_s35_nm_census.state` (scan cursor back on 63, census
done), census shots `s35_nm_g<grid>.png`, log `s35_census_nm.log`.

### S35 North Melmond — play (part 1)

- **Imitation Despot Lv 3** (disc 52; Opponent Info measured: HP 1121,
  CP 332, BRV 107, ATK 13, DEF 15, LUK 11, Cursed Ring x2, Defense
  -50%): standard rotation LOST (23 rot; she took the full 1939 — this
  Despot hits far harder than West Melmond's Lv 3s). **Drift +5 WON**,
  32 rotations, her HP 227 left. Rewards: 1 AP, +24 Gil (846). No
  level-up (still Lv 4), no battlegen seen. **Gauge stayed 0 — the win
  missed the "Win within 90 seconds" KP Chance.** Board HP persists
  between fights (she continues at 227/1939). States:
  `ch4_s35_nm_prompt52`, `ch4_s35_nmdespot_loss`,
  `ch4_s35_nmdespot5_winraw`, `ch4_s35_nm_despot_down`.
- **Chest 51 = Oak Staff** (matches guide). States:
  `ch4_s35_nm_chest51_open`.
- Harness fix: s35_nav's scan-exit range capped at grid 59; North
  Melmond's entry is grid 63, so the boot check never left Scan Mode.
  `s35_nav2.py` widens it to 80 discs. (Lesson: board id ranges vary;
  gate scan-exit on the pointer being ANY record slot, not a fixed cap.)
- Pre-battle flow note: engaging a piece may show a one-page tip
  carousel ("Team Up! -Wall Rush Combo-") before the Opponent Info
  card; Cross advances tip -> card -> battle. s35_adv.py (cross x8 with
  shots) reads the card reliably at press ~4.

### S35 North Melmond — play (part 2) + movement-graph lesson

- **False Hero Lv 1** (disc 33; HUD-measured HP 1000, BRV 95): standard
  rotation WIN, 24 rotations, her HP 227 -> 155. Rewards: **LEVEL UP
  4 -> 5**, **Learned Auto Assist Lock On**, **Learned Auto Recovery**,
  1 AP, 10 PP, +6 Gil (852). **Gauge +1 (=1)** — 24 rotations met the
  90-second chance. States: `ch4_s35_nm_prompt33`,
  `ch4_s35_nmhero_winraw`, `ch4_s35_nm_hero_down`.
- **Ephemeral Phantom Lv 3** (disc 34; card measured: HP 1121, CP 332,
  BRV 107, ATK 15, DEF 14, LUK 11, Cursed Ring x2, Defense -50%):
  standard rotation WIN, 18 rotations, NO damage taken. Rewards:
  **Phantom's Dream x1 created** (battlegen), 1 AP, +24 Gil (876).
  **Gauge +1 (=2)**. States: `ch4_s35_nm_prompt34`,
  `ch4_s35_nmphantom_winraw`, `ch4_s35_nm_phantom_down`.
- **Battle clock**: the in-battle timer counts DOWN from 1:30.00 — the
  "Win within 90 seconds" KP Chance = kill before it expires. Rough
  guide from today: 18-24 rotation wins pay; the 32-rotation Despot win
  did not.
- **Normal-mode movement graph != Scan graph** (measured by BFS probe
  `s35_bfs2.py`, states `ch4_s35_nm_on<grid>.state`): piece/character/
  chest/stigma discs are PROMPT-ONLY (you never stand on them; focus =
  their slot while grid stays yours). Scan-mode edges can be one-way or
  absent in normal mode (51 -up-> 31 and 52 -up-> 22 do not exist;
  22 -down-> 52 is a scan-only drop). Standable-disc graph so far:
  63 -up-> 53; 53 up 43 / down 63 / left 52 / right 54; 43 up PROMPT 33
  / down 53; 52 left 51 / right 53; 51 right 52; 54 left 53. Winning a
  fight consumes the piece and its disc becomes standable (33 did).
  Anomalies to retest at need: 43 -right-> (Zidane 45) probed none;
  54 -up-> (Phantom 34) probed none (Phantom was prompted from 33).

### S35 North Melmond — play (part 3)

- **Summonstone 35 = Ramuh AUTO** (scene: "Pray for the clouds. Then I
  shall deal judgment to your foes." / "I will not answer your call. I
  shall come when crisis befalls you..." — an AUTO summon, crisis-
  triggered). Stone disc became standable once consumed. States:
  `ch4_s35_nm_prompt35`, `ch4_s35_nm_ramuh`.
- **Zidane (45)**: talk scene only ("Yuna, what's wrong? You seem
  depressed."); no swap prompt seen; whether the active assist changed
  from Jecht is UNVERIFIED. State: `ch4_s35_nm_zidane`.
- **Fleeting Flash Lv 3** (disc 22; card: HP 1121, CP 332, BRV 107,
  ATK 12, DEF 14, LUK 11, Cursed Ring x2, Defense -50%): standard
  rotation LOST at 27 rotations with the foe at **19 HP**; **drift +5
  WON in 16 rotations**. **Auto Recovery is live** — her HP rose
  155 -> 216 during the winning fight. Rewards: **LEVEL UP 5 -> 6**,
  **Learned Speed Boost**, 1 AP, +24 Gil (900). Gauge stayed 2.
  States: `ch4_s35_nm_prompt22`, `ch4_s35_nmflash_loss`,
  `ch4_s35_nmflash5_winraw`, `ch4_s35_nm_flash_down`.
- **Ephemeral Summoner Lv 1** (disc 13; card: HP 1000, CP 330, BRV 95,
  ATK 11, DEF 3, LUK 10, Cracked Shield): standard rotation WIN, 12
  rotations, no damage. Rewards: 1 AP, 10 PP, +6 Gil (906). **Gauge
  jumped 2 -> 6 at this settle (+4).** States: `ch4_s35_nm_prompt13`,
  `ch4_s35_nmsummoner_winraw`, `ch4_s35_nm_summoner_down`.
- **KP gauge model (open question)**: per-settle gauge this board:
  Despot 0 (32-rot win after a loss), Hero 1 (24-rot first-try),
  Phantom 2 (18-rot first-try), Flash 2 (16-rot win after a loss),
  Summoner 6 (12-rot first-try). The +4 is unexplained by a flat +1 —
  possibly piece KP values, a board-completion bonus, or a Bonus-Line
  multiplier (she is Lv 6 vs Bonus Line 3). Recorded as measured; the
  payout at Stigma will bank whatever the gauge holds.

### S35 North Melmond Gateway — CLEAR

Stigma engaged from disc 13 (state `ch4_s35_nm_prompt03`); short field
dialogue; **GATEWAY CLEAR**. Payout: banked KP 62 + gauge 6 = **68**.
Field re-entry at **(-100.43, -0.28, 55.32)** — a mountain ridge area
NE of the North Melmond disc; HP restored to full **2060** (Lv 6 max).
Yuna now: **Lv 6, HP 2060, Gil 906, banked KP 68.** State:
`ch4_s35_nm_clear`.

Segment 2 survey (spin shots `s35_f2_*`): the path runs EAST along the
ridge toward a glowing gateway portal on a rise. Per link3895 1.4.2
World Map note: wandering **False Stalwart Lv 10** straight ahead
(roams right), chest **Oerba Beads** behind it to the right, second
chest **(Cure)** by a boulder near the two next gateways; Mogshop 9.2.5
in the segment. Guide order ahead: **Gateway to the Shrine (Blue)**,
then **Southern Shrine Gateway** (boss The Emperor Lv 18).

### S35 Gateway to the Shrine (Blue) — census + segment-2 route notes

Segment-2 route (measured): Southern Shrine's red portal arch stands
at field **(-1.5, 30.7)** sealing the shore narrows (its trigger fires
when she walks/dashes INTO the portal mouth; standing on its disc is
inert, and exiting via the pause menu deposits her on the disc — exit
and walk away in the SAME drv session or a fresh load re-triggers it).
The Blue gateway's white-blue portal stands on the NORTH shore of the
inlet at ~(-25, 5): wade north from the south shore at (-35, 35) across
the shallows to (-25.7, 11). She entered Southern Shrine first by
accident (banner state `ch4_s35_f2_x`), exited via pause menu (Exit
Gateway confirm defaults to NO — press Left for YES; gauge was 0, no
loss), then entered the Blue gateway in guide order. States:
`ch4_s35_f2_out`, `ch4_s35_f2_away`, `ch4_s35_f2_n1`,
`ch4_s35_blue_board`. NOT yet collected in segment 2: False Stalwart
Lv 10 (wandering, never encountered), chests Oerba Beads + (Cure),
Mogshop 9.2.5. Segment 1's field chests (EX Charge, Rosetta Stone) and
its Counterfeit Wraith Lv 4 were also skipped — the ford route bypassed
them; outstanding if a backtrack ever happens.

**Blue board census** (17 discs, entry **13**, KP Chance: Win within
90 seconds): 14 **Stigma of Chaos** (immediately right of entry);
1 **Ephemeral Phantom Lv 11** (World of Darkness); 5 **Idle Sky Pirate
Lv 11** (Lunar Subterrane); 25 **Idle Sky Pirate Lv 11** (Old Chaos
Shrine) — TWO Sky Pirates (link3895's duplicated "3" was real);
20 **Counterfeit Wraith Lv 11** (Lunar Subterrane); 34 **Fleeting
Flash Lv 6** (Dream's End); 26 **Treasure Chest** (guide: Guard Stick);
empty: 2, 3, 12, 22, 24, 31, 32, 33, 42. Scan edges in
`s35_census_blue.log`; shots `s35_blue_g<grid>.png`; state
`ch4_s35_blue_census.state`.

### S35 Gateway to the Shrine (Blue) — play (part 1)

Movement graph (BFS-measured, `s35_bfs_blu.json`, states
`ch4_s35_blu_on<grid>.state`): 13 up 3 / left 12 / right PROMPT 14
(Stigma); 3 down 13 / left 2; 2 down 12 / left PROMPT 1 / right 3;
12 up 2 / down 22 / right 13; 22 up 12 / down 32; 32 up 22 / down 42 /
left 31 / right 33; 42 up 32; 31 right 32; 33 left 32 / right PROMPT
34. Scan-only edges that do NOT exist in normal mode: 13 -down-> 33,
22 -left-> 20 (Wraith prompt untested — retest with nav p-op),
24 reachable only via 34 once the Flash falls, 25/5 beyond it.

- **Fleeting Flash Lv 6** (disc 34; card: HP 1303, CP 336, BRV 124,
  ATK 15, DEF 9, LUK 13, Cracked Shield): standard rotation STALLED —
  the script's 70-rotation cap hit with the foe at 155 HP (her 853;
  the Flash's evasion stalls the rotation for long stretches; a
  "STALLED" outcome saves a `_loss` state but she is NOT dead).
  **Drift +5 WON in 28 rotations** (her HP 1288). Rewards: **LEVEL UP
  6 -> 7**, **Learned Reverse Air Dash**, **Learned Air Dash**,
  **Flash's Dream x1 created**, 1 AP, +66 Gil (972). Gauge 0 (win came
  after the stalled attempt). States: `ch4_s35_blue_prompt34`,
  `ch4_s35_blueflash_loss` (stall), `ch4_s35_blueflash5_winraw`,
  `ch4_s35_blue_flash_down`.

### S35 Gateway to the Shrine (Blue) — play (part 2) + CLEAR

- **Ephemeral Phantom Lv 11** (disc 1; card: HP 1606, CP 342, BRV 214
  (153+61, Bravery Bonus: Lv +4), ATK 23, DEF 22, LUK 15): standard
  rotation WIN 29 rot, NO damage taken (she entered at 1288 HP).
  **Phantom's Dream x1 created**, 1 AP, +176 Gil (1148). **Gauge +1**.
- **Idle Sky Pirate Lv 11** (disc 25; card: HP 1606, CP 342, BRV 214
  (153+61), ATK 20, DEF 23, LUK 15, Bravery Bonus: Lv +4): standard
  rotation WIN 43 rot, her HP 1288 -> 630 (this one hits back).
  **Sky Pirate's Dream x1 created**, 1 AP, +176 Gil (1324). Gauge 1.
- **Chest 26 = Guard Stick** (duplicate of her equipped weapon; pool).
- **TWO PIECES ARE JUMP-GATED** (measured, spatial): **Counterfeit
  Wraith Lv 11** (disc 20 at (-18,-4)) and **Idle Sky Pirate Lv 11**
  (disc 5 at (2,-12)) each sit TWO cells from every standable disc
  (the intervening lattice cells 21 and 15/4 have no discs). Scan-mode
  edges to them exist; normal-mode prompts never fire across the gap
  (nav p-op x30 and BFS single-pair probes and bare-direction probes
  all negative). They need a Jump skill (Mogshop) — OPTIONAL content,
  left standing; the gateway clears without them.
- **Stigma (14) engaged -> GATEWAY CLEAR.** Gauge 1 banked: KP 68 ->
  **69**. Field re-entry (-21.12, -1.23, 37.98) on the south shore;
  HP restored. **Yuna: Lv 7, Gil 1324, banked KP 69.** State
  `ch4_s35_blue_clear.state`. Remaining in Chapter 4: the Southern
  Shrine Gateway (portal at field (-1.5, 30.7)) — manikins Lv 6-11 and
  the boss, **The Emperor Lv 18**; Bonus Line 12; guide chests Pink
  Tail + Soul of Valor. Segment 2's field content (False Stalwart
  Lv 10, Oerba Beads, (Cure), Mogshop 9.2.5) still unvisited — the
  Mogshop may sell the Jump skill for the two gated pieces above, but
  cleared gateways do not reopen, so they are likely lost for this
  playthrough; recorded as measured, not assumed.

### S35 Southern Shrine Gateway — census + movement analysis

Entry **53**; Bonus Line 12 per link3895; KP Chance not yet read from
a banner (no banner fired on entry — the board loaded straight from
the transition). **26 discs**: 0 **Ephemeral Vision Lv 11** (Lunar
Subterrane), 1 **Treasure Chest**, 10 empty, 11 **Ephemeral Phantom
Lv 11** (Lunar Subterrane), 12 empty, 14 **False Hero Lv 11** (Lunar
Subterrane), 15 **Treasure Chest**, 20 empty, 22 empty, 24 empty,
30 empty, 31 **Ephemeral Vision Lv 11** (Lunar Subterrane), 32 empty,
33 **Imitation Despot Lv 6** (Dream's End), 34 **Ephemeral Summoner
Lv 11** (Lunar Subterrane), 35/36 empty, 37 **??? — "Engage this piece
to find its true form"** (the boss; guide: **The Emperor Lv 18**),
40 **Potion** (restores HP and EX Gauge to 100%), 42 empty,
44 **Ephemeral Phantom Lv 6** (Dream's End), 51/52/54 empty,
60 **False Hero Lv 6** (World of Darkness). **No Stigma disc** — the
??? piece is the clear condition. Guide chests: Pink Tail + Soul of
Valor (discs 1 and 15, order TBD by opening them).

Movement (BFS `s35_bfs_shr.json` + scan edges + lattice coords): the
board opens progressively as pieces fall. Entry cluster {51,52,53,54,
42,32,22,12} prompts: Phantom Lv 6 (44, from 54-up), Despot Lv 6 (33,
from 32-right), Vision Lv 11 (31, from 32-left), Phantom Lv 11 (11,
from 12-left). Beaten discs become standable bridges: 44 -> up 34
(Summoner) -> up 24 -> up 14 (Hero Lv 11) -> right 15 (chest); 34 ->
right 35 -> 36 -> right 37 (boss). 31 beaten -> left 30 -> up 20 ->
up 10 -> up 0 (Vision) -> right 1 (chest); 30 -> down 40 (Potion) ->
down 60 (Hero Lv 6). Scan-only long edges (53-up->33, 51-up->31,
40<->42, 14-left->12) are NOT normal-mode moves — same lesson as the
Blue board's Jump gaps, except here every region has a piece-bridge
route, so no Jump skill is needed. States: `ch4_s35_ss_board`,
`ch4_s35_ss_census.state`, `ch4_s35_shr_on<grid>.state`.

### S35 Southern Shrine — play (part 1) + the chain-dialog KP lesson

Harness fix first: the first Despot attempt returned BLOCK NOT FOUND —
`s18lib.find_block_a`'s her-HP ceiling (the fight script passed 1800)
is below Lv 7 Yuna's 2121 HP. Raised the fight script's cap to 3000;
expect to raise it again as she levels.

- **Imitation Despot Lv 6** (disc 33; card: HP 1303, CP 336, BRV 124,
  ATK 16, DEF 10, LUK 13, Cracked Shield): standard rotation WIN
  19 rot, no damage (Auto Recovery + the level-up left her at 2181).
  **LEVEL UP 7 -> 8**, **Learned Hellfire**, **Despot's Dream x1
  created**, 1 AP, +66 Gil (1390). This board's KP Chance (banner
  text): **Win battle**.
- **CHAIN/KP LESSON (important)**: beating the Despot offered a chain
  to the Ephemeral Vision (31). The settle's three Circles only opened
  a **"Cancel chain?" confirm dialog (defaults to NO)** — the fight's
  KP stayed PENDING (gauge 0) behind it, and the dialog silently
  blocked the next nav run (garbage focus, a fade-corrupted state).
  Pressing **Left + Cross (YES)** canceled the chain and the gauge
  banked immediately (0 -> 1). New settle discipline on chain-offering
  boards: after the circles, run a cancel pass (Left, Cross, circles)
  and verify gauge/screen before navigating. Script:
  `s35_cancelchain.py`; clean state `ch4_s35_ss_nochain.state`
  (on 32, gauge 1, Yuna Lv 8, HP 2181, Gil 1390, banked KP 69).

### S35 Southern Shrine — play (part 2)

- **Ephemeral Phantom Lv 6** (disc 44; card: HP 1303, CP 336, BRV 124,
  ATK 18, DEF 9, LUK 13, Cracked Shield): standard rotation WIN 17 rot
  (her HP 2181 -> 2074). **LEVEL UP 8 -> 9**, **Learned Wall Jump**,
  **Phantom's Desire x1 AND Phantom's Dream x1 created**, 1 AP,
  +66 Gil (1456). Gauge 1 -> 2 at settle (no chain dialog this time).
  Note: the cancel-chain pass is NOT harmless when no dialog is open —
  its Left+Cross acts as a movement hop (54 -> 53); run it only when a
  dialog is actually pending (screenshot/focus check first). Current
  clean state: `ch4_s35_ss_phantom_nc.state` (on 53, gauge 2).

### S35 Southern Shrine — play (part 3) + the Ephemeral Vision problem

- **Ephemeral Summoner Lv 11** (disc 34; card: HP 1606, CP 342,
  BRV 183 (153+30), ATK 21, DEF 21, LUK 15, Bravery Bonus: Lv +2):
  standard rotation WIN 40 rot (she ended at 2134 — Auto Recovery
  outpaced its damage). **LEVEL UP 9 -> 10**, **Learned Auto EX
  Burst**, **Learned Auto Chase**, 1 AP, +176 Gil (1632). Gauge 3.
- **False Hero Lv 11** (disc 14; card: HP 1606, CP 342, BRV 168
  (153+15), ATK 21, DEF 24, LUK 15, Bravery Bonus: Lv +1): standard
  rotation WIN 38 rot but it hits HARD (her HP 2130 -> 576).
  **LEVEL UP 10 -> 12** (two levels), **Learned Sonic Wings**,
  **Hero's Dream x1 created**, 1 AP, +176 Gil (1808). Gauge 4.
- **Ephemeral Vision Lv 11 (disc 31) — UNSOLVED, parked.** Card:
  **HP 1**, CP 342, **BRV 3213 (153 + 3060, "Initial BRV +2000%")**,
  ATK 21, DEF 22, LUK 15; accessories **Soul of the Destroyer** and
  **The Rotten**. In the arena it appears as a giant Odin figure; the
  game tip reads "A square attack can win it!" and its AP Chance is
  "First square attack over 400: +2". Measured behavior: with zero
  input she dies **~390 frames after the battle block appears**
  (BRV drift ticks -9/+9, then HP 576 -> 0 — Odin's Zantetsuken).
  Standard rotation, drift +5/+30, triangle spam (triangle is NOT her
  HP attack — **circle is**; triangle did nothing), assist-first, and
  two R+stick dodge timings (strike ~390-430 frames post-block) all
  lost at rotation 2. Circle (Energy Ray) spam changed the fight:
  periodic **+/-87 BRV transfers** (her BRV up, foe's down — as if the
  ray interacts with the summon) and death delayed to rotation 7-9,
  but still a loss. The block-search itself eats the race window
  (block appears at poll 3; inputs may be locked during the intro).
  The piece gates ONLY the optional west cluster (discs 30/20/10/0
  Vision, chest 1, Potion 40, Hero Lv 6 at 60) — the boss route via
  34 -> 35 -> 36 -> 37 is open without it, so it is parked, not
  blocking. Tools left for a future attempt: `s35_fight2.py`
  (exact-BRV block finder), `s35_watch.py` (kill-timing probe),
  `s35_fight5/6.py` (dodge attempts), prompt state
  `ch4_s35_ss_prompt31.state` (her HP 576 on entry — a Potion or a
  level-up first would widen the margin only slightly; the kill is
  BRV-based, ~3200, so HP is nearly irrelevant).

### S35 Southern Shrine — play (part 4): boss + CHAPTER 4 COMPLETE

- **Chest 15 = Soul of Valor** (matches the guide pair with chest 1,
  which was never opened — it sits in the Vision-gated west cluster).
- **Ephemeral Phantom Lv 11** (disc 11; card: HP 1606, CP 342,
  BRV 153, ATK 23, DEF 22, LUK 15, Cursed Ring, Defense -50%):
  standard rotation WIN 25 rot (her HP 576 -> 414). **Phantom's
  Desire x1 AND Phantom's Dream x1 created**, 1 AP, +176 Gil (1984).
  Gauge 5.
- **The ??? piece (37)** opens with a long cutscene, not a card:
  Tidus confronts Jecht ("Taking you down, old man"), Yuna insists on
  fighting herself ("I'm not moving. I know you won't hurt me."), the
  Emperor appears. The battle that follows is against **Jecht's body**
  — the Emperor explains afterwards: "He has given his own strength
  to his son. The man's body is now but an empty vessel." Measured
  battle block (via `s35_bossprobe2.py`, a her-HP-wildcard scanner —
  the cutscene runs ~44 advance polls before the block exists):
  **foe HP 3270, foe BRV 414, stage pool 569**, block base 096c6508,
  her HP 414 carried in (no story heal). **Standard rotation WIN in
  49 rotations** — the foe's BRV stayed pinned at 414 all fight and
  Auto Recovery outpaced its damage (her HP 414 -> 535). Rewards:
  **LEVEL UP 12 -> 14**, **Learned Reverse Ground Dash**, **Learned
  Ground Dash**, **Despot's Dream x1 created** (battlegen as shown),
  1 AP, +414 Gil (2398). NOTE: an earlier pass through this sequence
  with scene-script crosses let the battle start unobserved and she
  DIED (414 HP, no inputs) — the Retry/Flee menu and a 10 PP loss
  consolation were the tells; the clean run went through
  `ch4_s35_ss_prompt37.state` with the long-poll fight script
  (`s35_fight9.py`, poll cap 60).
- Post-battle scenes: the Emperor's exposition, Jecht's body
  dissolves, Laguna and Vaan regroup with Tidus and Yuna, narration
  ("So resolute was his will, the crystal could have been revealed at
  any moment."), then the game returns to the **main menu — CHAPTER 4
  COMPLETE**. The Southern Shrine gauge (5) paid out at chapter end:
  **banked KP 69 -> 74**.
- **Chapter 5 opening state saved UNPLAYED**: main menu -> Story Mode
  -> Main Scenario 012 -> Chapter 5 - Decision (lead **Lightning
  Lv 13**, "Lightning arrives at the Cornelia Plains, where Sanctuary
  is located"). Opening narration ("Kefka's enigmatic words— Kain's
  betrayal—") and a short field dialogue end with the hand-over
  message ("The character in your control has changed."). State
  **`ch5_s35_chapter5_start.state`**: Lightning Lv 13, HP 2155, free
  field control on a coastal cliff, Gil 2398, KP 74, a gateway portal
  visible across the water. No Ch5 play was done.
- Left unfinished on the Southern Shrine board (documented, optional):
  the Vision-gated west cluster — Ephemeral Vision Lv 11 (disc 0),
  chest 1 (guide pair: Pink Tail), Potion (40), False Hero Lv 6 (60).
  Also unvisited in Ch4's field segments: segment 1's chests (EX
  Charge, Rosetta Stone) + Counterfeit Wraith Lv 4; segment 2's False
  Stalwart Lv 10, Oerba Beads, (Cure), Mogshop 9.2.5; and the Blue
  gateway's two Jump-gated pieces. Cleared gateways do not reopen, so
  these stand as measured-and-missed for this playthrough.
- **Yuna at chapter end: Lv 14, Gil 2398, banked KP 74** (shared
  pools). Chapter 4 boss order note for the record: the guide's
  "Emperor Lv 18" is the story's puppet-master; the fought boss was
  Jecht's emptied body (HP 3270 measured).


## Session 36 — Chapter 5 (Decision): Lightning returns;
North Sanctuary survey under way

Branch dissidia-012-ch5 from main d4d7ed6. Start state
ch5_s35_chapter5_start.state (Chapter 5 opening, saved
unplayed by session 35). Scripts s36_*.py staged in
~/workspace/d12s36, run in WSL via stdin stream against
s18lib, as in session 35.

### Boot + verification (measured from the game)

- Boot: ctrl state 2 (free field control), Gil 2398,
  banked KP 74 (field HUD), Lightning Lv 13, HP
  2155/2155. Position (130.19, -0.85, 43.68) on the
  Cornelia Plains coast, facing NE across a bay; a red
  portal on the far headland, Sanctuary's light pillar
  far NW. Held board skill: Cross Chain.
- Customization (Triangle): EXP next 3715 / total
  84920, CP 225/344, BRV 203, ATK 28, DEF 27, LUK 16.
  Equipment — her Chapter 1 kit is intact: Broadsword,
  Kunai, Bronze Helm, Leather Armor (base Lv 13:
  HP 1727, BRV 165, ATK 22, DEF 24, LUK 16; the gear
  adds HP+428, BRV+38, ATK+6, DEF+3). Assist page not
  re-read; Tifa was her set assist at Chapter 1's end.
- Summons pool: Ifrit AUTO 3 uses, Ramuh AUTO 3,
  Carbuncle AUTO 3 — none was equipped for Lightning;
  Ifrit AUTO equipped this session ("Bravery boosted
  to 150%. Auto when stage bravery gained.").
- MENU LESSON: opening Customization after a chapter
  break queues a stack of notification banners ("New
  inventory in the shop!", "New prizes creatable
  through Battlegen!", "New accomplishments added!");
  every button press is consumed dismissing one banner
  until the stack clears — only then does the cursor
  move. Budget ~6 sacrificial presses after opening
  menus in a fresh chapter.

### Field census (RAM point table, measured)

Point table (mgr 0x09AEF8D8, tab rd(+0x6C), 0x3C
stride) holds exactly one Chapter 5 gate anchor:
POS_gate05_00 @ (307.50, 0.00, -9.00), with
POS_jump_gate05_00 @ (309.27, -0.34, 2.18) — the
Forsaken Kingdom, far ESE (guide data). Other named
points in the region are earlier chapters' anchors
(gate01_00..06, gate02_00/05, gate04_04) and three
kaname crystals on the eastern route: kaname_01
(197.53, -1.62, 56.70), kaname_02 (276.09, -1.37,
10.89), kaname_03 (304.80, -3.32, 39.24). The North
Sanctuary Gateway (blue, repeatable — guides) has no
gate05 point; it must reuse an existing portal in
the Sanctuary area NW of spawn.

Guide ground truth for the chapter (FF Wiki Ch. 5;
link3895 1.5.0; GameSpy p.12): field treasures BRV
Charge (chest past the blue gateway) and 1500 Gil
(chest by the wandering Capricious Reaper Lv 15,
HP 1848, in the grass right of the Cornelia-ruins
moogle); the moogle is Moglin with a special shop
(9.2.X, may not be unlocked yet). North Sanctuary
board (7 discs used of the grid): False Hero Lv 13,
Imitation Despot Lv 13, Idle Sky Pirate Lv 13
(HP 1 glass cannon), Delusory Dragoon Lv 8, Fleeting
Flash Lv 8; chests Iifa Leaf, Zombie Rag, Soul of
Patience; KP Chance: Win within 90 seconds.
Forsaken Kingdom: False Hero Lv 13, Ephemeral
Summoner Lv 13, Ephemeral Phantom Lv 23 (Expert
piece, Jecht manikin, guards the lower chests),
Odin AUTO summonstone, Potion, chests 2000 Gil,
Blazefire Saber, Mythril; the Cosmos Piece is the
boss — Warrior of Light Lv 18. KP Chance: Win
battle. GameSpy recommends Lv 18+ (23 ideal) for
the Forsaken Kingdom battles; Lightning starts
at Lv 13, so North Sanctuary farming comes first.

### Field survey (in progress)

- gate01_02 (145.50, 48.00), 16 units NE of spawn:
  a stone ruin tower on the shore — inert, no portal
  (a Chapter 1 shell).
- South coast from the tower runs S along a cliff
  top (z ~ 46 -> 7) then W along the cliff edge to
  (111.7, 8.1); south of the edge is a tidal flat
  (no descent found along the walked stretch) and
  gate01_03's red portal shows across the flat SW —
  scenery for this chapter.
- The west shore is cut by a bay: from gate01_01's
  ruin (76.5, 72.0) all westward probes fail (water).
  The land route runs NORTH along the bay's east
  shore through the Cornelia ruins field (broken
  walls around (80-90, 90-110)) to gate01_00
  (66.98, 126.21).
- gate01_00's arch holds a bright white-pink portal.
  MEASURED: it does NOT trigger on walk-in — fine
  probes stood her at (67.1, 124.3), (67.9, 124.6),
  (69.1, 125.0), inside the arch pocket (the pocket
  tops out ~z 125; the anchor z 126 is inside the
  masonry) with state68 = 2 throughout — and Cross
  there is just her field jump (it carried her NE
  along the shore). Status: not enterable by either
  method; possibly a lit shell, possibly gated by a
  story flag not yet set. NOT yet resolved which
  gateway this is for Chapter 5 (if any).
- Ruins field: a Light Orb sparkle photographed in
  the ruins ~(75, 108) area, and a small red-pom
  figure (moogle?) at the grass edge WNW of
  (81.5, 100) — approach under way.
- PINT LIST DECODED for this map (list 0 at
  0x095D2B20 via the descriptor array at
  0x089D83B0 — the same addresses as the prologue
  boot; the layout is deterministic). 160 records,
  stride 0x2C: +0x00 id, +0x02 sub, +0x04 set (k2),
  +0x06 KIND, +0x08 flags (all 0x0000FFFF here),
  +0x10/+0x14 float X/Z, +0x18 yaw, +0x1C packed
  model id. The table holds EVERY chapter's objects
  for this shared map, grouped by the set field:
  e.g. set 1 = the prologue's orb cluster
  (97,-4)/(107,24)/(97,30)/(97,60); set 3's path
  (112,49)/(92,55)/(59,80)/(75,108) matches the orb
  photographed live at (75, 108). Gateway shells:
  kind 5 at (66,129)/(74,122) = gate01_00's arch,
  (108,-14)/(110,-12) = gate01_03's, (171,114) = an
  eastern arch; kind 6 at (75,100) and (243,78) in
  sets 138-142 with model ids stepping 0x1A/0x27/
  0x34/0x41/0x4E — one gateway-object variant per
  chapter-set. Which set is live in Chapter 5 is
  decided by the game at runtime, not by the table.
- MOOGLE FOUND (photographed): floating among the
  ruin walls around (67, 107), NW of the ruins
  centre — a speech bubble (x-mark) over its head.
  This is the Cornelia-ruins moogle of the guides
  (Moglin). Talk attempt under way.
- gate01_00 VERDICT (measured): sealed in Ch. 5 —
  behaves exactly like the prologue's locked arch
  (aura lit, no prompt, no walk-in, no Cross entry
  from the south pocket at point-blank range).
- SE headland (145-155, 30-46) is a cul-de-sac
  (the prologue's boxed area); the coast east from
  spawn dead-ends at (154.5, 34.4) and inland NE
  from there is walled. The eastern route does NOT
  leave from the south coast here.
- TOOLING LESSONS: (1) never pipe a streamed drv
  script through `head` — SIGPIPE kills the remote
  python mid-run (it may still commit later steps;
  check the WSL log + dash_step state before
  assuming where she is). (2) The spin script's
  camera rotations TRANSLATE her ~10 units (the
  stick arcs her as the camera swings) — a spin
  state's position is not the pre-spin position.
  (3) shot() EOF'd twice after Cross-jump sequences
  (drv died); spin's shots are the reliable visual
  route. (4) s18lib's Drv has state68()/gil() but
  no pos()/state() — position is read from the
  player object (ptr 0x08B83DE4, floats +0x30/34/38,
  facing +0x08/+0x00) as in the s35 scripts.


### Field survey (continued) — the two portals, and an
entry problem (measured, unresolved)

- WEST SHORE ROUTE FOUND: from gate01_01's ruin the
  shore runs SOUTH along the bay's west side —
  (76, 71.5) -> (93.5, 48) -> (97.8, 28.1) ->
  (91.9, 21.7) -> (105, -15.2) — reaching gate01_03's
  peninsula. (The east ledge over the tidal flat at
  (136.3, 7.3) is a dead shelf; all probes fail.)
- gate01_03 (113.43, -22.21 anchor; objects at
  (108, -14)/(110, -12)): a stone arch on the point
  holding a BLUE-WHITE CRACKLING SPHERE portal,
  floating in the opening above the path. This
  matches the guides' "blue gateway on a peninsula"
  (GameSpy: turn around from spawn, follow the
  water; link3895: the blue gateway with the BRV
  Charge chest around the small mountains behind
  it). The path runs under the sphere to the
  peninsula tip (she has stood on the tip at
  (115.5, -9.8)).
- ENTRY TESTS ON THE BLUE SPHERE (all measured,
  all negative): fine-probe crossings through the
  arch at z -10..-18 from both faces; a continuous
  closed-loop walk (s35 twalk2) across its north
  side; six Cross-dash/jump crossings through its
  x-range (dash paths passing within ~0.6-3 units
  of (109, -13.5)); Cross at 4.3 units facing it;
  Circle (a lunging slash-step); the field pause
  menu at its base (Return / Save and Quit / Help
  Manual only). State68 stayed 2 throughout.
- SQUARE on the field = the skill-description
  overlay (ctrl state 4): shows the equipped board
  skill ("Cross Chain — Engage opponents in four
  directions, diagonally in a chain"). Not an entry.
- gate01_00's white portal received the same test
  battery from its north apron as well (fine-nav
  west along the apron to (66.8, 128.1), then south
  probes): its face is walled from the north too.
  Both portals are inert to every method that
  entered gateways in Chapters 1-4 (walk/dash into
  the mouth per S35's Southern Shrine note, Cross
  at the arch per S24).
- Also measured this session: the SE headland
  (145-155, 30-46) is a cul-de-sac; the coast east
  from spawn dead-ends at (154.5, 34.4); inland NE
  from (150, 40) is walled; the tower hill at
  (146, 46) blocks the direct line to (152, 54);
  the north-coast route east from gate01_00's apron
  descends to (94.9, 98.6) and stops at a cliff
  above the ruins' NE side. A Light Orb sparkle
  photographed at the (128, 37) pint record by the
  spawn rocks; after she stood on the spot the
  sparkle no longer showed in later views
  (collection NOT confirmed — no banner, no
  counter read).
- The moogle (photographed among the ruins walls,
  ~(67, 107) area, x-mark speech bubble) has not
  been reached at talk range: wall blocks stop her
  at (70.9, 103.9), 4.9 units S of it, and spins
  from (66.8, 109.2) and (67.6, 113.5) did not show
  it again — it may drift, or its perch is inside
  the wall cluster's pocket. The guides' wandering
  Capricious Reaper has NOT been seen anywhere in
  the west (guide order puts it after the blue
  gateway, so it may not spawn before that clear).
- OPEN QUESTION for the next session: why no field
  trigger fires. Candidates: (a) the sphere must be
  touched AIRBORNE at its centre height (jump apex
  tests crossed under/around it); (b) a trigger
  needs the chapter's first orb collected (the
  (128, 37) orb may not have registered); (c) the
  savestate's field script is unarmed despite the
  clean chapter-select entry S35 documented — the
  structural remedy is an in-game Save and Quit,
  then re-enter Chapter 5 from the memstick save
  through the chapter select. NOT yet tried.


### Session 36 close — state integrity VERIFIED,
entry mechanism still unknown

- STRUCTURAL TEST DONE: from the field pause at the
  blue gateway, "Save and Quit" (confirm defaults
  NO; Left+Cross = YES) wrote the memstick save and
  returned to the Start Menu (play time 13:05:53).
  Story Mode -> Main Scenario 012 -> "There is a
  story in progress" -> Continue -> "Chapter 5 -
  Decision, Chapter: 12-5-1, Stage: Cornelia
  Plains — Continue this story?" YES resumed the
  chapter FROM THE GAME'S OWN SAVE with Lightning
  standing at (110.9, -12.2), exactly where the
  save was taken. In that fresh, game-managed
  state she walked the blue sphere's whole
  neighbourhood again — state stayed 2. THE
  SAVESTATE WAS NOT THE PROBLEM; the field script
  is armed (position, Gil, KP all persisted).
- IDLE TESTS (negative): 1,200 frames standing at
  (108.4, -12.8) and 1,200 at (110.3, -12.5) — the
  two pint-record points of the arch — no trigger.
- BUTTON LEDGER at the sphere: Cross = jump/dash
  (no prompt at any tested range/facing), Circle =
  lunging slash-step, Square = skill-description
  overlay (state 4), Triangle = Customization menu
  (state 11), Start = field pause (Return / Save
  and Quit / Help Manual — no Enter option).
- SESSION 36 VERDICT: Chapter 5's field is fully
  surveyed on the west half (spawn, both ruins
  gateways, Cornelia ruins, moogle photographed,
  west-shore route both ways), Lightning is geared
  (Ifrit AUTO) and the pools verified (Gil 2,398,
  banked KP 74), but NO GATEWAY WAS ENTERED and the
  chapter is NOT finished. The blue gateway on the
  gate01_03 peninsula — the guides' first gateway
  (North Sanctuary) — resisted every entry method
  that worked in Chapters 1-4.
- LEADS FOR SESSION 37, cheapest first:
  (1) a video/walkthrough reference of an actual
  Chapter 5 gateway entry, watched for the exact
  mechanic (the guides' text does not specify one
  beyond walking in); (2) the moogle at true
  point-blank (its perch inside the ruins wall
  cluster was never reached closer than ~5 units;
  the x-bubble may clear after a story beat — or
  talking may BE the beat); (3) the NE arch at
  (171, 114) — untested, no route found yet; the
  shallows SE of spawn and the coast S of the tip
  are the open route leads; (4) verify the
  (128, 37) orb's collection state via the pint
  record flags before/after standing on it.
- States left in ~/d12s: ch5_s36_fresh2.state is
  the game-managed Continue resume (at the blue
  portal); ch5_s36_idle2.state idles at
  (110.3, -12.5); the memstick save in fp-save-s8
  now holds Chapter 5 in progress at the blue
  gateway.

## Session 37 — Chapter 5: the gateway was never
locked; its entry zone is a small volume at the
POS_ anchor on the arch's SOUTH face

Branch dissidia-012-ch5 from main cba6530. Start
state ch5_s36_fresh2.state (game-managed Continue
resume at the blue portal). Scripts s37_*.py in
~/workspace/d12s37, run in WSL via stdin stream
against s18lib.

### Guide + manual ground truth (re-read)

- GameSpy Ch. 5: from spawn "turn around and follow
  the water to a blue gateway on a peninsula. Walk
  past the blue gateway and open the treasure chest
  for a BRV Charge skill... use the blue gateway to
  earn some levels and KP" — then orbs -> moogle ->
  Reaper -> "step forward and enter this chapter's
  only gateway, Forsaken Kingdom" by the moogle.
- link3895 1.5.0: "to the right is a blue gateway,
  and around the small mountains behind it is the
  first chest... Before tackling this blue gateway,
  go further along to the giant white pillar, you'll
  get the Light Orbs and there's a moogle in the
  ruins of Cornelia." After the blue gateway: "head
  back to the moogle and hang a right. You'll see
  the Reaper... Back to the moogle again and head
  into the gateway."
- Official manual (already in this doc, s-prologue):
  entry = stand in the trigger zone -> a PANEL shows
  the gateway's name and conditions -> press the
  button to enter. The panel is the detector.

### RAM corrections to session 36's model (measured
from the s36 boot dump, offline)

- The point table (tab 0x095e62c0) holds POS_
  anchors for gates 01_00..06, 02_00, 02_05, 04_04,
  05_00 and OBB records ONLY for gates 02_00,
  02_05, 04_04, 05_00 (OBB_jump_* + OBB_mes_*) and
  OBB_cpu_* pathing. Per session 32c's own working
  guess, OBB_jump/OBB_mes are QUICKMOVE-LINE
  triggers, not gateway-entry triggers — the lack
  of OBBs at gate01_* says nothing about entry.
- Pint list 0 decoded fully: KIND 2 = Light Orb
  spawn points (set 12 = spawn-area trail incl.
  (128,37); set 13 = eastern trail (152,54),
  (191,79), (180,82), (194,40); set 8 = the
  gate05_00 cluster). KIND 5 = arch/portal shells.
  KIND 6 at (75,100) and (243,78) in the five
  chapter-variant sets 138-142 are the two MOOGLE
  SHOPS (model ids step per chapter = shop
  inventories), not gateway objects — session 36's
  "gateway-object variant" reading was wrong.
- POS_gate01_03 anchor = (113.43, 0.20, -22.21);
  the sphere visual (kind 5) sits at (110,-12).
  Session 36's probe lines stopped at z=-18 and
  its idles stood on the kind-5 points — the
  anchor, 4+ units further south, was never
  occupied.

### THE BLOCKER BREAKS (measured, photographed)

Fine-walked from fresh2 (110.9,-12.2) to the
anchor: at (113.7,-21.8), 0.5 units from
POS_gate01_03, the screenshot shows the CONDITIONS
PANEL UP: "North Sanctuary Gateway / KP Chance:
Win within 90 seconds / Rules: Disabled / Skills:
Enabled", the blue sphere framed in the arch
ahead (north). The entry zone is a small volume
at the anchor on the arch's SOUTH face — the
side a player reaches walking the coast south
from spawn, opposite the west-shore approach
session 36 used. DETECTOR LESSON: the panel does
NOT change ctrl state (stayed 2 through 600 idle
frames on the spot) and the gate-id cell
(ctrl+0x7B0) stayed -1; only the screenshot (or a
panel-flag cell not yet identified) proves zone
entry. Session 36's "inert portal" verdict was a
detector failure plus a 4-unit miss, not a lock.
State: ch5_s37_anchor.state (panel up, at the
anchor). Gil 2398, banked KP 74, Lightning Lv 13
HP 2155 — all as at chapter start.



### Session 37 addendum 2 — North Sanctuary board: map, two wins, KP banking measured

Board entry plays a short Lightning monologue ("Seems no
one's been through here... Did Kain get to them?"); board
inputs are eaten until it ends (14 Cross advances; ctrl
state stays 20 throughout). Clean board state:
ch5_s37_board2.state (grid 52 = start disc).

BOARD MAP (measured by BFS, s37_bfs.py; disc index =
row*10+col, row 0 at the north; wiki cell = letter col,
row+1): start 52 (C6); spine 42 (C5), 32 (C4), 22 (C3),
12 (C2); east 33 (D4), 43 (D5), 34 (E4), 24 (E3), 23 (D3);
west 21 (B3), 20 (A3). Prompts (pieces): 2 (C1) STIGMA
from 12 up; 21 Delusory Dragoon Lv 8 from 22 left; 23
Fleeting Flash Lv 8 from 22 right / 33 up / 24 left; 14
Idle Sky Pirate Lv 13 from 24 up; 10 False Hero Lv 13
from 20 up; 30 Imitation Despot Lv 13 from 20 down.
Unreached until their guards fall: 0 (A1 chest), 40 (A5
chest), 4 (E1 chest). The live map matches the FF Wiki
grid cell-for-cell.

CROSS CHAIN probe (from disc 32): Square opens the skill
targeting view (top-down), Cross opens the description
popup ("Engage opponents in four directions, diagonally
in a chain. Use X / Cancel O"). From 32 the diagonals
hold both Lv 8 manikins (21, 23) — chainable — but the
session plan fights singles per the house banking rule;
the probe popup was cancelled unused.

FIGHTS (dumb-block rotation square,square,cross,triangle;
L+Circle chord every 6th rotation):
- Delusory Dragoon Lv 8 (HP 1424) from 22: WIN in 30
  rotations, ZERO damage taken; final-blow clock read
  00:08:13 remaining of the 1:30 KP-chance clock.
- Fleeting Flash Lv 8 (HP 1424) from the prompt card the
  post-fight presses happened to raise: WIN in 16
  rotations, zero damage; MID-BATTLE LEVEL-UP — her HP
  cell rose 2155 -> 2216 during the fight; Lightning is
  now Lv 14, HP 2216.

KP BANKING (measured, IMPORTANT): after a win, rewards
flush AUTOMATICALLY over ~1200+ idle frames — gil landed
first (+104 per Lv 8 win: 2398 -> 2502 -> 2606), then the
gauge jumped 0 -> 4 after both wins (2 KP per win with
the chance met: 1 for the win + 1 for the chance, by the
count). Mashing Cross through the results DELAYS banking
(the first win's KP had still not landed after the whole
cancel-chain ritual; it flushed during the second fight's
idle observation). New ritual: on WIN, idle ~1500 frames
in chunks until gauge/gil are stable across 3 checks
(s37_settle.py), THEN save. The Left+Cross cancel-chain
ritual is only for an actual chain prompt.

Standing after two fights: board disc 22, Lightning
Lv 14 HP 2216, Gil 2606, gateway KP 4, banked KP 74.
States: ch5_s37_flash8_slow.state (settled board),
per-disc states ch5_s37_ns_on*.state (pre-fight board)
and ch5_s37_ns2_on*.state (post-two-wins board).



### Session 37 addendum 3 — False Hero stalls the dumb block

False Hero Lv 13 (Warrior of Light manikin, HP 1727,
Demon Wall summon) from disc 20 up: the Ch4 dumb block
(square,square,cross,triangle) STALLED — 70 rotations,
only 172 net damage. Measured dynamics: her BRV kept
draining to 0 (broken at rotation ~15, pool dumped to
0), his BRV sat at 870-980, he landed one 1004-damage HP
attack at rotation ~36 (her HP 2216 -> 1212), and his HP
ticked UP +48 twice (1558 -> 1606 -> 1654: a regen
effect). The battle clock expired during the stall; the
decision went to him (90% HP vs her 55%) and she was
returned to board disc 20 (post-loss state
ch5_s37_hero13_loss.state; fights are therefore restarted
from the pre-fight disc state at full HP, not continued
from the loss state). Lesson restated from Ch1: the dumb
block is repeated HP attacks at base BRV and only works
when the foe closes distance and doesn't block; a
defensive Lv 13 needs a BRV-build/spend cycle (Circle =
BRV attack builds from range, Square spends at >= ~480
BRV) — s37_fight2.py, plus the deterministic drift
search as fallback.



### Session 37 addendum 4 — BRV-cycle at range does nothing; Cross is the approach

Second False Hero attempt (s37_fight2.py: Circle-build
to >=480 BRV, then Square-spend): clean LOSS in 73
rotations — his HP never moved off 1727. Her Circle BRV
attacks whiffed at range for the entire fight; her BRV
only decayed under his ranged BRV attacks (his BRV
repeatedly climbed to ~900 and his HP attacks took her
2216 -> 1256 -> 1020 -> 132 -> 0). Lesson: in battle,
CROSS is the approach (jump/dash toward the foe) — the
dumb block works at all only because its Cross presses
keep closing distance so the Square HP attacks connect
at melee. Any build phase without Cross starves. The
house answer is the session-19 Kuja policy (s37_fight3.py):
dumb-block offense + DEFENSE triggers (foe BRV lethal or
rising >=450/+80 -> R+direction dodge chord, R hold,
analog sidestep, alternating sides) + break-window extra
offense rotations, with a WAIT timing knob as the drift
search dimension.



### Session 37 addendum 5 — Kuja policy nearly kills the False Hero; the cap did it

Third attempt (s37_fight3.py, session-19 policy, WAIT 0):
she took ZERO damage across all 70 rotations — the
defense triggers + Triangle blocks neutralized him
completely (his BRV climbed to 1179 unspent) — and his
HP fell steadily 1727 -> 324 (her ~225-BRV HP attacks
landing every couple of rotations; his regen ticks only
+5..+24). Then the script hit its inherited 70-rotation
cap and IDLED to conclusion, and during the idle he
charged to 2652 BRV and one-shot her. The idle was the
only mistake: the battle clock had NOT expired at 70
rotations of this slower loop. Fix for attempt 4: cap
raised to 120 rotations, no idle-to-conclusion (a stall
now saves a midbattle state instead). Note for the
chapter: at her current damage rate the Lv 13s are
killable but slow (~90+ rotations); the 90-second KP
chance is out of reach for them with this rotation —
acceptable; KP is farmable on gateway repeats.



### Session 37 addendum 6 — False Hero DOWN (policy, 79 rotations, no damage); the chain mechanic decoded

Attempt 4 (cap 120, no idle): WIN in 79 rotations, her
HP 2216 untouched end to end — the deterministic line
(324 HP left at rotation 70) finished 324 -> 142 -> 0.
Reward: +234 gil (2606 -> 2840), banked after ~1200 idle
frames as usual. But the gateway gauge did NOT move —
because of the CHAIN rule, now fully decoded:

After a board win, if another piece is in range of her
disc, the game AUTO-PRESENTS that piece's card (Engage /
Cancel) — she is in a CHAIN. KP from chained wins stays
PENDING (gauge frozen) until the chain ends: Cancel on
the card raises "Cancel chain?" (cursor defaults to NO;
Left+Cross = YES banks the pending KP), or the chain
ends when no piece is in range. The multiplier is the
chain length: the two Lv 8 wins banked 4 KP total
(2 wins x chain 2) once the Flash's win left nothing in
range of disc 22. The False Hero's win auto-presented
the Imitation Despot card (adjacent at disc 30), so its
KP is pending in a live chain of 1 — the settled board
state ch5_s37_ns3_base.state boots with that card up,
and ch5_s37_peek.state holds the "Cancel chain?" dialog
itself (cursor on NO), the safe point from which either
banking (Left+Cross) or engaging is one input away.
Corollary: whether the Hero met the 90-second chance is
unknowable until the chain resolves; and a chain LOSS
risks the pending KP — savestates before a chained
engagement are the insurance.



### Session 37 addendum 7 — NORTH SANCTUARY GATEWAY CLEARED

Remaining board work, in order (all from settled states,
s37_settle2 banking after every win):

- Imitation Despot Lv 13 (HP measured 1568) engaged
  THROUGH the chain from ch5_s37_peek.state (Cross = NO
  on "Cancel chain?", poll Crosses hit Engage): WIN in
  40 rotations of the policy, zero HP damage; she was
  broken once early, irrelevantly. MID-BATTLE LEVEL-UP
  #2: her HP cell 2216 -> 2276 (Lv 15). Reward +234 gil.
  The chain then ended (nothing in range of disc 20) and
  banked NOTHING — both Lv 13 wins missed the 90-second
  KP chance. KP RULE SETTLED: a win pays 1 KP only if
  the chance is met, times the chain multiplier; gil is
  always paid (+104 per Lv 8 win, +234 per Lv 13 win).
- Chest A1 (from 10, aim up): Iifa Leaf. Chest A5 (from
  30, aim down): Soul of Patience. (Both match the wiki.)
- Idle Sky Pirate Lv 13 (HP 1) from 24 up: WIN in 8
  rotations, +1 KP (chance met, no chain), +234 gil —
  BUT the block search took 7 polls and he acted first:
  the battle opened with her HP already at 658 (Torrent
  landed during the search, ~1618 damage from 2276).
  WARNING: against the HP-1 pirate class, the foe moves
  during the block-search polls; he nearly one-shot her
  (the guides' warning is real). Faster block acquisition
  matters for this fight shape.
- Chest E1 (from 14, aim up): Zombie Rag.
- Stigma (from 12, aim up): gateway cleared — state
  20 -> 2, landing at the gate01_03 anchor (113.7, -21.8).

FIELD STANDING after North Sanctuary: Lightning Lv 15,
HP 2276/2276 (CLEARING A GATEWAY FULLY HEALS), Gil 3308,
banked KP 79 (74 + 5). State ch5_s37_field1.state,
standing before the cleared (re-enterable) blue gateway.
Loot: Iifa Leaf, Soul of Patience, Zombie Rag.



### Session 37 addendum 8 — field: peninsula swept, BRV Charge chest not found

Two full circuits of the gateway headland (greedy
waypoint walker s37_walk2.py, screenshots at every
waypoint): anchor (112.8,-22.1), far side (117.1,-27.1),
east shore (122.8,-20.5), tip (116.7,-3.6), west shore
(109.4,-7.9), west flank (103.5,-25.1), (107.8,-29.4),
(113.8,-29.6). The headland's east side is walled at
~(119,-7.4). NO chest anywhere on the headland — the
guides' "chest past the blue gateway / around the small
mountains" (BRV Charge skill) was not located in this
pass; it is optional loot and the search is parked
(revisit with a working object-table read). Warning:
the pint object table's base address from the session-36
boot does NOT hold in this boot — a full rd() scan at
0x095D2B20 returned misaligned garbage (float-bit-pattern
"models", repeating ids), so object-table addresses are
boot-dependent and must be re-derived per boot before
use. The cleared North Sanctuary shows its conditions
panel again on approach (re-enterable for KP farming,
as the guides say).



### Session 37 addendum 9 — the moogle reached; first Moogle Shop is EMPTY; field controls measured

**Ruins approach (from `ch5_s37_field1.state`).** The Cornelia ruins
plateau's SOUTH wall is solid along z~66-68 for x 72-88 (greedy-walker
probes, `s37_walk2.py` runs mog/mog2): the gatehouse arch visible beside
the wall base at ~(80,68) is barred decoration, same lesson as Ch.1's
archway. The ledge along the wall base ends at x~72 (all probes ~0
displacement at (71.8,67.6)). ENTRY IS A GULLY AT x~74-75: a northward
walker leg from the wall base climbed through the cliff into the bowl,
reaching (77.0,85.2) (`ch5_s37_moogle4.state`). An INNER ruins wall at
z~86 (the wall cluster s36 hit) is passed along its WEST side:
(68.7,86.7) -> (61.7,91.8) -> (60.8,100.3) all walkable.

**Moogle intro (lead 2 of the brief: RESOLVED).** Walking NE from
(60.8,100.3), a probe step into (66.6,102.9) fired state 3 — a proximity
story trigger, no button press. Dialogue (screenshots s37_talk_mog7_*):
Moogle: "Oh! It's the Thunder Lady! Remember me?" / "I've opened my own
shop! You're my first customer!!!" — Lightning: "It doesn't look like
you have a great stock..." — Moogle: "Well, it's... Um..." — Lightning:
"—Fine. Let me see what you've got." Then back to field at the same
spot (`ch5_s37_moogle7.state`). NOTE the moogle's live position was
~(70,103), ~5 units NW of its kind-6 anchor (75,100) — it wanders.

**FIELD CONTROLS, measured this session (new).** Plain CROSS with no
stick = an ~8-unit DASH in her facing direction (three identical
(+8.2,-0.8) steps east from the bowl; idle 300 frames = no drift).
CIRCLE = attack lunge (one test: (+3.2,-4.1)) — the orb-absorb/object
break move. SQUARE and TRIANGLE at standstill = no-ops. TALK = hold the
stick TOWARD the NPC and press Cross at close range: from (69.8,98.8),
stick-N + Cross fired state 3. Plain proximity does NOT re-trigger a
talk (walker arrival at (72.7,101.8), 2.3 units from the anchor, was
silent), and a bare Cross beside the moogle only dashes away.

**First Moogle Shop (state 13).** Second talk = one line ("I'm truly
sorry, this is all I have now. It'd be great if you'd equip one of my
items...") then state 13, the MOOGLE SHOP (`ch5_s37_shop3.state` saved
with it open). Categories cycle with R/L (drv names "r"/"l"; "r1"/"l1"
are no-ops). EVERY category list is EMPTY — nine blank rows, Cross on a
row does nothing, gil unchanged at 3,308 across all shop experiments.
The first shop genuinely has no stock yet, exactly the FAQ 9.2.X caveat
("might not actually be unlocked"). Exit = one Circle -> field.
Nothing bought; nothing needed from it for the guide sequence.

**Standing:** `ch5_s37_field2.state`, ruins bowl (69.8,98.8), Lightning
Lv 15, HP 2276/2276, Gil 3,308, banked KP 79. NEXT: the Forsaken Kingdom
gateway (guides: "back to the moogle and gateway; step forward and
enter" — candidates: the white portal POS_gate01_00 (67,126) at the
ruins' north end, and a bright portal glow at the bowl's east shore
~(95,103)); then the Capricious Reaper Lv 15 + 1,500 gil chest in the
grass east of the moogle (guide order puts the Reaper/chest detour
before entering).


### Session 37 addendum 10 — FORSAKEN KINGDOM gateway found (panel photographed)

From the ruins bowl (69.8,98.8), the north ruins open to the portal
pocket: (73.5,103.9) -> (66.4,120.4) -> fine approach to (67.5,124.5),
1.7 units south of POS_gate01_00 (66.98,126.21). The screenshot
(s37_w2_north2_0.png) shows the CONDITIONS PANEL UP: "Forsaken Kingdom /
KP Chance: Win battle / Rules: Disabled / Skills: Enabled" — a tall
stone arch with a blazing white portal, the sea directly behind it.
Same entry pattern as North Sanctuary: zone = the POS_ anchor's
reachable face, panel only visible on screenshot, state stays 2,
gate-id stays -1. `ch5_s37_north2.state` is saved standing in the zone
with the panel up — Cross from there should enter (state 2 -> 5 -> 20).
So gate01_00 IS Forsaken Kingdom; the eastern gate05_00 anchor belongs
to a later field segment. Per the guides, the Reaper + 1,500 gil chest
detour (grass east of the moogle) comes before entering.


### Session 37 addendum 11 — Reaper sweeps negative (optional extra unlocated)

The wandering Capricious Reaper Lv 15 + 1,500 gil chest (both guides
place it in a grass field "right of the moogle") was swept three times
with the greedy walker, which stops on any state change (an attack
would surface): (1) lake-shore grass east of the bowl (80,110) ->
(87.7,102.1) -> (94.8,100.2); (2) the north terrace (89.6,111.7) ->
terrace ends at a cliff z~116-117, (91.8,114.3); (3) the south-west
plains via the gully: (77.0,85.2) -> (82.7,64.2), west legs then
bounced off the plateau's SW cliff corner (71.8,67.6) — the plains
west of x~72 at z 55-68 are NOT reachable from the bowl side. No
attack, no chest, no Reaper sighting in any sweep screenshot. It
remains unlocated — possibly east-field content, possibly gated on a
later story beat. NOT blocking: the guides' critical path is the
gateway. States: ch5_s37_reaper1/2/3.state.


### Session 37 addendum 12 — Forsaken Kingdom: both Lv 13s down, Odin + Potion claimed

**Entry:** from `ch5_s37_north2.state` (panel up), Cross: state 2 -> 5
-> 20, start disc 20 = A3 (matches the wiki). No blocking entry
dialogue. West-half BFS: prompts 11 (False Hero) from 10 right / 21 up;
31 (Ephemeral Summoner) from 30 right / 21 down; chest prompt 22 from
21 right. Disc 40 (A5) is a dead end.

**C3 chest (22) = 2,000 gil** (wiki match), claimed from 21 aim right.
NOTE: the first claim was on a BFS branch; after the Hero fight (which
started from the pre-chest on10 state) the chest was re-claimed on the
fight timeline — check which timeline a state belongs to before
building on it.

**False Hero Lv 13 (HP 1,727)** from 10 aim right, fight3 policy:
WIN in 29 rotations, her HP 2276 -> 2087 (she took one hit early,
during the 4-poll block search; a +61 HP tick mid-fight, seen again
below — likely a level/regen effect). Settled: gil 3,542, gateway KP 1
(the "Win battle" chance pays 1 KP per win). She reached Lv 16 from
this win's EXP (board HUD after the next fight).

**Ephemeral Summoner Lv 13 (HP 1,568)** from 21 aim down: FIRST
ATTEMPT LOST. The block search took 20 polls (vs 4 for the Hero) and
the Summoner free-fired through it (her HP 2,087 -> 1,817 before the
policy started); its damage barely moves its own BRV cell, so the
policy's BRV-based defense triggers fired too late; she was broken at
rot 18 and killed at rot 23 (foe left at 952). Board loss = attempt
reset: she keeps her fight-ENTRY HP (2,087), piece restored.
RETRY WITH WAIT=+10 DRIFT: WIN in 24 rotations, her HP -> 790.
Settled: gil 5,776, gateway KP 2. Lv 17 from this win.

**Full board map (fk2 BFS after both pieces fell):** spine 21-22-23-24;
11 up = prompt 1 (Odin stone); 23 up = prompt 13 (Potion); 33 down =
prompt 43 (Ephemeral Phantom Lv 23 — NOT engaged; it guards only the
two row-5 chests, Blazefire Saber + Mythril); 24 right = prompt 25 =
WARRIOR OF LIGHT boss (F3). No C2/C4/E4 discs (11 right, 31 right,
33 left/right all self-loops).

**Odin AUTO summonstone claimed** from 11 aim up (his line: "Seeking
assistance? Then I shall split your enemies in two."). Claiming it
plays a short board dialogue (Lightning: "Fine. Then let's go.") that
eats movement inputs until advanced through — summonstone claims can
carry dialogue; advance ~16 Crosses before moving.

**Potion (13) claimed** from 23 aim up: "HP recovered 100%" — full
heal to 2,398 (Lv 17 max). Standing: `ch5_s37_fk_potion.state`, disc
23, Lv 17, HP 2398/2398, Gil 5,776, gateway KP 2, banked KP 79.
NEXT: the Warrior of Light (disc 25 from 24 aim right). The Phantom
and its two chests are optional and stay untouched unless the boss
route changes.


### Session 37 addendum 13 — Warrior of Light: SEVEN losses; farming levels

**Boss measured:** disc 25 from 24 aim right. The pre-battle intro
runs ~30 probe polls (240f each, Cross pressed between) before the
battle block exists — `s37_bossprobe.py` (wide finder 1000-4000)
caught it at base 0x0969D688: **Warrior of Light HP 2,762**, her HP
2,398, pool 516. Live state: `ch5_s37_fk_bosslive.state` (mid-intro;
fight scripts run from it with MOVE_DIR=none and exact FOE_HP=2762).

**Attempts (all from bosslive, Lightning Lv 17, HP 2,398):**
fight3 policy WAIT 0: LOSS 39 rots, dealt 226. WAIT +5: LOSS 35,
dealt 692 (best). WAIT +10: LOSS 34, dealt 452. WAIT +15: LOSS 18,
dealt 584. WAIT +20: LOSS 57, dealt 452. Dumb block WAIT 0: LOSS 36,
dealt 0 (every attack whiffed/blocked). Tuned policy (s37_fight4.py:
rising threshold 300/+40, plus a danger trigger at foeBRV >= 55% of
her HP) WAIT +5: LOSS 31, dealt 692.
Pattern: WoL builds BRV in small steps under the old triggers, breaks
her (foeBRV jumps by the pool bonus), and HP-attacks for 1,000-1,426 —
two hits kill her from full. Her own BRV attacks connect too rarely at
Lv 17 to race him. The guides' warning is the story: Lv 18 minimum,
23 ideal; she is 17.

**Decision: FARM.** Exit Gateway (board pause menu: Start -> Exit
Gateway -> confirm defaults to NO; Left+Cross = YES) KEEPS EXP and
rewards — its own description: "Save EXP/rewards gained and escape
gateway. *KP gained in gateway will be lost." Only the 2 unbanked FK
gateway KP were lost; gil 5,776 and the Odin stone kept. Plan: clear
North Sanctuary (full clear via Stigma banks its KP and fully heals)
one or two times for levels, then re-run Forsaken Kingdom (Hero,
Summoner, Odin, Potion all measured) and take the boss at Lv ~20+.
Exited to `ch5_s37_field3.state` at the FK portal (67.5,124.5).

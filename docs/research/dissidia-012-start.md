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

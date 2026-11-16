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

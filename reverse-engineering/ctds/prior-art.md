# ChronoAccess (SNES) Prior Art — Reusable for OGA

- Source: C:/Users/Devin Prater/Downloads/CT_handoff_essential (gadeu SNES ChronoAccess, Bible: code wins over docs).
- Scope: BizHawk SNES WRAM (7E, 128 KB, domain WRAM) + CARTROM + CARTRAM save domain. All below are SNES addresses unless marked ROM.
- Status words: verified-live = read in shipped code paths with dated probe confirms; assumed = probe-only candidates or heuristic never confirmed.

## 1. RAM/WRAM address inventory actually used in code

- Field / position (GameData.ADDR; readers: EntityManager, CoordinateTracker, AutoExplore, Pathfinding, MovementAudio, PassabilityLogger) — verified-live:
- 0x0100 u8 (u16 in PassabilityLogger) current location id.
- 0x0102 u8 current tile x, 0x0103 u8 current tile y (overworld direct reads in Pathfinding).
- 0x011F u8 explore mode.
- 0x0197 / 0x0199 / 0x019B u16le party member 1/2/3 to object slot.
- 0x1100 u8 object member identity, 0x1101 u8 object type identity (per-object, 0x40 objects).
- 0x1180 u16le object script code pointer (per-object stride 2; active-object filter).
- 0x1600 u8 object facing (0 up, 1 down, 2 left, 3 right).
- 0x1800 u8 x high flag, 0x1801 u8 x tile, 0x1880 u8 y high flag, 0x1881 u8 y tile (per-object).
- 0x1A00 u8 move flag, 0x1A01 u8 move length (per-object; footstep/bump source).
- 0x1A81 u8 drawing mode, 0x1B01 u8 solid props, 0x1C00 u8 priority, 0x1C01 u8 event flag, 0x1C80 u8 move props (per-object; entity classify/filter).
- Mode / context byte (Menus, MenuProbe, ChronoAccess, NameEntry) — verified-live:
- 0x0D13 u8 context: 0x01 gameplay, 0x1D name entry, 0x29 battle, 0x2F shop, 0x39 items, 0x4D equipment, 0x65/0x67 status.
- 0x0D76 u8 transition/busy (nonzero = ignore reads).
- Field menus (Menus, MenuProbe) — verified-live:
- 0x0AFC u8 menu selector (steps of 6; /6 = item index).
- 0x0F00 block u8 equipment/item ids, 0x0F61 counts, 0x0FC4 char cursor limit, 0x0FC5 cursor index, 0x0FC6 list limit.
- 0x0F01/0x0F02/0x0F03/0x0F05/0x0F07 u8 menu state bytes; 0x0A7A row value; 0x0B39 selected char token.
- Character stats (Menus, ChronoAccess H key) — verified-live:
- 0x2600 base + pid*0x50: +0x03 u16 cur HP, +0x05 u16 max HP, +0x07 u16 cur MP, +0x09 u16 max MP, +0x12 u8 level, +0x13 3B exp, +0x27 equip, +0x36-0x39 power/stamina/speed/magic, +0x3D/0x3E attack/defense, +0x3F u16 displayed max HP.
- Battle (Menus battle block, ChronoAccess H key; dated confirms 2026-05-25/28) — verified-live:
- 0x095D5 u8 focus slot (0/1/2).
- 0x095DC u8 command cursor slot N = base+N; 0x095DF u8 tech cursor; 0x095E6 u8 item cursor.
- 0x09615 u8 submenu (0 command, 1 tech, 2 item; lingers, only trusted with next).
- 0x099E0 u8 in-list flag (0x40 = inside tech/item list; the trustworthy edge).
- 0x09EE3 u8 highlighted tech id; 0x095EB u8 dual partner slot (lags id, needs settle delay).
- 0x09614 u8 target cursor pos; 0x0A62D u8 target entity slot.
- 0x5E30 + slot*0x80 u16 live HP (slots 0-2 party, 3-12 enemies); 0x5E34 + slot*0x80 u16 live MP.
- 0x2980-0x2982 u8 party char ids; 0x09AE6 preview mirror of char block (diff for equip preview); 0x02C23 + cid*6 custom name.
- Dialog / text (TextMonitor) — verified-live:
- 0x0210/0x0211/0x0212 u8 text buffer ptr lo/hi/bank; 0x0215 u8 status; 0x0217 u8 printed count; 0x0234 u8 pixel x; 0x0235 u16le char word.
- 0x0F03 u8 choice-active (0xA5 showing, 0x03 otherwise); 0x0163 u8 choice nav counter (+1 per move).
- Name entry (NameEntry) — verified-live:
- 0x0F08 u8 cursor col, 0x0F09 u8 cursor row, 0x0F00-0x0F05 6B name buffer (0xFF empty), 0x0F06 u8 typed count.
- Save / config (Menus; CARTRAM domain 0x2000, slot bases 0x0000/0x0A00/0x1400) — verified-live:
- Live WRAM: 0x2C53 3B gold, 0x0A78 time, 0x01F4 scenario, 0x2980 party base, 0x2600 char block.
- SRAM layout offsets: 0x580 party ids, 0x59C save count, 0x5E0 3B gold, 0x5E3-0x5E8 time, 0x5F3 world, 0x200 + pid*0x50 stats.
- Config bytes: 0x0D87 pad, 0x0D8A gauge, 0x0D8B gauge speed, 0x0D8C window color, 0x0D8F skill/item info, 0x0D90 msg speed, 0x2ECA-0x32CA option flags.
- Save display tilemap: 0x3264 slot-empty check, 0x3310-0x3318 time digits, 0x3416/0x3418 save num, 0x3262 char base, 0x337C gold digits.
- Shop: WRAM 0x1000 item list, WRAM 0x2400 shop inventory; live WRAM slots 0x2400/0x2E00/0x3800.
- ROM data (CARTROM reads; Techs, Menus) — verified-live SNES ROM offsets:
- 0x0C15CF tech names 11B each, max id 0x72; 0x0C3B0E tech descriptions; 0x0C06A1 shop price table; 0x1EFA00 text token dictionary.
- Assumed / unconfirmed (do not treat as truth):
- 0x1603 u8 live z-plane id (cited in handoff, never read in code).
- 0x00D77/0x00AF9/0x00AFC/0x05E89/0x05EE8-0x05EEB/0x05F29/0x05F9D/0x05F9E/0x06139/0x0614E setup candidates; windows 0x10A00/0x12000/0x05E80 (RealtimeProbe only).
- MapHeader WRAM scan 0x0000-0x7F00 for width/draw-layer3 pattern (fragile heuristic).
- Disproven, do not reuse: 0x1E927 raw-byte WRAM matching (34,772 false positives, empty intersection); 0x2600 record-table shortcut (unvalidated).
- Enemies.lua is a pure name table (enemy id to name, no RAM reads).

## 2. Feature inventory with hotkeys and maturity

- Entity cycling PageDown/PageUp, category Shift+PageDown/PageUp, reset Shift+Home, re-announce Home, route-to-entity End — working (HotkeyTester dispatches first, EntityManager acts).
- Position announce y, entity debug t — fragile (HotkeyTester checks lowercase only; case depends on BizHawk key reporting).
- Battle HP/MP H, gold G, silver Shift+G, text dump O, choice cursor dot — fragile, verify live (CONFIG fields; dot may report as Period).
- PassabilityLogger J arm, K status, N automap room, B / Shift+B automap overworld, C position search, V reset — working research tools that interrupt play.
- Accessibility menu Delete — working.
- Summary Insert, position M, probe T/Y, repeat R — dead (Navigation.lua and SimpleEntityList.lua missing from tree; R/M/T/Y never dispatched; no working repeat-speech key).
- TextMonitor always-on dialog reader + choice detection (0x0F03/0x0163) — working.
- Battle menus (command/tech/item cursors, focus, target, enemy HP seed, tech desc gate on cached 0x0D8F) — working; most mature menu code in tree.
- Field menus (items/equip with 0x09AE6 preview diff, status, shop, save slots, config) — working but probe-dependent.
- Name entry reader — working.
- Indoor pathfinding on decoder output (whole-tile BFS + object blockers) — working v1, over-cautious by design.
- Overworld routing — dead/not implemented (separate model documented only: solid Layer-2, /8 subtiles).
- MovementAudio footsteps/wall-bump — working (snapshot reader proven by ground-truth logs).
- AutoExplore, Minigames (Bekkler tent loc 0x01B2, Simon counter 0x01F8) — fragile/experimental.
- MenuProbe, RealtimeProbe, TextMonitor dump paths, Menus SaveRAM helpers — dead off gadeu machine (absolute paths, silent no-op).

## 3. Solidity decoder summary

- Format: MapExtraData 24-bit property word per tile, 3 bytes little-endian, row-major, stride 3*width.
- Formula: tile_type = (word & 0xFC) >> 2, range 0-63.
- Classes: 0 open passable; 1 full block blocked unless word & 0x280000 (z-plane neutralize); 2-21 partial corner/angle; 22-23 stairs; 24-29 solid quad; 30 ladder; >30 editor-open.
- v1 rule: is_blocked = full (un-neutralized), partials and quads collapse to blocked, stairs/ladder/>30 passable.
- Validation: offline harness Tools/validate_solidity.lua vs dumps/passability_groundtruth.txt (485 STEP / 554 BUMP, 4 rooms / 2 maps): 32/34 confident walls 94.1%, all walls 63/72 87.5%, walkable 137/158 86.7%, zero real walls read as walkable; misses are furniture-on-floor, directional stairs, single-bump artifacts.
- Module: Build/Data/SolidityDecoder.lua (~99 lines, is_blocked/tile_type/classify, bit-lib shim with arithmetic fallback); MapData.decode_tile_runtime_data consumes it, nil-guarded.
- DS applicability: formula itself is SNES-engine-specific (port of Temporal Flux IL notes) and will NOT decode TOSE DS maps; reusable as design only (conservative whole-tile first + recorded-movement validation harness + quad/stair/z-plane backlog order).

## 4. Portability verdicts

- Directly portable to OGA DS adapter: SolidityDecoder module shape (pure function word-to-blocked + classify + fallback shim); validate_solidity harness pattern (score decoded collision vs recorded STEP/BUMP log); battle UX pattern (focus + submenu + in-list edge flag + settle delay before reading dependent bytes); TextMonitor snapshot pattern (buffer ptr + status + printed count + choice-active/choice-counter); 0x09AE6 preview-diff pattern for equip compare; per-object table reader shape (identity/type/code-ptr/facing/x/y/props loop over 0x40 slots); config-byte caching pattern (cache 0x0D8F only when valid, survive clobber).
- Portable as design only: full WRAM address inventory (DS YQUE layout differs; OGA ram-leads already shows party block at 0x0207291C stride 0x60 vs SNES 0x2600 stride 0x50); solidity formula and class ladder; text token dictionary and buffer addresses; shop/price/tech ROM offsets; save layout and CARTRAM domain trick; overworld Layer-2//8 model (unimplemented even on SNES).
- SNES-only: all CARTROM offsets (0x0C15CF, 0x0C3B0E, 0x0C06A1, 0x1EFA00); BizHawk memory-domain switching code; speech_bridge.ps1/start_bridge.bat Tolk/SAPI bridge; learned NavMaps/extracted mapprops blobs.

## 5. Top 5 open gaps for OGA

- DS equivalents of mode/context byte (0x0D13), menu selector (0x0AFC), battle focus/cursors (0x095D5/0x095DC/0x095DF/0x095E6/0x09615/0x099E0), and live party HP (0x5E30 stride 0x80) — OGA ram-leads has stats/money but no cursor/mode/battle flags.
- DS dialog text buffer location and encoding (SNES token dict + 0x0210 ptr design needs a DS counterpart).
- DS collision format (TOSE port): needs a STEP/BUMP ground-truth recorder plus a validate harness before any router.
- DS entity/object table equivalent (SNES 0x11xx-0x1Cxx per-object arrays) for cycling and routing targets.
- DS menu state machine map (shop/items/equip/status/save display addresses) and name-entry addresses; SNES values do not transfer.

# Chrono Trigger DS (YQUE, USA) — Public RAM Leads

- Game: Chrono Trigger DS, USA, game code YQUE, Game ID YQUE-0B43DC74.
- Scope: documentation and short numeric cheat codes only. No ROM, save, BIOS, or game assets used.
- AR code-type decoding used throughout (NDS Action Replay):
  - 02XXXXXX = 32-bit write to absolute address 0x02XXXXXX.
  - 12XXXXXX = 16-bit write to absolute address 0x02XXXXXX.
  - 22XXXXXX = 8-bit write to absolute address 0x02XXXXXX.
  - C0000000 N / DC000000 OO = repeat following block N+1 times, address stride OO bytes.
  - D8/D6/D5/D4/D2/94/52/E-types = fill, control, button-activator, or ARM-code patches, not plain RAM.
- JPN version note: TAS Lua adds 0x20 to Money, ArenaHP, EnemyStart when cart header word at 0x00000C reads 0x2655DEFF.
- Confidence levels: confirmed (cheat DBs plus independent TAS Lua), corroborated (3 or more independent cheat DBs), single-source (one public source only).
- No menu-cursor, menu-mode, or battle-state-flag addresses found in any public doc searched. Cursor/mode section below records that gap explicitly.

## Party records (field-test anchors: HP, MP, level)

- Base of per-character block: 0x0207291C, stride 0x60 bytes, 7 characters (repeat count 6 = 7 iterations). Corroborated across Neoseeker, GameFAQs board 47122799, Ethereal Games, ChapterCheats.
- Address 0x0207291C, width 16-bit, meaning Max HP of character i at base plus i times 0x60, confidence corroborated.
- Address 0x0207291E, width 16-bit, meaning Current HP, same stride, confidence corroborated.
- Address 0x02072920, width 16-bit, meaning Max MP, same stride, confidence corroborated.
- Address 0x02072922, width 16-bit, meaning Current MP, same stride, confidence corroborated.
- Address 0x0207292D, width 8-bit, meaning Level, same stride, confidence corroborated.
- Address 0x02072930, width 32-bit, meaning Experience (value 0098967F in Max-EXP code), same stride, confidence corroborated.
- Address 0x0207294E, width 16-bit, meaning adjacent EXP word written as zero by the Max-EXP code (exact purpose unclear), confidence single-source.
- Address 0x02072950, width 16-bit, meaning skill/tech points slot written as zero by the Max-Skill-Points code (value looks like a reset, treat with suspicion), confidence single-source.
- Address 0x02072958, width 8-bit, meaning Strength, same stride, confidence corroborated.
- Address 0x02072959, width 8-bit, meaning Stamina, same stride, confidence corroborated.
- Address 0x0207295A, width 8-bit, meaning Speed, same stride, confidence corroborated.
- Address 0x0207295B, width 8-bit, meaning Magic, same stride, confidence corroborated.
- Address 0x0207295C, width 8-bit, meaning Hit/Accuracy, same stride, confidence corroborated.
- Address 0x0207295D, width 8-bit, meaning Evasion, same stride, confidence corroborated.
- Address 0x0207295E, width 8-bit, meaning Magic Defense, same stride, confidence corroborated.
- Address 0x0207295F, width 8-bit, meaning Attack (max FF), same stride, confidence corroborated.
- Address 0x02072960, width 8-bit, meaning Defense (max FF), same stride, confidence corroborated.
- Sources: Neoseeker DS Action Replay page, GameFAQs board 47122799, Ethereal Games US codes, ChapterCheats Set 1 (URLs in Source list below).

## Money, items, equipment, unlock flags

- Address 0x020731FC, width 32-bit, meaning Gil/money (Max Money 000F423F; Infinite Gil 0098967F; TAS Lua reads s32 here), confidence confirmed.
- Address 0x02073127, width 32-bit fill over 0x1F words, meaning learned techs/skills bitfield region start (All Skills/Magic code), confidence corroborated.
- Address block 0x02072FC0 to 0x02073038, width 32-bit itemID-plus-count pairs stride 4, meaning consumable inventory slots (99x codes write 006340NN), confidence corroborated.
- Address 0x02072FC2 (plus 4 per slot), width 8-bit, meaning item count byte within each slot pair, confidence corroborated.
- Address block 0x02072E38 to 0x02072EA4, width 32-bit pairs, meaning helm inventory, confidence corroborated.
- Address 0x02072BC4, width 32-bit fill, meaning weapons inventory start (3 fills cover weapon ID ranges), confidence corroborated.
- Address 0x02072D80, width 32-bit fill over 0x26 entries, meaning armour inventory start, confidence single-source (blogspot mirror set only).
- Address 0x02072EE4, width 32-bit fill over 0x2F entries, meaning accessory inventory start, confidence single-source.
- Address 0x0207307C, width 32-bit fill over 0x23 entries, meaning important/plot items start, confidence corroborated.
- Address 0x02073219, width 8-bit, meaning New Game Plus enable flag, confidence corroborated.
- Address 0x0207450E, width 32-bit fill over 0x72 words, meaning EXTRA modes unlock flags, confidence corroborated.
- Address 0x0207455B, width 32-bit fill over 0x25 words, meaning monster-album completion flags, confidence corroborated.
- Address 0x02119968, width 16-bit, meaning TP awarded after battle (999 = 03E7), confidence corroborated.
- Sources: Ethereal Games US codes, actionreplay-codes blogspot US page, TASVideos CTdata.lua (URLs below).

## Party control, Dimensional Vortex extras, map state

- Address 0x021C01CC, width 32-bit, meaning party-control / Save-plus-Change-Party-Anywhere flag (write 80010000), confidence corroborated.
- Address 0x021C0052 and 0x021C0242, width 8-bit each, meaning Silver Points (Arena of Ages currency), confidence corroborated.
- Address 0x021C0053, width 8-bit, meaning cats owned (Have All Cats), confidence corroborated.
- Address 0x021C005F and 0x021C021C, width 8-bit each, meaning cat food (button-activated), confidence single-source.
- Address 0x021DE000, width byte-field block, meaning map/overworld state: MapX at plus 0xE3, MapY at plus 0xE7, Epoch status at plus 0x294, Epoch map s16 at plus 0x29F, Epoch choice s16 at plus 0x2AF; all from TAS Lua, confidence single-source.
- Address 0x021C00CD, width 8-bit, meaning Epoch-use flag (one byte before Lavos room byte), confidence single-source.
- Address 0x021C00CE, width 8-bit, meaning Lavos room status; plus 0x10 = Lavos phase byte; from TAS Lua, confidence single-source.
- Sources: actionreplay-codes blogspot US page, TASVideos CTdata.lua (URLs below).

## Battle state (live combat + Arena of Ages)

- Address 0x022119C00, width 16-bit index at plus 0, stride 0x80, 8 slots, meaning enemy table start (TAS Lua: slot active when index 1..254; Enemy-HP-1 code writes here), confidence confirmed.
- Address 0x022119C03 and 0x022119C04, width 8-bit each, meaning enemy current-HP low/high byte (max-HP copy at plus 5 per TAS Lua reads), confidence confirmed (AR plus TAS agree).
- Address 0x022119A83 and 0x022119A84, width 8-bit each, meaning party member 1 battle current-HP low/high byte, confidence corroborated.
- Address 0x022119B03 and 0x022119B04, width 8-bit each, meaning party member 2 battle current-HP bytes, confidence corroborated.
- Address 0x022119B83 and 0x022119B84, width 8-bit each, meaning party member 3 battle current-HP bytes, confidence corroborated.
- Address 0x022119A87 and 0x022119A89, width 8-bit each, meaning party member 1 battle MP bytes, confidence corroborated.
- Address 0x022119B07 and 0x022119B09, width 8-bit each, meaning party member 2 battle MP bytes, confidence corroborated.
- Address 0x022119B87 and 0x022119B89, width 8-bit each, meaning party member 3 battle MP bytes, confidence corroborated.
- Address 0x020FD776, width 16-bit, meaning Arena monster wins (max 03E7), confidence corroborated.
- Address 0x020FD780, width 16-bit, meaning Arena monster HP (9999 = 270F; TAS Lua reads s16 plus HP-change at plus 2, carried item at plus 0x48, prize at plus 0x50), confidence confirmed.
- Address 0x020FD784, width 16-bit, meaning Arena second HP slot (paired 9999 write), confidence corroborated.
- Address 0x020FD788 onward, width 16-bit stat block, meaning Arena monster stats (99s) plus prize table from 0x020FD78C, confidence corroborated.
- Sources: Neoseeker DS Action Replay page, TASVideos CTdata.lua (URLs below).

## Cursor and mode (party select, menu state) — gap

- No menu-cursor position, menu-mode, or party-select-cursor address found in any public doc searched (GameFAQs boards, Neoseeker, Ethereal, ChapterCheats, AlmarsGuides, TASVideos, Chrono Compendium).
- The 0x94000130 codes are button-activator conditions on the NDS keypad register, not game cursor state; not usable as cursor leads.
- Suggested next step: live melonDS RAM search — open party-select screen, change highlighted member, filter 8-bit values 0..6; then freeze candidates to confirm.
- Related but distinct: 0x021C01CC above gates whether party-change is allowed at all, the closest documented hook for party-select work.

## ARM-code hook addresses (patches, not data RAM; useful for breakpoints)

- Address 0x0202C8A8, width 32-bit ARM, meaning shop-spend routine NOP (money not spent), confidence single-source (AlmarsGuides).
- Address 0x0212257C, width 32-bit ARM, meaning walk-through-walls patch (check 0A000047, patch EA000047), confidence corroborated.
- Address 0x02113C62C plus 0x02113C53C, 0x02113C540, 0x02113C544, width 32-bit ARM each, meaning second collision-bypass patch set, confidence single-source.
- Address 0x02111278, width 32-bit ARM, meaning gold/EXP multiplier patch (E0832082 base, variants up to E0832402 for x2..x256), confidence corroborated.
- Address 0x02135FBC and 0x02135FC0, width 32-bit ARM, meaning Arena one-hit-kill patch, confidence corroborated.
- These are code addresses; set execute breakpoints here in melonDS rather than watching them as data.

## Source list

- https://www.neoseeker.com/chrono-trigger/action_replay/ds/
- https://gamefaqs.gamespot.com/boards/950181-chrono-trigger/47122799
- https://gamefaqs.gamespot.com/boards/950181-chrono-trigger/47183904
- https://etherealgames.com/nds/c/chrono-trigger/action-replay-codes-us/
- https://www.chaptercheats.com/cheats/ds/26934/chrono-trigger-cheat-codes
- https://www.almarsguides.com/retro/walkthroughs/NDS/Games/ChronoTrigger/ActionReplay/
- http://actionreplay-codes.blogspot.com/2011/03/chrono-trigger-ds-us.html
- https://tasvideos.org/UserFiles/Info/637995197367461851
- https://www.chronocompendium.com/Term/Event_Command_Differences.html (SNES-era memory model only; no DS absolute addresses usable)

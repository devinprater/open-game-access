# Chrono Trigger DS — RE Tooling Leads Survey

Scope: public docs, editors, and open-source tooling only. No ROM, save, BIOS, or game asset was downloaded, copied, or requested for this survey.

- Chrono Compendium Modification hub (useful-links page for everything below): https://www.chronocompendium.com/Term/Modification.html

## 1. Temporal Flux + Geiger's database (SNES-side knowledge)

- Temporal Flux is a .NET Chrono Trigger (SNES) editor by Geiger; latest is 3.04, with a 3.04 R1 fork by Reld; it edits dialogue, locations, overworlds, scenario script, and has a plugin architecture.
  - Sources: https://romhackplaza.org/utilities/temporal-flux-utility and https://www.chronocompendium.com/Term/Modification.html
- Geiger was the first to audit the SNES ROM end to end; the "Chrono Trigger Database" offsets text file is hosted at his homepage (geigercount.net/crypt path credited as the source of the compression reference implementation).
  - Sources: https://www.chronocompendium.com/Term/Modification.html and https://github.com/nmrugg/chrono-editor/blob/master/ReadMe.md
- Companion spreadsheet to Geiger's guide by FaustWolf: sprite-sheet graphics, palettes, tile assembly, field background tilesets, enemy AI, sequence data, BRR sound samples; graphics must be decompressed in Temporal Flux before viewing (except PC sprites).
  - Source: https://www.romhacking.net/documents/444/
- Event-command / script docs derived from this work (event command lists, memory-location notes such as 7E0290 Epoch coords) describe game-logic semantics, not just addresses.
  - Source: https://www.chronocompendium.com/Term/Temporal_Flux_Event_Tricks.html
- Open-source reimplementation of the exact LZ77 variant Chrono Trigger (SNES) uses for sprites, matching Temporal Flux's reference (11-bit and 12-bit offset modes, identical control bytes and termination).
  - Source: https://github.com/nmrugg/chrono-editor/blob/master/ReadMe.md
- Likely carried to the DS port (assessment, TOSE reimplemented the same game logic): event/script command semantics, tech/item/enemy stats and damage formulas, map and encounter design; SNES RAM-map values (e.g. 7E0290) are useful as semantic landmarks to hunt for, not as addresses.
  - Basis: DS port is the same game with the same systems; see section 5 sources.
- Almost certainly NOT carried (65816/SNES-specific): 65816 banked assembly, LoROM/HiROM ROM offsets, SPC700/BRR audio format, Mode 7 and SNES PPU register tricks, ZSNES/savestate workflows.
  - Basis: DS is ARM9/ARM7 with its own sound and 2D hardware; see gbatek memory map in section 2.
- Open question (unverified): whether the DS port reuses the same LZ77 variant for its assets; Temporal Flux source plus nmrugg/chrono-editor give a ready-made decompressor to test against DS-extracted blobs.

## 2. DS-specific disassembly notes, ARM9 overlays, Ghidra loader docs

- NTRGhidra (pedro-javierf/NTRGhidra): Nintendo DS binary loader + plugin for Ghidra; prompts ARM9 (ARM:LE:32:v5t) vs ARM7 (ARM:LE:32:v4t); handles compressed ARM9 binary and compressed/uncompressed overlays; overlay manager plugin dynamically loads/unloads overlays but requires the original ROM path unchanged.
  - Sources: https://github.com/pedro-javierf/NTRGhidra and loader source https://github.com/pedro-javierf/NTRGhidra/blob/f6a528919572865c7bacf21ee82582db9b238cdd/src/main/java/ntrghidra/NTRGhidraLoader.java
- Loader tutorial pair by the same author (how NDS headers, autopsy, overlay blocks work): linked from the NTRGhidra README ("Tutorial: Writing a Ghidra loader", "Advanced Ghidra Loader").
  - Source: https://github.com/pedro-javierf/NTRGhidra/blob/master/README.md
- GBATEK DS memory maps (nocash): ARM9 ITCM 32KB, DTCM 16KB, main RAM 02000000h 4MB, shared WRAM, I/O 04000000h, palettes 05000000h, VRAM 06000000h+, OAM 07000000h, ARM9 BIOS FFFF0000h; CP15 protection-unit region table included.
  - Sources: https://problemkaputt.de/gbatek-ds-memory-maps.htm and https://problemkaputt.de/gbatek-ds-memory-control-cache-and-tcm.htm
- Practical RE reading: DS game code of interest lives in the ARM9 binary plus numbered overlays that swap over the same RAM addresses; any static analysis must track overlay IDs (FAT/FileId in the overlay table), and live verification must confirm which overlay is resident.
  - Basis: NTRGhidra loader source (loadARM9Overlays) + gbatek maps above.
- No Chrono Trigger DS-specific disassembly notes or symbol maps surfaced in this survey; that gap is the reason for the live-verification workflow in sections 3–4.

## 3. melonDS debugger capabilities for live verification

- GDB stub: ARM9 port 3333, ARM7 port 3334, configurable in Emu Settings -> Devtools; supports register read/write, memory read/write, single-stepping, code breakpoints; JIT must be disabled while debugging.
  - Sources: https://github.com/melonDS-emu/melonDS/pull/1583 and https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/using-gdb-with-ghidra-and-melonds
- GDB stub internals (GdbStub.cpp/GdbCmds.cpp): Z/Z-packets map to hardware breakpoints and watchpoints; known limitation historically that watchpoints could be set but never fired.
  - Source: https://github.com/melonDS-emu/melonDS/blob/10a173b5/src/debug/GdbStub.cpp
- Watchpoint-enabling PR #2424 ("Enable GDB watchpoints, SIGSEGV on data/prefetch aborts") refactors ARMv5/ARMv4 classes; a Dragon Quest IX RCE developer vouched the fork's data-abort halt and memory-watch features were essential.
  - Source: https://github.com/melonDS-emu/melonDS/pull/2424
- Ghidra-to-melonDS live workflow exists and is documented (Ghidra Debugger + gdbmultiarch targeting localhost:3333, Dynamic PC listing for breakpoints); written for NSMB DS but game-agnostic.
  - Source: https://bookstack.nsmbcentral.net/books/new-super-mario-bros/page/using-gdb-with-ghidra-and-melonds
- Built-in RAM search dialog with hex view for cheat-finding without GDB.
  - Source: https://github.com/melonDS-emu/melonDS/issues/1857
- Long-standing debugger feature request (#249: ARM/THUMB code view, step over/out, register/memory/VRAM views, code+read+write breakpoints, debug-map/symbol support, callstack) marks what upstream melonDS still lacks; OGA's headless poke_core + ramwatch/ramscan tools are the in-repo workaround.
  - Source: https://github.com/melonDS-emu/melonDS/issues/249
- Caveat: melonDS with JIT allocates emulated RAM non-statically, so out-of-process memory scraping (Cheat Engine style) is unreliable; use the GDB stub or in-process reads instead.
  - Source: https://melonds.kuribo64.net/board/thread.php?pid=2295

## 4. RE tooling already inside /home/devin/oga-work

- reverse-engineering/: currently contains only fe11/ (static-analysis.json, symbols.json) — Fire Emblem work; no Chrono Trigger DS material yet, so ctds/ will be the first CT DS directory there.
  - Source path: /home/devin/oga-work/reverse-engineering/
- scripts/: Java decompile/query helpers (DisDecompile*.java, DisFindStrings, DisFindLoaders, DisMenuAccess, DisMenuInit, DisScanStringRefs, TagTeam*.java, DbzAr*.java, Sg*.java) plus shell probes (armstate.sh, bootstate.sh, bd-probe.py, bd-watch-dp.py, bt-snap2.py, etc.) — string/menu/trace mining toolkit reusable for ARM9 binaries.
  - Source path: /home/devin/oga-work/scripts/
- Core/ramscan.cpp: headless melonDS probe that scans a RAM window around a claimed address to distinguish "wrong ROM revision" from "structure not allocated yet".
  - Source path: /home/devin/oga-work/Core/ramscan.cpp
- Core/ramwatch.cpp: headless melonDS probe that samples claimed Action Replay addresses live (addr:width:label list to CSV) to confirm or reject published cheats as real state locations.
  - Source path: /home/devin/oga-work/Core/ramwatch.cpp

## 5. DS-port facts that shape RE work

- Developer TOSE; Square Enix publisher; USA release 25 Nov 2008, product ID NTR-YQUE-USA (JP NTR-YQUJ-JPN); 2008 TOSE port of the SNES game to DS ARM7/ARM9.
  - Sources: https://gamefaqs.gamespot.com/ds/950181-chrono-trigger/data and https://gamefaqs.gamespot.com/snes/563538-chrono-trigger/data
- Revised translation (not the SNES Woolsey script); controversially drops Frog's archaic "olde world" accent, which was never in the Japanese original.
  - Sources: https://www.eurogamer.net/chrono-trigger-review and https://strategywiki.org/wiki/Chrono_Trigger/Version_differences
- Dimensional Vortex: post-credits dungeon, three new Gates (12,000 B.C., 1000 A.D., 2300 A.D.), per-era layout rewarding permanent upgrades for Marle, Crono, Lucca; clearing all unlocks a new boss and ending.
  - Source: https://strategywiki.org/wiki/Chrono_Trigger/Version_differences
- Lost Sanctum: alternate-dimension dungeon spanning Prehistory and Middle Ages; fetch-quest heavy; source of overpowered new gear.
  - Sources: https://www.chronowiki.org/wiki/Lost_Sanctum and https://gamefaqs.gamespot.com/boards/950181-chrono-trigger/78165860
- Arena of the Ages: Wi-Fi monster-raising minigame; PS1 anime cutscenes included with the SNES slowdown fixed.
  - Sources: https://gamefaqs.gamespot.com/boards/950181-chrono-trigger/78165860 and https://www.eurogamer.net/chrono-trigger-review
- Touch menus optional: DS mode (menus/stats on bottom screen, large touch buttons, shortcuts) vs Classic mode (SNES-identical layout plus map); fully remappable controls, walk/run default, Active/Wait battle choice.
  - Source: https://www.ign.com/articles/2008/10/23/chrono-trigger-ds-hands-on
- AP warp-lock: DS Protect anti-piracy checks; on failure the first time-travel sequence loops endlessly; never in the SNES original; unpatched dumps circulating online still carry it, so RE must expect AP code paths in the ARM9 binary and prefer verified/patched dumps for testing.
  - Sources: https://tcrf.net/Chrono_Trigger_(Nintendo_DS) and https://gbatemp.net/threads/looking-for-help-with-chrono-trigger-stuck-in-warp-issue.369234/
- Published AP-bypass cheat landmarks (0204E334 E3A00000 / 0204E338 E12FFF1E / 0204E694 E3A00000 / 0204E698 E12FFF1E) double as known code addresses to anchor static analysis against live RAM.
  - Source: https://gbatemp.net/threads/chrono-trigger-ds-usa-piracy-fix.118540/

## Bottom line

- Single most useful tool: NTRGhidra (https://github.com/pedro-javierf/NTRGhidra) — without an NDS-aware loader that decompresses the ARM9 binary and maps overlays, no static work on the port is possible; pair it with the melonDS GDB stub on port 3333 for live overlay-residency checks via ramwatch.cpp.

# Dissidia board tooltip: name path (2026-10-01)

Live-traced with `scripts/psp-memtrace` (classic-interp MemCheck reads)
from `trunks10/final.ppst`, hover RRR -> cursor (3,2) = False Hero (338 HP).

## Proven

- Catalog -> O walk (M=[0x08B98940] -> ... -> K/CB -> O). Triple-confirmed.
- Hovered O (slot0): `0x9C11488`, type=u16[+4]=0, u32[+8]=0x300001,
  s16[+10]=0x30, s16[+0x18]/[+0x1A]=flag indices, slot+4=338 HP.
- Tooltip builder (near `001c5d90`): after `O = 001c5d90(C, key)` reads
  `lh` at **O+0x18 and O+0x1A** (NOT O+10 — the api-base-report `+10`
  attribution is wrong for this board), masks to byte, tests bits via
  `0x089C27B8` (`srav`+`&1` bitmap test).
- `f1750(s16[O+10])` = species-table binary search (stride 42, count 809)
  returns NULL for board keys (table min key 131410); tolerated because
  PPSSPP returns 0 for unmapped low reads (NULL+34 -> 0, NULL+3 -> 0).
  Callers: `0x089D3F94` (`lh a0,10(s3); jal f1750`), `0x893E24C`.
- Name text comes from the STATIC pool entry `0x09B4284E`
  ("False Hero", header `[id=0x1A, 0x1E, kind=0x1301]`), ~20x Read16 at
  hover, zero baseline. Heap copy `0x09C10D8E` never touched by builder.
- Entry fetch `0x089034A8(entry)`: header-kind check (`entry[0]==0x1C`
  takes the format branch at `0x890355C`), then glyph-layout copy
  `0x08904E5C` (`lhu` loop `0x08905000`, VFPU layout, mailbox allocators).
  Mailbox `[ctx+18944]` always points at ctx+0x180 composer buffer
  (writers `0x08903F60`, `0x08907718`).
- Composer call sites (board module, all `a0=[sX+0x6CCC],a1=0,a3=-1`):
  `0x0893BB7C` (a2=s5), `0x0893E21C` (a2=s0=sp+0x10),
  `0x0893E2F0` (a2=s1+0xA84), `0x0893F0C0` (a2=sp, buffer init
  `0x8B438E4` just above). All four may render (4 tooltip lines).
- Static pool: kind `0x1301` = roster names, header `[id, ?, kind]` at
  text-6/-4/-2. Heap pool holds 22 roster names, ids 0x1A-0x2E
  (see inventory in session notes). O+10 values (0x30/0x31/0x7F/0x80/
  0x00/0x32) do NOT overlap pool ids — O->entry map still open.
- Per-frame species-table polling via `0x08B6B21C` comparator dispatch
  (u16 fields +0/+40, stride 42): probe sets {0,2,6,12,24,34,36,198,396}
  (+0) and {0,2,30,34,36,48,98} (+40). Head of table sequential
  u16[0]=0x30+i. Exact search/count unresolved.

## Open

- O -> pool-entry computation (composer internals: 0x8A588CC string
  machinery, ctx object graph). WORKAROUND: empirical map
  {O+10: name} via battle-engage + Opponent-Info OCR (slots have
  distinct HP: 338/399/396/411/401/352), "enemy here" fallback.

- Slot1 (4,1) hovered 2026-10-02: its in-game tooltip title row is
  BLANK (no dark or light glyphs; lower rows Lv/DP CHANCE/BATTLE MAP
  populated). So the game's own board-tooltip path yields an empty
  name for slot1 — battle-engage OCR is REQUIRED for slots 1-5.

- Different-board coverage (per Devin 2026-10-02): EPYON 100% save
  (276h) + other characters (Terra/Cecil). NOTE: the save files live
  in `~/enemy-out/gfsave-20238/` — a fresh probe savedir has no save
  ("Load failed. The data is corrupted."). Copy the dir, reuse the
  copy as the probe savedir. Drive: Load screen X=Enter + trail for
  the load transition.

## Harness crash note (2026-10-01)

- 64-bit host builds do NOT define `MASKED_PSP_MEMORY` (only 32-bit /
  UWP / ASAN / **iOS** / Emscripten do). The game's tolerated-NULL reads
  (`f1750` -> NULL, then `NULL+0x22` etc.) SIGSEGV the host under the IR
  interpreter (`ReadUnchecked_U32(addr=16)`, `IRInterpreter.cpp:163`).
  Classic-interp memory ops are checked and return 0.
- **Device is unaffected** (iOS masks). Host tools must use
  `PSP_CLASSIC=1` (classic interp) or a full `-DMASKED_PSP_MEMORY`
  rebuild (needs ALL TUs rebuilt together — ODR).
- `scripts/psp-enemy-probe` honors `PSP_CLASSIC=1`.

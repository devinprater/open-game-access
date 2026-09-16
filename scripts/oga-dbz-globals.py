#!/usr/bin/env python3
"""Find a game's REAL globals by CODE REFERENCE DENSITY, not by value plausibility.

WHY THIS WORKS WHEN VALUE SCANNING FAILS
----------------------------------------
Scanning RAM for "a plausible integer that differs across stride-spaced records"
returned 348 hits of pure graphics noise, and a pointer hunt returned nothing —
because both approaches guess at VALUES.

But an ARM program names its important globals in a different way: the address
appears in a LITERAL POOL and is loaded with `LDR Rn, [pc, #imm]`. Those literals
are **4-byte aligned and sit inside the code region**. So:

  * counting how many times an address appears as an aligned u32 *inside code*
    measures how central that global is;
  * a value that is 4-aligned-referenced dozens of times is a real, hard-used
    global — not a coincidence, and not graphics data.

This inverts the search: stop asking "which value looks like HP" and ask "which
address does the code actually use".

Confirmed instance: 0x020CE8C0 is referenced 87 times, all aligned, and is
zero-filled — a BSS global. That is the kind of address an adapter needs.
"""
import struct
import sys
from collections import Counter

DUMP = sys.argv[1] if len(sys.argv) > 1 else \
    r"C:\Users\Devin Prater\AppData\Local\Temp\dbz\dbz-ram.bin"
MIN_REFS = int(sys.argv[2]) if len(sys.argv) > 2 else 10

raw = open(DUMP, "rb").read()
nl = raw.find(b"\n")
d = raw[nl + 1:]
BASE = 0x02000000

# ARM9 code lives low in main RAM here (literals seen from 0x0201Exxx upward).
# Count aligned u32 candidates across the whole dump, then filter to targets that
# land in the data region (0x020C0000+) — code literals pointing at game state.
CODE_LO, CODE_HI = 0x02000000, 0x020C0000
DATA_LO, DATA_HI = 0x020C0000, 0x02400000

refs = Counter()
for off in range(0, len(d) - 4, 4):
    addr = BASE + off
    if not (CODE_LO <= addr < CODE_HI):
        continue
    v = struct.unpack_from("<I", d, off)[0]
    if DATA_LO <= v < DATA_HI:
        refs[v] += 1

print(f"=== addresses referenced as CODE LITERALS (>= {MIN_REFS} times) ===")
print(f"    scanned code region 0x{CODE_LO:08X}..0x{CODE_HI:08X}")
print(f"    distinct data targets: {len(refs)}\n")

top = [(v, n) for v, n in refs.most_common() if n >= MIN_REFS]
print(f"  {len(top)} global(s) at or above the threshold\n")

zero, live = [], []
for v, n in top:
    off = v - BASE
    chunk = d[off:off + 64]
    nonzero = sum(1 for b in chunk if b)
    first = struct.unpack_from("<I", d, off)[0]
    tag = "ZERO-FILLED" if nonzero == 0 else f"{nonzero}/64 non-zero"
    (zero if nonzero == 0 else live).append((v, n))
    shown = chunk[:16].hex(" ")
    print(f"  0x{v:08X}  x{n:<4} {tag:<16} first4=0x{first:08X}")
    print(f"      {shown}")

print(f"\n  zero-filled (BSS-style) globals : {len(zero)}")
print(f"  populated globals               : {len(live)}")

print("\n=== reading this ===")
print("  A high reference count means the CODE depends on this address, which is")
print("  far stronger evidence than any value looking plausible. Zero-filled entries")
print("  are working areas not yet written; populated ones hold live state.")
print("  ⛔ Still confirm any specific field by changing it in-game and re-reading.")

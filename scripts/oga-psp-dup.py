#!/usr/bin/env python3
"""
oga-psp-dup.py — find COPIES of script lines outside the script block.

WHY THIS IS THE RIGHT NEXT TEST: hunting for a pointer INTO the script failed — the
addresses holding such pointers differ entirely between dumps, because the engine
reallocates its heap. But a visual novel has to get the current line onto the screen
somehow, and a very common design is to COPY the line into a display buffer, then
render from there.

If such a copy exists, it is the reader: whatever text appears outside the script block
AND matches a script line is the line currently being shown. No pointer hunt needed.

⛔ IT MUST BE OUTSIDE THE SCRIPT BLOCK. A copy inside the block is just the script.
⛔ AND IT MUST BE VERIFIED AGAINST A SCREEN. Finding a copy is a candidate, not an
answer; the tool prints the text so it can be compared with a screenshot.

Usage:
  oga-psp-dup.py DUMP.bin --lo 0x08AEA000 --hi 0x08AF3000 [--min 16] [--limit 40]
"""
import argparse
import re
import sys

RAM_BASE = 0x08800000
RAM_SIZE = 0x01800000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--lo", default="0x08AEA000")
    ap.add_argument("--hi", default="0x08AF3000")
    ap.add_argument("--base", default=hex(RAM_BASE))
    ap.add_argument("--min", type=int, default=16, help="min match length")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    base = int(args.base, 0)
    lo, hi = int(args.lo, 0), int(args.hi, 0)
    buf = open(args.dump, "rb").read()
    print(f"dump : {args.dump}  ({len(buf)} bytes)")
    print(f"script block: 0x{lo:08X} .. 0x{hi:08X}")
    print()

    # Collect the script lines (long ones only — short ones match everywhere).
    block = buf[lo - base:hi - base]
    lines = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % args.min, block):
        lines.append((lo + m.start(), m.group()))
    print(f"script lines of length >= {args.min}: {len(lines)}")
    print()

    # For each line, look for the same bytes OUTSIDE the block.
    outside = bytearray(buf)
    for i in range(lo - base, min(hi - base, len(outside))):
        outside[i] = 0                      # so a self-match cannot occur
    outside = bytes(outside)

    found = []
    for addr, raw in lines:
        idx = outside.find(raw)
        hits = []
        while idx >= 0 and len(hits) < 4:
            hits.append(base + idx)
            idx = outside.find(raw, idx + 1)
        if hits:
            found.append((addr, raw.decode("latin1"), hits))

    print(f"=== {len(found)} script lines ALSO present outside the block ===")
    if not found:
        print("  NONE. The engine does not keep a plain copy of the line in RAM.")
        print("  => the reader must come from the text-box RENDER or from code that")
        print("     walks the script (decompile), not from a duplicated string.")
        return 0

    for addr, text, hits in found[: args.limit]:
        where = " ".join(f"0x{h:08X}" for h in hits)
        print(f"  0x{addr:08X}  {text[:56]!r}")
        print(f"      also at: {where}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

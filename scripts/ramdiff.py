#!/usr/bin/env python3
"""ramdiff.py — diff two RAM snapshots from probe.cpp and rank changed addresses.

Raw "which bytes differ" is useless: a live DS game rewrites thousands of bytes a
second (frame counters, audio, RNG, VBlank flags). What is useful is the SIGNAL
SHAPE a specific experiment should produce:

  --small-pair   two adjacent small values (0..40) that BOTH changed — the shape a
                 cursor moving one tile produces
  --int <a,b>    a 2-byte value that went from a to b
  --byte <a,b>   1-byte
  --any          everything that changed (for eyeballing)

Usage:
  ramdiff.py before.ram after.ram --small-pair
  ramdiff.py before.ram after.ram --int 18 17
"""
import argparse, struct, sys

def load(p):
    with open(p, "rb") as f:
        return f.read()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before"); ap.add_argument("after")
    ap.add_argument("--small-pair", action="store_true",
                    help="adjacent bytes both in 0..40 that both changed")
    ap.add_argument("--small-triple", action="store_true",
                    help="3 adjacent small values where exactly one or two changed")
    ap.add_argument("--int", nargs=2, type=int, metavar=("FROM", "TO"))
    ap.add_argument("--int16", nargs=2, type=int, metavar=("FROM", "TO"))
    ap.add_argument("--byte", nargs=2, type=int, metavar=("FROM", "TO"))
    ap.add_argument("--any", action="store_true")
    ap.add_argument("--context", type=int, default=0,
                    help="hex dump this many bytes around each hit")
    ap.add_argument("--limit", type=int, default=40)
    a = ap.parse_args()

    b = load(a.before); c = load(a.after)
    if len(b) != len(c):
        print(f"size mismatch: {len(b)} vs {len(c)}"); sys.exit(2)
    n = len(b)
    RAM_BASE = 0x02000000
    hits = 0

    if a.small_pair:
        # both bytes small (a cursor's x and y are 0..~32) AND both changed
        for i in range(0, n - 2):
            x0, y0, x1, y1 = b[i], b[i+1], c[i], c[i+1]
            if x0 == x1 or y0 == y1:      continue
            if not (0 <= x1 <= 40 and 0 <= y1 <= 40): continue
            if not (0 <= x0 <= 40 and 0 <= y0 <= 40): continue
            print(f"{RAM_BASE+i:#010X}  ({x0},{y0}) -> ({x1},{y1})")
            hits += 1
            if hits >= a.limit: break

    if a.small_triple:
        for i in range(0, n - 4):
            v0 = list(b[i:i+4]); v1 = list(c[i:i+4])
            if any(x > 40 for x in v0) or any(x > 40 for x in v1): continue
            changed = sum(1 for k in range(4) if v0[k] != v1[k])
            if changed in (1, 2):
                print(f"{RAM_BASE+i:#010X}  {v0} -> {v1}")
                hits += 1
                if hits >= a.limit: break

    if a.__dict__.get("int"):
        frm, to = a.__dict__["int"]
        for i in range(0, n - 8, 1):
            for off in range(0, 8):
                if struct.unpack_from("<I", b, i+off)[0] == frm and \
                   struct.unpack_from("<I", c, i+off)[0] == to:
                    print(f"{ram_addr(RAM_BASE,i+off)} (u32 in+{off})  {frm} -> {to}")
                    hits += 1

    if a.int16:
        frm, to = a.int16
        for i in range(0, n - 2):
            if struct.unpack_from("<H", b, i)[0] == frm and \
               struct.unpack_from("<H", c, i)[0] == to:
                print(f"0x{RAM_BASE+i:08X} (u16)  {frm} -> {to}")
                hits += 1
                if hits >= a.limit: break

    if a.byte:
        frm, to = a.byte
        for i in range(0, n):
            if b[i] == frm and c[i] == to:
                print(f"0x{RAM_BASE+i:08X} (u8)   {frm} -> {to}")
                hits += 1
                if hits >= a.limit: break

    if a.any:
        for i in range(n):
            if b[i] != c[i]:
                print(f"0x{RAM_BASE+i:08X}  {b[i]:02X} -> {c[i]:02X}")
                hits += 1
                if hits >= a.limit: break

    print(f"\n{hits} hit(s)")
    if a.context and hits:
        pass

def ram_addr(base, i):
    return f"0x{base+i:08X}"

if __name__ == "__main__":
    main()

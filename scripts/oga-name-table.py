#!/usr/bin/env python3
"""Solve for the character-name table's exact (table, base) pair.

Confirmed already: the enemy table sits at table 0x10043BC with base 0x1004164,
and validates itself (Wild Boar, Champion Boar, Snow Boar... a coherent monster
list). The character table must be separate. 'Goku' is at ROM 0x10045D8.

A real string table satisfies two constraints at once, which pins the pair:
  1. every offset points at a clean printable run;
  2. the byte BEFORE that run is NUL (so entries do not start mid-string).
Constraint 2 is what the earlier attempt missed, which is why entries landed
mid-string ("ivor", "edic", "ueen" instead of "Survivor", "Medic", "Queen").
"""
import struct, glob

p = glob.glob(r"C:\Users\Devin Prater\Dropbox\Games\NDS\**\*Saiyans*.nds", recursive=True)[0]
d = open(p, "rb").read()
GOKU = 0x10045D8

best = []
for T in range(GOKU - 0x1000, GOKU, 4):
    v0 = struct.unpack_from("<I", d, T)[0]
    if not (0 < v0 < 0x8000):
        continue
    for delta in range(0, 32):
        B = GOKU - v0 - delta
        if not (0x1000000 <= B < 0x1010000):
            continue
        offs = [struct.unpack_from("<I", d, T + i * 4)[0] for i in range(64)]
        score = 0
        for o in offs:
            if not (0 < o < 0x8000):
                break
            a = B + o
            if a < 1 or a >= len(d):
                break
            if d[a - 1] != 0:          # must not start mid-string
                break
            e = d.find(b"\x00", a)
            seg = d[a:e]
            if not seg or not all(32 <= c < 127 for c in seg):
                break
            score += 1
        if score:
            best.append((score, T, B))

best.sort(key=lambda x: (-x[0], x[1]))
print("top candidates (score/64, table, base):")
for score, T, B in best[:6]:
    print(f"  {score}/64   table 0x{T:06X}   base 0x{B:06X}")

if best:
    score, T, B = best[0]
    print(f"\n=== decoding table 0x{T:06X}, base 0x{B:06X}  ({score} entries) ===")
    offs = [struct.unpack_from("<I", d, T + i * 4)[0] for i in range(score)]
    chars = ("Goku", "Gohan", "Piccolo", "Krillin", "Tien", "Yamcha",
             "Bubbles", "Gregory")
    for i, o in enumerate(offs):
        a = B + o
        e = d.find(b"\x00", a)
        name = d[a:e].decode("ascii", "replace")
        mark = "   <== CHARACTER" if name in chars else ""
        print(f"  [{i:2d}] +0x{o:04X}  {name!r}{mark}")

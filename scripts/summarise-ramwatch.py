#!/usr/bin/env python3
"""summarise-ramwatch.py — turn the raw CSV watches into a verdict per address.

An address is only trustworthy if its values are plausible for its meaning AND
they move when the game moves. Reading zero for the whole run proves nothing
except that no code touched it during the window.
"""
import csv, glob, os, sys

d = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/open-game-access/ramwatch")
for f in sorted(glob.glob(os.path.join(d, "*.csv"))):
    print("=" * 78)
    print("###", os.path.basename(f))
    rows = list(csv.reader(open(f)))
    if len(rows) < 2:
        print("  (no data)"); continue
    hdr = rows[0][1:]
    data = rows[1:]
    print(f"  samples: {len(data)}")
    for i, h in enumerate(hdr):
        vals = [r[i + 1] for r in data if len(r) > i + 1]
        ints = []
        for v in vals:
            try: ints.append(int(v))
            except ValueError: pass
        if not ints:
            print(f"  {h:<26} no numeric data"); continue
        nz = [v for v in ints if v != 0]
        uniq = set(ints)
        if nz:
            print(f"  {h:<26} nonzero={len(nz):>4}/{len(ints):<4} "
                  f"range={min(nz)}..{max(nz)} uniq={len(uniq)} "
                  f"sample={sorted(uniq)[:6]}")
        else:
            print(f"  {h:<26} ALWAYS ZERO over {len(ints)} samples")

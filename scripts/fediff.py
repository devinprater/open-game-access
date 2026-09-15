#!/usr/bin/env python3
"""fediff.py — what changed in the Fire Emblem cursor experiment?

Prints the values of the KNOWN fields at each snapshot, then ranks other changed
bytes by "small value that moved consistently in the same direction as the
cursor". The known fields are the hypothesis; the ranked list is what might be a
second copy we have not accounted for.

Usage: fediff.py <dir-with-.ram-files>
"""
import struct, sys, os, glob

RAM_BASE = 0x02000000
A_gMSM   = 0x021E3328
A_gUnits = 0x021974D8
MSM_CUR  = 0x010

def load(p):
    with open(p, "rb") as f:
        return f.read()

def at(ram, addr):
    o = addr - RAM_BASE
    if o < 0 or o + 4 > len(ram): return 0
    return struct.unpack_from("<I", ram, o)[0]

def u8(ram, addr):
    o = addr - RAM_BASE
    return ram[o] if 0 <= o < len(ram) else 0

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/fe/out")
    files = sorted(glob.glob(os.path.join(d, "c*.ram")))
    if not files:
        print("no c*.ram in", d); return
    rams = [(os.path.basename(f), load(f)) for f in files]

    print(f"{'snapshot':<14} {'cursorPtr':<12} {'xTile':>5} {'yTile':>5} {'vis':>4} "
          f"{'xDisp':>6} {'yDisp':>6} {'camX':>6} {'camY':>6} {'units':>5}")
    print("-" * 84)
    prev = None
    for name, ram in rams:
        msm = at(ram, A_gMSM)
        cur = at(ram, msm + MSM_CUR) if msm else 0
        if msm and 0x02000000 <= msm < 0x02400000 and 0x02000000 <= cur < 0x02400000:
            xt, yt = u8(ram, cur + 8), u8(ram, cur + 9)
            vis = u8(ram, cur + 0x0A)
            xd = struct.unpack_from("<h", ram, cur + 4 - RAM_BASE)[0]
            yd = struct.unpack_from("<h", ram, cur + 6 - RAM_BASE)[0]
        else:
            xt = yt = vis = xd = yd = -1
        cam = at(ram, msm) if msm else 0
        camx = struct.unpack_from("<i", ram, cam - RAM_BASE)[0] if 0x02000000 <= cam < 0x02400000 else 0
        camy = struct.unpack_from("<i", ram, cam - RAM_BASE + 4)[0] if 0x02000000 <= cam < 0x02400000 else 0
        units = at(ram, A_gUnits)
        print(f"{name:<14} 0x{cur:08X} {xt:>5} {yt:>5} {vis:>4} {xd:>6} {yd:>6} "
              f"{camx:>6} {camy:>6} 0x{units:08X}")
        prev = (name, ram)

    # Which OTHER addresses changed in step with the cursor?
    print("\n=== addresses whose value tracks the cursor's x (small ints, changed a lot) ===")
    if len(rams) >= 3:
        # collect the x value per snapshot for the known cursor
        xs = []
        for name, ram in rams:
            msm = at(ram, A_gMSM); cur = at(ram, msm + MSM_CUR) if msm else 0
            xs.append(u8(ram, cur + 8) if 0x02000000 <= cur < 0x02400000 else 0)
        n = len(rams[0][1])
        cands = []
        step = 1
        for i in range(0, n - 1, step):
            vals = [u8(ram, RAM_BASE + i) for _, ram in rams]
            if vals == xs and len(set(xs)) > 2:
                cands.append((RAM_BASE + i, vals))
        for a, v in cands[:20]:
            print(f"  0x{a:08X}  {v}")
        if not cands:
            print("  (none — the cursor's x lives only where we found it)")

if __name__ == "__main__":
    main()

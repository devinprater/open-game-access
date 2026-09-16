#!/usr/bin/env python3
"""cursorstruct.py — dump the whole Cursor struct at every snapshot.

The +8/+9 bytes tracked the cursor but by an inconsistent step (+3 for x on one
press, +1 for y on another), which means they may be scaled or animated rather
than the plain tile coordinate. Rather than guess, print EVERY field of the
Cursor object at each snapshot: the real tile coordinate is the one that moves by
exactly 1 per single press.
"""
import struct, sys, os, glob

RAM_BASE = 0x02000000
A_gMSM   = 0x021E3328
MSM_CUR  = 0x010

def load(p):
    with open(p, "rb") as f: return f.read()

def u32(ram, a):
    o = a - RAM_BASE
    return struct.unpack_from("<I", ram, o)[0] if 0 <= o + 4 <= len(ram) else 0

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/fe/out")
    files = sorted(glob.glob(os.path.join(d, "c*.ram")))
    if not files:
        print("no c*.ram"); return
    rams = [(os.path.basename(f)[:-4], load(f)) for f in files]

    # Cursor pointer per snapshot
    ptrs = []
    for name, ram in rams:
        msm = u32(ram, A_gMSM)
        cur = u32(ram, msm + MSM_CUR) if msm and RAM_BASE <= msm < RAM_BASE+0x400000 else 0
        ptrs.append(cur)
    print("cursor pointers:", [(n, hex(p)) for n, p in zip([r[0] for r in rams], ptrs)])
    if len(set(ptrs)) != 1 or ptrs[0] == 0:
        print("!! cursor pointer is not stable across snapshots — reading the first valid one")
    cur = next((p for p in ptrs if p), 0)
    if not cur:
        print("!! no valid cursor pointer"); return

    print(f"\nCursor at 0x{cur:08X} — all 36 bytes per snapshot, as u8 / s16 / s32")
    print(f"{'off':>4} {'field':<12}", " ".join(f"{n:>10}" for n, _ in rams))
    print("-" * (18 + 11 * len(rams)))

    def row(off, label, fmt, size):
        vals = []
        for _, ram in rams:
            o = cur + off - RAM_BASE
            if 0 <= o + size <= len(ram):
                vals.append(struct.unpack_from(fmt, ram, o)[0])
            else:
                vals.append(None)
        flag = ""
        try:
            nums = [v for v in vals if v is not None]
            if len(set(nums)) > 1 and all(isinstance(v, int) for v in nums):
                deltas = [nums[i+1] - nums[i] for i in range(len(nums)-1)]
                flag = "   deltas=" + str(deltas)
        except Exception:
            pass
        print(f"{off:>4} {label:<12}", " ".join(f"{str(v):>10}" for v in vals), flag)

    for off in range(0, 0x20, 2):
        # print as u8 pairs and as s16
        row(off,     f"u8[{off:#x}]",    "<B", 1)
        if off % 4 == 0:
            row(off, f"s16[{off:#x}]",   "<h", 2)
    print()
    for off in (0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C):
        row(off, f"s32[{off:#x}]", "<i", 4)

    # Also: xDisplay/yDisplay are px with tileSize=24; check cur*24 vs those fields
    print("\n=== does any s16 field equal tileCoord * 24 (tileSize)? ===")
    for off in range(0, 0x20, 2):
        vals = []
        for _, ram in rams:
            o = cur + off - RAM_BASE
            vals.append(struct.unpack_from("<h", ram, o)[0] if 0 <= o + 2 <= len(ram) else 0)
        if all(abs(v) % 24 == 0 and v != 0 for v in vals):
            print(f"  s16 at +{off:#x}: {vals}  (/24 = {[v//24 for v in vals]})")

if __name__ == "__main__":
    main()

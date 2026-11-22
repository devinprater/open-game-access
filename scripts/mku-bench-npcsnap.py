import pathlib
exec(open(pathlib.Path(__file__).resolve().parent / "mku-bench-lib.py").read())
import sys, json as _j
out = sys.argv[1]
recs = {}
for i in range(0, 95):
    base = 0x09151098 - i * 0x810
    p = u32(base)
    if not p or not (0x08800000 <= p < 0x0A000000):
        continue
    nm = rd(p, 40).split(b"\0")[0].decode("latin1")
    if not nm.startswith("npc_"):
        continue
    x, y, z = struct.unpack("<3f", rd(base + 0x1C, 12))
    recs[i] = [nm, round(x, 2), round(y, 2), round(z, 2), u32(base + 0x14), u32(base + 0x04)]
px = struct.unpack("<3f", rd(0x08AED478, 12))
_j.dump({"me": [round(v, 2) for v in px], "sector": u32(0x08AED5B4), "recs": recs}, open(out, "w"))
print("saved", len(recs), "records; me", [round(v, 1) for v in px], "sector", u32(0x08AED5B4))

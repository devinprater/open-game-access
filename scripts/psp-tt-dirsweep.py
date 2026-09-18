#!/usr/bin/env python3
"""psp-tt-dirsweep.py -- hold each of the 8 flight directions and record what the glyph does.

WHY
    The user reported: "the panning works, just doesn't seem to work when the arrow points, say, to
    the bottom right". So the glyph's bearing is NOT simply the objective's left/right offset -- it
    has a large component that does not map onto a left/right pan.

    The user's requested behaviour is simpler and better:
        "point right until the arrow points up, and then go back to pointing forward"

    To implement that reliably we must know what the glyph's bearing actually does per direction,
    rather than assuming. This sweep holds each direction in turn, captures frames, and reports the
    bearing distribution. It also reports how OFTEN the glyph appears per direction, and whether the
    glyph ROTATES at all.

USAGE
    python scripts/psp-tt-dirsweep.py
"""
import asyncio, json, math, os, statistics, subprocess, sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "psp-shot.py")
T = os.path.join(os.environ.get("LOCALAPPDATA", "/tmp"), "Temp", "psp-dirsweep")

FILL = (73, 112, 254)
TOL = 8
MIN_PX = 80
MAX_EXTENT = 280

# 8 directions plus centre
DIRS = [
    ("ahead",      (0.0, -1.0)),
    ("ahead-right",(0.7071, -0.7071)),
    ("right",      (1.0, 0.0)),
    ("back-right", (0.7071, 0.7071)),
    ("back",       (0.0, 1.0)),
    ("back-left",  (-0.7071, 0.7071)),
    ("left",       (-1.0, 0.0)),
    ("ahead-left", (-0.7071, -0.7071)),
]


def capture(path, tries=4):
    tmp = path + ".part"
    for _ in range(tries):
        if os.path.exists(tmp):
            try: os.remove(tmp)
            except OSError: pass
        subprocess.run([sys.executable, SHOT, tmp], capture_output=True, timeout=60)
        if not os.path.exists(tmp):
            continue
        try:
            with Image.open(tmp) as im:
                im.verify()
            os.replace(tmp, path)
            return True
        except Exception:
            continue
    return False


def bearing(path):
    """Glyph bearing in degrees: 0 = up, + = clockwise (right). None if no glyph."""
    try:
        im = Image.open(path).convert("RGB")
    except Exception:
        return None, 0
    a = np.asarray(im).astype(np.int16)
    m = ((np.abs(a[:, :, 0] - FILL[0]) <= TOL) &
         (np.abs(a[:, :, 1] - FILL[1]) <= TOL) &
         (np.abs(a[:, :, 2] - FILL[2]) <= TOL))
    ys, xs = np.nonzero(m)
    n = len(xs)
    if n < MIN_PX:
        return None, n
    if xs.max() - xs.min() > MAX_EXTENT or ys.max() - ys.min() > MAX_EXTENT:
        return None, n
    xs = xs.astype(float); ys = ys.astype(float)
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    sxx = float((dx * dx).mean()); syy = float((dy * dy).mean()); sxy = float((dx * dy).mean())
    th = 0.5 * math.atan2(2 * sxy, sxx - syy)
    pvx, pvy = -math.sin(th), math.cos(th)
    pr = dx * pvx + dy * pvy
    lo, hi = float(pr.min()), float(pr.max())
    if hi - lo < 1:
        return None, n
    near = lambda v: int((np.abs(pr - v) < 12.0).sum())
    apex = lo if near(lo) < near(hi) else hi
    k = int(np.argmin(np.abs(pr - apex)))
    return math.degrees(math.atan2(xs[k] - cx, -(ys[k] - cy))), n


def normalise(d):
    return ((d + 180) % 360) - 180


async def run(samples_per_dir):
    import websockets
    os.makedirs(T, exist_ok=True)
    async with websockets.connect("ws://127.0.0.1:12345/debugger",
                                  subprotocols=["debugger.ppsspp.org"]) as ws:
        async def send(ev, **kw):
            await ws.send(json.dumps({"event": ev, "ticket": "1", **kw}))
        await send("version", name="psp-tt-dirsweep", version="1")

        print("# holding each direction; recording the glyph bearing (0=up, +=clockwise)")
        print("%-12s %-8s %-46s %s" % ("direction", "glyph", "bearings seen", "mean|b|"))
        print("%-12s %-8s %-46s %s" % ("-" * 12, "-" * 8, "-" * 46, "-" * 8))

        summary = {}
        for name, (X, Y) in DIRS:
            got = []
            for i in range(samples_per_dir):
                for _ in range(3):
                    await send("input.analog.send", stick="left", x=X, y=Y)
                p = os.path.join(T, "d_%s_%02d.png" % (name, i))
                if capture(p):
                    b, n = bearing(p)
                    if b is not None:
                        got.append(normalise(b))
                await asyncio.sleep(0.3)
            # release between directions so the next one starts clean
            for _ in range(3):
                await send("input.analog.send", stick="left", x=0.0, y=0.0)
            await asyncio.sleep(0.5)

            shown = " ".join("%+4.0f" % v for v in got[:12])
            mabs = "%.1f" % (sum(abs(v) for v in got) / len(got)) if got else "-"
            print("%-12s %-8s %-46s %s" % (name, "%d/%d" % (len(got), samples_per_dir), shown, mabs))
            summary[name] = got

        print()
        print("# If the glyph ROTATES with heading, bearings differ per direction.")
        print("# If it reads ~0 for every direction, the glyph always points 'ahead' and carries no")
        print("# left/right information -- in which case a pan from its bearing is meaningless and the")
        print("# beacon should instead use the glyph's SCREEN POSITION (or sound a simple 'on target'")
        print("# when it reads ahead).")
        return summary


if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    asyncio.run(run(n))

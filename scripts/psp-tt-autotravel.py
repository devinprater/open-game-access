#!/usr/bin/env python3
"""psp-tt-autotravel.py -- CLOSED-LOOP steering to the objective, using the chevron itself.

THE IDEA (and why it needs no objective address)
    The game's chevron ROTATES to point along the objective bearing -- validated on known rotations
    of the real glyph (8/8). So the chevron already IS the answer to "which way is the objective".

    Instead of telling a human where to go, this closes the loop:
        read the chevron  ->  push the stick that way  ->  repeat
    Following the chevron IS pathfinding. No player position, no objective position, no camera
    matrix. And because the stick acts in CAMERA space anyway (the FFXII screen reader documents
    this), steering by the on-screen chevron is already in the correct frame by construction.

WHY THIS BEATS THE RAM ROUTE HERE
    Attempts to find the objective in RAM both came up empty:
      * "floats constant while you move" -> 174,900 hits across all RAM, 60,411 live in the entity
        region alone. Most of memory is static (code, tables, assets), so constancy isolates nothing.
      * the entity band around the player triple holds ONLY moving (player-side) triples -> 0 fixed.
    The chevron route sidesteps all of that.

THE HONEST RISK
    Field measurements showed the bearing reading ~0 on most frames even while flying. Two
    explanations: (a) corrections are small and continuous, or (b) the glyph is a "keep going"
    indicator rather than a proportional bearing. This script therefore MEASURES whether the loop
    converges: it logs |bearing| over time while steering. If |bearing| trends toward 0, the loop
    works. If it stays flat or random, the cheque is a keep-going cue and steering gains nothing.

USAGE
    python scripts/psp-tt-autotravel.py --seconds 40 --debug
    python scripts/psp-tt-autotravel.py --dry-run 30      # steer by the chevron, no stick output
"""
import argparse, asyncio, json, math, os, subprocess, sys, time

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "psp-shot.py")
TMP = os.path.join(os.environ.get("LOCALAPPDATA", "/tmp"), "Temp", "psp-beacon")

FILL = (73, 112, 254)
TOL = 8
MIN_PX = 80
MAX_EXTENT = 280
MAX_MSG = 16 * 1024 * 1024


def capture(path, tries=4):
    tmp = path + ".tmp.png"          # MUST keep .png: psp-shot picks the encoder from the extension
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for _ in range(tries):
        if os.path.exists(tmp):
            try: os.remove(tmp)
            except OSError: pass
        try:
            subprocess.run([sys.executable, SHOT, tmp], capture_output=True, text=True, timeout=60)
        except Exception:
            continue
        if not os.path.exists(tmp):
            continue
        try:
            with Image.open(tmp) as im:
                im.verify()
            os.replace(tmp, path)
            return True
        except Exception:
            time.sleep(0.2)
    return False


def bearing(path):
    """Chevron bearing: 0 = up (objective ahead), + = clockwise (right). None if no glyph."""
    try:
        im = Image.open(path).convert("RGB")
    except Exception:
        return None
    a = np.asarray(im).astype(np.int16)
    m = ((np.abs(a[:, :, 0] - FILL[0]) <= TOL) &
         (np.abs(a[:, :, 1] - FILL[1]) <= TOL) &
         (np.abs(a[:, :, 2] - FILL[2]) <= TOL))
    ys, xs = np.nonzero(m)
    if len(xs) < MIN_PX:
        return None
    if xs.max() - xs.min() > MAX_EXTENT or ys.max() - ys.min() > MAX_EXTENT:
        return None
    xs = xs.astype(float); ys = ys.astype(float)
    cx, cy = xs.mean(), ys.mean()
    dx, dy = xs - cx, ys - cy
    sxx = float((dx * dx).mean()); syy = float((dy * dy).mean()); sxy = float((dx * dy).mean())
    th = 0.5 * math.atan2(2 * sxy, sxx - syy)
    pvx, pvy = -math.sin(th), math.cos(th)
    pr = dx * pvx + dy * pvy
    lo, hi = float(pr.min()), float(pr.max())
    if hi - lo < 1:
        return None
    near = lambda v: int((np.abs(pr - v) < 0.12 * max(1e-6, hi - lo)).sum())
    apex = lo if near(lo) < near(hi) else hi
    k = int(np.argmin(np.abs(pr - apex)))
    return math.degrees(math.atan2(xs[k] - cx, -(ys[k] - cy)))


class Dev:
    def __init__(self, dry):
        self.ws = None
        self.n = 0
        self.dry = dry

    async def open(self):
        import websockets
        self.ws = await websockets.connect("ws://127.0.0.1:12345/debugger",
                                          subprotocols=["debugger.ppsspp.org"],
                                          max_size=MAX_MSG, ping_interval=20, ping_timeout=20)
        self.n += 1
        await self.ws.send(json.dumps({"event": "version", "ticket": str(self.n),
                                       "name": "psp-tt-autotravel", "version": "1"}))
        await self.ws.recv()

    async def stick(self, x, y):
        """Fire-and-forget: the stick is polled per frame, so it must be re-sent."""
        if self.dry:
            return
        self.n += 1
        try:
            await self.ws.send(json.dumps({"event": "input.analog.send", "ticket": str(self.n),
                                           "stick": "left", "x": x, "y": y}))
        except Exception:
            pass


async def run(args):
    frame = os.path.join(TMP, "travel.png")
    os.makedirs(TMP, exist_ok=True)
    dev = Dev(args.dry_run)
    await dev.open()

    print("# closed-loop steering: chevron -> stick%s" % ("  [DRY RUN: no stick output]" if args.dry_run else ""))
    print("# measuring whether |bearing| CONVERGES toward 0 while steering")

    history = []
    t0 = time.time()
    last_bear = 0.0

    while time.time() - t0 < args.seconds:
        # Sample the chevron, then steer toward it continuously.
        for _ in range(args.hold_frames):
            if capture(frame):
                b = bearing(frame)
                if b is not None:
                    b = ((b + 180) % 360) - 180
                    last_bear = b
                    history.append((time.time() - t0, b))
            # steer: aim the stick in the chevron's direction
            rad = math.radians(last_bear)
            sx = math.sin(rad)
            sy = -math.cos(rad)
            # always keep some forward drive so the glyph keeps being drawn
            scale = max(0.35, min(1.0, abs(last_bear) / 45.0))
            await dev.stick(sx * scale, -abs(sy) * scale if sy < 0 else sy * scale)
            await asyncio.sleep(0.08)

    await dev.stick(0.0, 0.0)

    print()
    if not history:
        print("# NO CHEVRON SEEN in %.0fs -- the glyph is only drawn while moving." % args.seconds)
        print("# Nothing to conclude about the loop.")
        return
    print("# %d bearing samples" % len(history))
    print("%6s %8s" % ("t(s)", "bearing"))
    step = max(1, len(history) // 25)
    for t, b in history[::step]:
        print("%6.1f %+8.1f" % (t, b))

    absb = [abs(b) for _, b in history]
    first = sum(absb[:max(1, len(absb) // 4)]) / max(1, len(absb[:max(1, len(absb) // 4)]))
    lastq = absb[-max(1, len(absb) // 4):]
    last = sum(lastq) / len(lastq)
    print()
    print("# mean |bearing| first quarter = %.1f deg" % first)
    print("# mean |bearing| last  quarter = %.1f deg" % last)
    if last < first * 0.7:
        print("# => CONVERGES: steering by the chevron reduces off-axis error. The loop WORKS.")
    elif last > first * 1.3:
        print("# => DIVERGES: steering increases error. The bearing sign is likely INVERTED.")
    else:
        print("# => FLAT: no convergence. The chevron behaves as a 'keep going' cue rather than a")
        print("#    proportional bearing, so steering by its angle gains little. Report this honestly.")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=40)
    ap.add_argument("--hold-frames", type=int, default=3)
    ap.add_argument("--debug", action="store_true")
    ap.add_argument("--dry-run", type=float, default=0.0,
                    help="measure without emitting stick input (value = seconds)")
    a = ap.parse_args()
    if a.dry_run:
        a.seconds = a.dry_run
    asyncio.run(run(a))

#!/usr/bin/env python3
"""psp-tt-pos.py -- find the PLAYER POSITION by comparing two REST states.

THE MISTAKE THIS FIXES
    Every earlier position search sampled WHILE HOLDING the stick. That makes velocity nonzero, and
    velocity fields dominated the rankings (patterns like `S=0 A=-22 B=-7`, and Q7 camera-basis
    values -128/-32). Those tests could not separate position from velocity because both were moving.

    The clean discriminator is to sample AT REST:
        fly NORTH, release, let it settle  -> read snapshot A
        fly SOUTH, release, let it settle  -> read snapshot B
    At rest, VELOCITY is zero in both snapshots, so it cannot appear in the difference. Only values
    that genuinely differ between two stationary positions survive -- i.e. POSITION (and things
    derived from it). This removes the whole velocity/basis family automatically.

WHY WE NEED THIS
    The FFXII screen reader documents the approach that scales: give directions CAMERA-RELATIVE,
    computed from the player position and the target position. For Tag Team the pixel beacon proved
    unreliable, so we need the real numbers: player position, and the objective position (which is
    FIXED in world space while the player moves).

USAGE
    python scripts/psp-tt-pos.py                       # full-RAM, north/south at rest
    python scripts/psp-tt-pos.py --axis east-west
    python scripts/psp-tt-pos.py --limit 30 --json out.json
"""
import argparse, asyncio, json, math, os, struct, sys, time

import numpy as np

RAM_LO, RAM_HI = 0x08800000, 0x0A000000
# ⚠️ 0x100000 (1 MiB) base64-encodes to ~1.4 MB, which exceeds websockets' default
# max_size of 1 MiB -> the server drops the connection ("message too big"). Use 256 KiB
# chunks and an explicit generous max_size.
CHUNK = 0x40000
MAX_MSG = 8 * 1024 * 1024

# Values that are certainly not a coordinate: huge, denormal, or exact sentinels.
def usable(v):
    if not math.isfinite(v):
        return False
    a = abs(v)
    return 0.01 < a < 200000.0 and a != 1.0 and a != 0.0


class Dev:
    def __init__(self):
        self.ws = None
        self.n = 0
        self.pending = {}

    async def open(self):
        import websockets
        self.ws = await websockets.connect("ws://127.0.0.1:12345/debugger",
                                          subprotocols=["debugger.ppsspp.org"],
                                          max_size=MAX_MSG,
                                          ping_interval=20, ping_timeout=20)
        await self.req("version", name="psp-tt-pos", version="1")

    async def req(self, event, **kw):
        self.n += 1
        t = str(self.n)
        await self.ws.send(json.dumps({"event": event, "ticket": t, **kw}))
        while True:
            m = json.loads(await self.ws.recv())
            if m.get("ticket") == t:
                if m.get("event") == "error":
                    raise RuntimeError(m.get("message"))
                return m

    async def full_ram(self):
        parts = []
        for a in range(RAM_LO, RAM_HI, CHUNK):
            r = await self.req("memory.read", address="0x%08X" % a, size=CHUNK)  # 256 KiB
            parts.append(__import__("base64").b64decode(r.get("base64", "")))
        return b"".join(parts)

    async def hold(self, x, y, seconds):
        """Hold a direction, re-sending at ~20 Hz (the stick is polled per frame)."""
        t0 = time.time()
        while time.time() - t0 < seconds:
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send", "ticket": str(self.n),
                                               "stick": "left", "x": x, "y": y}))
            except Exception:
                pass
            await asyncio.sleep(0.05)

    async def release(self, settle=1.4):
        for _ in range(6):
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send", "ticket": str(self.n),
                                               "stick": "left", "x": 0.0, "y": 0.0}))
            except Exception:
                pass
            await asyncio.sleep(0.05)
        await asyncio.sleep(settle)      # let motion and animation settle at REST


AXES = {
    "north-south": ((0.0, -1.0), (0.0, 1.0)),
    "east-west":   ((1.0, 0.0), (-1.0, 0.0)),
}


async def run(args):
    dev = Dev()
    await dev.open()
    (a1, a2) = AXES[args.axis]

    print("# axis %s : hold (%.1f,%.1f) then (%.1f,%.1f)" % (args.axis, a1[0], a1[1], a2[0], a2[1]))
    print("# sampling AT REST in both cases, so velocity is 0 in both and cannot survive the diff")
    await dev.release(0.8)

    print("# flying FIRST direction for %.1fs..." % args.fly)
    await dev.hold(a1[0], a1[1], args.fly)
    await dev.release()
    print("# reading RAM at rest (A)...")
    A = await dev.full_ram()

    print("# flying SECOND direction for %.1fs..." % args.fly)
    await dev.hold(a2[0], a2[1], args.fly)
    await dev.release()
    print("# reading RAM at rest (B)...")
    B = await dev.full_ram()

    n = min(len(A), len(B))
    print("# both snapshots %.1f MiB" % (n / 1048576.0))

    # Floats that differ between the two REST states.
    cands = []
    for o in range(0, n - 4, 4):
        if A[o:o + 4] == B[o:o + 4]:
            continue
        fa = struct.unpack_from("<f", A, o)[0]
        fb = struct.unpack_from("<f", B, o)[0]
        if not (usable(fa) and usable(fb)):
            continue
        delta = fb - fa
        cands.append({"addr": RAM_LO + o, "a": fa, "b": fb, "d": delta})

    print("# %d float(s) differ between the two REST states" % len(cands))

    # Group consecutive addresses: a position is 3 adjacent floats (x,y,z).
    cands.sort(key=lambda c: c["addr"])
    groups, cur = [], []
    for c in cands:
        if cur and c["addr"] - cur[-1]["addr"] == 4:
            cur.append(c)
        else:
            if len(cur) >= 2:
                groups.append(cur)
            cur = [c]
    if len(cur) >= 2:
        groups.append(cur)

    print()
    print("=== runs of adjacent differing floats ===")
    shown = 0
    for g in groups:
        if shown >= args.limit:
            print("  ... (%d more)" % (len(groups) - shown))
            break
        addr = g[0]["addr"]
        vals_a = " ".join("%10.2f" % c["a"] for c in g[:4])
        vals_b = " ".join("%10.2f" % c["b"] for c in g[:4])
        print("  0x%08X  n=%d" % (addr, len(g)))
        print("      A: %s" % vals_a)
        print("      B: %s" % vals_b)
        shown += 1

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(cands, f)

    print()
    print("# A POSITION triple should show 3 adjacent floats that all change, with plausible")
    print("# world magnitudes. Re-run with the OPPOSITE axis: a real position changes on both axes,")
    print("# whereas a heading/angle only responds to turning.")
    return cands


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--axis", choices=list(AXES), default="north-south")
    ap.add_argument("--fly", type=float, default=3.5, help="seconds to fly each way")
    ap.add_argument("--limit", type=int, default=25)
    ap.add_argument("--json")
    asyncio.run(run(ap.parse_args()))

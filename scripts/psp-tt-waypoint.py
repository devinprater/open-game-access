#!/usr/bin/env python3
"""psp-tt-waypoint.py -- find the OBJECTIVE position: the world point that stays FIXED while you move.

WHY
    Pathfinding needs two world positions:
        * the PLAYER position   -- changes as you fly
        * the OBJECTIVE position -- FIXED in world space while the player moves

    scripts/psp-tt-pos.py already finds things that CHANGE with movement (player position, plus
    camera basis and many render copies). This script does the complementary search: take several
    rest snapshots from DIFFERENT places and keep the float triples that do NOT move.

    A fixed triple with plausible world magnitudes, sitting alongside the moving ones, is the
    objective candidate.

METHOD
    1. fly a short leg, release, settle, snapshot
    2. repeat N times so the player visits several distinct positions
    3. for every 4-byte-aligned float: keep it only if it is CONSTANT across all snapshots
    4. group survivors into runs of >=3 adjacent floats (an x,y,z triple)
    5. print the runs, and cross-check that the player-position triple (from psp-tt-pos.py) DID move

    ⚠️ Samples are taken AT REST so velocity is zero everywhere and cannot masquerade as a fixed
    value (a velocity of 0 at rest is constant, which would flood the results). Filtering by
    magnitude removes most of that, but the rest-to-rest design is what makes it trustworthy.

USAGE
    python scripts/psp-tt-waypoint.py --legs 6 --leg-seconds 2.5
    python scripts/psp-tt-waypoint.py --legs 8 --json "$LOCALAPPDATA/Temp/waypoints.json"
"""
import argparse, asyncio, base64, json, math, os, struct, sys, time

import numpy as np

RAM_LO, RAM_HI = 0x08800000, 0x0A000000
CHUNK = 0x40000          # 256 KiB: a 1 MiB read exceeds websockets' max message size
MAX_MSG = 16 * 1024 * 1024

# Plausible world-coordinate range for this game (from psp-tt-pos.py observations: hundreds to
# a few thousand). Deliberately excludes near-zero noise and the 0x3F800000 (=1.0) sentinels.
LO_MAG, HI_MAG = 20.0, 20000.0


def plausible(v):
    if not math.isfinite(v):
        return False
    a = abs(v)
    return LO_MAG <= a <= HI_MAG


class Dev:
    def __init__(self):
        self.ws = None
        self.n = 0

    async def open(self):
        import websockets
        self.ws = await websockets.connect("ws://127.0.0.1:12345/debugger",
                                          subprotocols=["debugger.ppsspp.org"],
                                          max_size=MAX_MSG, ping_interval=20, ping_timeout=20)
        await self.req("version", name="psp-tt-waypoint", version="1")

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
        out = []
        for a in range(RAM_LO, RAM_HI, CHUNK):
            r = await self.req("memory.read", address="0x%08X" % a, size=CHUNK)
            out.append(base64.b64decode(r.get("base64", "")))
        return b"".join(out)

    async def hold(self, x, y, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            self.n += 1
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send", "ticket": str(self.n),
                                               "stick": "left", "x": x, "y": y}))
            except Exception:
                pass
            await asyncio.sleep(0.05)

    async def rest(self, settle=1.2):
        for _ in range(6):
            self.n += 1
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send", "ticket": str(self.n),
                                               "stick": "left", "x": 0.0, "y": 0.0}))
            except Exception:
                pass
            await asyncio.sleep(0.05)
        await asyncio.sleep(settle)


LEGS = [
    (0.0, -1.0), (1.0, 0.0), (0.0, 1.0), (-1.0, 0.0),
    (0.7071, -0.7071), (0.7071, 0.7071), (-0.7071, 0.7071), (-0.7071, -0.7071),
]


async def run(args):
    dev = Dev()
    await dev.open()
    snaps = []
    await dev.rest(0.6)
    print("# taking %d rest snapshots from different positions" % args.legs)
    for i in range(args.legs):
        if i:
            dx, dy = LEGS[(i - 1) % len(LEGS)]
            print("#   leg %d: fly (%.2f, %.2f) for %.1fs" % (i, dx, dy, args.leg_seconds))
            await dev.hold(dx, dy, args.leg_seconds)
            await dev.rest()
        print("#   snapshot %d..." % i)
        snaps.append(await dev.full_ram())

    n = min(len(s) for s in snaps)
    print("# %d snapshots, %.1f MiB each" % (len(snaps), n / 1048576.0))

    # Constant float slots that are plausible coordinates.
    const = []
    for o in range(0, n - 4, 4):
        vals = [struct.unpack_from("<f", s, o)[0] for s in snaps]
        first = vals[0]
        if not plausible(first):
            continue
        if all(v == first for v in vals[1:]):
            const.append({"addr": RAM_LO + o, "v": first})

    print("# %d constant, plausible float slot(s)" % len(const))

    # runs of >=3 adjacent
    const.sort(key=lambda c: c["addr"])
    runs, cur = [], []
    for c in const:
        if cur and c["addr"] - cur[-1]["addr"] == 4:
            cur.append(c)
        else:
            if len(cur) >= 3:
                runs.append(cur)
            cur = [c]
    if len(cur) >= 3:
        runs.append(cur)

    print()
    print("=== CONSTANT runs of >=3 adjacent floats (objective / fixed world point candidates) ===")
    for r in runs[:args.limit]:
        print("  0x%08X  n=%d" % (r[0]["addr"], len(r)))
        print("      values: %s" % " ".join("%10.2f" % c["v"] for c in r[:6]))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"constant": const, "runs": [[c["addr"] for c in r] for r in runs]}, f)
        print("\n# written %s" % args.json)

    print()
    print("# NEXT: cross-check a run against psp-tt-pos.py -- the player triple must have MOVED")
    print("# across these same snapshots, while these stay fixed. A fixed triple beside the moving")
    print("# one is the objective, and (objective - player) gives the bearing for pathfinding.")
    return runs


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--legs", type=int, default=6)
    ap.add_argument("--leg-seconds", type=float, default=2.5)
    ap.add_argument("--limit", type=int, default=40)
    ap.add_argument("--json")
    asyncio.run(run(ap.parse_args()))

#!/usr/bin/env python3
"""psp-tt-objective.py -- find the OBJECTIVE as an entity in the world-map entity array.

WHAT THE RESEARCH CHANGED
    GameFAQs/forums on Tenkaichi Tag Team's Dragon Walker mode establish that Dragon Walker IS the
    story mode and is a free-flight world map where objectives are THINGS ON THE MAP: towns you fly
    to, enemies you fight, items ("the white monster flies around the world map and runs away if he
    sees you... just fly around the world and if you see something moving away from you, chase it"),
    and NPCs you talk to.

    So the objective is an ENTITY WITH A POSITION, very likely in the same entity array as the
    player -- not an abstract vector.

WHY THE EARLIER CONSTANT-HUNT FAILED
    Searching ALL of RAM for "floats that stay constant while you move" returned 174,900 hits,
    because most of RAM is code, tables, textures and inactive entities. The fix is to CONSTRAIN the
    search by structure: only look at the entity region that contains the player's position triple
    (found at ~0x08B6A08C), and classify each position triple there as MOVING or FIXED.

A POSITION TRIPLE HERE LOOKS LIKE
    two large components and one small one, e.g. the player read `1565  -13  1153`.
    Rule: at least 2 of the 3 in [100, 20000] and all finite.

METHOD
    1. snapshot the entity region at REST
    2. fly a leg, settle, snapshot again  (repeat a few times)
    3. any triple that CHANGES is player-like; any triple that stays FIXED across all snapshots is
       a stationary entity -- the objective / an NPC / a landmark
    4. report both, with the distance from the player to each fixed candidate, since the objective
       is usually a sensible distance away and in the direction the chevron points

USAGE
    python scripts/psp-tt-objective.py --legs 4 --leg-seconds 2.5
"""
import argparse, asyncio, base64, json, math, os, struct, sys, time

RAM_LO, RAM_HI = 0x08800000, 0x0A000000
CHUNK = 0x40000
MAX_MSG = 16 * 1024 * 1024

# Entity region: the player triple lives near 0x08B6A08C, so scan a generous band around it.
REGION_LO = 0x08B69000
REGION_HI = 0x08B6C000          # default: the entity band holding the player triple
FULL = False                    # --full scans all user RAM instead


def f32(buf, off):
    return struct.unpack_from("<f", buf, off)[0]


def is_world_triple(a, b, c):
    """Player-like position: 2 of 3 large, all finite, none absurd."""
    vals = (a, b, c)
    if not all(math.isfinite(v) and abs(v) < 100000 for v in vals):
        return False
    big = sum(1 for v in vals if 100.0 <= abs(v) <= 20000.0)
    return big >= 2


class Dev:
    def __init__(self):
        self.ws = None
        self.n = 0

    async def open(self):
        import websockets
        self.ws = await websockets.connect("ws://127.0.0.1:12345/debugger",
                                          subprotocols=["debugger.ppsspp.org"],
                                          max_size=MAX_MSG, ping_interval=20, ping_timeout=20)
        await self.req("version", name="psp-tt-objective", version="1")

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

    async def read_region(self):
        out = []
        lo, hi = (RAM_LO, RAM_HI) if FULL else (REGION_LO, REGION_HI)
        a = lo
        while a < hi:
            size = min(CHUNK, hi - a)
            r = await self.req("memory.read", address="0x%08X" % a, size=size)
            out.append(base64.b64decode(r.get("base64", "")))
            a += size
        return b"".join(out)

    async def hold(self, x, y, secs):
        t0 = time.time()
        while time.time() - t0 < secs:
            self.n += 1
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send",
                                               "ticket": str(self.n),
                                               "stick": "left", "x": x, "y": y}))
            except Exception:
                pass
            await asyncio.sleep(0.05)

    async def rest(self, settle=1.3):
        for _ in range(6):
            self.n += 1
            try:
                await self.ws.send(json.dumps({"event": "input.analog.send",
                                               "ticket": str(self.n),
                                               "stick": "left", "x": 0.0, "y": 0.0}))
            except Exception:
                pass
            await asyncio.sleep(0.05)
        await asyncio.sleep(settle)


LEGS = [(0.0, -1.0), (1.0, 0.0), (0.0, 1.0), (-1.0, 0.0),
        (0.7071, -0.7071), (0.7071, 0.7071)]


async def run(args):
    dev = Dev()
    await dev.open()
    snaps = []
    await dev.rest(0.6)

    for i in range(args.legs):
        if i:
            dx, dy = LEGS[(i - 1) % len(LEGS)]
            print("# leg %d: fly (%.2f,%.2f) for %.1fs" % (i, dx, dy, args.leg_seconds))
            await dev.hold(dx, dy, args.leg_seconds)
            await dev.rest()
        snaps.append(await dev.read_region())
        print("# snapshot %d taken" % (i))

    n = min(len(s) for s in snaps)

    # Collect every position triple in the region, and track whether it moved.
    moved, fixed = [], []
    for o in range(0, n - 12, 4):
        trips = []
        for s in snaps:
            trips.append((f32(s, o), f32(s, o + 4), f32(s, o + 8)))
        if not is_world_triple(*trips[0]):
            continue
        base = (RAM_LO if FULL else REGION_LO) + o
        entry = {"addr": base, "first": trips[0], "all": trips}
        if any(t != trips[0] for t in trips[1:]):
            moved.append(entry)
        else:
            fixed.append(entry)

    print()
    print("=== MOVING triples (player-like): %d ===" % len(moved))
    for e in moved[:args.limit]:
        a = e["first"]
        print("  0x%08X  %8.1f %8.1f %8.1f" % (e["addr"], a[0], a[1], a[2]))

    print()
    print("=== FIXED triples across all snapshots (stationary entities): %d ===" % len(fixed))
    for e in fixed[:args.limit]:
        a = e["first"]
        print("  0x%08X  %8.1f %8.1f %8.1f" % (e["addr"], a[0], a[1], a[2]))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"moving": [e["addr"] for e in moved],
                       "fixed": [{"addr": e["addr"], "v": e["first"]} for e in fixed]}, f)
        print("\n# written %s" % args.json)

    print()
    print("# The objective is a FIXED triple. The player position is the MOVING triple we already know")
    print("# (0x08B6A08C). (objective - player) gives the world bearing for pathfinding.")
    return moved, fixed


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--legs", type=int, default=4)
    ap.add_argument("--full", action="store_true",
                    help="scan all user RAM instead of just the entity band")
    ap.add_argument("--leg-seconds", type=float, default=2.5)
    ap.add_argument("--limit", type=int, default=25)
    ap.add_argument("--json")
    _a = ap.parse_args()
    globals()["FULL"] = _a.full
    asyncio.run(run(_a))

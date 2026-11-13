#!/usr/bin/env python3
"""Greedy four-direction navigation that re-measures every step.

No calibration table: at each step it nudges each stick direction, keeps the one
that shortened the distance, then commits a longer push. Robust to camera swings.

Usage: python3 greedy4.py <x> <z> [stop_distance]
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]

def rd(a, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 't%d' % _tk[0]
        d.ws.send(json.dumps({"event": "memory.read", "address": a, "size": n, "ticket": tk}))
        end = time.time() + 3
        while time.time() < end:
            try:
                raw = d.ws.recv()
            except Exception:
                break
            if ('"ticket":"%s"' % tk) not in raw:
                continue
            try:
                j = json.loads(raw)
            except Exception:
                break
            if 'base64' in j:
                b = base64.b64decode(j['base64'])
                if len(b) == n:
                    return b
            break
        time.sleep(0.02)
    return b''

def pos():
    r = rd(0x08AED478, 12)
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.10)

def dist(p, tx, tz):
    return math.hypot(tx - p[0], tz - p[1])

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.2
DIRS = [(0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)]

stuck = 0
for step in range(60):
    p = pos()
    dd = dist(p, tx, tz)
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    best = None
    for dx, dy in DIRS:
        push(dx, dy, 0.16)
        p2 = pos()
        d2 = dist(p2, tx, tz)
        if best is None or d2 < best[0]:
            best = (d2, dx, dy)
    # commit in the direction that helped
    push(best[1], best[2], 0.30)
    p3 = pos()
    d3 = dist(p3, tx, tz)
    print('%2d (%.1f, %.1f) d=%.2f -> %.2f  stick %.0f,%.0f' % (
        step, p3[0], p3[1], dd, d3, best[1], best[2]), flush=True)
    stuck = stuck + 1 if d3 > dd - 0.05 else 0
    if stuck >= 4:
        print('stuck at (%.1f, %.1f) d=%.2f' % (p3[0], p3[1], d3))
        break
d.close()

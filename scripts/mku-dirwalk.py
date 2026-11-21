#!/usr/bin/env python3
"""Walk toward a world point by converting the bearing into stick coordinates.

The player's draw transform gives the character's basis: row0 is forward (proven
by calibration). The horizontal row perpendicular to it is the right vector.
A world direction B becomes stick (dot(B,right), dot(B,forward)), so no turning
guesswork is needed.

Usage: python3 dirwalk.py <x> <z> [stop] [max_iters]
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]
NODE = 0x09E51510

def rd(a, n=4):
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

def basis():
    r = rd(NODE, 0x30)
    f = struct.unpack('<12f', r)
    rows = [(f[0], f[2]), (f[4], f[6]), (f[8], f[10])]
    fwd = rows[0]
    n = math.hypot(*fwd)
    fwd = (fwd[0] / n, fwd[1] / n)
    # pick the row most perpendicular to forward for the right vector
    best = None
    for r2 in rows[1:]:
        n2 = math.hypot(*r2)
        if n2 < 0.5:
            continue
        r2 = (r2[0] / n2, r2[1] / n2)
        dot = abs(r2[0] * fwd[0] + r2[1] * fwd[1])
        if best is None or dot < best[0]:
            best = (dot, r2)
    right = best[1] if best else (fwd[1], -fwd[0])
    return fwd, right

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.05)

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.2
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 40

for i in range(maxit):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    fwd, right = basis()
    B = ((tx - p[0]) / dd, (tz - p[1]) / dd)
    sx = B[0] * right[0] + B[1] * right[1]
    sy = B[0] * fwd[0] + B[1] * fwd[1]
    n = math.hypot(sx, sy) or 1.0
    sx, sy = sx / n, sy / n
    before = dd
    push(sx, sy, 0.45)
    p2 = pos()
    print('%2d (%.1f, %.1f) d %.1f -> %.1f stick(%.2f,%.2f)' % (
        i, p2[0], p2[1], before, math.hypot(tx - p2[0], tz - p2[1]), sx, sy), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

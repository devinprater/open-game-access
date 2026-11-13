#!/usr/bin/env python3
"""Navigate using the player's own rotation matrix for facing.

Reads the 3x3 at the player's draw transform, picks the row that matches a
forward push, then alternates turning and walking with the facing measured each
step instead of guessed.

Usage: python3 facewalk.py <x> <z> [stop] [max_steps]
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]

NODE = 0x09E51510          # player draw transform (verified live: translation == vec3)

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

def matrix():
    r = rd(NODE, 0x30)
    if len(r) != 0x30:
        return None
    f = struct.unpack('<12f', r)
    return [(f[0], f[1], f[2]), (f[4], f[5], f[6]), (f[8], f[9], f[10])]

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.05)

def unit(v):
    n = math.hypot(v[0], v[1])
    return (v[0] / n, v[1] / n) if n > 1e-6 else (0.0, 0.0)

def ang(a, b):
    v = a - b
    while v > math.pi: v -= 2 * math.pi
    while v < -math.pi: v += 2 * math.pi
    return v

# --- calibrate: which matrix row is the facing vector? ---
rows = matrix()
p0 = pos(); push(0, 1, 0.40); p1 = pos()
mv = unit((p1[0] - p0[0], p1[1] - p0[1]))
print('push moved (%.2f, %.2f)' % mv, flush=True)
best = None
for i, r in enumerate(rows):
    for s in (1.0, -1.0):
        v = unit((s * r[0], s * r[2]))
        dot = v[0] * mv[0] + v[1] * mv[1]
        print('  row%d %s dot %.2f' % (i, '-' if s < 0 else '+', dot), flush=True)
        if best is None or dot > best[0]:
            best = (dot, i, s)
print('facing = row%d %s (dot %.2f)' % (best[1], '-' if best[2] < 0 else '+', best[0]), flush=True)
ROW, SIGN = best[1], best[2]

def facing():
    r = matrix()[ROW]
    return unit((SIGN * r[0], SIGN * r[2]))

def bearing(p0, t):
    return unit((t[0] - p0[0], t[1] - p0[1]))

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.2
maxst = int(sys.argv[4]) if len(sys.argv) > 4 else 60

# turn-rate calibration
f0 = facing(); push(1, 0, 0.20); f1 = facing()
turn = ang(math.atan2(f1[0], f1[1]), math.atan2(f0[0], f0[1]))
print('right push turns %.0f deg' % math.degrees(turn), flush=True)
sign = 1.0

for i in range(maxst):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    f = facing()
    want = math.atan2(tx - p[0], tz - p[1])
    diff = ang(want, math.atan2(f[0], f[1]))
    if abs(diff) > math.radians(12) and abs(turn) > 1e-3:
        secs_t = max(0.05, min(0.16, abs(diff) / abs(turn) * 0.16))
        sgn = sign if (diff > 0) == (turn > 0) else -sign
        before = abs(diff)
        push(sgn, 0, secs_t)
        fc = facing()
        after = abs(ang(want, math.atan2(fc[0], fc[1])))
        if after > before + 0.05:
            sign = -sign
        continue
    push(0, 1, 0.40)
    print('%2d (%.1f, %.1f) d=%.2f' % (i, pos()[0], pos()[1], math.hypot(tx - pos()[0], tz - pos()[1])), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

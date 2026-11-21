#!/usr/bin/env python3
"""Walk toward a world bearing by solving the stick from the true heading.

The heading vector at 0x09E5C180 is the character's facing, and the stick axes
are relative to it: push (right_component, forward_component). No turning needed.

Usage: python3 dirwalk2.py <x> <z> [stop] [max_iters]
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]
HEADING = 0x09E5C180

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
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2])

def head():
    v = struct.unpack('<2f', rd(HEADING, 8))
    n = math.hypot(*v)
    return (v[0] / n, v[1] / n) if n > 1e-6 else (0.0, 0.0)

def push(x, y, s):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(s)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.04)

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 60

# choose the right-vector sign once by trying one step each way
p = pos()
h = head()
B = ((tx - p[0]), (tz - p[1]))
n = math.hypot(*B) or 1
B = (B[0] / n, B[1] / n)
best_side = None
for sgn in (1.0, -1.0):
    R = (sgn * h[1], -sgn * h[0])
    sx = B[0] * R[0] + B[1] * R[1]
    sy = B[0] * h[0] + B[1] * h[1]
    p0 = pos()
    push(sx, sy, 0.35)
    p1 = pos()
    d1 = math.hypot(tx - p1[0], tz - p1[1])
    d0 = math.hypot(tx - p0[0], tz - p0[1])
    print('side %+.0f: d %.1f -> %.1f' % (sgn, d0, d1), flush=True)
    if best_side is None or d1 < best_side[0]:
        best_side = (d1, sgn)
SGN = best_side[1]

for i in range(maxit):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    h = head()
    B = ((tx - p[0]) / dd, (tz - p[1]) / dd)
    R = (SGN * h[1], -SGN * h[0])
    sx = B[0] * R[0] + B[1] * R[1]
    sy = B[0] * h[0] + B[1] * h[1]
    m = math.hypot(sx, sy) or 1
    before = dd
    push(sx / m, sy / m, 0.50)
    after = math.hypot(tx - pos()[0], tz - pos()[1])
    if after > before + 0.5:
        SGN = -SGN
    if i % 8 == 0:
        print('%2d (%.1f, %.1f) d=%.1f stick(%.2f,%.2f)' % (i, pos()[0], pos()[1], after, sx / m, sy / m), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

#!/usr/bin/env python3
"""Navigate with the real heading vector at 0x09E5C180 (dot 1.00 with movement).

Turn by short sideways bursts, re-reading the heading after each; walk forward
only when within a few degrees of the bearing.

Usage: python3 facewalk3.py <x> <z> [stop] [max_iters]
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
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

def head():
    v = struct.unpack('<2f', rd(HEADING, 8))
    n = math.hypot(*v)
    return (v[0] / n, v[1] / n) if n > 1e-6 else (0.0, 0.0)

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.04)

def adiff(a, b):
    v = a - b
    while v > math.pi: v -= 2 * math.pi
    while v < -math.pi: v += 2 * math.pi
    return v

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.2
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 220
sign = 1.0
stall = 0
lastp = None
for i in range(maxit):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    # stuck recovery: back out along the reversed heading, sidestep, replan
    if lastp is not None and math.hypot(p[0] - lastp[0], p[1] - lastp[1]) < 0.06:
        stall += 1
    else:
        stall = 0
    lastp = p
    if stall >= 8:
        print('  stuck at (%.1f, %.1f); backing out' % (p[0], p[1]), flush=True)
        push(0, -1, 0.45)
        push(sign, 0, 1.20)      # slide along the wall, not just a nudge
        sign = -sign
        stall = 0
        lastp = None
        continue
    h = head()
    want = math.atan2(tx - p[0], tz - p[1])
    err = adiff(want, math.atan2(h[0], h[1]))
    if abs(err) > math.radians(22):
        before = abs(err)
        push(sign, 0, 0.08)
        h2 = head()
        after = abs(adiff(want, math.atan2(h2[0], h2[1])))
        if after > before + 0.25:
            sign = -sign
        continue
    push(0, 1, 0.35)
    if i % 10 == 0:
        print('%3d (%.1f, %.1f) d=%.1f hdg=%.0f' % (i, pos()[0], pos()[1],
              math.hypot(tx - pos()[0], tz - pos()[1]), math.degrees(math.atan2(*head()))), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

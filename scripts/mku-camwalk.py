#!/usr/bin/env python3
"""Steer by rotating the camera, then walking forward.

Movement is camera-relative: a right-stick rotation changes where a forward push
goes while the character's own heading vector stays put (measured). So the camera
is the steering wheel.

Calibrate the rotation rate once, then loop: walk a short burst, read the world
direction that burst actually took, rotate the camera by the error, repeat.

Usage: python3 camwalk.py <x> <z> [stop] [max_iters]
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]

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

def walk(secs=0.40):
    p0 = pos()
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 1}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.12)
    p1 = pos()
    mv = (p1[0] - p0[0], p1[1] - p0[1])
    n = math.hypot(*mv)
    return (mv[0] / n, mv[1] / n) if n > 0.02 else None

def camera(secs, sign=1.0):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": sign, "y": 0}))
    time.sleep(abs(secs))
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": 0, "y": 0}))
    time.sleep(0.15)

def ang(v):
    return math.atan2(v[0], v[1])

def wrap(a):
    while a > math.pi: a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 80

m1 = walk(0.40)
if m1 is None:
    print('cannot walk'); raise SystemExit(1)
camera(0.40, 1.0)
m2 = walk(0.40)
if m2 is None:
    print('cannot walk after camera'); raise SystemExit(1)
rate = wrap(ang(m2) - ang(m1)) / 0.40
print('camera rate %.0f deg per second' % math.degrees(rate), flush=True)

lastp = None
stall = 0
for i in range(maxit):
    p = pos()
    if lastp and math.hypot(p[0]-lastp[0], p[1]-lastp[1]) < 0.15:
        stall += 1
    else:
        stall = 0
    lastp = p
    if stall >= 5:
        print('  stalled at (%.1f, %.1f); escaping' % p, flush=True)
        d.ws.send(json.dumps({'event': 'input.analog.send', 'stick': 'left', 'x': 0, 'y': -1}))
        time.sleep(0.9)
        d.ws.send(json.dumps({'event': 'input.analog.send', 'stick': 'left', 'x': 0, 'y': 0}))
        time.sleep(0.2)
        camera(1.0, 1.0)
        lastp = None
        stall = 0
        continue
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    m = walk(0.40)
    if m is None:
        print('  blocked at (%.1f, %.1f); backing out' % p, flush=True)
        d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": -1}))
        time.sleep(0.8)
        d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
        time.sleep(0.2)
        camera(0.8, 1.0)
        continue
    want = math.atan2(tx - p[0], tz - p[1])
    err = wrap(want - ang(m))
    if abs(err) > math.radians(10) and abs(rate) > 1e-3:
        camera(min(0.6, abs(err) / abs(rate)), 1.0 if (err > 0) == (rate > 0) else -1.0)
    if i % 8 == 0:
        print('%2d (%.1f, %.1f) d=%.1f move(%.2f,%.2f) err=%.0f' % (
            i, pos()[0], pos()[1], math.hypot(tx - pos()[0], tz - pos()[1]), m[0], m[1],
            math.degrees(err)), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

#!/usr/bin/env python3
"""Wall-follow by rotating the camera, not by push-turning the character.

Loop: walk a burst toward the target heading; if the burst made no progress,
rotate the camera 45 degrees in a committed direction and try again. Commit to a
rotation direction per obstacle so the follower does not oscillate, and resume
the direct bearing when a probe shows progress.

Usage: python3 rotwalk.py <x> <z> [stop] [max_iters]
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

def walk(secs=0.38):
    p0 = pos()
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 1}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.10)
    p1 = pos()
    dist = math.hypot(p1[0] - p0[0], p1[1] - p0[1])
    if dist < 0.02:
        return None
    return (math.atan2(p1[0] - p0[0], p1[1] - p0[1]), dist, p1)

def cam(deg, sign=1.0):
    # calibration is approximate: 0.6 s of full deflection is about 90 degrees
    secs = abs(deg) / 150.0
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": sign, "y": 0}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": 0, "y": 0}))
    time.sleep(0.12)

def wrap(a):
    while a > math.pi: a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 120

# measure the camera rate once
m1 = None
for k in range(8):
    m1 = walk()
    if m1:
        break
    cam(45, 1.0)
if not m1:
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": -1}))
    time.sleep(0.9)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.3)
    for k in range(8):
        m1 = walk()
        if m1:
            break
        cam(45, 1.0)
if not m1:
    print('cannot walk at start'); raise SystemExit(1)
cam(90, 1.0)
m2 = None
for k in range(8):
    cam(45, 1.0)
    m2 = walk()
    if m2:
        break
if not m2:
    print('cannot walk in any direction'); raise SystemExit(1)
rate = abs(wrap(m2[0] - m1[0])) / 0.6
print('camera ~%.0f deg/s' % math.degrees(rate), flush=True)

wall_side = 1.0
stall = 0
for i in range(maxit):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    want = math.atan2(tx - p[0], tz - p[1])
    m = walk()
    if m is None:
        cam(45, wall_side)
        stall += 1
        if stall % 4 == 0:
            wall_side = -wall_side
        continue
    heading, moved, p1 = m
    err = wrap(want - heading)
    if abs(err) > math.radians(20):
        cam(min(60.0, abs(math.degrees(err))), 1.0 if (err > 0) == (rate > 0) else -1.0)
    if i % 10 == 0:
        print('%3d (%.1f, %.1f) d=%.1f moved %.1f err=%.0f' % (
            i, p1[0], p1[1], math.hypot(tx - p1[0], tz - p1[1]), moved, math.degrees(err)), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

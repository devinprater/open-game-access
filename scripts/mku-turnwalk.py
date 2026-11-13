#!/usr/bin/env python3
"""Face-and-walk navigation: measure the forward world bearing, turn, walk.

Holding forward walks a straight line (the camera follows the character), so the
controller only needs to know the current forward bearing and the turn rate.
Every push is followed by a fresh bearing measurement.

Usage: python3 turnwalk.py <x> <z> [stop] [max_steps]
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
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.06)

def bearing(p0, p1):
    return math.atan2(p1[0] - p0[0], p1[1] - p0[1])     # atan2(dx, dz)

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
maxst = int(sys.argv[4]) if len(sys.argv) > 4 else 40

# calibrate forward bearing
p0 = pos(); push(0, 1, 0.30); p1 = pos()
if math.hypot(p1[0]-p0[0], p1[1]-p0[1]) < 0.05:
    print('forward does not move'); raise SystemExit(1)
fwd = bearing(p0, p1)
# calibrate turn rate (right push)
p2 = pos(); push(1, 0, 0.30); p3 = pos()
if math.hypot(p3[0]-p2[0], p3[1]-p2[1]) > 0.05:
    turn = bearing(p2, p3) - fwd
    while turn > math.pi: turn -= 2*math.pi
    while turn < -math.pi: turn += 2*math.pi
else:
    turn = 0.0
print('forward %.0f deg, right push turns %+.0f deg' % (math.degrees(fwd), math.degrees(turn)), flush=True)

def ang_diff(a, b):
    v = a - b
    while v > math.pi: v -= 2*math.pi
    while v < -math.pi: v += 2*math.pi
    return v

sign = 1.0
for i in range(maxst):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    want = math.atan2(tx - p[0], tz - p[1])
    diff = ang_diff(want, fwd)
    if abs(diff) > math.radians(15) and abs(turn) > 1e-3:
        secs = max(0.10, min(0.55, abs(diff) / abs(turn) * 0.30))
        sgn = sign if (diff > 0) == (turn > 0) else -sign
        pa = pos(); push(sgn, 0, secs); pb = pos()
        if math.hypot(pb[0]-pa[0], pb[1]-pa[1]) > 0.03:
            fwd = bearing(pa, pb)
            if abs(ang_diff(want, fwd)) > abs(diff) + 0.05:
                sign = -sign
        continue
    pa = pos(); push(0, 1, 0.40); pb = pos()
    if math.hypot(pb[0]-pa[0], pb[1]-pa[1]) > 0.03:
        fwd = bearing(pa, pb)
    print('%2d (%.1f, %.1f) d=%.2f fwd=%.0f' % (i, pb[0], pb[1],
          math.hypot(tx-pb[0], tz-pb[1]), math.degrees(fwd)), flush=True)
d.close()

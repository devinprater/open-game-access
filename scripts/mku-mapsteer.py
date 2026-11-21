#!/usr/bin/env python3
"""Steer by a measured stick-to-world matrix.

Measure where (1,0) and (0,1) actually take the player, build the 2x2 matrix
M = [right | forward], then solve stick = M^-1 * bearing for any target. Works
without knowing the camera yaw; recalibrates when progress stalls.

Usage: python3 mapsteer.py <x> <z> [stop] [max_iters]
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

def push(x, y, s):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(s)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.06)

def calibrate():
    p0 = pos(); push(1, 0, 0.30); p1 = pos()
    vx = (p1[0] - p0[0], p1[1] - p0[1])
    p2 = pos(); push(0, 1, 0.30); p3 = pos()
    vy = (p3[0] - p2[0], p3[1] - p2[1])
    det = vx[0] * vy[1] - vy[0] * vx[1]
    if abs(det) < 1e-6:
        return None
    inv = [[vy[1] / det, -vy[0] / det], [-vx[1] / det, vx[0] / det]]
    return inv

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 120

inv = calibrate()
if inv is None:
    print('degenerate calibration'); raise SystemExit(1)
print('calibrated', flush=True)

stall = 0
last = None
for i in range(maxit):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    if last is not None and dd > last - 0.05:
        stall += 1
    else:
        stall = 0
    last = dd
    if stall == 1:
        # sidestep around the obstruction, then keep going
        push(1, 0, 0.5)
        last = None
        stall = 0
        continue
    if stall >= 3:
        inv = calibrate() or inv
        stall = 0
        last = None
        continue
    B = ((tx - p[0]) / dd, (tz - p[1]) / dd)
    sx = inv[0][0] * B[0] + inv[0][1] * B[1]
    sy = inv[1][0] * B[0] + inv[1][1] * B[1]
    m = math.hypot(sx, sy) or 1
    push(sx / m, sy / m, 0.45)
    if i % 10 == 0:
        print('%3d (%.1f, %.1f) d=%.1f stick(%.2f,%.2f)' % (
            i, pos()[0], pos()[1], math.hypot(tx - pos()[0], tz - pos()[1]), sx / m, sy / m), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

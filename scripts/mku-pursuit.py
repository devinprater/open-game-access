#!/usr/bin/env python3
"""Pursuit navigation: hold forward, correct only when the distance stops falling.

Forward walks a straight line (the camera follows the character), so the loop
walks in long bursts and only strafes when a burst fails to close the distance.

Usage: python3 pursuit.py <x> <z> [stop] [max_iterations]
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
    time.sleep(0.05)

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.2
maxit = int(sys.argv[4]) if len(sys.argv) > 4 else 60

def dist():
    p = pos()
    return math.hypot(tx - p[0], tz - p[1]), p

for i in range(maxit):
    dd, p = dist()
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    push(0, 1, 0.55)                       # long forward burst
    d1, p1 = dist()
    if d1 < dd - 0.05:
        continue
    # no progress: try a side step, then forward
    best = None
    for sx in (1.0, -1.0):
        push(sx, 0, 0.28)
        push(0, 1, 0.35)
        d2, p2 = dist()
        if best is None or d2 < best[0]:
            best = (d2, p2)
    if best[0] > dd - 0.05:
        # both sides worse: back up and try again
        push(0, -1, 0.35)
    print('%2d d=%.1f' % (i, dist()[0]), flush=True)
print('final %.2f' % dist()[0])
d.close()

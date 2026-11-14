#!/usr/bin/env python3
"""Matrix steering plus recorded free cells, with A* on the record.

The measured stick-to-world matrix makes probing dependable, so the walker
records the cells it crosses and plans over them. Unknown cells stay blocked.

Usage: python3 routeslam.py <x> <z> [minutes]
"""
import base64, heapq, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]
CELL = 2.0

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
    time.sleep(0.05)

def calibrate():
    p0 = pos(); push(1, 0, 0.30); p1 = pos()
    vx = (p1[0] - p0[0], p1[1] - p0[1])
    p2 = pos(); push(0, 1, 0.30); p3 = pos()
    vy = (p3[0] - p2[0], p3[1] - p2[1])
    det = vx[0] * vy[1] - vy[0] * vx[1]
    if abs(det) < 1e-6:
        return None
    return [[vy[1] / det, -vy[0] / det], [-vx[1] / det, vx[0] / det]]

def cellof(p):
    return (int(round(p[0] / CELL)), int(round(p[1] / CELL)))

def center(c):
    return (c[0] * CELL, c[1] * CELL)

tx, tz = float(sys.argv[1]), float(sys.argv[2])
minutes = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0
goal = cellof((tx, tz))
inv = calibrate()
if inv is None:
    print('degenerate calibration'); raise SystemExit(1)
free = set()
path = []
t0 = time.time()
side = 1.0
stall = 0
lastd = None

while time.time() - t0 < minutes * 60:
    p = pos()
    free.add(cellof(p))
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= 2.0:
        print('ARRIVED (%.1f, %.1f) after %.1f min, %d cells' % (p[0], p[1], (time.time() - t0) / 60, len(free)), flush=True)
        break
    if goal in free and not path:
        start = cellof(p)
        openq = [(0.0, start)]
        came = {start: None}
        cost = {start: 0.0}
        found = False
        while openq:
            f, cur = heapq.heappop(openq)
            if cur == goal:
                found = True
                break
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    nb = (cur[0] + dx, cur[1] + dy)
                    if nb not in free:
                        continue
                    nc = cost[cur] + math.hypot(dx, dy)
                    if nb not in cost or nc < cost[nb]:
                        cost[nb] = nc
                        came[nb] = cur
                        heapq.heappush(openq, (nc + math.hypot(nb[0] - goal[0], nb[1] - goal[1]), nb))
        if found:
            cur = goal
            seq = []
            while cur is not None:
                seq.append(cur)
                cur = came[cur]
            path = list(reversed(seq))[1:]
            print('planned %d waypoints over %d cells' % (len(path), len(free)), flush=True)
    if path:
        wx, wz = center(path[0])
        aim = (wx, wz)
    else:
        aim = (tx, tz)
    B = (aim[0] - p[0], aim[1] - p[1])
    n = math.hypot(*B) or 1
    B = (B[0] / n, B[1] / n)
    if lastd is not None and dd > lastd - 0.05:
        stall += 1
    else:
        stall = 0
    lastd = dd
    if stall >= 2:
        push(side, 0, 0.6)
        side = -side
        stall = 0
        lastd = None
        inv = calibrate() or inv
        path = []
        continue
    sx = inv[0][0] * B[0] + inv[0][1] * B[1]
    sy = inv[1][0] * B[0] + inv[1][1] * B[1]
    m = math.hypot(sx, sy) or 1
    p0 = pos()
    push(sx / m, sy / m, 0.45)
    p1 = pos()
    free.add(cellof(p1))
    free.add(cellof(((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2)))
    if path and math.hypot(p1[0] - center(path[0])[0], p1[1] - center(path[0])[1]) < 2.0:
        path.pop(0)
    if int(time.time() - t0) % 30 == 0:
        print('  %3.0fs (%.1f, %.1f) d=%.1f cells=%d path=%d' % (
            time.time() - t0, p1[0], p1[1], math.hypot(tx - p1[0], tz - p1[1]), len(free), len(path)), flush=True)
print('done: (%.1f, %.1f) d=%.1f cells=%d' % (*pos(), math.hypot(tx - pos()[0], tz - pos()[1]), len(free)))
d.close()

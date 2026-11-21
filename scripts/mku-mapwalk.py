#!/usr/bin/env python3
"""Local map + A*: cast eight probe rays, mark free cells, plan, follow, replan.

Uses the real heading vector for turning and short pushes for probing. Unknown
cells count as blocked, so the plan only crosses ground that was actually walked.

Usage: python3 mapwalk.py <x> <z> [stop] [replans]
"""
import base64, heapq, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]
HEADING = 0x09E5C180
CELL = 1.5          # world units per grid cell
REACH = 14          # ray length in cells

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

def hang():
    h = head()
    return math.atan2(h[0], h[1])

def push(x, y, s):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(s)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.03)

def wrap(a):
    while a > math.pi: a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a

sign = [1.0]

def turn_to(ang, tries=45):
    for _ in range(tries):
        err = wrap(ang - hang())
        if abs(err) < math.radians(12):
            return True
        before = abs(err)
        push(sign[0], 0, 0.08)
        if abs(wrap(ang - hang())) > before + 0.02:
            sign[0] = -sign[0]
    return False

def probe(secs=0.26):
    p0 = pos()
    push(0, 1, secs)
    p1 = pos()
    return math.hypot(p1[0] - p0[0], p1[1] - p0[1]), p1

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
replans = int(sys.argv[4]) if len(sys.argv) > 4 else 6

def cellof(p):
    return (int(round(p[0] / CELL)), int(round(p[1] / CELL)))

def center(c):
    return (c[0] * CELL, c[1] * CELL)

for plan in range(replans):
    p = pos()
    if math.hypot(tx - p[0], tz - p[1]) <= stop:
        print('arrived at (%.1f, %.1f)' % p, flush=True)
        break
    free = set()
    # stand still and cast rays
    for k in range(8):
        ang = k * math.pi / 4
        if not turn_to(ang):
            continue
        cur = pos()
        free.add(cellof(cur))
        for step in range(REACH):
            moved, nxt = probe(0.24)
            if moved < 0.12:
                break
            free.add(cellof(nxt))
            cur = nxt
    p = pos()
    start = cellof(p)
    goal = cellof((tx, tz))
    if start not in free:
        free.add(start)
    # A* over free cells, 8-neighbour
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
                    h = math.hypot(nb[0] - goal[0], nb[1] - goal[1])
                    heapq.heappush(openq, (nc + h, nb))
    print('plan %d: %d free cells, goal %s' % (plan, len(free), 'reachable' if found else 'UNREACHABLE'), flush=True)
    if not found:
        # step toward the closest free cell to the goal, then replan
        best = min(free, key=lambda c: math.hypot(c[0] - goal[0], c[1] - goal[1]))
        path = [best]
    else:
        path = []
        cur = goal
        while cur is not None:
            path.append(cur)
            cur = came[cur]
        path.reverse()
    for way in path[1:6]:
        wx, wz = center(way)
        p = pos()
        want = math.atan2(wx - p[0], wz - p[1])
        turn_to(want)
        for _ in range(4):
            moved, _p = probe(0.30)
            if moved < 0.10:
                break
    print('  moved to (%.1f, %.1f), d=%.1f' % (*pos(), math.hypot(tx - pos()[0], tz - pos()[1])), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

#!/usr/bin/env python3
"""Wall-following navigation (bug2) using the real heading vector.

Primitives:
  heading()  unit (x,z) at 0x09E5C180, dot 1.00 with movement
  turn_to()  short side pushes until the heading matches an absolute bearing
  probe()    push forward briefly and report whether the player moved

Bug2: head for the target; when blocked, follow the obstacle with the wall on
the left, and resume the direct line as soon as it is clear again.

Usage: python3 bugwalk.py <x> <z> [stop] [max_steps]
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

def hang():
    h = head()
    return math.atan2(h[0], h[1])

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.03)

def wrap(a):
    while a > math.pi: a -= 2 * math.pi
    while a < -math.pi: a += 2 * math.pi
    return a

sign = [1.0]

def turn_to(ang, tries=40):
    for _ in range(tries):
        err = wrap(ang - hang())
        if abs(err) < math.radians(12):
            return True
        before = abs(err)
        push(sign[0], 0, 0.08)
        after = abs(wrap(ang - hang()))
        if after > before + 0.02:
            sign[0] = -sign[0]
    return False

def probe(secs=0.28):
    p0 = pos()
    push(0, 1, secs)
    p1 = pos()
    return math.hypot(p1[0] - p0[0], p1[1] - p0[1]), p1

tx, tz = float(sys.argv[1]), float(sys.argv[2])
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
maxst = int(sys.argv[4]) if len(sys.argv) > 4 else 160

wall = False
best = 1e9
for i in range(maxst):
    p = pos()
    dd = math.hypot(tx - p[0], tz - p[1])
    if dd <= stop:
        print('arrived %.2f at (%.1f, %.1f)' % (dd, p[0], p[1]), flush=True)
        break
    if dd < best - 0.4:
        best = dd
        if wall:
            wall = False
            globals()['wall_dir'] = None
            print('%3d free again at d=%.1f' % (i, dd), flush=True)
    if not wall:
        want = math.atan2(tx - p[0], tz - p[1])
        if not turn_to(want):
            # wedged: back off, flip the turn direction, try once more
            push(0, -1, 0.45)
            sign[0] = -sign[0]
            if not turn_to(want):
                moved, p1 = probe(0.30)   # let the walk direction fix the heading
                if moved < 0.08:
                    push(sign[0], 0, 0.8)  # slide along whatever is there
        moved, p1 = probe(0.32)
        if moved >= 0.10:
            continue
        wall = True
        turn_to(wrap(want + math.pi / 2))     # wall on the left
        print('%3d blocked at (%.1f, %.1f) d=%.1f, following' % (i, p[0], p[1], dd), flush=True)
        continue
    # wall following with a COMMITTED direction: never re-decide mid-wall
    wd = globals().get('wall_dir')
    if wd is None:
        wall = False
        continue
    wall_dir = wd
    wall_steps = globals().get('wall_steps', 0)
    moved, p1 = probe(0.30)
    wall_steps += 1
    globals()['wall_steps'] = wall_steps
    if moved < 0.08:
        push(sign[0], 0, 0.35)        # slide sideways, keep the same side
    else:
        turn_to(wall_dir)
    if wall_steps > 70:
        globals()['wall_dir'] = None
        wall = False
    if i % 15 == 0:
        print('%3d wall (%.1f, %.1f) d=%.1f' % (i, p1[0], p1[1], math.hypot(tx - p1[0], tz - p1[1])), flush=True)
print('final %.2f' % math.hypot(tx - pos()[0], tz - pos()[1]))
d.close()

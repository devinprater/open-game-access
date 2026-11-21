#!/usr/bin/env python3
"""Follow a walking actor with face-and-walk steering, then punch it.

Usage: python3 punchwalk.py <transform_hex> [presses] [stop]
Reads the actor's live translation at transform+0x30 each step.
"""
import base64, json, math, struct, sys, time, os, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=8)
_tk = [0]

def rd(a, n):
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

def player():
    r = rd(0x08AED478, 12)
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

def sub():
    return rd(0x09106354, 120).split(b'\0')[0].decode('latin1', 'replace')

def btn(b, v):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: v}}))

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.06)

def bearing(p0, p1):
    return math.atan2(p1[0] - p0[0], p1[1] - p0[1])

def ang_diff(a, b):
    v = a - b
    while v > math.pi: v -= 2 * math.pi
    while v < -math.pi: v += 2 * math.pi
    return v

def shot(name):
    base = '/mnt/c/Users/Public/'
    for e in ('done', 'png'):
        try: os.remove(base + name + '.' + e)
        except OSError: pass
    open(base + 'shot.req', 'w').write(name)
    t = time.time()
    while time.time() - t < 5:
        if os.path.exists(base + name + '.done'):
            return True
        time.sleep(0.05)
    return False

addr = int(sys.argv[1], 16) + 0x30
presses = int(sys.argv[2]) if len(sys.argv) > 2 else 2
stop = float(sys.argv[3]) if len(sys.argv) > 3 else 1.3

def tgt():
    r = rd(addr, 12)
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

p0 = player(); x0 = tgt()
push(0, 1, 0.30); p1 = player()
fwd = bearing(p0, p1)
p2 = player(); push(1, 0, 0.30); p3 = player()
turn = ang_diff(bearing(p2, p3), fwd) if math.hypot(p3[0]-p2[0], p3[1]-p2[1]) > 0.05 else 0.0
sign = 1.0

for i in range(45):
    p, t = player(), tgt()
    dd = math.hypot(t[0] - p[0], t[1] - p[1])
    if dd <= stop:
        break
    want = math.atan2(t[0] - p[0], t[1] - p[1])
    diff = ang_diff(want, fwd)
    if abs(diff) > math.radians(15) and abs(turn) > 1e-3:
        secs = max(0.10, min(0.45, abs(diff) / abs(turn) * 0.28))
        sgn = sign if (diff > 0) == (turn > 0) else -sign
        pa = player(); push(sgn, 0, secs); pb = player()
        if math.hypot(pb[0]-pa[0], pb[1]-pa[1]) > 0.03:
            fwd = bearing(pa, pb)
            if abs(ang_diff(want, fwd)) > abs(diff) + 0.05:
                sign = -sign
    else:
        pa = player(); push(0, 1, 0.32); pb = player()
        if math.hypot(pb[0]-pa[0], pb[1]-pa[1]) > 0.03:
            fwd = bearing(pa, pb)

p, t = player(), tgt()
dd = math.hypot(t[0] - p[0], t[1] - p[1])
print('closed to %.2f' % dd, flush=True)
base_line = sub()
for n in range(presses):
    for dx, dy in [(0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)]:
        push(dx, dy, 0.12)
        btn('square', True); time.sleep(0.09); btn('square', False)
        time.sleep(0.4)
        s = sub()
        if s and s != base_line:
            print('REACTION: %r' % s[:110], flush=True)
            shot('pw_reaction')
            raise SystemExit(0)
    time.sleep(0.5)
shot('pw_end')
print('no line', flush=True)
d.close()

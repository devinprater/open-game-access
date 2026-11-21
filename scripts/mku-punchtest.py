#!/usr/bin/env python3
"""Reach a tracked actor, punch it, and record everything that changes.

Before and after each punch: the actor's transform block, the subtitle buffer, the
dialogue buffer, and screenshots at +0.15 s and +1.0 s. Usage:
  python3 punchtest.py <actor_hex_addr> [button] [presses]
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

def pos(a):
    r = rd(a, 12); return struct.unpack('<3f', r) if len(r) == 12 else None

def text(a):
    return rd(a, 100).split(b'\0')[0].decode('latin1', 'replace')

def btn(b, v):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: v}}))

def push(x, y, s):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(s)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.12)

def shot(name):
    base = '/mnt/c/Users/Public/'
    for e in ('done', 'png'):
        try: os.remove(base + name + '.' + e)
        except OSError: pass
    open(base + 'shot.req', 'w').write(name)
    t = time.time()
    while time.time() - t < 6:
        if os.path.exists(base + name + '.done'):
            return True
        time.sleep(0.04)
    return False

actor_base = int(sys.argv[1], 16)
actor = actor_base + 0x30
button = sys.argv[2] if len(sys.argv) > 2 else 'square'
presses = int(sys.argv[3]) if len(sys.argv) > 3 else 3
PLAYER = 0x08AED478

table = []
for sx, sy in [(1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)]:
    p0 = pos(PLAYER); push(sx, sy, 0.3); p1 = pos(PLAYER)
    dx, dz = p1[0] - p0[0], p1[2] - p0[2]
    n = math.hypot(dx, dz)
    if n > 1e-3:
        table.append((sx, sy, dx / n, dz / n))

def dist():
    p, a = pos(PLAYER), pos(actor)
    return math.hypot(a[0] - p[0], a[2] - p[2]), p, a

for i in range(40):
    dd, p, a = dist()
    if dd < 1.3:
        break
    ux, uz = (a[0] - p[0]) / dd, (a[2] - p[2]) / dd
    best = max(table, key=lambda t: t[2] * ux + t[3] * uz)
    push(best[0], best[1], 0.25)
dd, p, a = dist()
print('adjacent: dist %.2f' % dd, flush=True)

def snapshot():
    return rd(actor_base, 0x200)

for n in range(presses):
    before = snapshot()
    sb, db = text(0x09106354), text(0x09178568)
    btn(button, True); time.sleep(0.1); btn(button, False)
    time.sleep(0.15)
    shot('punch%d_a' % n)
    time.sleep(0.9)
    shot('punch%d_b' % n)
    after = snapshot()
    sa, da = text(0x09106354), text(0x09178568)
    changed = [i for i in range(0, 0x200, 4) if before[i:i+4] != after[i:i+4]]
    dd, p, a = dist()
    print('press %d: dist %.2f  actor words changed %d  sub %r->%r  dlg %r->%r' % (
        n + 1, dd, len(changed), sb[:30], sa[:40], db[:20], da[:20]), flush=True)
    time.sleep(0.6)
d.close()

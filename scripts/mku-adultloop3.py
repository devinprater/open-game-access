#!/usr/bin/env python3
"""Short-range sweep: only targets within a radius, micro-steps only.

Long turn bursts wander; inside 20 units the measured micro-step loop is stable.

Usage: python3 adultloop3.py [radius] [max_candidates]
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

def u32(a):
    r = rd(a, 4)
    return struct.unpack('<I', r)[0] if len(r) == 4 else None

def f32(a):
    r = rd(a, 4)
    return struct.unpack('<f', r)[0] if len(r) == 4 else 0.0

def pos():
    r = rd(0x08AED478, 12)
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]) if len(r) == 12 else None

def sub():
    return rd(0x09106354, 120).split(b'\0')[0].decode('latin1', 'replace')

def name_at(a):
    p = u32(a)
    if not p or not (0x08800000 <= p < 0x0A000000):
        return None
    raw = rd(p, 32).split(b'\0')[0]
    try:
        t = raw.decode('ascii')
    except Exception:
        return None
    return t if t.startswith('npc_') else None

def model_at(a):
    rec = u32(a + 4)
    if not rec or not (0x08800000 <= rec < 0x0A000000):
        return None
    mp = u32(rec)
    if not mp or not (0x08800000 <= mp < 0x0A000000):
        return None
    raw = rd(mp, 24).split(b'\0')[0]
    try:
        return raw.decode('ascii')
    except Exception:
        return None

def btn(b, v):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: v}}))

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.04)

DIRS = [(0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)]

def micro_walk(tx, tz, stop=1.1, steps=80):
    for j in range(steps):
        p = pos()
        dd = math.hypot(tx - p[0], tz - p[1])
        if dd <= stop:
            return dd
        best = None
        for dx, dy in DIRS:
            push(dx, dy, 0.09)
            p2 = pos()
            d2 = math.hypot(tx - p2[0], tz - p2[1])
            if best is None or d2 < best[0]:
                best = (d2, dx, dy)
        push(best[1], best[2], 0.16)
        if best[0] >= dd:
            push(-best[1], -best[2], 0.18)
    p = pos()
    return math.hypot(tx - p[0], tz - p[1])

def punch_round():
    base = sub()
    for dx, dy in DIRS:
        push(dx, dy, 0.12)
        btn('square', True); time.sleep(0.09); btn('square', False)
        time.sleep(0.38)
        s = sub()
        if s and s != base:
            return s
    return None

radius = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
count = int(sys.argv[2]) if len(sys.argv) > 2 else 4
TOP, S = 0x09151098, 0x810
p0 = pos()
rows = []
for i in range(90):
    a = TOP - i * S
    nm = name_at(a)
    if not nm:
        continue
    model = model_at(a)
    if model not in ('KON_VILL', 'KON_OCC'):
        continue
    x, y, z = f32(a + 0x1C), f32(a + 0x20), f32(a + 0x24)
    if x == 0 and z == 0:
        continue
    dd = math.hypot(x - p0[0], z - p0[1])
    if dd <= radius:
        rows.append((dd, nm, model, x, z))
rows.sort()
print('player (%.1f, %.1f); %d adults within %.0f' % (p0[0], p0[1], len(rows), radius), flush=True)
for dd0, nm, model, x, z in rows[:count]:
    print('%s %s (%.1f, %.1f) %.1f away' % (nm, model, x, z, dd0), flush=True)
    dd = micro_walk(x, z, 1.1, 70)
    print('  at %.2f' % dd, flush=True)
    if dd > 1.8:
        print('  skipped', flush=True)
        continue
    line = punch_round()
    if line:
        print('REACTION %s: %r' % (nm, line[:110]), flush=True)
        break
    print('  no line', flush=True)
d.close()

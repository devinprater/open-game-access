#!/usr/bin/env python3
"""Find the heading: unit float pairs whose angle matches the measured movement.

Dumps RAM, finds every consecutive float pair (x, z) with |(x,z)| ~ 1 and
atan2(x, z) close to the direction a forward push just produced, then turns the
character and dumps again to see which of those pairs rotate.

Usage: python3 findheading.py
"""
import base64, json, math, struct, time, importlib.util
import numpy as np

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=10)
_tk = [0]
BASE, END, CH = 0x08800000, 0x0A000000, 0x10000

def rd(a, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 't%d' % _tk[0]
        d.ws.send(json.dumps({"event": "memory.read", "address": a, "size": n, "ticket": tk}))
        end = time.time() + 6
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

def dump():
    out = bytearray()
    a = BASE
    while a < END:
        n = min(CH, END - a)
        b = rd(a, n)
        out += b + b'\0' * (n - len(b))
        a += n
    return bytes(out)

def pos():
    r = rd(0x08AED478, 12)
    return (struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2])

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.2)

p0 = pos()
push(0, 1, 0.6)
p1 = pos()
mv = (p1[0] - p0[0], p1[1] - p0[1])
n = math.hypot(*mv) or 1.0
M = math.atan2(mv[0] / n, mv[1] / n)
print('forward moved (%.2f, %.2f) -> bearing %.0f deg' % (mv[0] / n, mv[1] / n, math.degrees(M)), flush=True)

buf = dump()
arr = np.frombuffer(buf, dtype='<f4')
x, z = arr[:-1], arr[1:]
norm = x * x + z * z
mask = np.abs(norm - 1.0) < 0.02
ang = np.arctan2(x, z)
diff = np.abs(np.arctan2(np.sin(ang - M), np.cos(ang - M)))
mask &= diff < 0.15
idx = np.where(mask)[0]
print('candidates matching the movement bearing: %d' % idx.size, flush=True)

# turn in place, then see which candidates rotated
push(1, 0, 0.30)
buf2 = dump()
arr2 = np.frombuffer(buf2, dtype='<f4')
changed = []
for i in idx:
    i = int(i)
    a0 = (float(arr[i]), float(arr[i+1]))
    a1 = (float(arr2[i]), float(arr2[i+1]))
    if abs(a0[0] - a1[0]) > 0.05 or abs(a0[1] - a1[1]) > 0.05:
        changed.append((BASE + i * 4, a0, a1))
print('of those, %d rotated when the character turned:' % len(changed), flush=True)
for addr, a0, a1 in changed[:25]:
    print('  %08X (%+.2f,%+.2f) -> (%+.2f,%+.2f)' % (addr, a0[0], a0[1], a1[0], a1[1]))
d.close()

#!/usr/bin/env python3
"""Find drawn actors: 4x4-ish world transforms (orthonormal 3x3 + translation) that move.

Layout seen on Apep's node at 0x09CF97C0: rows of (x, y, z, flag) as 3 floats + word,
then translation floats. This scan looks for any 12 consecutive floats with a unit
3x3 block (stride 4 words) and a translation in the village bounds.

Usage: python3 actorscan.py <wait_seconds>
"""
import base64, json, math, struct, sys, time, importlib.util

sp = importlib.util.spec_from_file_location('c', '/home/devin/oga-work/scripts/psp-ppsspp-client.py')
c = importlib.util.module_from_spec(sp); sp.loader.exec_module(c)
d = c.Debugger(timeout=10)
_tk = [0]

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
        time.sleep(0.03)
    return b''

BASE, END, CH = 0x08800000, 0x0A000000, 0x10000

def dump():
    out = bytearray()
    a = BASE
    while a < END:
        n = min(CH, END - a)
        b = rd(a, n)
        out += b + b'\0' * (n - len(b))
        a += n
    return bytes(out)

def unit(v, tol=0.03):
    return abs(math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]) - 1.0) < tol

def find(buf):
    """Return {addr: (x,y,z)} for transforms: three unit rows at +0,+0x10,+0x20, translation at +0x30."""
    out = {}
    n = len(buf) // 4
    fl = struct.unpack('<%df' % n, buf)
    for i in range(0, n - 14):
        r0 = fl[i:i+3]
        # cheap reject: a unit row has all components within [-1.01, 1.01]
        if not (-1.01 <= r0[0] <= 1.01 and -1.01 <= r0[1] <= 1.01 and -1.01 <= r0[2] <= 1.01):
            continue
        if not unit(r0):
            continue
        r1 = fl[i+4:i+7]
        r2 = fl[i+8:i+11]
        if not (unit(r1) and unit(r2)):
            continue
        t = fl[i+12:i+15]
        if -200 < t[0] < 200 and 90 < t[2] < 200 and 0.0 <= t[1] < 4.0:
            out[BASE + i * 4] = t
    return out

wait = float(sys.argv[1]) if len(sys.argv) > 1 else 2.0
A = find(dump())
time.sleep(wait)
B = find(dump())
print('transforms: %d then %d' % (len(A), len(B)))
moved = []
for addr, t in A.items():
    t2 = B.get(addr)
    if t2 is None:
        continue
    dist = math.hypot(t2[0] - t[0], t2[2] - t[2])
    if 0.15 < dist < 8.0:
        moved.append((addr, t, t2, dist))
print('moved between dumps: %d' % len(moved))
json.dump([(a, t, t2, dd) for a, t, t2, dd in moved], open('/mnt/c/Users/Public/mku-dumps/actors_moved.json', 'w'))
for addr, t, t2, dist in sorted(moved)[:50]:
    print('  %08X  (%7.2f %5.2f %7.2f) -> (%7.2f %5.2f %7.2f)  d=%.2f' % (addr, *t, *t2, dist))
d.close()

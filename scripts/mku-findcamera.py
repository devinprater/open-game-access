#!/usr/bin/env python3
"""Find the camera yaw: unit float pairs that rotate when the camera stick moves.

Dumps RAM, rotates the camera with the right stick, dumps again, and reports
consecutive float pairs whose length is 1 and whose angle moved coherently.

Usage: python3 findcamera.py
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

def push(x, y, s):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": x, "y": y}))
    time.sleep(s)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "right", "x": 0, "y": 0}))
    time.sleep(0.25)

a1 = dump()
push(1, 0, 0.6)
a2 = dump()

f1 = np.frombuffer(a1, dtype='<f4')
f2 = np.frombuffer(a2, dtype='<f4')
n = f1.size
x1, z1 = f1[:-1], f1[1:]
x2, z2 = f2[:-1], f2[1:]
norm1 = x1 * x1 + z1 * z1
norm2 = x2 * x2 + z2 * z2
mask = (np.abs(norm1 - 1.0) < 0.05) & (np.abs(norm2 - 1.0) < 0.05)
idx = np.where(mask)[0]
print('unit pairs present in both dumps: %d' % idx.size, flush=True)

rows = []
for i in idx:
    i = int(i)
    ang1 = math.atan2(float(x1[i]), float(z1[i]))
    ang2 = math.atan2(float(x2[i]), float(z2[i]))
    dA = (ang2 - ang1 + math.pi) % (2 * math.pi) - math.pi
    if abs(dA) > 0.12:
        rows.append((abs(dA), BASE + i * 4, ang1, ang2, float(x1[i]), float(z1[i])))
rows.sort(reverse=True)
print('pairs whose angle moved with the camera: %d' % len(rows), flush=True)
for dA, addr, a1v, a2v, xv, zv in rows[:25]:
    print('  %08X  %.0f deg -> %.0f deg (delta %.0f)  v=(%.2f,%.2f)' % (
        addr, math.degrees(a1v), math.degrees(a2v), math.degrees(dA), xv, zv))
d.close()

#!/usr/bin/env python3
"""Park at a spot, learn every NPC-height draw transform nearby, then punch on proximity.

One dump, a numpy pass for candidate transforms, then a 10 Hz poll that punches the
moment somebody comes within reach — no screenshots and no walking in the loop.

Usage: python3 park.py [seconds] [radius] [scan_radius]
"""
import base64, json, math, struct, sys, time, importlib.util
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
        end = time.time() + 5
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
    time.sleep(0.04)

def scan():
    t0 = time.time()
    buf = dump()
    arr = np.frombuffer(buf, dtype='<f4')
    n = arr.size
    # cheap pre-filter: translation y in an NPC-height band
    ty = arr[13:n - 1]
    idx = np.where((ty > 0.6) & (ty < 1.3))[0]
    cands = []
    p = player()
    for i in idx:
        i = int(i)
        if i < 12 or i + 14 >= n:
            continue
        r0 = arr[i:i+3]; r1 = arr[i+4:i+7]; r2 = arr[i+8:i+11]
        if abs(float(np.linalg.norm(r0)) - 1.0) > 0.03:
            continue
        if abs(float(np.linalg.norm(r1)) - 1.0) > 0.03:
            continue
        if abs(float(np.linalg.norm(r2)) - 1.0) > 0.03:
            continue
        x, y, z = float(arr[i+12]), float(arr[i+13]), float(arr[i+14])
        if not (-300 < x < 300 and -300 < z < 300):
            continue
        dd = math.hypot(x - p[0], z - p[1])
        if dd > scan_radius or dd < 2.0:      # skip the player's own transform
            continue
        cands.append((BASE + 4 * i, x, z, dd))
    print('dump %.1fs, %d NPC-height transforms within %.0f units' % (time.time() - t0, len(cands), scan_radius), flush=True)
    return cands

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 120
radius = float(sys.argv[2]) if len(sys.argv) > 2 else 1.6
scan_radius = float(sys.argv[3]) if len(sys.argv) > 3 else 30

cands = scan()
addrs = [a for a, x, z, dd in cands]
for a, x, z, dd in sorted(cands, key=lambda t: t[3])[:10]:
    print('   %08X (%.1f, %.1f) %.1f away' % (a, x, z, dd), flush=True)

base_line = sub()
t0 = time.time()
hits = 0
while time.time() - t0 < secs:
    p = player()
    near = None
    for a in addrs:
        r = rd(a + 0x30, 12)
        if len(r) != 12:
            continue
        x, z = struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]
        dd = math.hypot(x - p[0], z - p[1])
        if dd <= radius and (near is None or dd < near[0]):
            near = (dd, x, z, a)
    if near:
        dd, x, z, a = near
        print('%5.1f close %08X at %.2f' % (time.time() - t0, a, dd), flush=True)
        hits += 1
        for dx, dy in [(0.0, 1.0), (0.0, -1.0), (1.0, 0.0), (-1.0, 0.0)]:
            push(dx, dy, 0.08)
            btn('square', True); time.sleep(0.08); btn('square', False)
            time.sleep(0.25)
            s = sub()
            if s and s != base_line:
                print('REACTION: %r' % s[:110], flush=True)
                raise SystemExit(0)
        time.sleep(0.2)
    time.sleep(0.06)
print('park done: %d close encounters, no line' % hits)
d.close()

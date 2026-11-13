#!/usr/bin/env python3
"""Ambush walkers: stand still, poll known actor transforms, punch when one is close.

Usage: python3 ambush.py <seconds> [radius]
Reads transform addresses from /mnt/c/Users/Public/mku-dumps/actors_moved.json.
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
    time.sleep(0.05)

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 60
radius = float(sys.argv[2]) if len(sys.argv) > 2 else 1.7

rows = json.load(open('/mnt/c/Users/Public/mku-dumps/actors_moved.json'))
addrs = sorted({r[0] + 0x30 for r in rows if r[1][1] > 0.6})   # NPC height only
print('tracking %d transforms' % len(addrs), flush=True)

base_line = sub()
t0 = time.time()
punches = 0
while time.time() - t0 < secs:
    p = player()
    near = None
    for a in addrs:
        r = rd(a, 12)
        if len(r) != 12:
            continue
        x, z = struct.unpack('<3f', r)[0], struct.unpack('<3f', r)[2]
        dd = math.hypot(x - p[0], z - p[1])
        if dd <= radius and (near is None or dd < near[0]):
            near = (dd, x, z, a)
    if near:
        dd, x, z, a = near
        print('%5.1f near %08X at %.2f' % (time.time() - t0, a - 0x30, dd), flush=True)
        for dx, dy in [(0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)]:
            push(dx, dy, 0.10)
            btn('square', True); time.sleep(0.08); btn('square', False)
            time.sleep(0.30)
            s = sub()
            if s and s != base_line:
                print('REACTION: %r' % s[:110], flush=True)
                raise SystemExit(0)
        punches += 1
        time.sleep(0.3)
    time.sleep(0.10)
print('ambush done, %d close encounters, no line' % punches)
d.close()

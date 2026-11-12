#!/usr/bin/env python3
"""Walk a grid and try to talk to whoever is standing there.

Konquest's villagers carry no marker until they have something to say, and the
old man with the toothache walks the park in A-6. This walks a grid of world
positions, presses Cross at each stop, and reads the dialogue buffer. It stops at
the first real line and prints it.

Closed-loop movement only: the stick is calibrated against the world and
re-calibrated as the camera swings, because movement here is camera-relative.

Usage: python3 mku-talk-search.py [x0 x1 z0 z1 step]
"""
import base64
import json
import pathlib
import struct
import sys
import time
import importlib.util

CLIENT = pathlib.Path(__file__).resolve().parent / 'psp-ppsspp-client.py'
if not CLIENT.exists():
    CLIENT = pathlib.Path('/home/devin/oga-work/scripts/psp-ppsspp-client.py')
spec = importlib.util.spec_from_file_location('c', str(CLIENT))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

d = c.Debugger(timeout=8)
POS, TEXT, COL, PTR = 0x08AED478, 0x09178568, 0x09178554, 0x09178D30
_tk = [0]


def rd(a, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 'q%d' % _tk[0]
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
        time.sleep(0.1)
    return b''


def pos():
    r = rd(POS, 12)
    if len(r) != 12:
        return None
    x, y, z = struct.unpack('<3f', r)
    return x, z


def line():
    raw = rd(TEXT, 200)
    if not raw:
        return ''
    return raw.split(b'\0')[0].decode('latin1', 'replace')


def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.2)


def tap(b):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: True}}))
    time.sleep(0.1)
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: False}}))
    time.sleep(0.5)


def calibrate():
    table = []
    for sx, sy in [(1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)]:
        p0 = pos()
        if p0 is None:
            continue
        push(sx, sy, 0.3)
        p1 = pos()
        if p1 is None:
            continue
        dx, dz = p1[0] - p0[0], p1[1] - p0[1]
        n = (dx * dx + dz * dz) ** 0.5
        if n > 1e-3:
            table.append((sx, sy, dx / n, dz / n, n))
    return table


def goto(tx, tz, table, tol=2.0, budget=26):
    for i in range(budget):
        p = pos()
        if p is None:
            return False
        dx, dz = tx - p[0], tz - p[1]
        n = (dx * dx + dz * dz) ** 0.5
        if n <= tol:
            return True
        ux, uz = dx / n, dz / n
        best, score = None, -2.0
        for sx, sy, wx, wz, mag in table:
            s = wx * ux + wz * uz
            if s > score:
                score, best = s, (sx, sy)
        if best is None:
            return False
        push(best[0], best[1], 0.3)
        if i and i % 6 == 0:
            table[:] = calibrate()
    return False


def main():
    x0, x1, z0, z1, step = (-72.0, -54.0, 140.0, 160.0, 4.0)
    if len(sys.argv) >= 6:
        x0, x1, z0, z1, step = (float(v) for v in sys.argv[1:6])
    table = calibrate()
    if not table:
        print('no calibration; not in free-roam?')
        return
    print('calibrated with %d directions' % len(table), flush=True)

    x = x0
    while x <= x1:
        z = z0
        while z <= z1:
            ok = goto(x, z, table)
            tap('cross')
            time.sleep(0.4)
            txt = line()
            print('%s x%.0f z%.0f -> %r' % ('at' if ok else 'near', x, z, txt[:70]), flush=True)
            if txt and 'Strike me' not in txt and 'step toward' not in txt:
                print('FOUND: %s' % txt, flush=True)
                d.close()
                return
            z += step
        x += step
    print('grid done, nothing said', flush=True)
    d.close()


main()

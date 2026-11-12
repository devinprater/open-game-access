#!/usr/bin/env python3
"""Closed-loop walking for MKU Konquest on the PPSSPP bench.

Open-loop analog pushes do not work: movement is camera-relative and the camera
swings as you turn, so the same stick direction is a different world direction
two seconds later. This calibrates the stick against the world once, then walks
to a world position by re-measuring after every pulse.

Usage: python3 mku-walk.py <x> <z> [tolerance]
Prints each pulse so a failed approach is visible rather than silent.
"""
import base64
import json
import pathlib
import struct
import sys
import time
import importlib.util

CLIENT = pathlib.Path('/home/devin/oga-work/scripts/psp-ppsspp-client.py')
spec = importlib.util.spec_from_file_location('c', str(CLIENT))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

d = c.Debugger(timeout=8)
POS = 0x08AED478
_tk = [0]


def rd(a, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 'w%d' % _tk[0]
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


def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.25)


DIRS = [(1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0)]


def calibrate():
    """Measure what each stick direction does in world space, right now."""
    table = []
    for sx, sy in DIRS:
        p0 = pos()
        if p0 is None:
            continue
        push(sx, sy, 0.35)
        p1 = pos()
        if p1 is None:
            continue
        dx, dz = p1[0] - p0[0], p1[1] - p0[1]
        n = (dx * dx + dz * dz) ** 0.5
        if n < 1e-3:
            continue
        table.append((sx, sy, dx / n, dz / n, n))
    return table


def step_toward(table, tx, tz):
    p = pos()
    if p is None:
        return None
    dx, dz = tx - p[0], tz - p[1]
    n = (dx * dx + dz * dz) ** 0.5
    if n < 1e-3:
        return 0.0
    ux, uz = dx / n, dz / n
    best, score = None, -2.0
    for sx, sy, wx, wz, mag in table:
        s = wx * ux + wz * uz
        if s > score:
            score, best = s, (sx, sy, mag)
    if best is None:
        return None
    sx, sy, mag = best
    push(sx, sy, 0.30)
    return n


def main():
    tx, tz = float(sys.argv[1]), float(sys.argv[2])
    tol = float(sys.argv[3]) if len(sys.argv) > 3 else 1.5
    table = calibrate()
    print('calibration:')
    for sx, sy, wx, wz, mag in table:
        print('  stick (%.0f,%.0f) -> world (%.2f, %.2f)  %.2f u/pulse' % (sx, sy, wx, wz, mag))
    if not table:
        print('no calibration; is the game in free-roam?')
        return
    for i in range(60):
        left = step_toward(table, tx, tz)
        if left is None:
            print('lost the position word')
            break
        p = pos()
        print('%2d remaining %.1f at (%.1f, %.1f)' % (i, left, p[0], p[1]), flush=True)
        if left <= tol:
            print('arrived')
            break
        if i and i % 8 == 0:
            table = calibrate()   # the camera has swung; re-learn the mapping
    d.close()


main()

#!/usr/bin/env python3
"""Stand still and punch in four directions, watching for a subtitle line.

Useful where a walker passes a fixed spot: no navigation, just repeated punches.

Usage: python3 patrol.py [seconds] [interval]
"""
import base64, json, struct, sys, time, importlib.util

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

def sub():
    return rd(0x09106354, 120).split(b'\0')[0].decode('latin1', 'replace')

def btn(b, v):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: v}}))

def push(x, y, secs):
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": x, "y": y}))
    time.sleep(secs)
    d.ws.send(json.dumps({"event": "input.analog.send", "stick": "left", "x": 0, "y": 0}))
    time.sleep(0.04)

secs = float(sys.argv[1]) if len(sys.argv) > 1 else 90
interval = float(sys.argv[2]) if len(sys.argv) > 2 else 1.4
base_line = sub()
t0 = time.time()
rounds = 0
while time.time() - t0 < secs:
    for dx, dy in [(0.0, 1.0), (1.0, 0.0), (0.0, -1.0), (-1.0, 0.0)]:
        push(dx, dy, 0.08)
        btn('square', True); time.sleep(0.08); btn('square', False)
        time.sleep(0.22)
        s = sub()
        if s and s != base_line:
            print('REACTION after %.1fs: %r' % (time.time() - t0, s[:110]), flush=True)
            raise SystemExit(0)
    rounds += 1
    time.sleep(interval)
print('patrol done: %d rounds, no line' % rounds)
d.close()

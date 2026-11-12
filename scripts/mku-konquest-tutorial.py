#!/usr/bin/env python3
"""Play Bo' Rai Cho's tutorial to the end, by watching the game rather than guessing.

The tutorial is flaky for direction+button moves: a set that lands on one attempt
whiffs on the next. So this driver does not run a fixed script. It reads the red
objective line and the lesson counter, fires the input set for whichever lesson is
up, and retries on the TRY AGAIN prompt until the objective changes. It stops when
the fight engine lets go (flag B clears) and the village position appears.

One connection for the whole run; PPSSPP's debugger degrades under short ones.

Usage: python3 mku-konquest-tutorial.py [timeout_seconds]
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

d = c.Debugger(timeout=10)

TEXT, COL, PTR = 0x09178568, 0x09178554, 0x09178D30
FLAG_A, FLAG_B, POS = 0x08AF69C8, 0x08B123DC, 0x08AED478
DASH = 0x091796D4

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
        time.sleep(0.1)
    return b''


def u32(a):
    r = rd(a, 4)
    return struct.unpack('<I', r)[0] if len(r) == 4 else None


def line():
    raw = rd(TEXT, 200)
    if not raw:
        return ''
    return raw.split(b'\0')[0].decode('latin1', 'replace')


def btn(b, down):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: down}}))


def tap(b, down=0.1, up=0.45):
    btn(b, True)
    time.sleep(down)
    btn(b, False)
    time.sleep(up)


def dtap(b):
    btn(b, True)
    time.sleep(0.045)
    btn(b, False)
    time.sleep(0.055)
    btn(b, True)
    time.sleep(0.045)
    btn(b, False)
    time.sleep(0.45)


def combo(direction, b):
    btn(direction, True)
    time.sleep(0.08)
    btn(b, True)
    time.sleep(0.1)
    btn(b, False)
    time.sleep(0.06)
    btn(direction, False)
    time.sleep(0.45)


def walk_in(secs=0.7):
    btn('right', True)
    time.sleep(secs)
    btn('right', False)
    time.sleep(0.3)


def do_lesson(text):
    """Fire the input set for the lesson this objective line belongs to."""
    if 'step toward me three times' in text or 'step away from me three times' in text:
        for _ in range(3):
            tap('right', 0.12, 0.55)
        for _ in range(3):
            tap('left', 0.12, 0.55)
        return 'steps'
    if 'Backward Dashes' in text or 'Backward Dash' in text:
        for _ in range(3):
            dtap('left')
        return 'dashes'
    if 'four different basic attacks' in text:
        walk_in()
        for b in ('square', 'triangle', 'cross', 'circle'):
            tap(b, 0.1, 0.9)
        return 'four attacks'
    if 'high punch attacks' in text:
        walk_in(0.6)
        tap('triangle', 0.1, 0.9)
        combo('up', 'triangle')
        return 'high punches'
    if 'high and low attacks' in text:
        walk_in(0.6)
        tap('cross', 0.1, 0.9)
        combo('left', 'cross')
        return 'high/low'
    if 'two high attacks and two low attacks' in text:
        walk_in(0.7)
        tap('square', 0.1, 0.9)
        tap('triangle', 0.1, 0.9)
        combo('down', 'square')
        combo('down', 'cross')
        return 'mix'
    return None


deadline = time.time() + (float(sys.argv[1]) if len(sys.argv) > 1 else 900)
t0 = time.time()
last_text = ''
quiet_free = 0
played = []

while time.time() < deadline:
    a, b = u32(FLAG_A), u32(FLAG_B)
    col, ptr = u32(COL), u32(PTR)
    txt = line()

    if a == 1 and b == 0:
        quiet_free += 1
        if quiet_free >= 4:
            x, y, z = struct.unpack('<3f', rd(POS, 12) or b'\0' * 12)
            print('%6.1f FREE ROAM at (%.1f, %.1f, %.1f)' % (time.time() - t0, x, y, z), flush=True)
            break
        time.sleep(0.6)
        continue
    quiet_free = 0

    if ptr and col == 0xC8C8C8FF:
        # a dialogue box: the TRY AGAIN prompt and the lesson preambles both
        # sit behind Cross, and Cross is never an attack outside a bout
        if 'Try again' in txt:
            print('%6.1f retry prompt: %s' % (time.time() - t0, txt[:60]), flush=True)
        tap('cross', 0.1, 0.8)
        time.sleep(0.6)
        continue

    if txt and txt != last_text:
        print('%6.1f objective: %s' % (time.time() - t0, txt[:90]), flush=True)
        last_text = txt

    played_now = do_lesson(txt) if txt else None
    if played_now:
        played.append(played_now)
        print('%6.1f   -- fired %s (#%d)' % (time.time() - t0, played_now, len(played)), flush=True)
        time.sleep(0.8)
    else:
        time.sleep(0.6)

print('%6.1f done; lessons fired: %s' % (time.time() - t0, ', '.join(played) or 'none'), flush=True)
d.close()

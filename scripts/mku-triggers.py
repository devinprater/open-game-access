#!/usr/bin/env python3
"""mku-triggers.py — read the live Konquest trigger table.

The Earthrealm village's named triggers are a linked list of 0x90-byte records.
Each record holds:
  +0x00  previous record
  +0x04  next record
  +0x18  pointer to the data record
  +0x1C  pointer to the name string ("trigger_dojo_1", "nightwolf_trigger", ...)

The data record carries the world position and radius:
  +0x08 x, +0x0C y, +0x10 z, +0x14 radius

Measured 2026-11-12: 40 triggers read out, e.g. dojo_0 (-53.95, 154.98) r=3,
nightwolf_trigger (-44.37, 169.68) r=12, trigger_park_tease_2 (-62.16, 156.29)
r=3, and the four classroom triggers dojo_1..4 spread across the village.

The anchor moves between boots, so the table is found by name: the string
"trigger_dojo_1" appears once in the name bank and one word points at it.

Usage: python3 mku-triggers.py
"""
import base64
import importlib.util
import json
import struct
import sys
import time
from pathlib import Path

CLIENT = Path('/home/devin/oga-work/scripts/psp-ppsspp-client.py')
if not CLIENT.exists():
    CLIENT = Path(__file__).resolve().parent / 'psp-ppsspp-client.py'
spec = importlib.util.spec_from_file_location('c', str(CLIENT))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

BASE, END, CHUNK = 0x08800000, 0x0A000000, 0x10000
d = c.Debugger(timeout=10)
_tk = [0]


def rd(addr, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 't%d' % _tk[0]
        d.ws.send(json.dumps({"event": "memory.read", "address": addr, "size": n, "ticket": tk}))
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
        time.sleep(0.05)
    return b''


def dump():
    out = bytearray()
    addr = BASE
    while addr < END:
        n = min(CHUNK, END - addr)
        b = rd(addr, n)
        out += b + b'\0' * (n - len(b))
        addr += n
    return bytes(out)


ram = dump()


def u32(addr):
    o = addr - BASE
    return struct.unpack_from('<I', ram, o)[0] if 0 <= o < len(ram) - 4 else None


def f32(addr):
    o = addr - BASE
    return struct.unpack_from('<f', ram, o)[0] if 0 <= o < len(ram) - 4 else None


def cstr(addr, n=40):
    o = addr - BASE
    if not (0 <= o < len(ram)):
        return None
    raw = ram[o:o + n].split(b'\0')[0]
    try:
        t = raw.decode('ascii')
    except Exception:
        return None
    return t if t and all(32 <= ord(ch) < 127 for ch in t) else None


anchor = ram.find(b'trigger_dojo_1')
if anchor < 0:
    print('trigger name bank not in RAM (not in a village scene?)')
    d.close()
    raise SystemExit(1)
name_addr = BASE + anchor
records = [BASE + o for o in range(0, len(ram) - 4, 4)
           if struct.unpack_from('<I', ram, o)[0] == name_addr]
if not records:
    print('no record points at %08X' % name_addr)
    d.close()
    raise SystemExit(1)
cur = records[0] - 0x1C
seen = set()
print('%-34s %10s %6s %10s %6s' % ('trigger', 'x', 'y', 'z', 'r'))
while cur and cur not in seen and len(seen) < 200:
    seen.add(cur)
    name = cstr(u32(cur + 0x1C) or 0)
    data = u32(cur + 0x18)
    if name and data:
        print('%-34s %10.2f %6.2f %10.2f %6.2f' % (
            name, f32(data + 8), f32(data + 12), f32(data + 16), f32(data + 20)))
    cur = u32(cur)
print('%d triggers' % len(seen))
d.close()

#!/usr/bin/env python3
"""mku-npcs.py — read the live Konquest NPC table on the PPSSPP bench.

Every village NPC has a record with a name pointer and a world position. The
table is anchored at npc_apep and strides DOWNWARD by 0x810:

  +0x00  name pointer, into the entity-name table at 0x09D9D6F0
  +0x14  state word (4, 0x850, 0x0002000C seen; changes as NPCs spawn)
  +0x1C  x float        +0x20  y float (0 when not spawned)   +0x24  z float
  +0x40  second position copy
  +0x4C  radius (5.66 for Apep, 1.38 for ordinary villagers)

Usage: python3 mku-npcs.py [count] [player_x player_z]
With a player position it prints the distance and bearing, so a reader can
say "Apep, 12 units south" instead of reading a compass off the screen.
"""
import base64
import importlib.util
import json
import math
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

TOP, STRIDE = 0x09151098, 0x810
POS, FLAGS = 0x08AED478, 0x08AF69C8

d = c.Debugger(timeout=8)
_tk = [0]


def rd(addr, n):
    for _ in range(3):
        _tk[0] += 1
        tk = 't%d' % _tk[0]
        d.ws.send(json.dumps({"event": "memory.read", "address": addr, "size": n, "ticket": tk}))
        end = time.time() + 4
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


def u32(addr):
    r = rd(addr, 4)
    return struct.unpack('<I', r)[0] if len(r) == 4 else None


def name_at(addr):
    if not addr or not (0x08800000 <= addr < 0x0A000000):
        return None
    raw = rd(addr, 40).split(b'\0')[0]
    try:
        t = raw.decode('ascii')
    except Exception:
        return None
    return t if t and all(32 <= ord(ch) < 127 for ch in t) else None


def bearing(dx, dz):
    """Compass words as the HUD draws them: north is +z, east is +x."""
    deg = math.degrees(math.atan2(dx, dz)) % 360.0
    names = ['north', 'north east', 'east', 'south east',
             'south', 'south west', 'west', 'north west']
    return names[int((deg + 22.5) % 360 // 45)]


count = int(sys.argv[1]) if len(sys.argv) > 1 else 22
if len(sys.argv) > 3:
    px, pz = float(sys.argv[2]), float(sys.argv[3])
else:
    px, pz = struct.unpack('<3f', rd(POS, 12))[0::2]

if u32(FLAGS) != 1:
    print('not in Konquest (flag A = %s)' % u32(FLAGS))
    d.close()
    raise SystemExit(1)

print('player at (%.1f, %.1f)' % (px, pz))
for i in range(count):
    base = TOP - i * STRIDE
    nm = name_at(u32(base))
    if not nm:
        continue
    x, y, z = struct.unpack('<3f', rd(base + 0x1C, 12))
    radius = struct.unpack('<f', rd(base + 0x4C, 4))[0]
    state = u32(base + 0x14)
    spawned = abs(y) > 1e-3
    dist = math.hypot(x - px, z - pz)
    print('%-22s %s at (%.1f, %.1f)%s  r=%.2f  state=%08X' % (
        nm, 'spawned' if spawned else 'away', x, z,
        ('  %4.1f units %s' % (dist, bearing(x - px, z - pz))) if spawned else '',
        radius, state))
d.close()

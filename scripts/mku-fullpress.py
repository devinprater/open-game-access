#!/usr/bin/env python3
"""Full-RAM press diff.

Usage: python3 fullpress.py <tag> [button]

Dumps 24 MB three times: idle, idle again, and with <button> held for the whole
dump. Words equal across the two idle dumps but different under the hold are
the press reaction. Result goes to /mnt/c/Users/Public/mku-dumps/press_<tag>.json.
"""
import base64, json, struct, sys, time, importlib.util

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

def btn(b, down):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: down}}))

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

tag = sys.argv[1]
button = sys.argv[2] if len(sys.argv) > 2 else 'square'
i1 = dump()
i2 = dump()
btn(button, True)
time.sleep(0.3)
h = dump()
btn(button, False)
time.sleep(1.0)
i3 = dump()

hits = []
for off in range(0, len(i1), 4):
    a, b, x, z = i1[off:off+4], i2[off:off+4], h[off:off+4], i3[off:off+4]
    # steady while idle, different under the hold, and back to baseline after release
    if a == b and a != x and z == a:
        hits.append((BASE + off, a.hex(), x.hex()))
print('%s: %d words changed only under %s' % (tag, len(hits), button))
json.dump(hits, open('/mnt/c/Users/Public/mku-dumps/press_%s.json' % tag, 'w'))
for addr, before, after in hits[:40]:
    print('  %08X %s -> %s' % (addr, before, after))
d.close()

#!/usr/bin/env python3
"""mk.py - tiny live driver for MKU on Windows PPSSPP.
  mk.py tap cross [left ...]   tap buttons with gaps
  mk.py hold btn secs          hold a button
  mk.py shot name              screenshot -> C:\\Users\\Public\\name.png
  mk.py rd 0xADDR [n]          read n u32 words
  mk.py rdf 0xADDR [n]         read floats
  mk.py dump file              dump user RAM 0x08800000-0x0A000000"""
import pathlib
import sys, time, struct, subprocess, json
import importlib.util
spec = importlib.util.spec_from_file_location('c', str(pathlib.Path(__file__).resolve().parent / 'psp-ppsspp-client.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
a = sys.argv[1:]
cmd = a[0]
if cmd == 'shot':
    subprocess.run(['python.exe', r'C:\Users\Public\psp-shot.py', r'C:\Users\Public\%s.png' % a[1], '--title', 'PPSSPP'])
    sys.exit(0)
d = c.Debugger(timeout=4)


def btn(b, down):
    d.ws.send(json.dumps({"event": "input.buttons.send", "buttons": {b: down}}))


if cmd == 'tap':
    for b in a[1:]:
        btn(b, True); time.sleep(0.12)
        btn(b, False); time.sleep(0.35)
elif cmd == 'hold':
    btn(a[1], True); time.sleep(float(a[2])); btn(a[1], False)
elif cmd == 'dump':
    out = open(a[1], 'wb')
    lo = int(a[2], 0) if len(a) > 2 else 0x08800000
    hi = int(a[3], 0) if len(a) > 3 else 0x0A000000
    base = lo
    while base < hi:
        chunk = b''
        for _ in range(3):
            chunk = d.read(base, 0x40000)
            if len(chunk) == 0x40000:
                break
            time.sleep(0.3)
        out.write(chunk + bytes(0x40000 - len(chunk)))
        base += 0x40000
    out.close()
elif cmd in ('rd', 'rdf'):
    addr = int(a[1], 0)
    n = int(a[2]) if len(a) > 2 else 1
    raw = d.read(addr, 4 * n)
    for i in range(n):
        w = raw[4 * i:4 * i + 4]
        if len(w) < 4:
            break
        print('%08X: ' % (addr + 4 * i),
              ('%.3f' % struct.unpack('<f', w)[0]) if cmd == 'rdf' else '%08X' % struct.unpack('<I', w)[0])
d.close()

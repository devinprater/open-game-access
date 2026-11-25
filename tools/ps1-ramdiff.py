#!/usr/bin/env python3
"""ps1-ramdiff.py — helpers for measuring a PlayStation reader's memory map.

Works on the .ram (2 MiB main RAM) and .ppm files tools/ps1-map-harness.cpp writes.

  ps1-ramdiff.py png DIR            convert every .ppm in DIR to .png (so a screenshot can be viewed)
  ps1-ramdiff.py diff A.ram B.ram [WIDTH]     list the aligned values that differ (first 200)
  ps1-ramdiff.py find A.ram VALUE [WIDTH]     every address holding VALUE
  ps1-ramdiff.py peek A.ram ADDR [WIDTH] [COUNT]
  ps1-ramdiff.py narrow WIDTH A.ram:VALUE B.ram:VALUE ...   addresses that held each VALUE in each dump
  ps1-ramdiff.py same WIDTH A.ram B.ram ...   addresses whose value is identical in all dumps
Addresses are shown as KSEG0 (0x80......), the way the reader spells them.
"""
import struct, sys, zlib, glob, os

def load(p):
    with open(p, 'rb') as f:
        return f.read()

def fmt(w):
    return {1: 'B', 2: '<H', 4: '<I'}[w]

def vals(buf, w):
    n = len(buf) // w
    return struct.unpack('<%d%s' % (n, {1: 'B', 2: 'H', 4: 'I'}[w]), buf[:n * w])

def png(ppm):
    with open(ppm, 'rb') as f:
        d = f.read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    px = parts[3]
    raw = b''.join(b'\x00' + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, c):
        x = struct.pack('>I', len(c)) + t + c
        return x + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    with open(ppm[:-4] + '.png', 'wb') as f:
        f.write(out)

def main(a):
    c = a[0]
    if c == 'png':
        for p in glob.glob(os.path.join(a[1], '*.ppm')):
            png(p)
    elif c == 'diff':
        w = int(a[3]) if len(a) > 3 else 2
        x, y = vals(load(a[1]), w), vals(load(a[2]), w)
        n = 0
        for i, (p, q) in enumerate(zip(x, y)):
            if p != q:
                print('0x%08X  %d -> %d' % (0x80000000 + i * w, p, q))
                n += 1
                if n >= 200:
                    break
    elif c == 'find':
        w = int(a[3]) if len(a) > 3 else 2
        v = int(a[2], 0)
        for i, p in enumerate(vals(load(a[1]), w)):
            if p == v:
                print('0x%08X' % (0x80000000 + i * w))
    elif c == 'peek':
        w = int(a[3]) if len(a) > 3 else 1
        cnt = int(a[4]) if len(a) > 4 else 16
        b = load(a[1])
        o = (int(a[2], 0) & 0x1FFFFF)
        print(' '.join(('%0' + str(w * 2) + 'X') % struct.unpack_from(fmt(w), b, o + k * w)[0] for k in range(cnt)))
    elif c == 'narrow':
        w = int(a[1])
        sets = None
        for spec in a[2:]:
            p, v = spec.rsplit(':', 1)
            v = int(v, 0)
            s = set(i for i, q in enumerate(vals(load(p), w)) if q == v)
            sets = s if sets is None else sets & s
        for i in sorted(sets)[:300]:
            print('0x%08X' % (0x80000000 + i * w))
        print(len(sets), 'match')
    elif c == 'same':
        w = int(a[1])
        vs = [vals(load(p), w) for p in a[2:]]
        n = 0
        for i in range(len(vs[0])):
            if all(v[i] == vs[0][i] for v in vs):
                n += 1
        print(n, 'aligned values identical')

main(sys.argv[1:])

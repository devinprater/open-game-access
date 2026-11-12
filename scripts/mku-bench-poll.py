#!/usr/bin/env python3
"""poll.py SECONDS [lo hi]: log every change in a small RAM window with a timestamp."""
import pathlib
import sys, time, struct, importlib.util
spec = importlib.util.spec_from_file_location('c', str(pathlib.Path(__file__).resolve().parent / 'psp-ppsspp-client.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
secs = float(sys.argv[1])
lo = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x08D92990
hi = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x08D92B10
d = c.Debugger(timeout=4)
prev = None
t0 = time.time()
while time.time() - t0 < secs:
    raw = d.read(lo, hi - lo)
    if len(raw) == hi - lo:
        if prev is None:
            print('%5.1f initial' % (time.time() - t0))
        else:
            ch = [(lo + i, struct.unpack_from('<I', prev, i)[0], struct.unpack_from('<I', raw, i)[0])
                  for i in range(0, len(raw), 4)
                  if prev[i:i + 4] != raw[i:i + 4]]
            ch = [x for x in ch if x[0] != 0x08D92AF0]
            if ch:
                print('%5.1f ' % (time.time() - t0) + ' '.join('%X:%X>%X' % x for x in ch[:14]), flush=True)
        prev = raw
    time.sleep(0.2)
d.close()

#!/usr/bin/env python3
"""bt-snap.py -- full readable-RAM snapshot to file (for HP differential hunts)."""
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
pp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pp)

BASE = 0x08800000
SIZE = 0x01800000  # 24 MB
CHUNK = 0x100000   # 1 MB per read


def main():
    out = sys.argv[1]
    c = pp.Debugger()
    t0 = time.time()
    with open(out, "wb") as f:
        for off in range(0, SIZE, CHUNK):
            b = c.read(BASE + off, CHUNK)
            if b is None or len(b) != CHUNK:
                print("SHORT at 0x%X" % off, flush=True)
                b = bytes(CHUNK)
            f.write(b)
    print("wrote %s in %.1fs" % (out, time.time() - t0), flush=True)
    c.close()


if __name__ == "__main__":
    main()

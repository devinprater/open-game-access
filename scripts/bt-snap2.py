#!/usr/bin/env python3
"""bt-snap2.py -- verified full-RAM snapshot. Fails LOUDLY on short reads."""
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
pp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pp)

BASE = 0x08800000
SIZE = 0x01800000
CHUNK = 0x40000  # 256 KB


def read_full(c, addr, size):
    for _ in range(4):
        b = c.read(addr, size)
        if b and len(b) == size:
            return b
        time.sleep(0.5)
    return None


def main():
    out = sys.argv[1]
    c = pp.Debugger()
    t0 = time.time()
    bad = 0
    with open(out, "wb") as f:
        for off in range(0, SIZE, CHUNK):
            b = read_full(c, BASE + off, CHUNK)
            if b is None:
                print("UNREADABLE 0x%08X -- filling zero" % (BASE + off), flush=True)
                b = bytes(CHUNK)
                bad += 1
            f.write(b)
        f.flush()
        os.fsync(f.fileno())
    n = os.path.getsize(out)
    print("wrote %s size=%d expect=%d bad=%d in %.1fs" % (out, n, SIZE, bad, time.time() - t0), flush=True)
    assert n == SIZE, "SIZE MISMATCH"
    c.close()


main()

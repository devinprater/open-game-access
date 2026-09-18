#!/usr/bin/env python3
"""psp-find-wrapping.py -- find a press-driven field that WRAPS (the decisive cursor filter).

WHY THIS FILTER
    Sections 34-37 established that a press-driven field is common (press counters, event logs, growing
    list indices) but a SELECTION CURSOR must do one extra thing: **return to a value it already held**
    once the presses exceed the number of rows. The one-hot walk found at 0x08C023BC failed exactly this
    -- it reached index 20 across 22 presses while the visible menu wrapped at ~2-6 rows.

    So instead of reporting everything that moves, this reports only words whose series REPEATS with a
    short period. That is a positive test for a wrapping index, and it is cheap to state: over N presses
    of a K-row menu, a cursor shows period K (or 2K for up/down pairs).

ALL THREE INSTRUMENT FIXES APPLIED (sections 34-36)
    two connections (reads / input), settle before capture, and a refusal guard if no press moves the
    display. Sampling is set past the expected number of rows.

USAGE
    python scripts/psp-find-wrapping.py --press down --lo 0x08BE0000 --hi 0x08C20000 --rounds 20 --maxperiod 12
"""
import argparse
import importlib.util
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
S = os.path.join(TMP, "wrap")


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def grab(tag):
    os.makedirs(S, exist_ok=True)
    p = os.path.join(S, "%s.png" % tag)
    try:
        if os.path.exists(p):
            os.remove(p)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), p], capture_output=True)
    return p if os.path.exists(p) and os.path.getsize(p) > 1000 else None


def pixdiff(p1, p2):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(p1).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(p2).convert("RGB")).astype(np.int16)
    return int((np.abs(a - b).max(axis=2) > 16).sum())


def read_region(d, lo, hi, chunk=1 << 20):
    out = bytearray()
    pos = lo
    while pos < hi:
        b = d.read(pos, min(chunk, hi - pos))
        out.extend(b if b else b"\x00" * min(chunk, hi - pos))
        pos += chunk
    return bytes(out)


def shortest_period(vals, maxp):
    """Return p if vals[i] == vals[i+p] for all valid i (p <= maxp), else None."""
    n = len(vals)
    for p in range(1, maxp + 1):
        if n < 2 * p + 1:
            break
        # require at least 2 full repetitions plus one
        if all(vals[i] == vals[i + p] for i in range(n - p)):
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08BE0000)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x08C20000)
    ap.add_argument("--rounds", type=int, default=20)
    ap.add_argument("--maxperiod", type=int, default=12)
    ap.add_argument("--settle", type=float, default=1.3)
    a = ap.parse_args()

    pp = load_client()
    reader = pp.Debugger()
    presser = pp.Debugger()
    print("game:", reader.status().get("game", {}).get("title"))
    print("region 0x%08X-0x%08X   presses=%d   maxperiod=%d"
          % (a.lo, a.hi, a.rounds, a.maxperiod))
    print()

    def state(tag):
        w = read_region(reader, a.lo, a.hi)
        time.sleep(a.settle)
        p = grab(tag)
        return p, list(struct.unpack_from("<%di" % (len(w) // 4), w, 0))

    samples = []
    p0, w0 = state("s0")
    samples.append((p0, w0))
    moved = 0
    for i in range(1, a.rounds + 1):
        presser.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}'
                        % a.press)
        time.sleep(a.settle)
        p, w = state("s%d" % i)
        if samples[-1][0] and p and pixdiff(samples[-1][0], p):
            moved += 1
        samples.append((p, w))
        print("  sample %-2d" % i, end="\r", flush=True)

    print("presses that moved the display: %d/%d" % (moved, a.rounds))
    if moved == 0:
        print("REFUSING: no press moved the display.")
        reader.close(); presser.close()
        return 2

    print()
    print("=== words whose series REPEATS (a wrapping index) ===")
    width = min(len(s[1]) for s in samples)
    hits = []
    for i in range(width):
        vals = [s[1][i] for s in samples]
        if len(set(vals)) == 1:
            continue
        p = shortest_period(vals, a.maxperiod)
        if p:
            hits.append((a.lo + i * 4, p, vals))
    if hits:
        for addr, p, vals in hits[:40]:
            print("  0x%08X  period=%-3d %s" % (addr, p, vals))
        print("   -> %d wrapping field(s)" % len(hits))
    else:
        print("   NONE: no word in this region repeats within period %d" % a.maxperiod)
        print("   => the selection index is not in this range (or the menu's rows exceed maxperiod)")

    reader.close(); presser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

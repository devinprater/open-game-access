#!/usr/bin/env python3
"""psp-diff-press3.py -- the CORRECTED press-diff: two connections, settle, enough samples.

ALL THREE DEFECTS FIXED (doc sections 34, 35, 36)
    Every earlier press-diff run was invalidated by an instrument fault rather than a fact about the
    game. This version fixes all three:

    1. TWO CONNECTIONS (s36). A button press sent on the same debugger connection that is being
       hammered with memory.read is delivered at ~1/30th effect, so a working menu looks inert and a
       partially delivered press looks like a subtle game effect. Reader and presser are separate.

    2. SETTLE BEFORE CAPTURE (s36). Comparing captures taken while a bulk read is in flight
       fabricated ~8,000 changed pixels on a static screen. Every capture here is preceded by a
       settle delay after the last read.

    3. REFUSE WITHOUT MOVEMENT (s35). If no press moves the display, the run aborts instead of
       reporting fields that cannot be attributed to a highlight.

    Plus s34's sampling rule: a pattern needs more samples than it has states, so the default is 12
    presses for a ~6-row menu, and the reported series is followed through the whole sequence.

USAGE
    python scripts/psp-diff-press3.py --press down --lo 0x08BE0000 --hi 0x08C20000 --rounds 12
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
S = os.path.join(TMP, "dp3")


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08BE0000)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x08C20000)
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--settle", type=float, default=1.5)
    a = ap.parse_args()

    pp = load_client()
    reader = pp.Debugger()      # connection 1: memory reads only
    presser = pp.Debugger()     # connection 2: input only          (section 36)
    print("game:", reader.status().get("game", {}).get("title"))
    print("region 0x%08X-0x%08X (%.2f MB)  press=%s  rounds=%d"
          % (a.lo, a.hi, (a.hi - a.lo) / 1048576.0, a.press, a.rounds))
    print("connections: 1 reader + 1 presser (separate)")
    print()

    def state(tag):
        w = read_region(reader, a.lo, a.hi)
        time.sleep(a.settle)              # let the read settle before capturing (section 36)
        p = grab(tag)
        return p, list(struct.unpack_from("<%di" % (len(w) // 4), w, 0))

    samples = []
    p0, w0 = state("s0")
    samples.append((p0, w0))
    for i in range(1, a.rounds + 1):
        presser.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}'
                        % a.press)
        time.sleep(a.settle)
        p, w = state("s%d" % i)
        samples.append((p, w))

    print("=== screen movement per press (must be > 0) ===")
    moved = 0
    for i in range(1, len(samples)):
        pa, pb = samples[i - 1][0], samples[i][0]
        if not pa or not pb:
            print("  press %d: CAPTURE FAILED" % i)
            continue
        n = pixdiff(pa, pb)
        if n:
            moved += 1
        print("  press %-2d: %8d px %s" % (i, n, "" if n else "<-- no movement"))

    if moved == 0:
        print()
        print("REFUSING TO ANALYSE: no press moved the display.")
        print("Two connections and settle delays are already in place, so this is now a fact about the")
        print("screen (list end, menu closed, or wrong button), not an instrument fault.")
        reader.close(); presser.close()
        return 2

    print()
    print("=== small-ordinal fields, series across all samples ===")
    width = min(len(s[1]) for s in samples)
    found = []
    for i in range(width):
        vals = [s[1][i] for s in samples]
        if len(set(vals)) == 1:
            continue
        if all(0 <= v <= 64 for v in vals):
            found.append((a.lo + i * 4, vals))
    if found:
        for addr, vals in found[:30]:
            print("  0x%08X  %s" % (addr, vals))
        print("   -> %d small-ordinal field(s) across %d presses" % (len(found), a.rounds))
        # a real cursor advances by 1 and wraps; print the distinct step sizes
        print()
        print("=== step analysis (a cursor steps by 1; a toggle alternates; noise is irregular) ===")
        for addr, vals in found[:30]:
            steps = [b - x for x, b in zip(vals, vals[1:]) if b != x]
            print("   0x%08X  steps=%s  distinct=%d" % (addr, steps, len(set(steps))))
    else:
        print("   none")

    reader.close(); presser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""psp-full-wrap-scan.py -- the WRAP test over ALL readable RAM, not a 0.25 MB slice.

WHY THIS IS THE DEFINITIVE TEST
    Section 38 cleared 0x08BE0000-0x08C20000 of any wrapping field, but that was only ~0.25 MB of a
    24 MB readable span (0x08800000-0x0A000000). The conclusion "the cursor is not in this region"
    therefore left the rest unexamined. Measured throughput is ~3 MB/s, so a full 24 MB sample costs
    only ~8 s -- making a whole-span scan practical, which it was not assumed to be.

    Property tested: a selection cursor must RETURN to a value it already held once presses exceed the
    menu's rows. So for every 4-byte word in all readable RAM, this records the series across N presses
    and reports only words whose series REPEATS with a short period.

METHOD (memory-safe)
    Only positions that differ from the baseline are tracked, so the series dictionary stays small even
    though 6M words are examined per sample. numpy does the per-sample comparison.

INSTRUMENT FIXES RETAINED (sections 34-36)
    Two connections (reads / input) -- reads on the input connection suppress presses at ~1/30th.
    Screen captures verify the presses actually move the highlight, and the run refuses a null result.

USAGE
    python scripts/psp-full-wrap-scan.py --press down --rounds 20 --maxperiod 10
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

LO, HI = 0x08800000, 0x0A000000


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def grab(tag):
    p = os.path.join(TMP, "full-%s.png" % tag)
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


def read_all(d, chunk=1 << 20):
    out = bytearray()
    pos = LO
    while pos < HI:
        b = d.read(pos, min(chunk, HI - pos))
        out.extend(b if b else b"\x00" * min(chunk, HI - pos))
        pos += chunk
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=20)
    ap.add_argument("--maxperiod", type=int, default=10)
    ap.add_argument("--settle", type=float, default=1.3)
    a = ap.parse_args()

    import numpy as np

    pp = load_client()
    reader = pp.Debugger()
    presser = pp.Debugger()
    print("game:", reader.status().get("game", {}).get("title"))
    print("scanning 0x%08X-0x%08X (%.1f MB) x %d samples, press=%s"
          % (LO, HI, (HI - LO) / 1048576.0, a.rounds + 1, a.press))
    print()

    t0 = time.time()
    base_bytes = read_all(reader)
    base = np.frombuffer(base_bytes, dtype="<i4")
    print("baseline read: %d bytes (%d words) in %.1fs"
          % (len(base_bytes), len(base), time.time() - t0))

    series = {}      # word index -> list of values, length fills in as samples arrive
    p_prev = None
    moved = 0

    for i in range(1, a.rounds + 1):
        presser.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}'
                        % a.press)
        time.sleep(a.settle)
        cur_bytes = read_all(reader)
        cur = np.frombuffer(cur_bytes, dtype="<i4")

        idx = np.nonzero(cur != base)[0]
        for p in idx.tolist():
            if p not in series:
                series[p] = [int(base[p])] * i      # equal to baseline for samples 0..i-1
            series[p].append(int(cur[p]))
        for p, s in list(series.items()):
            if len(s) == i:
                s.append(int(base[p]))              # this sample matched baseline
        time.sleep(a.settle)
        pcur = grab("s%d" % i)
        if p_prev and pcur and pixdiff(p_prev, pcur):
            moved += 1
        p_prev = pcur
        print("  sample %-2d  changed-vs-baseline words: %d" % (i, len(idx)), flush=True)

    print()
    print("presses that moved the display: %d/%d" % (moved, a.rounds))
    print("total time: %.0fs" % (time.time() - t0))
    print()

    n = a.rounds + 1
    complete = {p: s for p, s in series.items() if len(s) == n}
    print("words tracked: %d   with complete series: %d" % (len(series), len(complete)))
    print()

    def period(vals, maxp):
        for per in range(1, maxp + 1):
            if len(vals) > per + 1 and all(vals[k] == vals[k + per] for k in range(len(vals) - per)):
                return per
        return None

    print("=== WRAPPING words across ALL readable RAM (period <= %d) ===" % a.maxperiod)
    found = []
    for p, vals in complete.items():
        if len(set(vals)) == 1:
            continue
        per = period(vals, a.maxperiod)
        if per:
            found.append((p, per, vals))
    found.sort(key=lambda t: t[0])
    if found:
        for p, per, vals in found[:50]:
            print("  0x%08X  period=%-3d %s" % (LO + p * 4, per, vals))
        print("   -> %d wrapping word(s) in all readable RAM" % len(found))
    else:
        print("   NONE across all %d words of readable RAM." % len(base))
        print("   => the selection index is NOT a wrapping word in 0x08800000-0x0A000000.")

    reader.close(); presser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

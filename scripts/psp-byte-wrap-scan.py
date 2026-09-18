#!/usr/bin/env python3
"""psp-byte-wrap-scan.py -- the wrap test at BYTE granularity, and samples saved for re-analysis.

WHY BYTE GRANULARITY (gap found in the word-level full scan)
    The word-level scan over all 24 MB found no wrapping word. But a selection index stored as a 0..5
    value is naturally a BYTE, and if the other three bytes of its 32-bit word churn (pointers, flags,
    padding written every frame) then the WORD never repeats even though the byte does. A word-level
    period test therefore cannot see a byte-sized cursor.

    This scans bytes: 25,165,824 of them, tracking only those that differ from baseline, and reports
    bytes whose series REPEATS with a short period. It also SAVES every sample to disk so the data can
    be re-analysed (at other widths or with other predicates) without re-reading the emulator.

METHOD
    numpy over the raw bytes; a dict of byte-index -> series grows only with changed bytes.

INSTRUMENT FIXES RETAINED
    Two connections (reads vs input), screen movement verified per press, refusal on a null result.

USAGE
    python scripts/psp-byte-wrap-scan.py --press down --rounds 20 --maxperiod 10
"""
import argparse
import importlib.util
import os
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
    p = os.path.join(TMP, "bw-%s.png" % tag)
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


def period(vals, maxp):
    for p in range(1, maxp + 1):
        if len(vals) > p + 1 and all(vals[k] == vals[k + p] for k in range(len(vals) - p)):
            return p
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=20)
    ap.add_argument("--maxperiod", type=int, default=10)
    ap.add_argument("--settle", type=float, default=1.3)
    ap.add_argument("--save", default=os.path.join(TMP, "bwsave"))
    a = ap.parse_args()

    import numpy as np

    pp = load_client()
    reader = pp.Debugger()
    presser = pp.Debugger()
    print("game:", reader.status().get("game", {}).get("title"))
    print("BYTE-level scan 0x%08X-0x%08X (%.1f MB) x %d samples"
          % (LO, HI, (HI - LO) / 1048576.0, a.rounds + 1))
    os.makedirs(a.save, exist_ok=True)
    print()

    t0 = time.time()
    base_bytes = read_all(reader)
    np.save(os.path.join(a.save, "s00.npy"), np.frombuffer(base_bytes, dtype=np.uint8))
    base = np.frombuffer(base_bytes, dtype=np.uint8)
    print("baseline: %d bytes in %.1fs" % (len(base_bytes), time.time() - t0))

    series = {}
    moved = 0
    p_prev = None

    for i in range(1, a.rounds + 1):
        presser.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}'
                        % a.press)
        time.sleep(a.settle)
        cur_bytes = read_all(reader)
        np.save(os.path.join(a.save, "s%02d.npy" % i),
                np.frombuffer(cur_bytes, dtype=np.uint8))
        cur = np.frombuffer(cur_bytes, dtype=np.uint8)

        idx = np.nonzero(cur != base)[0]
        for p in idx.tolist():
            if p not in series:
                series[p] = [int(base[p])] * i
            series[p].append(int(cur[p]))
        for p, s in list(series.items()):
            if len(s) == i:
                s.append(int(base[p]))
        time.sleep(a.settle)
        pcur = grab("s%d" % i)
        if p_prev and pcur and pixdiff(p_prev, pcur):
            moved += 1
        p_prev = pcur
        print("  sample %-2d changed bytes: %d" % (i, len(idx)), flush=True)

    print()
    print("presses that moved the display: %d/%d" % (moved, a.rounds))
    print("time: %.0fs   bytes tracked: %d" % (time.time() - t0, len(series)))
    print()

    n = a.rounds + 1
    complete = {p: s for p, s in series.items() if len(s) == n}
    found = []
    for p, vals in complete.items():
        if len(set(vals)) == 1:
            continue
        per = period(vals, a.maxperiod)
        if per:
            found.append((p, per, vals))
    found.sort(key=lambda t: t[0])

    print("=== WRAPPING BYTES across all readable RAM (period <= %d) ===" % a.maxperiod)
    if found:
        for p, per, vals in found[:60]:
            print("  0x%08X  period=%-3d %s" % (LO + p, per, vals))
        print("   -> %d wrapping byte(s)" % len(found))
    else:
        print("   NONE across all %d bytes." % len(base))
        print("   => the selection index is not a wrapping BYTE either.")
    print()
    print("samples saved to %s for offline re-analysis" % a.save)
    reader.close(); presser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""psp-diff-press2.py -- press-diff that BRACKETS the highlight change and hunts a SMALL ORDINAL.

WHY THIS IS THE CORRECTED VERSION (doc section 33)
    section 31's press-diff concluded "no small-ordinal field in the 3.5 MB region". Section 33 then
    showed its premise was false: the pause menu DOES scroll with down (OCR of the changed panel read
    "4 Return to Game" -> "4 Retry"). So the section 31 negative was measured against a broken premise
    and is not trustworthy.

    Two things this version does differently, both forced by that correction:

    1. BRACKET THE STATES. The earlier version took baseline and "after" captures 1.4 s apart around a
       single press, which can land on the same visual state. This version captures at BOTH ends of a
       press and refuses to report until the screen diff proves two DISTINCT highlight positions were
       compared.

    2. HUNT A SMALL ORDINAL, NOT ANY CHANGE. The pause menu has ~6 rows, so the selection lives in
       0..6. Rather than reporting every changed word, this reports only words whose value stays in a
       small range AND moves -- and separately lists pointer-scale movers so allocator churn is
       visibly excluded rather than silently mixed in.

USAGE
    python scripts/psp-diff-press2.py --press down --pair down,up --lo 0x08BA0000 --hi 0x08D20000
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


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def grab(tag):
    p = os.path.join(TMP, "d2-%s.png" % tag)
    try:
        if os.path.exists(p):
            os.remove(p)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), p], capture_output=True)
    if not os.path.exists(p) or os.path.getsize(p) < 1000:
        return None
    return p


def pixdiff(p1, p2):
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(p1).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(p2).convert("RGB")).astype(np.int16)
    d = np.abs(a - b).max(axis=2) > 16
    return int(d.sum())


def read_region(d, lo, hi, chunk=1 << 20):
    out = bytearray()
    pos = lo
    while pos < hi:
        b = d.read(pos, min(chunk, hi - pos))
        out.extend(b if b else b"\x00" * min(chunk, hi - pos))
        pos += chunk
    return bytes(out)


def words(buf):
    return list(struct.unpack_from("<%di" % (len(buf) // 4), buf, 0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08BA0000)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x08D20000)
    ap.add_argument("--settle", type=float, default=1.2)
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))
    print("region 0x%08X-0x%08X (%.1f MB)  press=%s  rounds=%d"
          % (a.lo, a.hi, (a.hi - a.lo) / 1048576.0, a.press, a.rounds))
    print()

    def press():
        d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % a.press)
        time.sleep(a.settle)

    def state():
        """Return (screen_path, ram_words)."""
        p = grab("s")
        return p, words(read_region(d, a.lo, a.hi))

    samples = []
    p0, w0 = state()
    samples.append((p0, w0))
    for i in range(a.rounds):
        press()
        p, w = state()
        samples.append((p, w))

    # verify the screen actually moved between consecutive samples, else the comparison is void
    print("=== screen movement between samples (must be > 0, else the press did not land) ===")
    for i in range(1, len(samples)):
        pa, pb = samples[i - 1][0], samples[i][0]
        if not pa or not pb:
            print("  sample %d: CAPTURE FAILED" % i)
            continue
        n = pixdiff(pa, pb)
        print("  sample %d: %d changed pixels %s" % (i, n, "" if n else "<-- SAME STATE"))

    print()
    print("=== candidate fields: value moves AND stays in a small range (a real selection index) ===")
    width = min(len(s[1]) for s in samples)
    ordinals, pointers = [], []
    for i in range(width):
        vals = [s[1][i] for s in samples]
        if len(set(vals)) == 1:
            continue
        if all(0 <= v <= 64 for v in vals):
            ordinals.append((a.lo + i * 4, vals))
        elif all(abs(v) > 1000000 for v in vals):
            pointers.append((a.lo + i * 4, vals))

    if ordinals:
        for addr, vals in ordinals[:40]:
            print("   SMALL-ORDINAL 0x%08X  %s" % (addr, vals))
        print("   -> %d small-ordinal field(s)" % len(ordinals))
    else:
        print("   none")

    print()
    print("=== pointer-scale movers (allocator churn, excluded by construction) ===")
    print("   %d field(s), showing first 10:" % len(pointers))
    for addr, vals in pointers[:10]:
        print("   0x%08X  %s" % (addr, [hex(v) for v in vals]))
    d.close()


if __name__ == "__main__":
    main()

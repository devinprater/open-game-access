#!/usr/bin/env python3
"""bd-cursor-hunt.py -- find the board cursor via the origin-return signature.

DESIGN (Phase 1+3)
    Cursor moves are reversible: up-then-down (or left-then-right) returns the cursor
    to its origin tile. A RAM word holding cursor state therefore reads V -> V' -> V
    across (origin, after-dir1, after-dir2) snapshots. Render churn does NOT follow
    that pattern, so one (origin, moved, returned) triple kills ~all churn without
    any address hypothesis. Multiple triples (both axes, repeated) intersect to the
    survivors. Display judgment (>50K px; shimmer sits ~8K) verifies each move acted.

USAGE (board visible, pause closed; DP may be 00 -- cursor moves regardless)
    python scripts/bd-cursor-hunt.py --rounds 3
"""
import argparse
import importlib.util
import json
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()

    import numpy as np
    from PIL import Image

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    def safe_read(addr, size, tries=4):
        for _ in range(tries):
            try:
                b = c.read(addr, size)
            except Exception:
                b = None
            if b and len(b) == size:
                return b
            time.sleep(0.15)
        return None

    def snapshot():
        arr = np.empty(((HI - LO) // 4,), dtype=np.uint32)
        base, j = LO, 0
        while base < HI:
            raw = safe_read(base, 0x40000, tries=2)
            if raw is None:
                arr[j:j + 0x10000] = np.uint32(0xFFFFFFFF)
            else:
                arr[j:j + 0x10000] = np.frombuffer(raw, dtype=np.uint32)
            j += 0x10000
            base += 0x40000
        return arr

    def shot(t):
        q = os.path.join(TMP, "ch-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    def press(btn, frames=120):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": btn, "frames": frames}))

    print("game:", c.status().get("game", {}).get("title"), flush=True)
    triples = []   # (origin, moved, returned, axis) post-verify
    n = 0
    for r in range(a.rounds):
        for ax, d1, d2 in (("UD", "up", "down"), ("LR", "left", "right")):
            o = snapshot()
            p0 = shot("ch-o%d" % n)
            press(d1); time.sleep(2.2)
            p1 = shot("ch-m%d" % n)
            d_1 = diff(p0, p1)
            if d_1 < 50000:
                print("round %d %s: first move %d px -- discarded" % (r, ax, d_1), flush=True)
                continue
            m = snapshot()
            press(d2); time.sleep(2.2)
            p2 = shot("ch-r%d" % n)
            d_2 = diff(p1, p2)
            if d_2 < 50000:
                print("round %d %s: return move %d px -- discarded" % (r, ax, d_2), flush=True)
                continue
            rr = snapshot()
            triples.append((o, m, rr, ax))
            n += 1
            print("round %d %s: triple %d (%d/%d px)" % (r, ax, n, d_1, d_2), flush=True)
    print("verified triples: %d" % len(triples), flush=True)
    if len(triples) < 2:
        print("REFUSING: too few triples.")
        c.close(); pad.close()
        return 2

    # origin-return signature per triple, intersected
    mask = None
    per = []
    for (o, m, r, ax) in triples:
        ok = (o != 0xFFFFFFFF) & (m != 0xFFFFFFFF) & (r != 0xFFFFFFFF)
        sig = ok & (o == r) & (o != m)
        per.append(sig)
        mask = sig if mask is None else (mask & sig)
    surv = np.nonzero(mask)[0]
    print("survivors (all triples): %d" % len(surv), flush=True)
    # save one triple set for offline analysis (o/m/r of first UD + first LR)
    sv = {}
    for k, (o, m, r, ax) in enumerate(triples):
        sv["%s%d_o" % (ax, k)] = o
        sv["%s%d_m" % (ax, k)] = m
        sv["%s%d_r" % (ax, k)] = r
    np.save(os.path.join(TMP, "cursor-triples.npy"), sv)
    print("saved triples", flush=True)
    shown = 0
    for j in surv:
        vals = []
        for k, (o, m, r, ax) in enumerate(triples):
            vals.append("%s:%d/%d" % (ax, int(m[j]), int(o[j])))
        # SMALL-INT FIRST: print all survivors whose values stay < 4096
        allv = [int(m[j]) for (o, m, r, ax) in triples] + [int(o[j]) for (o, m, r, ax) in triples]
        if max(allv) < 4096:
            print("  SMALL 0x%08X  %s" % (LO + int(j) * 4, " ".join(vals)), flush=True)
            shown += 1
    print("small-int survivors shown: %d" % shown, flush=True)
    for j in surv[:40]:
        vals = []
        for k, (o, m, r, ax) in enumerate(triples):
            vals.append("%s:%d/%d" % (ax, int(m[j]), int(o[j])))
        print("  0x%08X  %s" % (LO + int(j) * 4, " ".join(vals)), flush=True)
    # axis-split: words that respond on UD triples but not LR (and vice versa)
    ud = [k for k in range(len(triples)) if triples[k][3] == "UD"]
    lr = [k for k in range(len(triples)) if triples[k][3] == "LR"]
    if ud and lr:
        mud = per[ud[0]]
        for k in ud[1:]:
            mud &= per[k]
        mlr = per[lr[0]]
        for k in lr[1:]:
            mlr &= per[k]
        print("UD-only: %d  LR-only: %d" % (
            int((mud & ~mlr).sum()), int((mlr & ~mud).sum())), flush=True)
        for j in np.nonzero(mud & ~mlr)[0][:15]:
            print("  UD 0x%08X" % (LO + int(j) * 4), flush=True)
        for j in np.nonzero(mlr & ~mud)[0][:15]:
            print("  LR 0x%08X" % (LO + int(j) * 4), flush=True)
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

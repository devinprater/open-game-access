#!/usr/bin/env python3
"""psp-find-widget.py -- locate the live list-widget object W on the board pause menu.

THE TARGET (doc sections 95-96)
    FUN_00250538(obj): index = [obj+0x3C] (via +0x28/+0x14), count = [obj+0x240],
    element = obj+0x60+4+idx*0x44, cap 7. Call site: FUN_00250538(&DAT_000012c4 + param_1),
    so W lives near RAM 0x088052c4 (= 0x08804000 + 0x12c4). Stride unknown.

METHOD (uses only proven instruments)
    1. liveness via cpu.status (NOT game.status -- s92).
    2. open the board pause menu with a GATED start (verify at pad word).
    3. verify the highlight moves: per DELIVERED down press, require display change.
    4. scan RAM around 0x088052c4 (+/-64KB) per delivered press; intersect changed words;
       remove no-press churn; for each survivor W check:
         [W+0x3C] in 0..6 AND [W+0x240] in 1..7 (the index/count pair shape).
    5. report candidates with their series.

USAGE
    python scripts/psp-find-widget.py --center 0x088052c4 --span 0x20000 --presses 6
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
PAD_SLOT = 0x08B965B0
BITS = {"start": 0x0008, "down": 0x0040}


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def cpu_status(c, timeout=1.5):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 4242}))
    end = time.time() + timeout
    while time.time() < end:
        try:
            c.ws.settimeout(0.3)
            m = json.loads(c.ws.recv())
        except Exception:
            continue
        if m.get("event") == "cpu.status":
            return m
    return None


def liveness(c, label, secs=1.5):
    a = cpu_status(c)
    time.sleep(secs)
    b = cpu_status(c)
    ta = a.get("ticks") if a else None
    tb = b.get("ticks") if b else None
    d = (tb - ta) if (ta is not None and tb is not None) else None
    ok = d is not None and d > 0
    print("  %-7s ticks delta %.1fs: %-12s stepping=%s -> %s"
          % (label, secs, d, (b or {}).get("stepping"), "EXECUTING" if ok else "FROZEN"),
          flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--center", type=lambda x: int(x, 0), default=0x088052C4)
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x20000)
    ap.add_argument("--presses", type=int, default=6)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"), flush=True)
    print("=== liveness BEFORE ===", flush=True)
    if not liveness(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print("pad object 0x%08X" % PAD, flush=True)

    def gated(button, want, tries=12):
        for _ in range(tries):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                    "button": button, "frames": 90}))
            seen = [struct.unpack("<I", c.read(PAD, 4))[0] for _ in range(6)]
            if want in seen:
                return True
            time.sleep(0.4)
        return False

    def shot(t):
        q = os.path.join(TMP, "fw-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        from PIL import Image
        import numpy as np
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    # --- open pause with gated start ---
    print("=== opening pause (gated start) ===", flush=True)
    if not gated("start", BITS["start"]):
        print("REFUSING: start not delivered.")
        c.close(); pad.close()
        return 2
    time.sleep(3.0)
    prev = shot("pause")

    lo = a.center - a.span // 2
    size = a.span
    print("scan window 0x%08X..0x%08X (%d KB)" % (lo, lo + size, size // 1024), flush=True)

    # no-press churn baseline: 4 reads, no input
    print("=== churn baseline (4 reads, no input) ===", flush=True)
    base_reads = []
    for i in range(4):
        raw = c.read(lo, size)
        base_reads.append(raw)
        time.sleep(0.4)
    nwords = size // 4
    base = [struct.unpack_from("<i", base_reads[0], j * 4)[0] for j in range(nwords)]
    churn = set()
    for r in base_reads[1:]:
        for j in range(nwords):
            if struct.unpack_from("<i", r, j * 4)[0] != base[j]:
                churn.add(j)
    print("churn words: %d / %d" % (len(churn), nwords), flush=True)

    # gated presses
    print("=== %d gated down presses ===" % a.presses, flush=True)
    rounds = []
    moved = 0
    for i in range(a.presses):
        if not gated("down", BITS["down"]):
            print("  press %d NOT delivered -- discarded" % (i + 1), flush=True)
            continue
        time.sleep(1.2)
        raw = c.read(lo, size)
        cur = shot("p%d" % i)
        d = diff(prev, cur)
        prev = cur
        if d > 2000:
            moved += 1
        rounds.append([struct.unpack_from("<i", raw, j * 4)[0] for j in range(nwords)])
        print("  press %d delivered, display diff %d px" % (i + 1, d), flush=True)
    print("presses that moved the display: %d/%d" % (moved, len(rounds)), flush=True)
    if len(rounds) < 3:
        print("REFUSING: too few delivered presses.")
        c.close(); pad.close()
        return 2

    # intersect changed words, remove churn
    changed_sets = []
    for r in rounds:
        changed_sets.append({j for j in range(nwords) if r[j] != base[j] and j not in churn})
    inter = set.intersection(*changed_sets)
    print("intersection across %d rounds (churn removed): %d words" % (len(rounds), len(inter)), flush=True)

    # pair-shape test: [W+0x3C] small ordinal AND [W+0x240] count-like, stable across rounds
    print("=== pair-shape test ([W+0x3C] in 0..6 AND [W+0x240] in 1..7) ===", flush=True)
    cands = []
    for j in inter:
        ad = lo + j * 4
        # W+0x3C and W+0x240 must be inside the window
        k_idx = j + 0x3C // 4
        k_cnt = j + 0x240 // 4
        if k_cnt >= nwords:
            continue
        idx_series = [r[k_idx] for r in rounds]
        cnt_series = [r[k_cnt] for r in rounds]
        if all(0 <= v <= 6 for v in idx_series) and all(1 <= v <= 7 for v in cnt_series):
            if len(set(cnt_series)) == 1:  # count stable
                cands.append((ad, idx_series, cnt_series[0]))
    print("candidates: %d" % len(cands), flush=True)
    for ad, idxs, cnt in cands[:20]:
        print("  W=0x%08X  idx[W+0x3C]=%s  count[W+0x240]=%d" % (ad, idxs, cnt), flush=True)

    print("=== liveness AFTER ===", flush=True)
    liveness(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

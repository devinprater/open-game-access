#!/usr/bin/env python3
"""psp-find-uiroot3.py -- test ALL count==N pair candidates with churn + gated presses.

WHY V3 (v2 lessons)
    v2's open-vs-closed diff cut 1856 -> 6, but all 6 were static under delivered presses while the
    display moved 80K px -- so the true P was likely rejected by the diff itself (e.g. the widget
    persists across close, or phase-1 "closed" snapshot still had pause open). The diff was too clever.
    v3 tests EVERY open-snapshot pair candidate (count==N exactly, idx in range): ~2K addresses is fine
    because each poll is one 8-byte read (~2ms): 2000 x 15 churn samples ~= 1 min.
    Display movement is verified per delivered press (screenshots), so a null is meaningful.

USAGE
    python scripts/psp-find-uiroot3.py --count 4 --presses 6
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
LO, HI = 0x08800000, 0x0A000000
OFF = 0x204


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
    ap.add_argument("--count", type=str, default="4")
    ap.add_argument("--presses", type=int, default=6)
    a = ap.parse_args()

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

    def safe_u32(addr):
        b = safe_read(addr, 4)
        return struct.unpack("<I", b)[0] if b else None

    print("game:", c.status().get("game", {}).get("title"), flush=True)
    print("=== liveness BEFORE ===", flush=True)
    if not liveness(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = safe_u32(PAD_SLOT)
    print("pad object 0x%08X" % PAD, flush=True)

    def send_press(button, frames=120):
        # Fire-and-forget: DELIVERY IS JUDGED BY DISPLAY MOVEMENT, not by reading
        # the pad word back. The pad-word readback misses real holds (v6: a start
        # press that demonstrably closed the pause menu never appeared in 200+
        # polls), while display diffs never lie about the game acting.
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": button, "frames": frames}))

    def shot(t):
        q = os.path.join(TMP, "u3-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        from PIL import Image
        import numpy as np
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    print("=== opening pause (display-judged start) ===", flush=True)
    board = shot("u7board")
    opened = False
    for attempt in range(10):
        send_press("start")
        time.sleep(3.0)
        cur = shot("u7open%d" % attempt)
        d = diff(board, cur)
        print("  start attempt %d: display %d px" % (attempt + 1, d), flush=True)
        if d > 200000:
            opened = True
            break
        time.sleep(1.0)
    if not opened:
        print("REFUSING: pause never opened (no display change).")
        c.close(); pad.close()
        return 2
    time.sleep(1.0)

    # snapshot + pair test (counts = comma list, e.g. "2,3,4,5,6,7")
    counts = sorted(set(int(x) for x in a.count.split(",") if x.strip()))
    print("=== snapshot + pair test (counts=%s) ===" % counts, flush=True)
    words = []
    base = LO
    while base < HI:
        raw = safe_read(base, 0x40000, tries=2)
        if raw is None:
            words.extend([None] * 0x10000)
        else:
            words.extend([struct.unpack_from("<i", raw, j)[0]
                          for j in range(0, 0x40000, 4)])
        base += 0x40000
    n = len(words)
    cands = []
    for j in range(n):
        if words[j] not in counts:
            continue
        cc = words[j]
        k = j - OFF // 4
        if k >= 0 and words[k] is not None and 0 <= words[k] < cc:
            cands.append((LO + j * 4, cc))
    print("candidates: %d" % len(cands), flush=True)

    # churn poll (8 samples)
    print("=== churn poll (8 samples) ===", flush=True)
    calm = []
    for ad, cc in cands:
        vals = set()
        ok = True
        for _ in range(8):
            b = safe_read(ad - OFF, 8, tries=2)
            if b is None:
                ok = False
                break
            vals.add((struct.unpack("<i", b[0:4])[0], struct.unpack("<i", b[4:8])[0]))
        if ok and len(vals) == 1:
            calm.append((ad, cc))
    print("calm: %d / %d" % (len(calm), len(cands)), flush=True)

    # display-judged presses: a press counts iff the display moves (>2000 px).
    # Samples are recorded after EVERY press; the survivor rule requires the
    # index to change across moved presses, so unmoved presses only add noise
    # rows that the rule ignores (index identical => not selected as changed).
    print("=== %d display-judged down presses ===" % a.presses, flush=True)
    series = {ad: [] for ad, cc in calm}
    moved_series = []
    prev = shot("u3prev")
    ndel = 0
    nmoved = 0
    for i in range(a.presses):
        send_press("down")
        time.sleep(1.5)
        cur = shot("u3p%d" % i)
        d = diff(prev, cur)
        prev = cur
        row = {}
        for ad, cc in calm:
            b = safe_read(ad - OFF, 8, tries=2)
            if b is not None:
                row[ad] = (struct.unpack("<i", b[0:4])[0],
                           struct.unpack("<i", b[4:8])[0])
        for ad, v in row.items():
            series[ad].append(v)
        ndel += 1
        if d > 2000:
            nmoved += 1
            moved_series.append(row)
        print("  press %d display %d px %s" % (ndel, d, "MOVED" if d > 2000 else "still"),
              flush=True)
    print("pressed=%d moved=%d" % (ndel, nmoved), flush=True)

    print("=== survivors: index CHANGED across MOVED presses, count STABLE ===",
          flush=True)
    ccmap = dict(calm)
    surv = []
    if len(moved_series) >= 3:
        for ad in ccmap:
            cc = ccmap[ad]
            idxs = [r[ad][0] for r in moved_series if ad in r]
            cnts = [r[ad][1] for r in moved_series if ad in r]
            if len(idxs) < 3:
                continue
            if all(v == cc for v in cnts) and len(set(idxs)) > 1 \
                    and all(0 <= v < cc for v in idxs):
                surv.append((ad, cc, idxs))
    else:
        print("too few moved presses (%d) -- inconclusive by construction" % len(moved_series),
              flush=True)
    print("survivors: %d" % len(surv), flush=True)
    for ad, cc, idxs in surv[:30]:
        print("  P=0x%08X  idx[P+0x1300]=%s  count=%d" % (ad - 0x1504, idxs, cc),
              flush=True)

    print("=== liveness AFTER ===", flush=True)
    liveness(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

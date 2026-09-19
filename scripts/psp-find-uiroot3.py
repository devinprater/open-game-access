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
    ap.add_argument("--count", type=int, default=4)
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

    def gated(button, want, tries=12):
        for _ in range(tries):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                    "button": button, "frames": 90}))
            seen = []
            for _ in range(6):
                v = safe_u32(PAD)
                if v is not None:
                    seen.append(v)
            if want in seen:
                return True
            time.sleep(0.4)
        return False

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

    print("=== opening pause (gated start) ===", flush=True)
    if not gated("start", 0x0008):
        print("REFUSING: start not delivered.")
        c.close(); pad.close()
        return 2
    time.sleep(3.0)

    # snapshot + pair test (count==N)
    print("=== snapshot + pair test (count==%d) ===" % a.count, flush=True)
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
        if words[j] != a.count:
            continue
        k = j - OFF // 4
        if k >= 0 and words[k] is not None and 0 <= words[k] < a.count:
            cands.append(LO + j * 4)
    print("candidates: %d" % len(cands), flush=True)

    # churn poll (8 samples)
    print("=== churn poll (8 samples) ===", flush=True)
    calm = []
    for ad in cands:
        vals = set()
        ok = True
        for _ in range(8):
            b = safe_read(ad - OFF, 8, tries=2)
            if b is None:
                ok = False
                break
            vals.add((struct.unpack("<i", b[0:4])[0], struct.unpack("<i", b[4:8])[0]))
        if ok and len(vals) == 1:
            calm.append(ad)
    print("calm: %d / %d" % (len(calm), len(cands)), flush=True)

    # gated presses with display verification
    print("=== %d gated down presses (display verified) ===" % a.presses, flush=True)
    series = {ad: [] for ad in calm}
    prev = shot("u3prev")
    ndel = 0
    nmoved = 0
    for i in range(a.presses):
        if not gated("down", 0x0040):
            print("  press %d NOT delivered -- discarded" % (i + 1), flush=True)
            continue
        ndel += 1
        time.sleep(1.2)
        cur = shot("u3p%d" % i)
        d = diff(prev, cur)
        prev = cur
        if d > 2000:
            nmoved += 1
        for ad in calm:
            b = safe_read(ad - OFF, 8, tries=2)
            if b is not None:
                series[ad].append((struct.unpack("<i", b[0:4])[0],
                                   struct.unpack("<i", b[4:8])[0]))
        print("  press %d delivered, display %d px" % (ndel, d), flush=True)
    print("delivered=%d moved=%d" % (ndel, nmoved), flush=True)

    print("=== survivors: index CHANGED with display, count==%d STABLE ===" % a.count,
          flush=True)
    surv = []
    for ad in calm:
        s = series[ad]
        if len(s) < 3:
            continue
        idxs = [x[0] for x in s]
        cnts = [x[1] for x in s]
        if all(v == a.count for v in cnts) and len(set(idxs)) > 1 \
                and all(0 <= v < a.count for v in idxs):
            surv.append((ad, idxs))
    print("survivors: %d" % len(surv), flush=True)
    for ad, idxs in surv[:20]:
        print("  P=0x%08X  idx[P+0x1300]=%s  count=%d" % (ad - 0x1504, idxs, a.count),
              flush=True)

    print("=== liveness AFTER ===", flush=True)
    liveness(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

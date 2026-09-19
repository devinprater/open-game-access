#!/usr/bin/env python3
"""psp-find-uiroot.py -- locate the pause menu UI root P by its index/count pair signature.

THE CORRECTED MODEL (Ghidra file-bytes check + decompile)
    `&DAT_000012c4 + param_1` is Ghidra mislabeling an immediate: file bytes at 0x12c4 are MIPS
    code, so the computation is really  param_1 + 0x12c4  (a FIELD offset in the UI root P).
    Likewise (&DAT_00001296)[param_1] is *(P + 0x1296) (byte field).
    So with W = P + 0x12c4:
        index at W+0x3C = P+0x1300   (signed, -1 = none)
        count at W+0x240 = P+0x1504  (1..7)
    The pair is 0x204 apart: [X-0x204] in 0..6 AND [X] in 1..7, where X = P+0x1504.

METHOD
    1. liveness via cpu.status.
    2. open pause with gated start; verify highlight moves with a delivered down.
    3. ONE full-RAM snapshot; pair test over all words (cheap, no presses).
    4. fast-poll the candidates (cap 300) across gated down presses: index must CHANGE on a
       delivered press while count stays STABLE; no-press churn rejects.
    5. report survivors with series.

USAGE
    python scripts/psp-find-uiroot.py --presses 6 --maxcand 300
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
OFF = 0x204  # (P+0x1504) - (P+0x1300)


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
    ap.add_argument("--presses", type=int, default=6)
    ap.add_argument("--maxcand", type=int, default=300)
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

    def gated(button, want, tries=12):
        for _ in range(tries):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                    "button": button, "frames": 90}))
            seen = [struct.unpack("<I", c.read(PAD, 4))[0] for _ in range(6)]
            if want in seen:
                return True
            time.sleep(0.4)
        return False

    print("=== opening pause (gated start) ===", flush=True)
    if not gated("start", 0x0008):
        print("REFUSING: start not delivered.")
        c.close(); pad.close()
        return 2
    time.sleep(3.0)

    # verify highlight moves with one delivered down (display check)
    def shot(t):
        q = os.path.join(TMP, "ur-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        from PIL import Image
        import numpy as np
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    p0 = shot("p0")
    if not gated("down", 0x0040):
        print("REFUSING: down not delivered.")
        c.close(); pad.close()
        return 2
    time.sleep(1.5)
    p1 = shot("p1")
    d = diff(p0, p1)
    print("delivered down display diff: %d px" % d, flush=True)
    if d <= 2000:
        print("REFUSING: highlight did not move.")
        c.close(); pad.close()
        return 2

    # --- one full snapshot + pair test ---
    print("=== full-RAM snapshot + pair test ([X-0x204] in 0..6 AND [X] in 1..7) ===", flush=True)
    words = []
    t0 = time.time()
    base = LO
    while base < HI:
        raw = c.read(base, 0x40000)
        if not raw or len(raw) != 0x40000:
            words.extend([None] * (0x10000))
        else:
            words.extend([struct.unpack_from("<i", raw, j)[0] for j in range(0, 0x40000, 4)])
        base += 0x40000
    print("snapshot: %d words in %.1fs" % (len(words), time.time() - t0), flush=True)
    n = len(words)
    cands = []
    for j in range(n):
        v = words[j]
        if v is None or not (1 <= v <= 7):
            continue
        k = j - OFF // 4
        if k < 0:
            continue
        u = words[k]
        if u is not None and 0 <= u <= 6:
            cands.append(LO + j * 4)  # X = P+0x1504
    print("pair-signature candidates: %d" % len(cands), flush=True)
    cands = cands[:a.maxcand]
    print("testing first %d" % len(cands), flush=True)

    # --- fast no-press churn poll (20 samples, ~0.2s apart, only candidate addrs) ---
    print("=== churn poll (20 samples) ===", flush=True)
    series = {ad: [] for ad in cands}
    for _ in range(20):
        for ad in cands:
            b = c.read(ad - OFF, 8)
            if b and len(b) == 8:
                series[ad].append((struct.unpack("<i", b[0:4])[0],
                                   struct.unpack("<i", b[4:8])[0]))
        time.sleep(0.15)
    calm = [ad for ad in cands
            if len(series[ad]) == 20
            and len({s[0] for s in series[ad]}) == 1
            and len({s[1] for s in series[ad]}) == 1]
    print("calm (no churn): %d / %d" % (len(calm), len(cands)), flush=True)

    # --- gated presses, poll idx+count ---
    print("=== %d gated down presses ===" % a.presses, flush=True)
    press_series = {ad: [] for ad in calm}
    ndel = 0
    for i in range(a.presses):
        if not gated("down", 0x0040):
            print("  press %d NOT delivered -- discarded" % (i + 1), flush=True)
            continue
        ndel += 1
        time.sleep(0.8)
        for ad in calm:
            b = c.read(ad - OFF, 8)
            if b and len(b) == 8:
                press_series[ad].append((struct.unpack("<i", b[0:4])[0],
                                         struct.unpack("<i", b[4:8])[0]))
        print("  press %d delivered" % ndel, flush=True)
    print("delivered presses: %d" % ndel, flush=True)

    print("=== survivors: index CHANGED on delivered press, count STABLE ===", flush=True)
    surv = []
    for ad in calm:
        s = press_series[ad]
        if len(s) < 3:
            continue
        idxs = [x[0] for x in s]
        cnts = [x[1] for x in s]
        if len(set(cnts)) == 1 and len(set(idxs)) > 1 and all(0 <= v <= 6 for v in idxs):
            surv.append((ad, idxs, cnts[0]))
    print("survivors: %d" % len(surv), flush=True)
    for ad, idxs, cnt in surv[:20]:
        print("  P=0x%08X  idx[P+0x1300]=%s  count[P+0x1504]=%d" % (ad - 0x1504, idxs, cnt),
              flush=True)

    print("=== liveness AFTER ===", flush=True)
    liveness(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

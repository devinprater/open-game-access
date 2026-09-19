#!/usr/bin/env python3
"""psp-find-uiroot2.py -- locate UI root P via open-vs-closed diff + tight pair signature.

WHY V2 (failures of v1)
    v1's pair signature ([X-0x204] in 0..6 AND [X] in 1..7) matched 17,537 words -- small adjacent
    ordinals are everywhere. And v1 crashed on a short read (no None guard).
    v2 adds two decisive filters:
      1. count == COUNT exactly (the pause menu has 4 rows: Return to Game/Quicksave/Quit/Help),
         index in 0..COUNT-1;
      2. OPEN-vs-CLOSED diff: the pause UI root is allocated for the pause menu, so the pair must
         CHANGE between pause-open and pause-closed snapshots. Stable small-ordinal pairs (the 17K)
         are rejected without any presses.
    Survivors get the gated-press test (index moves on delivered down, count stable) plus churn poll.

    All reads go through safe_read (retries, None on failure) -- the v1 crash cannot recur.

USAGE
    python scripts/psp-find-uiroot2.py --count 4 --presses 6
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
    if PAD is None:
        print("REFUSING: cannot read pad slot.")
        c.close(); pad.close()
        return 2
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

    def snapshot():
        """Full-RAM word snapshot; None entries on failed blocks."""
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
        return words

    def pair_candidates(words, count):
        n = len(words)
        out = []
        for j in range(n):
            v = words[j]
            if v != count:
                continue
            k = j - OFF // 4
            if k < 0:
                continue
            u = words[k]
            if u is not None and 0 <= u < count:
                out.append(LO + j * 4)  # X = P+0x1504
        return out

    # --- phase 1: ensure pause CLOSED, snapshot ---
    print("=== phase 1: pause-closed snapshot ===", flush=True)
    # press circle a few times gated to leave any menu, then snapshot
    for _ in range(3):
        gated("circle", 0x2000, tries=4)
        time.sleep(1.0)
    closed = snapshot()
    print("closed snapshot done", flush=True)

    # --- phase 2: open pause (gated start), snapshot ---
    print("=== phase 2: pause-open snapshot (gated start) ===", flush=True)
    if not gated("start", 0x0008):
        print("REFUSING: start not delivered.")
        c.close(); pad.close()
        return 2
    time.sleep(3.0)
    opened = snapshot()
    print("open snapshot done", flush=True)

    n = len(closed)
    # pair candidates in OPEN that are NOT pairs in CLOSED (open-vs-closed diff)
    open_c = set(pair_candidates(opened, a.count))
    closed_c = set(pair_candidates(closed, a.count))
    print("pair candidates (count==%d): open=%d closed=%d" % (a.count, len(open_c), len(closed_c)),
          flush=True)
    cands = sorted(open_c - closed_c)
    print("open-only candidates: %d" % len(cands), flush=True)
    for ad in cands[:20]:
        print("  X=0x%08X (P=0x%08X)" % (ad, ad - 0x1504), flush=True)

    # --- phase 3: churn poll on candidates ---
    print("=== churn poll (15 samples) ===", flush=True)
    calm = []
    for ad in cands:
        vals = set()
        ok = True
        for _ in range(15):
            b = safe_read(ad - OFF, 8, tries=2)
            if b is None:
                ok = False
                break
            vals.add((struct.unpack("<i", b[0:4])[0], struct.unpack("<i", b[4:8])[0]))
            time.sleep(0.1)
        if ok and len(vals) == 1:
            calm.append(ad)
    print("calm: %d / %d" % (len(calm), len(cands)), flush=True)

    # --- phase 4: gated presses ---
    print("=== %d gated down presses ===" % a.presses, flush=True)
    series = {ad: [] for ad in calm}
    ndel = 0
    for i in range(a.presses):
        if not gated("down", 0x0040):
            print("  press %d NOT delivered -- discarded" % (i + 1), flush=True)
            continue
        ndel += 1
        time.sleep(0.8)
        for ad in calm:
            b = safe_read(ad - OFF, 8, tries=2)
            if b is not None:
                series[ad].append((struct.unpack("<i", b[0:4])[0],
                                   struct.unpack("<i", b[4:8])[0]))
        print("  press %d delivered" % ndel, flush=True)
    print("delivered: %d" % ndel, flush=True)

    print("=== survivors: index CHANGED, count==%d STABLE ===" % a.count, flush=True)
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

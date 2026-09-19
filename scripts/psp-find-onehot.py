#!/usr/bin/env python3
"""psp-find-onehot.py -- find a one-hot selection FLAG across menu highlight moves.

WHY (doc s100 + v8 blank)
    v7/v8 proved no plain index WORD wraps anywhere in RAM under verified highlight moves
    (pause menu AND mode-select menu, 9 moved presses each). The 24 MB word-wrap scan agreed.
    The alternative model: selection is not a counter but a FLAG -- exactly one address in a
    small set is "active" at a time, and the active position advances (and wraps) with moves.
    The accessor FUN_00250538 would then DERIVE the index by scanning for the flagged element.

SIGNATURE
    On the 5-row wrapping mode-select menu, 10 downs = 2 full cycles: each row's flag is
    active on exactly 2 presses, spaced 5 apart ({k, k+5}). The scan snapshots full RAM after
    each display-verified move and reports words whose values stay in {0,1} with that rhythm.

USAGE
    python scripts/psp-find-onehot.py --presses 10   (assumes a wrapping menu is already open)
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


def cpu_status(c, timeout=2.0):
    # NOTE: cpu.status sometimes answers nothing under load (v7/v8 "FROZEN" scares);
    # retry several times before believing it.
    for _ in range(4):
        try:
            c.ws.send(json.dumps({"event": "cpu.status", "requestId": 4242}))
        except Exception:
            time.sleep(0.5)
            continue
        end = time.time() + timeout
        while time.time() < end:
            try:
                c.ws.settimeout(0.4)
                m = json.loads(c.ws.recv())
            except Exception:
                continue
            if m.get("event") == "cpu.status":
                return m
        time.sleep(0.5)
    return None


def liveness(c, label, secs=1.5):
    a = cpu_status(c)
    time.sleep(secs)
    b = cpu_status(c)
    ta = a.get("ticks") if a else None
    tb = b.get("ticks") if b else None
    d = (tb - ta) if (ta is not None and tb is not None) else None
    ok = d is not None and d > 0
    print("  %-7s ticks delta %.1fs: %-12s -> %s"
          % (label, secs, d, "EXECUTING" if ok else "FROZEN-OR-DEAF"),
          flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--presses", type=int, default=10)
    a = ap.parse_args()

    import numpy as np

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

    print("game:", c.status().get("game", {}).get("title"), flush=True)
    print("=== liveness BEFORE ===", flush=True)
    if not liveness(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    def send_press(button, frames=120):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": button, "frames": frames}))

    def shot(t):
        q = os.path.join(TMP, "oh-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        from PIL import Image
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    def snapshot():
        arr = np.empty(((HI - LO) // 4,), dtype=np.uint32)
        base = LO
        j = 0
        while base < HI:
            raw = safe_read(base, 0x40000, tries=2)
            if raw is None:
                arr[j:j + 0x10000] = np.uint32(0xFFFFFFFF)
            else:
                arr[j:j + 0x10000] = np.frombuffer(raw, dtype=np.uint32)
            j += 0x10000
            base += 0x40000
        return arr

    print("=== %d display-judged downs + snapshots ===" % a.presses, flush=True)
    snaps = []
    prev = shot("ohprev")
    nmoved = 0
    for i in range(a.presses):
        send_press("down")
        time.sleep(1.5)
        cur = shot("ohp%d" % i)
        d = diff(prev, cur)
        prev = cur
        if d > 2000:
            nmoved += 1
            snaps.append(snapshot())
            print("  press %d MOVED %d px (snap %d)" % (i + 1, d, len(snaps)), flush=True)
        else:
            print("  press %d still (%d px) -- no snapshot" % (i + 1, d), flush=True)
    print("moved=%d snapshots=%d" % (nmoved, len(snaps)), flush=True)
    if len(snaps) < 4:
        print("REFUSING: too few moved presses.")
        c.close(); pad.close()
        return 2

    print("=== one-hot analysis ===", flush=True)
    S = np.stack(snaps)          # (T, W) uint32; 0xFFFFFFFF = unread
    T = S.shape[0]
    ok = S != np.uint32(0xFFFFFFFF)
    binv = ((S == 0) | (S == 1)) & ok
    allbin = binv.all(axis=0)
    ones = ((S == 1) & ok).sum(axis=0)
    cand = np.nonzero(allbin & (ones >= 2) & (ones <= T - 1))[0]
    print("binary-words active 2..%d times: %d" % (T - 1, len(cand)), flush=True)

    # rhythm check: for a period-P wrap over T moved presses, each flag is active
    # exactly T/P times, spaced P apart. Try P in 2..8, score candidates.
    for P in range(2, 9):
        if T % P != 0:
            continue
        want = T // P
        hit = []
        for j in cand:
            active = set(int(t) for t in range(T) if S[t, j] == 1)
            if len(active) != want:
                continue
            # all active presses congruent mod P?
            if len(set(t % P for t in active)) == 1:
                hit.append((LO + int(j) * 4, sorted(active)))
        print("  period %d (x%d each): %d rhythm hits" % (P, want, len(hit)), flush=True)
        for addr, active in hit[:25]:
            print("    0x%08X active on snaps %s" % (addr, active), flush=True)

    print("=== liveness AFTER ===", flush=True)
    liveness(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

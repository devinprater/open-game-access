#!/usr/bin/env python3
"""psp-full-gated-diff.py -- full-RAM diff around GATED (verified-delivered) presses, intersected.

WHY THIS IS THE RIGHT INSTRUMENT NOW
    Section 63 found a screen where the D-pad DEMONSTRABLY works: each delivered press of `down`/`up`
    changes ~245,400 pixels. Yet on that same screen `M + 0x18` (render count) stayed 0 and the SEL
    recipe did not move. So the visible menu is drawn by a DIFFERENT subsystem than the one section 61
    traced -- and the cursor for THIS menu is somewhere else, on a screen where input provably lands.

    This is the first time in the investigation that a screen has BOTH:
      * a demonstrated, per-press visual response to the D-pad, and
      * a working input gate to prove each press was delivered.

    So a full-RAM diff here is worth running again: the earlier full-RAM scans (sections 38, 40) were on
    screens where delivery was unverified, so their negatives are instrument-suspect.

THE METHOD
    For each round:
      1. read ALL of readable RAM (0x08800000-0x0A000000, 3 MB/s -> ~8 s);
      2. send a press and WAIT until the button word shows the documented bit (GATE);
      3. read all RAM again;
      4. record the addresses whose word changed.
    Rounds are then INTERSECTED: a field that changes on every delivered press is a candidate; churn that
    appears in only one round is dropped.

    A no-press control round is included: read, wait the same interval with NO press, read -- and any
    address that changes there is excluded.

USAGE
    python scripts/psp-full-gated-diff.py --button down --rounds 4
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
LO, HI = 0x08800000, 0x0A000000
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def drain(ws, sec):
    out = []
    end = time.time() + sec
    while time.time() < end:
        try:
            ws.settimeout(max(0.02, end - time.time()))
            out.append(json.loads(ws.recv()))
        except Exception:
            pass
    return out


def cpu(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def live(c, tag):
    a = cpu(c); time.sleep(1.2); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--button", default="down")
    ap.add_argument("--rounds", type=int, default=4)
    ap.add_argument("--control", action="store_true", default=True)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    want = BITS[a.button]
    print()
    print("pad 0x%08X   button '%s' expect bit 0x%04X" % (PAD, a.button, want))
    size = HI - LO
    words = size // 4

    def readall():
        t0 = time.time()
        b = c.read(LO, size)
        dt = time.time() - t0
        if not b or len(b) != size:
            return None, dt
        return b, dt

    def padword():
        x = c.read(PAD, 4)
        return struct.unpack("<I", x)[0] if x and len(x) == 4 else None

    def gated_press():
        """Press and confirm the bit is visible; return True if delivered."""
        for attempt in range(30):
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 5000 + attempt,
                                    "button": a.button, "frames": 60}))
            ok = False
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    ok = True
                    break
            if ok:
                return True
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 5900 + attempt,
                                    "button": a.button, "frames": 2}))
            time.sleep(0.6)
        return False

    changed_sets = []
    for r in range(a.rounds):
        b1, dt1 = readall()
        print("  round %d: baseline read %.1fs" % (r + 1, dt1))
        if b1 is None:
            print("   read failed"); break
        if not gated_press():
            print("   press NOT delivered after 30 attempts -- skipping round (no verdict)")
            continue
        time.sleep(0.6)
        b2, dt2 = readall()
        if b2 is None:
            print("   second read failed"); break
        # word-level diff
        s = set()
        for i in range(words):
            off = i * 4
            if b1[off:off + 4] != b2[off:off + 4]:
                s.add(LO + off)
        changed_sets.append(s)
        print("   delivered; changed words: %d" % len(s))
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 6000 + r,
                                "button": a.button, "frames": 2}))
        time.sleep(1.2)

    # no-press control
    ctrl = None
    if a.control:
        b1, _ = readall()
        time.sleep(3.0)
        b2, _ = readall()
        if b1 and b2:
            ctrl = set()
            for i in range(words):
                off = i * 4
                if b1[off:off + 4] != b2[off:off + 4]:
                    ctrl.add(LO + off)
            print("  no-press control: changed words %d" % len(ctrl))

    print()
    if not changed_sets:
        print("VERDICT: no valid rounds (presses could not be delivered).")
        c.close(); pad.close()
        return 0

    inter = set.intersection(*changed_sets) if len(changed_sets) > 1 else changed_sets[0]
    print("=== INTERSECTION across %d delivered rounds: %d words ===" % (len(changed_sets), len(inter)))
    if ctrl is not None:
        inter2 = {x for x in inter if x not in ctrl}
        print("=== after removing no-press churn: %d words ===" % len(inter2))
        inter = inter2

    if not inter:
        print("  NONE -- no word changes on every delivered press.")
    else:
        # group into clusters
        srt = sorted(inter)
        clusters, cur = [], [srt[0]]
        for x in srt[1:]:
            if x - cur[-1] <= 0x40:
                cur.append(x)
            else:
                clusters.append(cur); cur = [x]
        clusters.append(cur)
        print("  %d cluster(s):" % len(clusters))
        for cl in clusters[:25]:
            base = cl[0]
            vals = []
            for ad in cl[:6]:
                v = c.read(ad, 4)
                vals.append(struct.unpack("<i", v)[0] if v and len(v) == 4 else None)
            print("   0x%08X  %d word(s)  sample vals %s" % (base, len(cl), vals))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

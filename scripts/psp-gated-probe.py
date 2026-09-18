#!/usr/bin/env python3
"""psp-gated-probe.py -- press test with the INPUT GATE: undelivered presses are discarded, not counted.

WHY THIS IS THE CORRECTED INSTRUMENT (doc section 59)
    Section 59 found that debugger input injection is INTERMITTENT: the same button reads delivered once
    and dropped the next, while both look identical downstream ("nothing changed"). Four sections of
    results ("the D-pad is inert on the reachable screens") were therefore instrument-suspect.

    The fix is a gate. `pad + 0x00` is the live sceCtrl button word carrying documented PSP bits:

        up 0x0010   right 0x0020   down 0x0040   left 0x0080
        l 0x0100    r 0x0200
        triangle 0x1000   circle 0x2000   cross 0x4000   square 0x8000
        start 0x0008      select 0x0001

    So each press is verified at the pad word WHILE HELD. If the documented bit is absent the press is
    DISCARDED -- it is not evidence of an inert control, it is evidence of a dropped input.

PROTOCOL
    1. liveness before (ticks);
    2. NO-PRESS baseline: sample the whole chain N times -> churn set;
    3. for each control of interest: press repeatedly until `need` DELIVERED presses are obtained (or
       `max_tries` exhausted), sampling the chain while the button is verifiably held;
    4. report only fields that responded to DELIVERED presses and did not churn without input;
    5. liveness after; report the delivery rate for each control.

USAGE
    python scripts/psp-gated-probe.py --buttons up,cross,circle --need 6
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
MGR_PTR = 0x08804000 + 0x00397770
BITS = {"select": 0x0001, "start": 0x0008, "up": 0x0010, "right": 0x0020, "down": 0x0040,
        "left": 0x0080, "l": 0x0100, "r": 0x0200, "triangle": 0x1000, "circle": 0x2000,
        "cross": 0x4000, "square": 0x8000}


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
    ap.add_argument("--buttons", default="up,cross,circle")
    ap.add_argument("--need", type=int, default=6, help="delivered presses required per button")
    ap.add_argument("--max-tries", type=int, default=40)
    ap.add_argument("--rounds", type=int, default=8, help="no-press baseline rounds")
    ap.add_argument("--hold", type=int, default=60)
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
    print()
    print("pad object 0x%08X   (button word at +0x00)" % PAD)
    if not (0x08800000 <= PAD < 0x0A000000):
        print("  invalid pad pointer"); c.close(); pad.close(); return 3

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def sample():
        """Manager + node chain, compact but broad."""
        out = {}
        raw = c.read(MGR_PTR, 4)
        M = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
        out["M"] = M
        if not M or not (0x08800000 <= M < 0x0A000000):
            return out
        hdr = c.read(M, 0x40)
        if not hdr or len(hdr) < 0x40:
            return out
        for nm, off in (("M+0x14_renderbase", 0x14), ("M+0x18_render", 0x18), ("M+0x1C", 0x1C),
                        ("M+0x20", 0x20), ("M+0x24", 0x24), ("M+0x28_head", 0x28),
                        ("M+0x2C_tail", 0x2C), ("M+0x30_nodes", 0x30), ("M+0x3C", 0x3C)):
            out[nm] = struct.unpack_from("<i", hdr, off)[0]
        # current outer node (Codex chain) + descriptor
        N = struct.unpack_from("<I", hdr, 0x28)[0]
        out["N"] = N
        if N and 0x08800000 <= N < 0x0A000000:
            nb = c.read(N, 0x40)
            if nb and len(nb) == 0x40:
                out["N+0x18_bias"] = struct.unpack_from("<h", nb, 0x18)[0]
                out["N+0x1c_cb"] = struct.unpack_from("<I", nb, 0x1c)[0]
                R = struct.unpack_from("<I", nb, 0x0C)[0]
                out["R"] = R
                if R and 0x08800000 <= R < 0x0A000000:
                    rb = c.read(R, 0x40)
                    if rb and len(rb) == 0x40:
                        out["R+0x0c_table"] = struct.unpack_from("<I", rb, 0x0C)[0]
                        out["R+0x2c_count"] = struct.unpack_from("<I", rb, 0x2C)[0]
        # walk nodes, capture every word
        p = struct.unpack_from("<I", hdr, 0x28)[0]
        i = 0
        seen = set()
        while p and p not in seen and i < 40 and 0x08800000 <= p < 0x0A000000:
            seen.add(p)
            nd = c.read(p, 0x40)
            if not nd or len(nd) < 0x40:
                break
            for w in range(0, 0x40, 4):
                out["n%d+0x%02X" % (i, w)] = struct.unpack_from("<i", nd, w)[0]
            p = struct.unpack_from("<i", nd, 0x24)[0]
            if p == struct.unpack_from("<I", hdr, 0x28)[0]:
                break
            i += 1
        return out

    print()
    print("=== NO-PRESS baseline (%d rounds) ===" % a.rounds)
    nopress = []
    for i in range(a.rounds):
        nopress.append(sample())
        print("  round %d" % (i + 1), end="\r", flush=True)
        time.sleep(0.7)
    print()

    results = {}
    for btn in [b.strip() for b in a.buttons.split(",") if b.strip()]:
        want = BITS.get(btn)
        if want is None:
            print("unknown button %s" % btn)
            continue
        print()
        print("=== button '%s' (expect bit 0x%04X); need %d DELIVERED presses ===" % (btn, want, a.need))
        got, tries, samples = 0, 0, []
        while got < a.need and tries < a.max_tries:
            tries += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 700 + tries,
                                    "button": btn, "frames": a.hold}))
            # sample DURING the hold, repeatedly
            delivered = False
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    delivered = True
                    break
            if delivered:
                samples.append(sample())
                got += 1
            # release + settle so holds never overlap
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 900 + tries,
                                    "button": btn, "frames": 2}))
            time.sleep(0.75)
        print("  delivered %d/%d  (tried %d)" % (got, a.need, tries))
        if got < 2:
            print("  -> TOO FEW DELIVERED: input injection is dropping this button; no verdict.")
            results[btn] = None
            continue
        results[btn] = samples

    print()
    print("=== analysis (only DELIVERED presses counted) ===")
    for btn, samples in results.items():
        if not samples:
            print("   %-9s no verdict (insufficient delivered presses)" % btn)
            continue
        keys = sorted(set().union(*[set(s.keys()) for s in nopress + samples]))
        churn, only = [], []
        for k in keys:
            nv = [s.get(k) for s in nopress]
            pv = [s.get(k) for s in samples]
            if len(set(nv)) > 1:
                churn.append(k)
            if len(set(pv)) > 1 and len(set(nv)) == 1:
                only.append((k, nv[0], pv))
        print()
        print("   %-9s fields %d | churn %d | PRESS-ONLY %d" % (btn, len(keys), len(churn), len(only)))
        if only:
            for k, b0, pv in only[:20]:
                print("      %-18s base %-12s %s" % (k, b0, pv))
            print("      -> CANDIDATES: these respond to a DELIVERED '%s'." % btn)
        else:
            print("      -> nothing responds to a DELIVERED '%s'." % btn)

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

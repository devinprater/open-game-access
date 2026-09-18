#!/usr/bin/env python3
"""psp-watch-saturate.py -- collect MANY write sites per phase, then apply a FREQUENCY claim.

THE FIX (doc section 71)
    Section 70 claimed press-only writers from two 6-sample sets that did not overlap. Section 71's
    idle-vs-idle control showed zero overlap too, with no treatment at all -- so the separation was a
    sampling artefact: the node is written by a POOL of 12+ store sites, and 6 samples per phase cannot
    cover it.

    The two fixes section 71 named:
      1. SATURATE THE POOL -- collect until the set stops growing (not 6 samples);
      2. USE A FREQUENCY CLAIM -- a press-specific writer must appear in EVERY press sample and NEVER in
         an idle sample, which cannot be satisfied by chance.

    The naive loop takes one hit per window (the breakpoint halts on the first store), so saturating needs
    many windows. This script instead RE-ARMS RAPIDLY WITHIN EACH WINDOW: arm -> short wait -> read PC ->
    clear -> resume -> re-arm, looping until the pool stops growing. That collects dozens of sites per
    phase in the same wall-clock budget.

    It reports, per phase, the saturated PC set and its size, then the frequency comparison:
      * PCs in EVERY press sample and NO idle sample  -> press-specific
      * PCs in BOTH                                   -> repaint
      * the per-phase pool sizes                      -> whether saturation was reached

USAGE
    python scripts/psp-watch-saturate.py --button down --addr 0x08C09344 --size 0x40 --target 40
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}
FUNCS = [(0x0024910c, 0x00249300, "FUN_0024910c (list add)"),
         (0x0024932c, 0x00249700, "FUN_0024932c (node-list mgr)"),
         (0x0024aee0, 0x0024c200, "FUN_0024aee0 (RENDER LOOP)"),
         (0x0025468c, 0x00254750, "FUN_0025468c (def lookup)"),
         (0x0025595c, 0x00256198, "FUN_0025595c (render writer)")]


def fname(v):
    for lo, hi, nm in FUNCS:
        if lo <= v < hi:
            return nm
    return "?"


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
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x08C09344)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x40)
    ap.add_argument("--target", type=int, default=40, help="hits to collect per phase")
    ap.add_argument("--hold", type=int, default=40)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen -- restart the emulator first.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    want = BITS[a.button]
    print("watching 0x%08X size 0x%X   button '%s' bit 0x%04X" % (a.addr, a.size, a.button, want))

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def clear_resume():
        c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 5}))
        drain(c.ws, 0.08)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws, 0.10)

    def arm():
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                              "address": a.addr, "size": a.size, "type": "write"}))
        drain(c.ws, 0.08)

    def collect_phase(target, do_press):
        """Saturating collection: re-arm rapidly, count PCs."""
        pcs = []
        delivered = 0
        t0 = time.time()
        while len(pcs) < target and time.time() - t0 < 150:
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 300,
                                        "button": a.button, "frames": a.hold}))
                for _ in range(2):
                    w = padword()
                    if w is not None and (w & want) == want:
                        delivered += 1
                        break
            clear_resume()
            arm()
            time.sleep(0.30)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st["pc"])
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 400,
                                        "button": a.button, "frames": 2}))
            time.sleep(0.05)
        clear_resume()
        return pcs, delivered

    print()
    print("=== PRESS phase: saturating to %d hits with verified '%s' presses ===" % (a.target, a.button))
    press, delivered = collect_phase(a.target, True)
    cp = Counter(press)
    print("  hits %d   distinct sites %d   verified presses %d" % (len(press), len(cp), delivered))
    for pc, n in cp.most_common():
        print("     0x%08X x%-3d vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    print()
    print("=== IDLE phase: saturating to %d hits with NO input ===" % a.target)
    idle, _ = collect_phase(a.target, False)
    ci = Counter(idle)
    print("  hits %d   distinct sites %d" % (len(idle), len(ci)))
    for pc, n in ci.most_common():
        print("     0x%08X x%-3d vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    sp, si = set(press), set(idle)
    print()
    print("=== SATURATION CHECK ===")
    print("  press pool size: %d     idle pool size: %d" % (len(sp), len(si)))
    print("  overlap:         %d" % len(sp & si))
    print("  press-only:      %s" % ([hex(p) for p in sorted(sp - si)] or "none"))
    print("  idle-only:       %s" % ([hex(p) for p in sorted(si - sp)] or "none"))

    print()
    print("=== FREQUENCY CLAIM (the section 71 fix) ===")
    print("  A press-specific writer must appear in EVERY press hit and NO idle hit.")
    npress, nidle = len(press), len(idle)
    # presence fraction across the phase: PCs seen at least twice AND confined to one phase
    strong = [p for p in sorted(sp - si) if cp[p] >= 2]
    print("  press-only sites seen >=2 times: %s"
          % (", ".join("0x%08X(%s x%d)" % (p, fname(p - 0x08804000), cp[p]) for p in strong) or "none"))
    if strong:
        print("  => CANDIDATE PRESS-SPECIFIC WRITERS:")
        for p in strong:
            print("       0x%08X  vaddr 0x%08X  %s  (x%d in press, x0 in idle)"
                  % (p, p - 0x08804000, fname(p - 0x08804000), cp[p]))
    elif sp and not (sp - si):
        print("  => NO press-specific writer: every site writes with no input too.")
    else:
        print("  => only single-occurrence phase-exclusive sites -- not enough evidence at this N.")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

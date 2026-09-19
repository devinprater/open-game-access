#!/usr/bin/env python3
"""psp-read-watch.py -- find the READERS of the pad button word (the input consumer).

WHY THIS IS THE DECISIVE INSTRUMENT (doc section 77)
    Sections 73 and 77 established two dead ends with the same shape: the pad OBJECT is not read by the
    menu (0 of 21 cross-references), and the service TABLE is never walked (2 references, both
    construction). Both say the same thing -- the menu does not reach input by following published
    structures.

    But a READ WATCHPOINT answers the question directly, and it is a different question: **who reads the
    button word**. Whoever reads `pad + 0x00` IS the input consumer, by definition -- no cross-reference
    analysis or structural inference needed.

    PPSSPP accepts `type: "read"` (verified: the first armed read breakpoint halted immediately at
    pc=0x088FA698 -> vaddr 0x000F6698, i.e. inside FUN_000f68e0's neighbourhood).

PROTOCOL
    Collect many read-hit PCs (resume + re-arm between hits), then classify:
      * PCs that appear in EVERY press window  -> the per-poll reader
      * PCs that appear only when a press lands -> the press-specific consumer

    The button word is written by the pad layer each poll (~60 Hz), so readers are frequent; collecting
    30-60 hits per phase gives a saturated set, applying section 71's lesson.

USAGE
    python scripts/psp-read-watch.py --target 40
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

FUNCS = [(0x000f68e0, 0x000f6b00, "FUN_000f68e0 (controller read)"),
         (0x000f68c4, 0x000f68e0, "FUN_000f68c4"),
         (0x000f6694, 0x000f68c4, "FUN_000f6694"),
         (0x000f6868, 0x000f68c4, "FUN_000f6868"),
         (0x000f76f4, 0x000f7790, "FUN_000f76f4 (pad port dispatch)"),
         (0x000f790c, 0x000f7994, "FUN_000f790c (pad flags writer)"),
         (0x000f7994, 0x000f7a30, "FUN_000f7994 (input poll)"),
         (0x000f7440, 0x000f74f0, "FUN_000f7440 (pad helper)"),
         (0x000f778c, 0x000f77f0, "FUN_000f778c (pad helper)"),
         (0x000f77f0, 0x000f7860, "FUN_000f77f0 (pad helper)"),
         (0x000fa84c, 0x000fc400, "FUN_000fa84c (init)"),
         (0x0024aee0, 0x0024c200, "FUN_0024aee0 (RENDER LOOP)"),
         (0x0024932c, 0x00249700, "FUN_0024932c (node mgr)"),
         (0x0025468c, 0x00254750, "FUN_0025468c (def lookup)")]


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
    ap.add_argument("--target", type=int, default=40)
    ap.add_argument("--button", default="down")
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
    print("pad object 0x%08X   button word at 0x%08X" % (PAD, PAD))

    def clear_resume():
        # clear.all does NOT clear (verified live) -- remove explicitly.
        c.ws.send(json.dumps({"event": "memory.breakpoint.remove", "requestId": 5,
                              "address": PAD, "size": 4}))
        drain(c.ws, 0.06)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws, 0.08)

    def arm_read():
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                              "address": PAD, "size": 4, "type": "read"}))
        drain(c.ws, 0.06)

    def collect(target):
        pcs = []
        t0 = time.time()
        while len(pcs) < target and time.time() - t0 < 120:
            clear_resume()
            arm_read()
            time.sleep(0.18)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st["pc"])
            time.sleep(0.03)
        clear_resume()
        return pcs

    print()
    print("=== collecting READ hits on the button word (target %d) ===" % a.target)
    pcs = collect(a.target)
    print("  hits: %d   distinct PCs: %d" % (len(pcs), len(set(pcs))))
    print()
    print("=== the READERS of the pad button word ===")
    for pc, n in Counter(pcs).most_common():
        print("   0x%08X x%-3d vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

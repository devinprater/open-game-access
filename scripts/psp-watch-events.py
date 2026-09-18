#!/usr/bin/env python3
"""psp-watch-events.py -- READ-watch the JUST-PRESSED edge field: enumerate the input consumers.

WHY THIS IS THE TERMINAL MEASUREMENT (doc sections 85-86)
    Section 85 derived the input event layout from code and section 86 CONFIRMED it live:

        pad + 0xC0  (and pad + 0x110, stride 0x50) is the INPUT EVENT OBJECT
          +0x00/+0x04  current logical button bits
          +0x08/+0x0C  JUST-PRESSED this frame      <-- one frame only
          +0x10/+0x14  JUST-RELEASED this frame
          +0x30        repeat timer (reads 30)

    Verified: a gated press of `cross` made `pressed` mirror `current` (`0x00084004`) on the press frame and
    then CLEAR by the next sample. The values contain `0x0004`, which is NOT a sceCtrl bit -- the object
    holds the game's LOGICAL action bits.

    THE ARGUMENT FOR THIS BEING CONCLUSIVE: a consumer of a ONE-FRAME JUST-PRESSED bit is unambiguously doing
    input handling. That is unlike every earlier reader enumeration, where a reader might be the platform
    layer reading back its own state (which is exactly what sections 78 and 81 found). Here the field only
    ever holds a value for a single frame, so any code that reads it is reacting to a button *event* -- which
    is what a menu selection does.

METHOD
    READ watchpoint (verified working in sections 78/81) on:
        pad + 0xC0 + 0x08   (port0 just-pressed A)
        pad + 0x110 + 0x08  (port1 just-pressed A)
    Collect MANY hits with resume+re-arm between them (section 71: saturate the pool), then report each
    reader with its function, and repeat for the second port as a control.

USAGE
    python scripts/psp-watch-events.py --target 40
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
PORT0, PORT1 = 0x00C0, 0x0110
JUST_PRESSED = 0x08

FUNCS = [(0x0024aee0, 0x0024c200, "FUN_0024aee0 (RENDER LOOP)"),
         (0x0024932c, 0x00249700, "FUN_0024932c (node mgr)"),
         (0x0025468c, 0x00254750, "FUN_0025468c (def lookup)"),
         (0x002489d0, 0x00248c00, "FUN_002489d0 (menu mgr ctor)"),
         (0x0025595c, 0x00256198, "FUN_0025595c (render writer)"),
         (0x002dbf50, 0x002dc400, "FUN_002dbf50 (boot)"),
         (0x000f6694, 0x000f67c0, "FUN_000f6694 (edge processor)"),
         (0x000f7028, 0x000f70c4, "FUN_000f7028"),
         (0x000f7138, 0x000f71b0, "FUN_000f7138 (reads raw)"),
         (0x000f70c4, 0x000f7138, "FUN_000f70c4 (translator)"),
         (0x000f7498, 0x000f74f0, "FUN_000f7498 (driver)")]


def fname(v):
    for lo, hi, nm in FUNCS:
        if lo <= v < hi:
            return nm
    return "? ** UNCLASSIFIED **"


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
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen -- restart the emulator first.")
        c.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print("pad 0x%08X" % PAD)

    def clear_resume():
        c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 5}))
        drain(c.ws, 0.06)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws, 0.08)

    def collect(addr, target):
        pcs = []
        t0 = time.time()
        while len(pcs) < target and time.time() - t0 < 130:
            clear_resume()
            c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                                  "address": addr, "size": 8, "type": "read"}))
            drain(c.ws, 0.06)
            time.sleep(0.18)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st["pc"])
            time.sleep(0.03)
        clear_resume()
        return pcs

    for label, port in (("port0 pad+0xC0", PORT0), ("port1 pad+0x110", PORT1)):
        addr = PAD + port + JUST_PRESSED
        print()
        print("############ READ-watch %s +0x08  (0x%08X) -- the JUST-PRESSED field ############"
              % (label, addr))
        pcs = collect(addr, a.target)
        print("  hits %d  distinct PCs %d" % (len(pcs), len(set(pcs))))
        if not pcs:
            print("  NO READERS -- nothing reads this field (with the CPU resumed before each window).")
            continue
        print()
        for pc, n in Counter(pcs).most_common():
            print("   0x%08X x%-3d vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

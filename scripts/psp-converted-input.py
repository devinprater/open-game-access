#!/usr/bin/env python3
"""psp-converted-input.py -- read and READ-watch the CONVERTED input object at pad + 0xC0.

WHY (doc section 80)
    Section 80 established the input chain end to end:
      sceCtrl -> pad object (raw button word at +0x00)
              -> FUN_000f7138 reads raw words
              -> FUN_000f70c4 maps raw bits -> LOGICAL bits via {mask,mask,out,out} table
              -> FUN_000f6fc8 stores the result at CONVERTED_OBJECT + 0x00
              -> the menu reads CONVERTED_OBJECT + 0x00

    And FUN_000f7498 names the object exactly:

        void FUN_000f7498(void) {
          iVar2 = 0; iVar1 = 0;
          do {
            FUN_000f6fc8(DAT_003925b0 + iVar1 + 0xc0);   // <-- pad + 0xC0, +0x110
            iVar2 = iVar2 + 1;  iVar1 = iVar1 + 0x50;
          } while (iVar2 < 2);
        }

    So there are TWO converted-input objects, at `pad + 0xC0` and `pad + 0x110` (stride 0x50), and each
    holds its logical button bits at `+0x00`.

    THIS SCRIPT:
      1. reads both objects' first words and the surrounding fields, to show the converted state;
      2. sets a READ watchpoint on `pad + 0xC0 + 0x00` -- the set of functions reading that value IS the
         set of input consumers, enumerated rather than searched;
      3. does the same for `pad + 0x110 + 0x00` as a control.

    Contrast with section 78: there the read watchpoint was on the RAW button word and found 5 sites, 3 of
    them the pad layer itself. Here the address is the CONVERTED state, so the pad layer should mostly
    vanish from the list and the menu-side readers should appear.

USAGE
    python scripts/psp-converted-input.py --target 40
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
PORT0 = 0x00C0
PORT1 = 0x0110

FUNCS = [(0x000f6fc8, 0x000f7028, "FUN_000f6fc8 (stores converted bits)"),
         (0x000f7028, 0x000f70c4, "FUN_000f7028 (2nd reader)"),
         (0x000f7138, 0x000f71b0, "FUN_000f7138 (reads raw, calls translator)"),
         (0x000f70c4, 0x000f7138, "FUN_000f70c4 (the bit-mapping translator)"),
         (0x000f7498, 0x000f74f0, "FUN_000f7498 (drives both ports)"),
         (0x000f73f0, 0x000f7420, "FUN_000f73f0"),
         (0x000f778c, 0x000f77f0, "FUN_000f778c"),
         (0x000f55f0, 0x000f5670, "FUN_000f55f0"),
         (0x001e5efc, 0x001e6140, "FUN_001e5efc"),
         (0x001e7718, 0x001e79c0, "FUN_001e7718"),
         (0x001e86cc, 0x001e8880, "FUN_001e86cc"),
         (0x001e904c, 0x001e9350, "FUN_001e904c"),
         (0x001e974c, 0x001e98c0, "FUN_001e974c"),
         (0x00287634, 0x002879c0, "FUN_00287634"),
         (0x0028f224, 0x0028f3e0, "FUN_0028f224"),
         (0x002cda1c, 0x002cdaa0, "FUN_002cda1c"),
         (0x002cdba4, 0x002cdd20, "FUN_002cdba4"),
         (0x001fc36c, 0x001fc6d0, "FUN_001fc36c"),
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
    ap.add_argument("--button", default="cross")
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
    want = {"cross": 0x4000, "down": 0x0040, "up": 0x0010, "circle": 0x2000}.get(a.button, 0x4000)
    print()
    print("pad object 0x%08X" % PAD)
    print("converted-input objects: 0x%08X (pad+0xC0) and 0x%08X (pad+0x110), stride 0x50"
          % (PAD + PORT0, PAD + PORT1))

    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    # --- 1. read the converted objects ---
    print()
    print("=== 1. the converted-input objects ===")
    for label, off in (("port0 pad+0xC0", PORT0), ("port1 pad+0x110", PORT1)):
        base = PAD + off
        print("  --- %s @ 0x%08X ---" % (label, base))
        raw = c.read(base, 0x50)
        if not raw or len(raw) != 0x50:
            print("     read failed"); continue
        for o in range(0, 0x50, 4):
            v = struct.unpack_from("<I", raw, o)[0]
            tag = "PTR" if 0x08800000 <= v < 0x0A000000 else ""
            mark = "  <== LOGICAL BUTTON BITS (converted)" if o == 0 else ""
            print("     +0x%02X = 0x%08X %s%s" % (o, v, tag, mark))

    # --- 2. read-watch the converted state ---
    print()
    print("=== 2. READ watchpoint on port0 + 0x00 (the converted logical bits) ===")
    ADDR = PAD + PORT0
    print("   watching 0x%08X" % ADDR)

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
                                  "address": addr, "size": 4, "type": "read"}))
            drain(c.ws, 0.06)
            time.sleep(0.18)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st["pc"])
            time.sleep(0.03)
        clear_resume()
        return pcs

    pcs = collect(ADDR, a.target)
    print("   hits %d  distinct PCs %d" % (len(pcs), len(set(pcs))))
    print()
    print("=== READERS of the CONVERTED input state (the input consumers) ===")
    for pc, n in Counter(pcs).most_common():
        print("   0x%08X x%-3d vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

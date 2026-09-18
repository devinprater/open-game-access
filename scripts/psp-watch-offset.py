#!/usr/bin/env python3
"""psp-watch-offset.py -- separate the node's INDEX field (+0x0C) from its POINTER field (+0x10).

THE DISCRIMINATION (doc section 70)
    Section 70 found the node field 0x08C09344 has two DISJOINT writer sets:
      * a per-frame REPAINT set (writes with no input) -- ruled out,
      * a PRESS-ONLY set: FUN_0024932c (node-list manager) and FUN_0025468c (def lookup base+index*0x1c).
    Both write the same 64-byte node, so neither is yet identified as the *index* holder.

    Section 61's recipe says the node carries two distinct things:
      * node + 0x0C : a SHORT index, used with a 0x1c stride (confirmed three times: s39, s61, and the
                      FUN_0025468c store cluster in s70)
      * node + 0x10 : a POINTER; the value passed to the draw call is read_s16(read32(node+0x10) + 4)

    So watching those two offsets SEPARATELY, each with the press/idle control, says which one the input
    path actually writes. If one has press-only writers and the other only repaint writers, the input
    path touches the first and not the second -- which localises the selection handling to a 4-byte slot.

USAGE
    python scripts/psp-watch-offset.py --node 0x08C09344 --button down --hits 5
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

FUNCS = [(0x00246d64, 0x00247000, "FUN_00246d64"),
         (0x00247b40, 0x00248000, "FUN_00247b40"),
         (0x002489d0, 0x00248c00, "FUN_002489d0"),
         (0x0024910c, 0x00249300, "FUN_0024910c (list add)"),
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
    ap.add_argument("--node", type=lambda x: int(x, 0), default=0x08C09344)
    ap.add_argument("--button", default="down")
    ap.add_argument("--hits", type=int, default=5)
    ap.add_argument("--hold", type=int, default=40)
    ap.add_argument("--win", type=float, default=1.1)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen -- restart the emulator before running this.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    want = BITS[a.button]

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(x):
        b = c.read(x, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def s16(x):
        b = c.read(x, 2)
        return struct.unpack("<h", b)[0] if b and len(b) == 2 else None

    # the two candidate slots
    IDX = a.node + 0x0C      # the short index (2 bytes)
    PTR = a.node + 0x10      # the pointer (4 bytes)
    print()
    print("node 0x%08X" % a.node)
    print("   +0x0C index short = %s   (watch 0x%08X, size 2)" % (s16(IDX), IDX))
    print("   +0x10 pointer     = 0x%08X  (watch 0x%08X, size 4)" % (u32(PTR) or 0, PTR))
    q = u32(PTR)
    if q and 0x08800000 <= q < 0x0A000000:
        print("   -> target +4 (the draw-call value) = %s" % s16(q + 4))

    def clear_resume():
        c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 5}))
        drain(c.ws, 0.22)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws, 0.3)

    def arm(addr, size):
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                              "address": addr, "size": size, "type": "write"}))
        drain(c.ws, 0.3)

    def collect(addr, size, n, do_press):
        pcs, delivered, tries = [], 0, 0
        while len(pcs) < n and tries < n * 10:
            tries += 1
            clear_resume()
            arm(addr, size)
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + tries,
                                        "button": a.button, "frames": a.hold}))
                for _ in range(3):
                    w = padword()
                    if w is not None and (w & want) == want:
                        delivered += 1
                        break
            time.sleep(a.win)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st["pc"])
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 200 + tries,
                                        "button": a.button, "frames": 2}))
            time.sleep(0.25)
        clear_resume()
        return pcs, delivered, tries

    report = {}
    for label, addr, size in (("+0x0C index short", IDX, 2), ("+0x10 pointer", PTR, 4)):
        print()
        print("############ watching %s  0x%08X size %d ############" % (label, addr, size))
        print("  --- PRESS phase (%d verified %s) ---" % (a.hits, a.button))
        p_pcs, delivered, tries = collect(addr, size, a.hits, True)
        print("     delivered %d  tries %d  write-hits %d" % (delivered, tries, len(p_pcs)))
        for pc, n in Counter(p_pcs).most_common():
            print("       0x%08X x%d  vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

        print("  --- IDLE phase (no input, equal windows) ---")
        i_pcs, _, itries = collect(addr, size, a.hits, False)
        print("     tries %d  write-hits %d" % (itries, len(i_pcs)))
        for pc, n in Counter(i_pcs).most_common():
            print("       0x%08X x%d  vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

        ps, is_ = set(p_pcs), set(i_pcs)
        report[label] = (ps, is_, ps - is_)
        print("  --- discrimination for %s ---" % label)
        print("     press-only: %s" % ([hex(p) for p in sorted(ps - is_)] or "none"))
        print("     idle-only : %s" % ([hex(p) for p in sorted(is_ - ps)] or "none"))
        print("     both      : %s" % ([hex(p) for p in sorted(ps & is_)] or "none"))
        if not p_pcs and not i_pcs:
            print("     => NEVER WRITTEN in either phase (with the CPU resumed each window)")
        elif ps - is_:
            print("     => PRESS-SPECIFIC writer(s): %s"
                  % ", ".join("%s@0x%08X" % (fname(p - 0x08804000), p) for p in sorted(ps - is_)))
        elif p_pcs:
            print("     => REPAINT only: every writer also writes with no input")

    print()
    print("=== SUMMARY: which node slot does the input path touch? ===")
    for label, (ps, is_, only) in report.items():
        if not ps and not is_:
            v = "never written"
        elif only:
            v = "PRESS-SPECIFIC (%d writer(s))" % len(only)
        else:
            v = "repaint only"
        print("   %-18s %s" % (label, v))

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

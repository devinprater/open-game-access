#!/usr/bin/env python3
"""psp-watch-consistent.py -- the section 69 discrimination: is the writer press-SPECIFIC or a repaint?

THE QUESTION
    Section 69 watched the index-source structures and got 6 hits on node N+1 (0x08C09344) across 6
    presses, from SIX DIFFERENT PCs in three routines. It noted the ambiguity:

        "six PCs, one per press, from three different routines, is consistent with a REPAINT -- every node
         field touched as the list redraws -- rather than with a single write of a single index. A
         selection write should be the SAME PC every time."

    This script settles it by comparing two PC sets on the SAME watched field:

        PRESS phase : 6 verified-delivered presses -> the PC set that writes the field
        IDLE phase  : 6 equal-length windows with NO input -> the PC set that writes the field

    Interpretation:
        * PC set identical in both phases          -> REPAINT. The field is rewritten every frame
                                                      regardless of input; it cannot be the index.
        * a PC present ONLY in the press phase     -> **PRESS-SPECIFIC WRITER**. That PC is the code
                                                      that responds to the menu input, and is the
                                                      strongest candidate for an index write.

    The IDLE phase is the no-press control applied to a WATCHPOINT rather than to a value -- which is the
    instrument-appropriate form of Rule 120 when the observable is "who wrote" rather than "what changed".

USAGE
    python scripts/psp-watch-consistent.py --button down --addr 0x08C09344 --size 0x40 --hits 6
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
MGR_PTR = 0x08804000 + 0x00397770
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}

# known function map, from earlier sections
FUNCS = [(0x00246d64, 0x00247000, "FUN_00246d64"),
         (0x00247b40, 0x00248000, "FUN_00247b40"),
         (0x002489d0, 0x00248c00, "FUN_002489d0 (menu-manager ctor)"),
         (0x0024910c, 0x00249300, "FUN_0024910c (list add)"),
         (0x0024932c, 0x00249700, "FUN_0024932c (node-list manager)"),
         (0x0024adf8, 0x0024b000, "FUN_0024adf8 (menu sound loader)"),
         (0x0024aee0, 0x0024c200, "FUN_0024aee0 (RENDER LOOP)"),
         (0x0025468c, 0x00254750, "FUN_0025468c (def lookup idx*0x1c)"),
         (0x0025595c, 0x00256198, "FUN_0025595c (render writer)"),
         (0x002dbf50, 0x002dc400, "FUN_002dbf50 (boot/init)")]


def fname(vaddr):
    for lo, hi, nm in FUNCS:
        if lo <= vaddr < hi:
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
        return False, False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0, b.get("stepping", False)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--button", default="down")
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x08C09344)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0x40)
    ap.add_argument("--hits", type=int, default=6)
    ap.add_argument("--hold", type=int, default=40)
    ap.add_argument("--win", type=float, default=1.2)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    ok, _ = live(c, "before")
    if not ok:
        print("REFUSING: frozen.")
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

    M = u32(MGR_PTR)
    print()
    print("manager 0x%08X   watching 0x%08X size 0x%X" % (M, a.addr, a.size))
    print("value now: 0x%08X" % (u32(a.addr) or 0))

    def clear_resume():
        c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 5}))
        drain(c.ws, 0.25)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws, 0.35)

    def arm():
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                              "address": a.addr, "size": a.size, "type": "write"}))
        drain(c.ws, 0.35)

    def collect(n, do_press):
        """Arm, optionally press (verified), read the halt PC, resume, repeat. Returns PC list."""
        pcs = []
        delivered = 0
        tries = 0
        while len(pcs) < n and tries < n * 10:
            tries += 1
            clear_resume()
            arm()
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + tries,
                                        "button": a.button, "frames": a.hold}))
                hit = False
                for _ in range(3):
                    w = padword()
                    if w is not None and (w & want) == want:
                        hit = True
                        break
                if hit:
                    delivered += 1
            # wait the same window either way
            time.sleep(a.win)
            st = cpu(c)
            if st and st.get("stepping") and st.get("pc"):
                pcs.append(st.get("pc"))
            else:
                # no hit in this window; still count the attempt
                pass
            if do_press:
                pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 200 + tries,
                                        "button": a.button, "frames": 2}))
            time.sleep(0.25)
        clear_resume()
        return pcs, delivered, tries

    print()
    print("=== PRESS phase: %d verified '%s' presses ===" % (a.hits, a.button))
    press_pcs, delivered, tries = collect(a.hits, True)
    print("  delivered %d  tries %d  write-hits captured %d" % (delivered, tries, len(press_pcs)))
    if press_pcs:
        for pc, n in Counter(press_pcs).most_common():
            print("     0x%08X x%d  vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    print()
    print("=== IDLE phase: %d equal-length windows, NO input ===" % a.hits)
    idle_pcs, _, itries = collect(a.hits, False)
    print("  tries %d  write-hits captured %d" % (itries, len(idle_pcs)))
    if idle_pcs:
        for pc, n in Counter(idle_pcs).most_common():
            print("     0x%08X x%d  vaddr 0x%08X  %s" % (pc, n, pc - 0x08804000, fname(pc - 0x08804000)))

    ps, is_ = set(press_pcs), set(idle_pcs)
    print()
    print("=== DISCRIMINATION ===")
    print("  PCs in PRESS only : %s" % ([hex(p) for p in sorted(ps - is_)] or "none"))
    print("  PCs in IDLE only  : %s" % ([hex(p) for p in sorted(is_ - ps)] or "none"))
    print("  PCs in BOTH       : %s" % ([hex(p) for p in sorted(ps & is_)] or "none"))
    print()
    if not press_pcs and not idle_pcs:
        print("  NO WRITES AT ALL in either phase -- the field is not rewritten frame to frame.")
        print("  (With the CPU resumed before every window this is a valid observation.)")
    elif ps and not (ps - is_):
        print("  VERDICT: every press-phase writer also writes with NO input ->")
        print("           the field is REPAINTED every frame; it is NOT a press-specific index.")
    elif ps - is_:
        print("  VERDICT: PRESS-SPECIFIC WRITER(S) FOUND:")
        for p in sorted(ps - is_):
            print("           0x%08X  vaddr 0x%08X  %s" % (p, p - 0x08804000, fname(p - 0x08804000)))
        print("           These write the watched field only when input arrives -- CURSOR CANDIDATES.")
    else:
        print("  VERDICT: inconclusive (press phase captured no writes).")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

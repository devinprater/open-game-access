#!/usr/bin/env python3
"""psp-event-state.py -- verify the input EVENT layout live, and test whether pad+0xC0 IS the event object.

THE HYPOTHESIS (doc sections 80 + 85)
    Section 80: FUN_000f7498 calls FUN_000f6fc8(DAT_003925b0 + iVar1 + 0xC0) for iVar1 = 0, 0x50 -- so the
    CONVERTED-INPUT objects are `pad + 0xC0` and `pad + 0x110`. FUN_000f6fc8 stores the translated bits at
    `*param_1` (offset 0x00).

    Section 85: FUN_000f7028 receives the same translated bits and passes them to FUN_000f6694, which
    computes edge state:
        param_1[1] = B_cur;  param_1[0] = A_cur;                  (+0x04, +0x00  current)
        param_1[3] = B_just_pressed; param_1[2] = A_just_pressed; (+0x0C, +0x08  JUST-PRESSED)
        param_1[5] = B_just_released; param_1[4] = A_just_released;(+0x14, +0x10 JUST-RELEASED)
        param_1[7] = sentinel_B; param_1[6] = sentinel_A;         (+0x1C, +0x18 changed flags)
        param_1[0xc] = repeat timer (float)                       (+0x30)
    And sections 78/81's READ watchpoints landed inside FUN_000f6694 when watching `pad + 0xC0` -- meaning
    FUN_000f6694 READS that object. So `param_1` is very likely `pad + 0xC0` (or `pad + 0x110`).

    If true, the EVENT OBJECT IS `pad + 0xC0`, and its just-pressed field at `+0x08` is the input the menu
    consumes.

THE TEST (pure reads + a gated press -- cannot freeze anything)
    1. read both candidate objects in full,
    2. press a button with the INPUT GATE (verified at the pad button word),
    3. re-read immediately: `+0x00`/`+0x04` should hold the CURRENT logical bits, and `+0x08`/`+0x0C` should
       hold the JUST-PRESSED bits for the frame(s) right after the press,
    4. re-read after a pause: the just-pressed field should CLEAR (edge = one frame).

USAGE
    python scripts/psp-event-state.py --button cross
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
    want = BITS[a.button]
    print("pad 0x%08X   button '%s' bit 0x%04X" % (PAD, a.button, want))

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def snap(addr, n=0x34):
        b = c.read(addr, n)
        if not b or len(b) != n:
            return None
        return list(struct.unpack_from("<%dI" % (n // 4), b, 0))

    def show(label, vals):
        if vals is None:
            print("   %s: read failed" % label)
            return
        # interpret +0x00 current, +0x08 just-pressed, +0x10 just-released, +0x30 timer
        print("   %s @ 0x%08X" % (label, 0))
        print("      +0x00 current  = 0x%08X 0x%08X" % (vals[0], vals[1]))
        print("      +0x08 pressed  = 0x%08X 0x%08X" % (vals[2], vals[3]))
        print("      +0x10 released = 0x%08X 0x%08X" % (vals[4], vals[5]))
        print("      +0x18 changed  = 0x%08X 0x%08X" % (vals[6], vals[7]))
        f = struct.unpack("<f", struct.pack("<I", vals[12]))[0]
        print("      +0x30 timer    = %g" % f)

    for label, off in (("pad+0xC0", 0xC0), ("pad+0x110", 0x110)):
        addr = PAD + off
        print()
        print("=== %s (0x%08X) at rest ===" % (label, addr))
        v = snap(addr)
        if v:
            print("      +0x00 current  = 0x%08X 0x%08X" % (v[0], v[1]))
            print("      +0x08 pressed  = 0x%08X 0x%08X  <-- JUST-PRESSED?" % (v[2], v[3]))
            print("      +0x10 released = 0x%08X 0x%08X  <-- JUST-RELEASED?" % (v[4], v[5]))
            print("      +0x18 changed  = 0x%08X 0x%08X" % (v[6], v[7]))
            f = struct.unpack("<f", struct.pack("<I", v[12]))[0]
            print("      +0x30 timer    = %g  (repeat timer float)" % f)

    # --- press with the gate, sampling DURING and AFTER ---
    print()
    print("=== gated press of '%s', sampling during and after ===" % a.button)
    ADDR = PAD + 0xC0
    got = 0
    tries = 0
    while got == 0 and tries < 30:
        tries += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + tries,
                                "button": a.button, "frames": 30}))
        delivered = False
        for _ in range(3):
            w = padword()
            if w is not None and (w & want) == want:
                delivered = True
                break
        if delivered:
            got = 1
            # sample repeatedly during the hold
            for k in range(5):
                v = snap(ADDR)
                if v:
                    f = struct.unpack("<f", struct.pack("<I", v[12]))[0]
                    print("   t=+%d  current 0x%08X/0x%08X  pressed 0x%08X/0x%08X  released 0x%08X/0x%08X  timer %g"
                          % (k, v[0], v[1], v[2], v[3], v[4], v[5], f))
                time.sleep(0.18)
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 200 + tries,
                                "button": a.button, "frames": 2}))
        time.sleep(0.5)

    print()
    print("=== after release (just-pressed should CLEAR) ===")
    for k in range(4):
        v = snap(ADDR)
        if v:
            f = struct.unpack("<f", struct.pack("<I", v[12]))[0]
            print("   t=+%d  current 0x%08X/0x%08X  pressed 0x%08X/0x%08X  released 0x%08X/0x%08X  timer %g"
                  % (k, v[0], v[1], v[2], v[3], v[4], v[5], f))
        time.sleep(0.25)

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

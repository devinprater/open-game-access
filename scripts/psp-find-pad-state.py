#!/usr/bin/env python3
"""psp-find-pad-state.py -- locate the pad object's BUTTON-STATE field using memory-level delivery.

WHY THIS TARGETS THE MISSING PIECE
    Codex (doc section 55) named exactly what is missing: "the menu-side input consumer/state-update
    routine showing which field changes when Up/Down changes the highlighted logical item." To find a
    consumer you need the address it consumes -- i.e. WHERE the polled button state lives.

    From the decompiles: `DAT_003925b0` is a static holding a pointer to the pad object
    (`&DAT_0132fb40`), and `FUN_000f790c` writes its flags at `+0x264`. So the object is addressable:

        padptr_slot = 0x08804000 + 0x003925b0      (the static)
        pad        = read32(padptr_slot)           (the object)
        flags      = pad + 0x264                   (already known)

    This scans a window of the pad object for fields that change when a button is pressed and NOT when
    it is not -- using the memory-level no-press control (Rule 146), which works on any screen and does
    not care whether the display animates.

    The result is a concrete address (pad + offset) that the game's input consumer must read, which is
    what a follow-up code query needs.

USAGE
    python scripts/psp-find-pad-state.py --button cross --span 0x400
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
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x400)
    ap.add_argument("--samples", type=int, default=8)
    ap.add_argument("--wait", type=float, default=0.6)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator frozen.")
        c.close(); pad.close()
        return 2

    raw = c.read(PAD_SLOT, 4)
    PAD = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    print()
    print("pad slot 0x%08X -> pad object 0x%08X" % (PAD_SLOT, PAD))
    if not (0x08800000 <= PAD < 0x0A000000):
        print("  pad pointer is not a valid RAM address")
        c.close(); pad.close()
        return 3

    nwords = a.span // 4

    def snap():
        b = c.read(PAD, a.span)
        if not b or len(b) < a.span:
            return None
        return list(struct.unpack_from("<%di" % nwords, b, 0))

    print()
    print("=== NO-PRESS baseline (%d samples of 0x%X bytes) ===" % (a.samples, a.span))
    base = []
    for i in range(a.samples):
        s = snap()
        base.append(s)
        print("  %d" % (i + 1), end="\r", flush=True)
        time.sleep(a.wait)
    print()

    print("=== PRESS phase ('%s' x%d, holding each press) ===" % (a.button, a.samples))
    pr = []
    for i in range(a.samples):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 300 + i,
                                "button": a.button, "frames": 20}))
        time.sleep(a.wait)
        pr.append(snap())
        print("  %d" % (i + 1), end="\r", flush=True)
    print()

    # fields constant without a press but changing with one
    churn, resp, only = [], [], []
    for w in range(nwords):
        nv = [s[w] for s in base if s]
        pv = [s[w] for s in pr if s]
        if len(set(nv)) > 1:
            churn.append(w)
        if len(set(pv)) > 1:
            resp.append(w)
        if len(set(pv)) > 1 and len(set(nv)) == 1:
            only.append((w, nv[0], pv))

    print()
    print("=== analysis ===")
    print("  words sampled:                     %d" % nwords)
    print("  vary with NO press (churn):        %d" % len(churn))
    print("  vary during presses:               %d" % len(resp))
    print()
    print("=== PAD FIELDS THAT RESPOND ONLY TO THE PRESS ===")
    if only:
        for w, b0, pv in only:
            print("   pad + 0x%03X   0x%08X -> %s" % (w * 4, PAD + w * 4, pv))
        print()
        print("   These addresses are what the game's input consumer reads.")
    else:
        print("   NONE -- no field of the pad object changed with the press.")
        print("   (With a memory-level control this is a real negative, not a delivery failure.)")

    print()
    print("  reference: the known flags word is pad + 0x264 = 0x%08X" % (PAD + 0x264))
    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

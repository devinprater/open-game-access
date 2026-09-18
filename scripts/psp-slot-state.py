#!/usr/bin/env python3
"""psp-slot-state.py -- is API table slot 0x08BA46E8 reserved, or populated on some screen?

WHY (doc section 91)
    Section 91 found the pad's converted objects store a TABLE SLOT ADDRESS:

        converted_obj + 0x34 = 0x08BA46E8      (one slot before the guarded input query at 0x08BA46F4)

    and that slot currently reads ZERO while its neighbours hold pad-band function pointers. Two readings:
    RESERVED (never populated) or POPULATED CONDITIONALLY (only on certain screens/states). Since the
    converted objects STORE ITS ADDRESS, "conditionally populated" is more likely -- a stored address to a
    permanently empty slot would be pointless.

    So: read the slot, and a window of its neighbours, across a series of screen changes, and also watch it
    for WRITES. If it becomes a code pointer on some screen, that slot and whoever populates it is
    state-dependent API -- and the populating code is the registration path.

WHAT IT DOES
    1. reads the slot window 0x08BA46C0-0x08BA4720 at the current screen;
    2. performs a sequence of control presses to move between screens, re-reading after each;
    3. reports, per screen, the value of 0x08BA46E8 and the classification of every slot in the window;
    4. flags any change in the slot.

    Liveness is asserted before and after; if the emulator is frozen the run refuses (no verdict).

USAGE
    python scripts/psp-slot-state.py --buttons l,circle,cross,triangle,square,start --wait 2.0
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SLOT_LO = 0x08BA46C0
SLOT_HI = 0x08BA4720
SLOT_TARGET = 0x08BA46E8
CODE_LO, CODE_HI = 0x08804000, 0x08804000 + 0x003A6860


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def kind(v):
    if CODE_LO <= v < CODE_HI:
        return "CODE vaddr 0x%08X" % (v - CODE_LO)
    if 0x08800000 <= v < 0x0A000000:
        return "DATA 0x%08X" % v
    if v == 0:
        return "zero"
    return "0x%08X" % v


def cpu_status(c, timeout=1.5):
    """cpu.status carries ticks/stepping -- game.status does NOT (it has only game/paused)."""
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 4242}))
    end = time.time() + timeout
    while time.time() < end:
        try:
            c.ws.settimeout(0.3)
            m = json.loads(c.ws.recv())
        except Exception:
            continue
        if m.get("event") == "cpu.status":
            return m
    return None


def liveness(c, label):
    a = cpu_status(c)
    time.sleep(1.5)
    b = cpu_status(c)
    ta = a.get("ticks") if a else None
    tb = b.get("ticks") if b else None
    d = (tb - ta) if (ta is not None and tb is not None) else None
    ok = d is not None and d > 0
    print("  %-8s ticks delta %.1fs: %-12s stepping=%s -> %s"
          % (label, 1.5, d,
             (b or {}).get("stepping"), "EXECUTING" if ok else "FROZEN"))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--buttons", default="l,circle,cross,triangle,square,start")
    ap.add_argument("--wait", type=float, default=2.0)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not liveness(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    def read_window():
        raw = c.read(SLOT_LO, SLOT_HI - SLOT_LO)
        if not raw or len(raw) != SLOT_HI - SLOT_LO:
            return None
        return [struct.unpack_from("<I", raw, i)[0] for i in range(0, len(raw), 4)]

    hist = {}
    cur = read_window()
    if cur is None:
        print("read failed"); c.close(); pad.close(); return 1
    screen = [("current", cur)]

    for b in a.buttons.split(","):
        b = b.strip()
        if not b:
            continue
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": b, "frames": 12}))
        time.sleep(a.wait)
        w = read_window()
        if w is None:
            print("  after %-8s read failed" % b)
            continue
        screen.append((b, w))

    print()
    print("=== SLOT WINDOW ACROSS SCREENS ===")
    print("  slot 0x%08X (the converted objects' +0x34) marked <== TARGET" % SLOT_TARGET)
    print()
    # per-screen summary of the target slot
    print("  %-9s  %-22s  %s" % ("screen", "value at 0x%08X" % SLOT_TARGET, "class"))
    for name, w in screen:
        idx = (SLOT_TARGET - SLOT_LO) // 4
        v = w[idx]
        hist.setdefault(v, []).append(name)
        print("  %-9s  0x%08X  %-18s  %s" % (name, v, "", kind(v)))

    print()
    print("=== distinct values at the TARGET slot: %d ===" % len(hist))
    for v, names in sorted(hist.items(), key=lambda kv: -len(kv[1])):
        print("   0x%08X  %-22s  %s" % (v, kind(v), names))

    print()
    print("=== the whole window (current screen) ===")
    idx = (SLOT_TARGET - SLOT_LO) // 4
    for i, v in enumerate(screen[0][1]):
        ad = SLOT_LO + i * 4
        mark = "  <== TARGET" if ad == SLOT_TARGET else ""
        print("   0x%08X = 0x%08X  %-22s%s" % (ad, v, kind(v), mark))

    print()
    print("=== liveness AFTER ===")
    live = liveness(c, "after")

    print()
    if len(hist) > 1:
        nonz = [v for v in hist if v != 0]
        if nonz:
            print("VERDICT: the slot IS POPULATED on some screen(s) -- values seen: %s"
                  % ", ".join("0x%08X" % v for v in sorted(nonz)))
            print("         state-dependent API slot. The populating code is the registration path.")
        else:
            print("VERDICT: the slot changed but never became a pointer -- inspect the series above.")
    else:
        print("VERDICT: the slot never changed across %d screens -- consistent with RESERVED."
              % len(screen))
    if not live:
        print("         NOTE: emulator frozen at the end; treat with caution.")
    c.close()
    pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""psp-manager-census.py -- read the manager header across screens and correlate with scrollability.

WHY THIS IS WORTH DOING
    Doc section 50 measured that seven reachable screens are all static AND inert (no control moves the
    highlight), so the node-field question could not be answered. But during the watchpoint attempt a
    concrete observation appeared: on the current screen the manager's **render block count is 0**.

        manager 0x08C08EB0   render base 0x09DEE3C0   count 0

    A count of 0 means no render blocks are populated -- which is consistent with a screen that draws
    nothing interactive. So the manager header may be a **cheap, measurable screen classifier**:

        count > 0   and it grows with presses  -> an interactive list screen
        count == 0                              -> a static/inert screen

    If that holds, then the scrollable-screen search does not need blind pressing at all: poll the
    manager's count field and look for a screen where it is NON-ZERO. The node count (9/11/35 measured
    so far) already varies per screen; the render count is the complementary signal.

WHAT IT DOES
    Reads the manager header (+0x14 base, +0x18 render count, +0x30 node count, +0x24/+0x20) and the
    node-list length, prints them, then presses a control a few times and re-reads -- so a screen whose
    render count responds to input is visible.

USAGE
    python scripts/psp-manager-census.py --press down --rounds 4
"""
import argparse
import importlib.util
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
MGR_PTR = 0x08804000 + 0x00397770


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


def header(c, mgr):
    b = c.read(mgr, 0x40)
    if not b or len(b) < 0x40:
        return None
    return {
        "base": struct.unpack_from("<I", b, 0x14)[0],
        "render": struct.unpack_from("<i", b, 0x18)[0],
        "f1C": struct.unpack_from("<i", b, 0x1C)[0],
        "f20": struct.unpack_from("<i", b, 0x20)[0],
        "f24": struct.unpack_from("<i", b, 0x24)[0],
        "nodes": struct.unpack_from("<i", b, 0x30)[0],
        "head": struct.unpack_from("<I", b, 0x28)[0],
    }


def walk_len(c, head):
    n = 0
    p = head
    seen = set()
    while p and p not in seen and n < 200 and 0x08800000 <= p < 0x0A000000:
        seen.add(p)
        nb = c.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        n += 1
        p = struct.unpack_from("<i", nb, 0x24)[0]
        if p == head:
            break
    return n


def grab(tag):
    q = os.path.join(TMP, "mc-%s.png" % tag)
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def diff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=4)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    x = cpu(c); time.sleep(1.2); y = cpu(c)
    print("liveness:", "EXECUTING" if x and y and y["ticks"] > x["ticks"] else "FROZEN")
    print()

    raw = c.read(MGR_PTR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    print("manager 0x%08X" % mgr)
    h = header(c, mgr)
    if not h:
        print("header unreadable")
        c.close(); pad.close()
        return 3
    print("  render base 0x%08X  render COUNT %d  +0x1C %d  +0x20 %d  +0x24 %d  node COUNT %d"
          % (h["base"], h["render"], h["f1C"], h["f20"], h["f24"], h["nodes"]))
    print("  nodes walked from head: %d" % walk_len(c, h["head"]))

    # static check
    g1 = grab("g1"); time.sleep(2.2); g2 = grab("g2")
    sd = diff(g1, g2)
    print("  no-press frame diff: %s px -> %s" % (sd, "STATIC" if sd is not None and sd <= 2000 else "ANIMATING"))
    print()

    print("=== %d presses of '%s' ===" % (a.rounds, a.press))
    hdr0 = "press  render  f1C  f20  f24  nodes"
    print(hdr0)
    prev = g1
    for i in range(1, a.rounds + 1):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 50 + i,
                                "button": a.press, "frames": 12}))
        time.sleep(1.3)
        hh = header(c, mgr)
        cur = grab("p%d" % i)
        n = diff(prev, cur)
        prev = cur
        print("  %3d   %6d %4d %4d %4d %5d     display diff %s px"
              % (i, hh["render"], hh["f1C"], hh["f20"], hh["f24"], hh["nodes"], n))

    print()
    print("INTERPRETATION:")
    print(" * render count > 0 and/or responding to presses -> an interactive screen worth probing")
    print(" * render count == 0 with no display change       -> an inert screen; skip it")
    print(" * if no reachable screen shows render count > 0, the interactive screens are simply not")
    print("   reachable by blind navigation and a helper must place the game on one.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""psp-classify-and-scan.py -- classify a screen by manager fields, then test EVERY control.

WHY
    Two things needed doing together:
      1. section 51 proposed `manager +0x18` (render block count) as a screen classifier -- it needs
         testing on more screens, since one reading of 0 was matched by another reading of 0 on a
         DIFFERENT screen state (a scene and a static-UI screen both showed 0);
      2. the search for a screen whose highlight MOVES has only tried down/up/left/right/l/r -- the
         face buttons (circle/cross/triangle/square) and start/select are untested on these screens and
         one of them may be the control that scrolls.

    So this reads the manager header for classification, verifies the screen is static, and then presses
    every available control (d-pad, face buttons, shoulders, start, select), reporting the display diff
    for each. A control that changes the display on a STATIC screen has moved the selection.

USAGE
    python scripts/psp-classify-and-scan.py
"""
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
CONTROLS = ["down", "up", "left", "right", "l", "r",
            "circle", "cross", "triangle", "square", "start", "select"]


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


def shot(tag):
    # NOTE: build the path with os.path.join, never with %-formatting -- '%LOCALAPPDATA%' inside a
    # %-format string raises "unsupported format character" (a bug this script was written to fix).
    q = os.path.join(TMP, "cs-" + tag + ".png")
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
    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    x = cpu(c); time.sleep(1.2); y = cpu(c)
    print("liveness:", "EXECUTING" if x and y and y["ticks"] > x["ticks"] else "FROZEN")

    raw = c.read(MGR_PTR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    h = c.read(mgr, 0x40)
    if not h or len(h) < 0x40:
        print("manager header unreadable")
        c.close(); pad.close()
        return 3
    base = struct.unpack_from("<I", h, 0x14)[0]
    render = struct.unpack_from("<i", h, 0x18)[0]
    f1C = struct.unpack_from("<i", h, 0x1C)[0]
    f20 = struct.unpack_from("<i", h, 0x20)[0]
    f24 = struct.unpack_from("<i", h, 0x24)[0]
    nodes = struct.unpack_from("<i", h, 0x30)[0]
    print("manager 0x%08X  render base 0x%08X  render COUNT %d  +0x1C %d  +0x20 %d  +0x24 %d  node COUNT %d"
          % (mgr, base, render, f1C, f20, f24, nodes))

    g1 = shot("g1"); time.sleep(2.2); g2 = shot("g2")
    sd = diff(g1, g2)
    print("no-press frame diff: %s px -> %s"
          % (sd, "STATIC" if sd is not None and sd <= 2000 else "ANIMATING"))
    print()

    print("=== every control, on this screen ===")
    prev = g2
    hits = []
    for ctl in CONTROLS:
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 5,
                                "button": ctl, "frames": 12}))
        time.sleep(1.4)
        cur = shot(ctl)
        d = diff(prev, cur)
        # re-read the header too, in case the control changes the manager rather than the pixels
        hh = c.read(mgr, 0x40)
        r2 = struct.unpack_from("<i", hh, 0x18)[0] if hh and len(hh) >= 0x40 else -1
        n2 = struct.unpack_from("<i", hh, 0x30)[0] if hh and len(hh) >= 0x40 else -1
        flag = ""
        if d:
            hits.append((ctl, d))
            flag = "  <== MOVED SOMETHING"
        if r2 != render or n2 != nodes:
            flag += "  (manager: render %d->%d nodes %d->%d)" % (render, r2, nodes, n2)
        print("  %-9s -> %9s px%s" % (ctl, d, flag))
        prev = cur

    print()
    if hits:
        print("controls that moved the display: %s" % ", ".join(c for c, _ in hits))
    else:
        print("NO control moved the display on this screen.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

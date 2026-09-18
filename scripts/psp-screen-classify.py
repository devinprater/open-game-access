#!/usr/bin/env python3
"""psp-screen-classify.py -- sample the manager header across screens to find a RELIABLE classifier.

WHY (doc section 51 asked for exactly this)
    Section 51 proposed `manager +0x18` (render count) as a screen classifier from a single reading.
    Readings since then:

        screen state                  render +0x18   +0x1C  +0x20  +0x24   node COUNT
        animating 3D scene                  0          0     -1      1        11
        static UI screen (via 'l')          0          0     -1      1        11
        frozen / halted                     1          0      1      1         9
        pause menu (s41/s44)                3         12     -1      1        35

    So `render count == 0` does NOT discriminate a scene from a static UI screen -- two different states
    both read 0. The `+0x20` field DOES differ between them (`-1` vs `1`), which makes it the better
    candidate.

WHAT THIS DOES
    Reads the manager header and reports every field, then measures the two cheap screen properties
    (liveness via ticks, staticness via a no-press frame diff) and prints them side by side. Running it
    across several states accumulates the sample table needed to pick a classifier on evidence rather
    than on one reading.

    It REFUSES to interpret if the CPU is not executing, because a halted emulator's header is not a
    screen's header.

USAGE
    python scripts/psp-screen-classify.py            # one sample
    python scripts/psp-screen-classify.py --tag "pause menu"
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
FIELDS = [("base", 0x14, "I"), ("render", 0x18, "i"), ("f1C", 0x1C, "i"),
          ("f20", 0x20, "i"), ("f24", 0x24, "i"), ("nodes", 0x30, "i"), ("head", 0x28, "I")]


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
    q = os.path.join(TMP, "sc-" + tag + ".png")
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
    ap.add_argument("--tag", default="sample")
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()

    x = cpu(c); time.sleep(1.5); y = cpu(c)
    live = bool(x and y and y.get("ticks", 0) > x.get("ticks", 0))
    delta = (y.get("ticks", 0) - x.get("ticks", 0)) if (x and y) else 0

    raw = c.read(MGR_PTR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    h = c.read(mgr, 0x40)

    vals = {}
    if h and len(h) >= 0x40:
        for name, off, kind in FIELDS:
            vals[name] = struct.unpack_from("<" + kind, h, off)[0]

    g1 = shot("a"); time.sleep(2.2); g2 = shot("b")
    sd = diff(g1, g2)

    print("tag: %s" % a.tag)
    print("  liveness: ticks delta %-13d -> %s" % (delta, "EXECUTING" if live else "FROZEN"))
    print("  static  : no-press frame diff %-8s px -> %s"
          % (sd, "STATIC" if sd is not None and sd <= 2000 else ("ANIMATING" if sd is not None else "?")))
    print("  manager 0x%08X" % mgr)
    print("    render base 0x%08X   render COUNT %-4d +0x1C %-4d +0x20 %-4d +0x24 %-4d node COUNT %-4d"
          % (vals.get("base", 0), vals.get("render", -1), vals.get("f1C", -1),
             vals.get("f20", -99), vals.get("f24", -99), vals.get("nodes", -1)))
    if not live:
        print("  NOTE: the CPU is FROZEN -- these header values are not a screen's state.")
    c.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

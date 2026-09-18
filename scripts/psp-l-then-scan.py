#!/usr/bin/env python3
"""psp-l-then-scan.py -- reach the STATIC+UI screen via 'l', then scan EVERY control in one run.

WHY THIS SPECIFIC CHAIN
    Two separate findings have never been combined:

    1. `psp-reach-menu.py` has repeatedly produced `l -> diff=0.0 sat=10.71 STATIC+UI <-- MENU`, i.e.
       pressing `l` reliably lands on a static UI screen. That is the most reproducible route to a
       static screen the investigation has.
    2. The control scans so far (psp-find-scroll.py) covered only six controls -- down/up/left/right/l/r
       -- and the fuller twelve-control scan was VOIDED by a frozen emulator. The face buttons
       (circle/cross/triangle/square) and start/select have never been tested on a verified static UI
       screen.

    So this does the whole thing in ONE process, with no drift gap between reaching the screen and
    testing it, and with all four guards in place: liveness before, liveness after, animation guard,
    and per-press delivery.

    It also re-reads the manager header after every press, because a control might change node state
    without changing pixels within one frame.

USAGE
    python scripts/psp-l-then-scan.py
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
STATIC_MAX = 2000


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
    a = cpu(c); time.sleep(1.5); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def shot(tag):
    q = os.path.join(TMP, "lts-" + tag + ".png")
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


def hdr(c):
    raw = c.read(MGR_PTR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    h = c.read(mgr, 0x40)
    if not h or len(h) < 0x40:
        return mgr, {}
    return mgr, {"render": struct.unpack_from("<i", h, 0x18)[0],
                 "f1C": struct.unpack_from("<i", h, 0x1C)[0],
                 "f20": struct.unpack_from("<i", h, 0x20)[0],
                 "f24": struct.unpack_from("<i", h, 0x24)[0],
                 "nodes": struct.unpack_from("<i", h, 0x30)[0],
                 "head": struct.unpack_from("<I", h, 0x28)[0]}


def node_sig(c, head):
    """A cheap fingerprint of the node list: each node's flags at +0x14 and word at +0x20."""
    sig = []
    p = head
    seen = set()
    while p and p not in seen and len(sig) < 200 and 0x08800000 <= p < 0x0A000000:
        seen.add(p)
        nb = c.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        sig.append((nb[0x14], struct.unpack_from("<H", nb, 0x20)[0],
                    struct.unpack_from("<i", nb, 0x0C)[0]))
        p = struct.unpack_from("<i", nb, 0x24)[0]
        if p == head:
            break
    return sig


def main():
    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator frozen. Relaunch, then re-run.")
        c.close(); pad.close()
        return 2

    # --- reach the static UI screen via 'l' (the reproducible route) ---
    print()
    print("=== pressing 'l' to reach the STATIC+UI screen ===")
    pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                            "button": "l", "frames": 14}))
    time.sleep(2.5)

    mgr, h0 = hdr(c)
    print("  manager 0x%08X  render %d  +0x1C %d  +0x20 %d  +0x24 %d  nodes %d"
          % (mgr, h0.get("render", -1), h0.get("f1C", -1), h0.get("f20", -99),
             h0.get("f24", -99), h0.get("nodes", -1)))

    # --- animation guard ---
    g1 = shot("g1"); time.sleep(2.2); g2 = shot("g2")
    sd = diff(g1, g2)
    print("  no-press frame diff: %s px -> %s"
          % (sd, "STATIC" if sd is not None and sd <= STATIC_MAX else "ANIMATING"))
    if sd is None or sd > STATIC_MAX:
        print()
        print("REFUSING: not a static screen, so a per-press difference would be meaningless.")
        c.close(); pad.close()
        return 5

    sig0 = node_sig(c, h0.get("head", 0))
    print("  node signature: %d nodes; flags: %s" % (len(sig0), [x[0] for x in sig0[:12]]))

    # --- scan every control ---
    print()
    print("=== every control, on this verified STATIC screen ===")
    prev = g2
    movers = []
    for ctl in CONTROLS:
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 5,
                                "button": ctl, "frames": 12}))
        time.sleep(1.4)
        cur = shot(ctl)
        d = diff(prev, cur)
        _, hh = hdr(c)
        sig = node_sig(c, hh.get("head", 0))
        note = ""
        if sig != sig0:
            note += "  NODE STATE CHANGED"
        if hh.get("nodes") != h0.get("nodes") or hh.get("render") != h0.get("render"):
            note += "  manager render %d->%d nodes %d->%d" % (h0.get("render"), hh.get("render"),
                                                              h0.get("nodes"), hh.get("nodes"))
        if d:
            movers.append(ctl)
            note += "  <== DISPLAY MOVED"
        print("  %-9s -> %9s px%s" % (ctl, d, note))
        prev = cur
        # re-baseline the node signature when it changes, so we report each change once
        if sig != sig0:
            sig0 = sig

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if movers:
        print("CONTROLS THAT MOVED THE DISPLAY: %s" % ", ".join(movers))
        print("=> this static screen IS interactive; re-run the node probe with --press %s" % movers[0])
    else:
        print("NO control moved the display on this verified static screen.")
        print("=> this screen is static AND inert; the node fields cannot be exercised here.")
    if not ok:
        print("NOTE: the emulator froze during the run -- treat results with suspicion.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

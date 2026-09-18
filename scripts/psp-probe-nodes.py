#!/usr/bin/env python3
"""psp-probe-nodes.py -- re-run the node-list walk under the VERIFIED harness.

WHY THIS IS THE DECISIVE TEST (doc section 46)
    Section 39 walked the manager's 35-node list and found no field changed across three presses -- but
    it was run on a DIFFERENT screen and with NO LIVENESS ASSERTION. Sections 43-45 showed that a
    halted emulator produces exactly the readings a true negative does, so section 39's negative is of
    the class that cannot be trusted.

    The nodes are the natural home for a per-item highlight flag (flags at +0x14, an active word at
    +0x20, an index at +0x0C, a debounce byte at +0x17), so this is the highest-value remaining test.
    If the nodes are static under a VERIFIED run, the selection is not in the manager's object graph at
    all -- a strong, defensible conclusion rather than another cleared address.

WHAT IS DIFFERENT FROM psp-walk-nodes.py
    * asserts the CPU is EXECUTING before and after (ticks delta);
    * TWO debugger connections -- reads on one, input on the other (reads suppress presses);
    * requires each press to MOVE THE DISPLAY, and voids the result if none does;
    * walks the list FRESH each sample (the manager may rebuild it) and reports the node count, so a
      rebuilt list is visible rather than silently mis-compared;
    * compares node fields BY INDEX, and also reports any node whose ADDRESS changed.

USAGE
    python scripts/psp-probe-nodes.py --press down --rounds 12
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
MGR_PTR_ADDR = 0x08804000 + 0x00397770

# per-node fields recovered from the decompiles
NODE_FIELDS = [("i0C", 0x0C, "i"), ("b14", 0x14, "b"), ("b17", 0x17, "b"),
               ("w20", 0x20, "H"), ("i24", 0x24, "i"), ("i28", 0x28, "i"), ("i3C", 0x3C, "i")]


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


def cpu_status(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def live(c, tag):
    a = cpu_status(c); time.sleep(1.5); b = cpu_status(c)
    if not a or not b:
        print("  %-7s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-7s ticks delta: %-13d stepping=%-5s -> %s"
          % (tag, d, b.get("stepping"), "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def grab(tag):
    q = os.path.join(TMP, "pn-%s.png" % tag)
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def pixdiff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def walk(c, mgr):
    hdr = c.read(mgr, 0x40)
    if not hdr or len(hdr) < 0x40:
        return None, None, []
    head = struct.unpack_from("<i", hdr, 0x28)[0]
    tail = struct.unpack_from("<i", hdr, 0x2C)[0]
    cnt = struct.unpack_from("<i", hdr, 0x30)[0]
    nodes = []
    p = head
    seen = set()
    while p and p not in seen and len(nodes) < 200:
        if not (0x08800000 <= p < 0x0A000000):
            break
        seen.add(p)
        nb = c.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        rec = {"addr": p}
        for name, off, kind in NODE_FIELDS:
            if kind == "b":
                rec[name] = nb[off]
            else:
                rec[name] = struct.unpack_from("<" + kind, nb, off)[0]
        nodes.append(rec)
        nxt = struct.unpack_from("<i", nb, 0x24)[0]
        p = nxt if nxt != head else 0
    return (head, tail, cnt), struct.unpack_from("<i", hdr, 0x30)[0], nodes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--wait", type=float, default=1.3)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator not executing. Kill and relaunch, then re-run.")
        c.close(); pad.close()
        return 2

    # ANIMATION GUARD -- the delivery check below counts movement per press, and an animating SCENE
    # passes that trivially. Measuring the no-press frame difference first distinguishes "the press
    # moved the highlight" from "the screen is never still". Without this a scene run reads as
    # 12/12 delivery and its verdict is void.
    g1 = grab("gate1")
    time.sleep(2.5)
    g2 = grab("gate2")
    base_diff = pixdiff(g1, g2)
    print()
    print("  no-press frame diff: %s px" % base_diff)
    if base_diff is None:
        print("REFUSING: could not capture the screen.")
        c.close(); pad.close()
        return 4
    if base_diff > 2000:
        print("REFUSING: the display is ANIMATING on its own (a scene, not a menu), so a per-press")
        print("movement check cannot distinguish a highlight move from ordinary motion.")
        print("Reach a STATIC screen (two-frame diff ~0) before running this test.")
        c.close(); pad.close()
        return 5
    print("  -> STATIC screen confirmed; a per-press diff is now meaningful.")

    raw = c.read(MGR_PTR_ADDR, 4)
    mgr = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    print()
    print("manager 0x%08X" % mgr)
    hdr, cnt, nodes = walk(c, mgr)
    print("  head/tail/count: %s   nodes walked: %d" % (hdr, len(nodes)))
    print("  first 6 nodes:")
    for n in nodes[:6]:
        print("     0x%08X  def=%-10d flags14=0x%02X f17=%d w20=0x%04X"
              % (n["addr"], n["i0C"], n["b14"], n["b17"], n["w20"]))

    snaps = [nodes]
    addrs = [[n["addr"] for n in nodes]]
    p_prev = grab("s0")
    moved = 0
    for i in range(1, a.rounds + 1):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + i,
                                "button": a.press, "frames": 10}))
        time.sleep(a.wait)
        _, _, nd = walk(c, mgr)
        snaps.append(nd)
        addrs.append([n["addr"] for n in nd])
        cur = grab("s%d" % i)
        n = pixdiff(p_prev, cur)
        if n:
            moved += 1
        p_prev = cur

    print()
    print("=== presses that moved the display: %d/%d ===" % (moved, a.rounds))
    print("node counts per sample: %s" % [len(s) for s in snaps])

    # did the list get rebuilt (addresses change) or stay identical?
    addr_stable = all(x == addrs[0] for x in addrs)
    print("node ADDRESSES identical across samples: %s" % addr_stable)

    print()
    print("=== node fields that changed (compared by index) ===")
    width = min(len(s) for s in snaps)
    found = 0
    for i in range(width):
        for name, off, kind in NODE_FIELDS:
            vals = [snaps[k][i][name] for k in range(len(snaps))]
            if len(set(vals)) > 1:
                found += 1
                print("   node[%d] 0x%08X  %-4s  %s"
                      % (i, snaps[0][i]["addr"], name, vals))
    if not found:
        print("   (no node field changed)")

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if moved == 0:
        print("VERDICT: VOID -- no press moved the display; readings are not evidence.")
    elif not ok:
        print("VERDICT: SUSPECT -- the emulator froze during the run.")
    elif not addr_stable:
        print("VERDICT: list REBUILT between samples -- re-run on a screen whose list is stable.")
    elif found == 0:
        print("VERDICT: TRUSTWORTHY NEGATIVE -- the node list does not track the highlight,")
        print("         so the selection is NOT in the manager's object graph.")
    else:
        print("VERDICT: node fields move -- inspect the ones listed above as cursor candidates.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

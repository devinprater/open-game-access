#!/usr/bin/env python3
"""psp-watch-write.py -- WRITE watchpoint during a VERIFIED press (the section 66 next step).

WHY THIS IS THE RIGHT INSTRUMENT NOW
    Three differencing attempts (full-RAM, 0.25 MB region, 96x256 KB region) agree: what changes on a
    verified menu-direction press is DRAWING DATA (glyph-cache bytes), and the logical selection index is
    either written earlier in the frame than any diff can catch, or re-derived each frame so its write is
    invisible to before/after comparison.

    That is exactly what a WRITE WATCHPOINT resolves and differencing cannot: it reports **who wrote**,
    not what differs. PPSSPP supports memory breakpoints (section 43: `memory.breakpoint.add` with a
    `type`), and delivery can now be VERIFIED per press at the pad button word (section 59 onwards).

    Earlier watchpoint attempts failed for two reasons that are both now fixed:
      * section 43: the game was FROZEN by debugger churn, so nothing executed and the watchpoint
        silently never fired -- fixed by asserting liveness (ticks) before and after;
      * section 43: the watched address was a remembered data address from a DIFFERENT screen -- fixed by
        resolving the address from the live manager each run.

WHAT IT WATCHES
    Resolved live per run:
      * the render array base  = read32(manager + 0x14)
      * the render count       = manager + 0x18     (section 60: responds to DELIVERED D-pad presses)
      * the node head          = read32(manager + 0x28)

    For each, a WRITE watchpoint is set and PPSSPP's LOG events are drained for hits, which carry the PC
    of the writing instruction. A hit PC is the name of the code that writes the state -- which is the
    thing sixty-six sections of differencing could not produce.

USAGE
    python scripts/psp-watch-write.py --button down --hold 45
"""
import argparse
import importlib.util
import json
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PAD_SLOT = 0x08804000 + 0x003925b0
MGR_PTR = 0x08804000 + 0x00397770
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
    ap.add_argument("--button", default="down")
    ap.add_argument("--hold", type=int, default=45)
    ap.add_argument("--presses", type=int, default=6)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator frozen -- a watchpoint would never fire.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    want = BITS[a.button]
    print("pad object 0x%08X" % PAD)

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(addr):
        b = c.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    M = u32(MGR_PTR)
    print()
    print("manager 0x%08X" % M)
    if not M or not (0x08800000 <= M < 0x0A000000):
        print("  manager pointer invalid")
        c.close(); pad.close()
        return 3

    RENDER_BASE = u32(M + 0x14)
    RENDER_COUNT_ADDR = M + 0x18
    NODE_HEAD = u32(M + 0x28)
    print("  render base  = read32(M+0x14) = 0x%08X" % (RENDER_BASE or 0))
    print("  render count = M+0x18          = 0x%08X  (value %s)" % (RENDER_COUNT_ADDR, u32(RENDER_COUNT_ADDR)))
    print("  node head    = read32(M+0x28) = 0x%08X" % (NODE_HEAD or 0))

    # --- clear any stale breakpoints first ---
    c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 50}))
    drain(c.ws, 1.0)

    targets = []
    if RENDER_BASE and 0x08800000 <= RENDER_BASE < 0x0A000000:
        targets.append(("render array base", RENDER_BASE, 0x40))
    targets.append(("render count M+0x18", RENDER_COUNT_ADDR, 4))
    if NODE_HEAD and 0x08800000 <= NODE_HEAD < 0x0A000000:
        targets.append(("node head fields", NODE_HEAD, 0x40))

    print()
    print("=== setting WRITE watchpoints ===")
    for label, addr, size in targets:
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 60,
                              "address": addr, "size": size, "type": "write"}))
        resp = drain(c.ws, 1.0)
        print("  %-20s 0x%08X size 0x%X -> %s" % (label, addr, size,
              [r.get("event") for r in resp if "breakpoint" in str(r.get("event", ""))] or "sent"))

    print()
    print("=== driving VERIFIED presses and draining LOG events for hits ===")
    got, tries = 0, 0
    hits = []
    while got < a.presses and tries < 40:
        tries += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + tries,
                                "button": a.button, "frames": a.hold}))
        delivered = False
        for _ in range(3):
            w = padword()
            if w is not None and (w & want) == want:
                delivered = True
                break
        ev = drain(c.ws, 0.9)
        for e in ev:
            s = json.dumps(e)
            if "breakpoint" in s.lower() or "CHK" in s or "Write" in s:
                hits.append(s)
        if delivered:
            got += 1
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 400 + tries,
                                "button": a.button, "frames": 2}))
        time.sleep(0.5)
    print("  delivered %d/%d (tried %d)" % (got, a.presses, tries))
    print("  breakpoint/CHK log events captured: %d" % len(hits))

    if hits:
        print()
        print("=== WRITE HITS (PC of the writing instruction) ===")
        for h in hits[:20]:
            print("   %s" % h)
        pcs = sorted(set(re.findall(r'PC[=:]\s*([0-9A-Fa-f]{6,8})', " ".join(hits))))
        if pcs:
            print()
            print("=== distinct writer PCs ===")
            for pc in pcs:
                print("   %s" % pc)
    else:
        print()
        print("  NO HITS. Causes to distinguish:")
        print("   * wrong address -- the array/count may be rewritten only on a redraw")
        print("   * the CPU was not executing (liveness is asserted above, so check AFTER)")
        print("   * PPSSPP does not service this breakpoint type in this mode")

    print()
    print("=== clearing watchpoints and resuming ===")
    c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 999}))
    drain(c.ws, 0.8)
    c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 1000}))
    drain(c.ws, 0.8)

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

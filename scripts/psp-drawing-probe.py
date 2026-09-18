#!/usr/bin/env python3
"""psp-drawing-probe.py -- find a screen that DRAWS (M+0x18 > 0) and apply the SEL recipe there.

THE LOGIC
    Section 61 extracted the selection recipe from the render loop:

        q   = read32(node + 0x10)          (node from the manager's list, walk via +0x24)
        SEL = read_s16(q + 4)              the value passed to the draw call, 1-based

    It resolved structurally in RAM (every q a valid pointer, +4 holding small ordinals) but SEL was
    STATIC -- because `M + 0x18` (the render block count) was 0 on that screen: nothing was being drawn,
    so the render loop that consumes the recipe never ran.

    Section 60 established that `M + 0x18` RESPONDS to delivered D-pad presses on a screen that draws
    rows. So `M + 0x18` is a DETECTOR for "this screen draws", and it is a single 4-byte read.

    Therefore: sweep controls with the INPUT GATE, and for every delivered press record both `M + 0x18`
    and the full SEL series. If a screen that draws is reached, the recipe is exercised there -- which is
    the one thing section 61 could not do.

USAGE
    python scripts/psp-drawing-probe.py --need 3
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
MGR_PTR = 0x08804000 + 0x00397770
BITS = {"up": 0x0010, "down": 0x0040, "left": 0x0080, "right": 0x0020,
        "triangle": 0x1000, "circle": 0x2000, "cross": 0x4000, "square": 0x8000,
        "start": 0x0008, "l": 0x0100, "r": 0x0200, "select": 0x0001}
ORDER = ["down", "up", "left", "right", "circle", "cross", "triangle", "square", "start", "l", "r"]


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
    ap.add_argument("--need", type=int, default=3, help="delivered presses to collect per control")
    ap.add_argument("--max-tries", type=int, default=30)
    ap.add_argument("--hold", type=int, default=45)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    print("game:", c.status().get("game", {}).get("title"))
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: frozen.")
        c.close(); pad.close()
        return 2

    PAD = struct.unpack("<I", c.read(PAD_SLOT, 4))[0]
    print("pad object 0x%08X" % PAD)

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(addr):
        b = c.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def s16(addr):
        b = c.read(addr, 2)
        return struct.unpack("<h", b)[0] if b and len(b) == 2 else None

    def sample():
        out = {}
        M = u32(MGR_PTR)
        out["M"] = M
        if not M or not (0x08800000 <= M < 0x0A000000):
            return out
        hdr = c.read(M, 0x40)
        if not hdr or len(hdr) < 0x40:
            return out
        out["RENDER"] = struct.unpack_from("<i", hdr, 0x18)[0]
        out["NODES"] = struct.unpack_from("<i", hdr, 0x30)[0]
        head = struct.unpack_from("<I", hdr, 0x28)[0]
        p = head
        i, seen = 0, set()
        while p and p not in seen and i < 40 and 0x08800000 <= p < 0x0A000000:
            seen.add(p)
            nd = c.read(p, 0x40)
            if not nd or len(nd) < 0x40:
                break
            q = struct.unpack_from("<I", nd, 0x10)[0]
            if q and 0x08800000 <= q < 0x0A000000:
                out["n%d.SEL" % i] = s16(q + 4)
            nxt = struct.unpack_from("<i", nd, 0x24)[0]
            if nxt == head:
                break
            p = nxt
            i += 1
        return out

    base = sample()
    print()
    print("baseline: manager 0x%08X  RENDER %s  NODES %s" % (base.get("M") or 0, base.get("RENDER"),
                                                             base.get("NODES")))
    base_sel = {k: v for k, v in base.items() if k.endswith(".SEL")}
    print("   SEL fields: %d" % len(base_sel))

    print()
    print("=== sweeping controls with the INPUT GATE; watching RENDER as the 'this screen draws' detector ===")
    findings = []
    for btn in ORDER:
        want = BITS[btn]
        got, tries, series, renders = 0, 0, [], []
        while got < a.need and tries < a.max_tries:
            tries += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1000 + tries,
                                    "button": btn, "frames": a.hold}))
            delivered = False
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    delivered = True
                    break
            if delivered:
                s = sample()
                series.append({k: v for k, v in s.items() if k.endswith(".SEL")})
                renders.append(s.get("RENDER"))
                got += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1400 + tries,
                                    "button": btn, "frames": 2}))
            time.sleep(0.55)
        moved = sorted({k for i in range(len(series)) for k in series[i]
                        if len({series[j].get(k) for j in range(len(series))}) > 1})
        rmax = max([r for r in renders if r is not None], default=None)
        print("  %-8s delivered %d/%d (tried %2d)  RENDER max %-4s  SEL moved: %s"
              % (btn, got, a.need, tries, rmax, moved if moved else "none"))
        if moved or (rmax or 0) > 0:
            findings.append((btn, moved, rmax, series, renders))

    print()
    print("=== screens that DRAW or move SEL ===")
    if findings:
        for btn, moved, rmax, series, renders in findings:
            print("   %s: RENDER max %s, SEL moved %s" % (btn, rmax, moved))
            if moved:
                for k in moved:
                    print("      %-10s %s" % (k, [s.get(k) for s in series]))
            print("      RENDER series: %s" % renders)
    else:
        print("   none -- no control produced RENDER>0 or a moving SEL on this screen.")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

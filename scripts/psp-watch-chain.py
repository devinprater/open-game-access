#!/usr/bin/env python3
"""psp-watch-chain.py -- watch Codex's validated render-node chain, with a MEMORY-level no-press control.

WHY THIS BYPASSES THE SCREEN BLOCKER (doc section 54)
    Every press-based test so far needed a STATIC screen, because delivery was judged by a pixel diff --
    and pixel diffs are meaningless when the screen animates. That constraint is what made the cursor
    hunt stall: no reachable screen both is static and responds.

    But delivery does not have to be judged by pixels. If a memory field changes when I press and does
    NOT change when I don't, that is a genuine press response at the memory level -- the same reasoning
    as the memory no-press control (Rule 120), which does not care what the display is doing.

    So this works on ANY screen, animating or not.

THE CHAIN (validated live in doc section 55, from Codex's trace)
    M      = read32(0x08B9B770)          manager
    N      = read32(M + 0x28)            current outer render/menu node (first list)
    bias   = read_s16(N + 0x18)          <- Codex's one concrete selection CANDIDATE
    cb     = read32(N + 0x1c)            callback pointer (proved, not a count)
    R      = read32(N + 0x0c)            resource/animation descriptor
    table  = read32(R + 0x0c)            0x1c-stride table base
    count  = read32(R + 0x2c)            table entry count

    It watches N, R and every word of the table's first `count` entries at stride 0x1c -- so a change
    anywhere in the chain, not just in the manager header, is visible.

PROTOCOL
    1. liveness asserted before and after (ticks delta);
    2. NO-PRESS phase: sample the whole chain ~8 times with no input -> the set of fields that vary on
       their own;
    3. PRESS phase: press the control ~8 times, sampling after each;
    4. report fields that change ONLY in the press phase. Those are genuine press responses.

USAGE
    python scripts/psp-watch-chain.py --press down --samples 8
"""
import argparse
import importlib.util
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MGR_PTR = 0x08804000 + 0x00397770
STRIDE = 0x1C


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
    a = cpu(c); time.sleep(1.3); b = cpu(c)
    if not a or not b:
        print("  %-6s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-6s ticks delta %-13d -> %s" % (tag, d, "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def u32(c, a):
    b = c.read(a, 4)
    return struct.unpack("<I", b)[0] if b and len(b) == 4 else None


def s16(c, a):
    b = c.read(a, 2)
    return struct.unpack("<h", b)[0] if b and len(b) == 2 else None


def sample(c):
    """Return a flat dict of the whole chain, keyed by a readable label."""
    out = {}
    M = u32(c, MGR_PTR)
    out["M"] = M
    if not M or not (0x08800000 <= M < 0x0A000000):
        return out
    for nm, off in (("M+0x28_head", 0x28), ("M+0x34_head", 0x34), ("M+0x30_nodes", 0x30),
                    ("M+0x18_render", 0x18), ("M+0x24", 0x24)):
        out[nm] = struct.unpack_from("<i", c.read(M + off, 4), 0)[0] if c.read(M + off, 4) else None
    N = u32(c, M + 0x28)
    out["N"] = N
    if not N or not (0x08800000 <= N < 0x0A000000):
        return out
    out["N+0x18_bias"] = s16(c, N + 0x18)
    out["N+0x1c_cb"] = u32(c, N + 0x1c)
    out["N+0x20"] = u32(c, N + 0x20)
    R = u32(c, N + 0x0C)
    out["R"] = R
    if not R or not (0x08800000 <= R < 0x0A000000):
        return out
    table = u32(c, R + 0x0C)
    count = u32(c, R + 0x2C)
    out["R+0x0c_table"] = table
    out["R+0x2c_count"] = count
    if table and 0x08800000 <= table < 0x0A000000 and count and 0 < count <= 64:
        for i in range(min(count, 24)):
            blk = c.read(table + i * STRIDE, STRIDE)
            if blk and len(blk) == STRIDE:
                for w in range(0, STRIDE, 4):
                    out["tbl[%d]+0x%02X" % (i, w)] = struct.unpack_from("<i", blk, w)[0]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--samples", type=int, default=8)
    ap.add_argument("--wait", type=float, default=1.2)
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

    print()
    print("=== NO-PRESS phase (%d samples) ===" % a.samples)
    nopress = []
    for i in range(a.samples):
        s = sample(c)
        nopress.append(s)
        print("  sample %d: %d fields" % (i + 1, len(s)), end="\r", flush=True)
        time.sleep(a.wait)
    print()

    print("=== PRESS phase (%d presses of '%s') ===" % (a.samples, a.press))
    pressed = []
    for i in range(a.samples):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + i,
                                "button": a.press, "frames": 12}))
        time.sleep(a.wait)
        s = sample(c)
        pressed.append(s)
        print("  press %d: %d fields" % (i + 1, len(s)), end="\r", flush=True)
    print()

    print()
    print("=== analysis ===")
    keys = sorted(set().union(*[set(s.keys()) for s in nopress + pressed]))
    varies_nopress, varies_press, press_only = [], [], []
    for k in keys:
        nv = [s.get(k) for s in nopress]
        pv = [s.get(k) for s in pressed]
        cn = len(set(nv)) > 1
        cp = len(set(pv)) > 1
        if cn:
            varies_nopress.append(k)
        if cp:
            varies_press.append(k)
        if cp and not cn:
            press_only.append((k, nv[0], pv))

    print("  fields that vary with NO press (churn):        %d" % len(varies_nopress))
    print("  fields that vary during presses:               %d" % len(varies_press))
    print()
    print("=== PRESS-ONLY fields (change with a press, constant without) ===")
    if press_only:
        for k, base, pv in press_only:
            print("   %-18s  baseline %-12s  series %s" % (k, base, pv))
    else:
        print("   NONE -- nothing in the chain responds to '%s' uniquely." % a.press)

    print()
    print("=== the specific candidate Codex flagged ===")
    b0 = [s.get("N+0x18_bias") for s in nopress]
    b1 = [s.get("N+0x18_bias") for s in pressed]
    print("   N+0x18 bias   no-press: %s" % b0)
    print("   N+0x18 bias   pressed : %s" % b1)
    if len(set(b1)) > 1 and len(set(b0)) == 1:
        print("   -> PRESS-RESPONSIVE; this is the scroll-bias candidate and it moves.")
    elif len(set(b1)) == 1:
        print("   -> constant under presses; not the selection on this screen.")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

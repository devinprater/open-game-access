#!/usr/bin/env python3
"""psp-watch-nodes-wide.py -- memory-level press test over the NODE LIST and the TABLE, with churn control.

WHY THIS AND NOT THE PIXEL TEST
    Section 56 replaced pixel-based delivery with a MEMORY-level no-press control, which works on any
    screen. That test covered the manager header, render node N and the 0x1c-stride table -- 90 fields --
    and found ZERO churn and ZERO press response.

    What it did NOT cover is the **node list itself**: sections 39 and 47 walked the nodes and found no
    change, but every one of those runs judged delivery by pixels and every one was either void or
    taken on a suspicious screen. The node list is the natural home for a per-item highlight flag -- it is
    the last unverified structural candidate, and it has never been tested with a valid instrument.

WHAT IT DOES
    Samples EVERY field of EVERY node in the manager's list (plus each node's own pointer fields), across
    a NO-PRESS phase and a PRESS phase, and reports:
      * fields that vary on their own (churn -- reported first, per Rule 147);
      * fields that vary only under presses (genuine press responses).

    Because the delivery judgement is in memory, the screen may be static or animating -- both are valid.

USAGE
    python scripts/psp-watch-nodes-wide.py --press down --samples 10
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
NODE_WORDS = list(range(0x00, 0x40, 4))      # every word of the node header


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


def sample(c):
    """Every word of every node, keyed stably by node index (order of the walk)."""
    out = {}
    raw = c.read(MGR_PTR, 4)
    M = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    out["M"] = M
    if not M or not (0x08800000 <= M < 0x0A000000):
        return out
    hdr = c.read(M, 0x40)
    if not hdr or len(hdr) < 0x40:
        return out
    out["M+0x18_render"] = struct.unpack_from("<i", hdr, 0x18)[0]
    out["M+0x30_nodes"] = struct.unpack_from("<i", hdr, 0x30)[0]
    out["M+0x24"] = struct.unpack_from("<i", hdr, 0x24)[0]

    head = struct.unpack_from("<I", hdr, 0x28)[0]
    p = head
    i = 0
    seen = set()
    while p and p not in seen and i < 64 and 0x08800000 <= p < 0x0A000000:
        seen.add(p)
        nb = c.read(p, 0x40)
        if not nb or len(nb) < 0x40:
            break
        for w in NODE_WORDS:
            out["n%d+0x%02X" % (i, w)] = struct.unpack_from("<i", nb, w)[0]
        for b in (0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B):
            out["nb%d+0x%02X" % (i, b)] = nb[b]
        p = struct.unpack_from("<i", nb, 0x24)[0]
        if p == head:
            break
        i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--samples", type=int, default=10)
    ap.add_argument("--wait", type=float, default=1.0)
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
    print("=== NO-PRESS phase (churn baseline, Rule 147) ===")
    nopress = []
    for i in range(a.samples):
        s = sample(c)
        nopress.append(s)
        print("  sample %-2d: %d fields" % (i + 1, len(s)), end="\r", flush=True)
        time.sleep(a.wait)
    print()

    print("=== PRESS phase ('%s' x%d) ===" % (a.press, a.samples))
    pressed = []
    for i in range(a.samples):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 200 + i,
                                "button": a.press, "frames": 12}))
        time.sleep(a.wait)
        s = sample(c)
        pressed.append(s)
        print("  press  %-2d: %d fields" % (i + 1, len(s)), end="\r", flush=True)
    print()

    keys = sorted(set().union(*[set(s.keys()) for s in nopress + pressed]))
    churn, resp, only = [], [], []
    for k in keys:
        nv = [s.get(k) for s in nopress]
        pv = [s.get(k) for s in pressed]
        if len(set(nv)) > 1:
            churn.append(k)
        if len(set(pv)) > 1:
            resp.append(k)
        if len(set(pv)) > 1 and len(set(nv)) == 1:
            only.append((k, nv[0], pv))

    print()
    print("=== analysis ===")
    print("  fields sampled:                    %d" % len(keys))
    print("  fields that VARY WITH NO PRESS:    %d   (churn)" % len(churn))
    if churn:
        for k in churn[:15]:
            print("      %-16s %s" % (k, [s.get(k) for s in nopress]))
    print("  fields that vary during presses:   %d" % len(resp))
    print()
    print("=== PRESS-ONLY fields (response, no churn) ===")
    if only:
        for k, base, pv in only:
            print("   %-16s baseline %-12s series %s" % (k, base, pv))
    else:
        print("   NONE -- nothing in the node list responds to '%s' uniquely." % a.press)

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if only:
        print("VERDICT: node fields respond to input -- inspect them as cursor candidates.")
    elif churn:
        print("VERDICT: no press response; %d field(s) churn on their own, so a response would still" % len(churn))
        print("         have been visible against that baseline.")
    else:
        print("VERDICT: TRUSTWORTHY NEGATIVE -- zero churn and zero press response across the whole")
        print("         node list; the selection is not stored in these nodes.")
    if not ok:
        print("NOTE: the emulator froze during the run.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

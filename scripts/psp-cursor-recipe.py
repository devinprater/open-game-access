#!/usr/bin/env python3
"""psp-cursor-recipe.py -- test the SELECTION recipe extracted from FUN_0024aee0, with the input gate.

THE RECIPE (doc section 61, from the decompile of the render loop)
    In FUN_0024aee0 the render loop walks the manager's node list and, for each node, does:

        iVar13 = -1;
        if ((undefined4 *)param_2[4] != (undefined4 *)0x0) {
            iVar13 = (*(short *)(param_2[4] + 4) + -1) * 0x10000 >> 0x10;
        }
        FUN_002478e0(*(undefined4 *)(param_1 + 4), *(undefined4 *)param_2[4], iVar13);

    `param_2` is a node of the manager's list (the function ends with `param_2 = param_2[9]`, i.e. it
    advances via +0x24 -- the node "next" link). `param_2[4]` is therefore `node + 0x10`, used as a
    POINTER, and the value read is `(short at that_pointer + 4) - 1`. The `- 1` and the use as a third
    argument to a draw call is the signature of a **selection index**, stored 1-based.

    So, per node N:
        q   = read32(N + 0x10)
        sel = read_s16(q + 4)        if q is a valid RAM pointer

    The neighbour check in the same loop corroborates the node layout:
        if ((int)*(short *)(puVar16 + 3) < local_50[0xb])          # node+0x0C < count
            iVar13 = local_50[3] + *(short *)(puVar16 + 3) * 0x1c; # index * 0x1c stride

METHOD
    Uses the INPUT GATE (doc section 59): each press is verified at the pad button word while held, so
    an undelivered press is discarded rather than counted as "no movement". Then it reports, per node,
    whether `sel` moved with DELIVERED presses and whether it wrapped.

USAGE
    python scripts/psp-cursor-recipe.py --buttons up,down --need 6
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
BITS = {"select": 0x0001, "start": 0x0008, "up": 0x0010, "right": 0x0020, "down": 0x0040,
        "left": 0x0080, "l": 0x0100, "r": 0x0200, "triangle": 0x1000, "circle": 0x2000,
        "cross": 0x4000, "square": 0x8000}


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
    ap.add_argument("--buttons", default="up,down")
    ap.add_argument("--need", type=int, default=6)
    ap.add_argument("--max-tries", type=int, default=60)
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--hold", type=int, default=50)
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
    print()
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
        """Apply the recipe to every node of the manager's list."""
        out = {}
        M = u32(MGR_PTR)
        out["M"] = M
        if not M or not (0x08800000 <= M < 0x0A000000):
            return out
        hdr = c.read(M, 0x40)
        if not hdr or len(hdr) < 0x40:
            return out
        for nm, off in (("M+0x18_render", 0x18), ("M+0x30_nodes", 0x30)):
            out[nm] = struct.unpack_from("<i", hdr, off)[0]
        head = struct.unpack_from("<I", hdr, 0x28)[0]
        p = head
        i = 0
        seen = set()
        while p and p not in seen and i < 40 and 0x08800000 <= p < 0x0A000000:
            seen.add(p)
            nd = c.read(p, 0x40)
            if not nd or len(nd) < 0x40:
                break
            # THE RECIPE: node+0x10 -> pointer, then short at +4
            q = struct.unpack_from("<I", nd, 0x10)[0]
            out["n%d.q" % i] = q
            if q and 0x08800000 <= q < 0x0A000000:
                out["n%d.SEL" % i] = s16(q + 4)
                out["n%d.q+0" % i] = u32(q)
                out["n%d.q+8" % i] = u32(q + 8)
            # corroborating: the node's own index field at +0x0C
            out["n%d.idx" % i] = struct.unpack_from("<h", nd, 0x0C)[0]
            out["n%d.f20" % i] = struct.unpack_from("<I", nd, 0x20)[0]
            nxt = struct.unpack_from("<i", nd, 0x24)[0]
            if nxt == head:
                break
            p = nxt
            i += 1
        return out

    print()
    print("=== baseline sample ===")
    base = sample()
    M = base.get("M")
    print("manager 0x%08X  nodes %s  render %s" % (M, base.get("M+0x30_nodes"), base.get("M+0x18_render")))
    for i in range(12):
        if ("n%d.q" % i) in base:
            print("   n%-2d  q=0x%08X  SEL=%-6s  idx=%-4s  f20=0x%08X"
                  % (i, base.get("n%d.q" % i) or 0, base.get("n%d.SEL" % i),
                     base.get("n%d.idx" % i), base.get("n%d.f20" % i) or 0))
        else:
            break

    results = {}
    for btn in [b.strip() for b in a.buttons.split(",") if b.strip()]:
        want = BITS.get(btn)
        if want is None:
            continue
        print()
        print("=== '%s' (bit 0x%04X): collecting %d DELIVERED presses ===" % (btn, want, a.need))
        got, tries, samples = 0, 0, []
        while got < a.need and tries < a.max_tries:
            tries += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 800 + tries,
                                    "button": btn, "frames": a.hold}))
            delivered = False
            for _ in range(3):
                w = padword()
                if w is not None and (w & want) == want:
                    delivered = True
                    break
            if delivered:
                samples.append(sample())
                got += 1
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 950 + tries,
                                    "button": btn, "frames": 2}))
            time.sleep(0.7)
        print("   delivered %d/%d (tried %d)" % (got, a.need, tries))
        results[btn] = samples

    print()
    print("=== per-node SEL series under DELIVERED presses ===")
    keys = sorted(set().union(*[set(s.keys()) for s in [base] + [x for v in results.values() for x in v]]))
    nodes = sorted({k.split(".")[0] for k in keys if k.startswith("n") and k.endswith(".SEL")})
    for btn, samples in results.items():
        if not samples:
            print("   %-6s no delivered presses" % btn)
            continue
        print()
        print("   --- %s ---" % btn)
        for n in nodes:
            series = [s.get(n + ".SEL") for s in samples]
            if base.get(n + ".q") is None:
                continue
            vals = set(series)
            mark = ""
            if len(vals) > 1:
                mark = "  <== MOVES"
            print("     %-4s q=0x%08X  %s%s" % (n, base.get(n + ".q") or 0, series, mark))
        print("     render count: %s" % [s.get("M+0x18_render") for s in samples])

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

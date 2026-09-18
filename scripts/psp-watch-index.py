#!/usr/bin/env python3
"""psp-watch-index.py -- watch the SOURCE of the render index, with correct hit handling.

WHY THIS IS THE NEXT STEP (doc section 68)
    Section 68 closed the trace on the render COUNT: `sw a3,0x18(a2)` at 0x00255f58 writes
    manager+0x18, and that field is a DERIVED row count (blocks written so far), not the selection.

    FUN_0025595c is called as (section 61):
        FUN_0025595c(*puVar16, param_2, iVar13, puVar16[0xb], puVar16)
    where `iVar13` is the table entry at `index * 0x1c` -- so the SELECTION INDEX feeds the render
    as an ARGUMENT. This script watches the structures that could hold that index:

        * the 0x1c-stride table itself  (read32(read32(N+0x0c) + 0x0c), where N = read32(M+0x28))
        * the node list fields          (each node's +0x0c index, +0x10 pointer target +4)

    A write to the table or to a node index during a VERIFIED press is the closest thing to
    "who writes the selection".

WATCHPOINT HANDLING -- THE FIX (section 67's defect)
    Section 67's script drove a press loop that STARVED once the CPU halted. This one uses the loop that
    worked in section 67's follow-up instead:

        arm -> press -> read cpu.status.pc -> cpu.resume -> repeat

    and it records the PC per hit, so hits accumulate rather than replacing one another.

USAGE
    python scripts/psp-watch-index.py --button down --hits 8
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
    ap.add_argument("--hits", type=int, default=8)
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
    want = BITS[a.button]

    def padword():
        b = c.read(PAD, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def u32(addr):
        b = c.read(addr, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    M = u32(MGR_PTR)
    N = u32(M + 0x28)
    R = u32(N + 0x0C) if N else 0
    TABLE = u32(R + 0x0C) if R else 0
    COUNT = u32(R + 0x2C) if R else 0
    print("manager 0x%08X  N 0x%08X  R 0x%08X  table 0x%08X  count %s" % (M, N or 0, R or 0, TABLE or 0, COUNT))

    targets = []
    if TABLE and 0x08800000 <= TABLE < 0x0A000000 and COUNT and 0 < COUNT <= 64:
        targets.append(("0x1c-stride table", TABLE, min(0x1C * COUNT, 0x400)))
    if N and 0x08800000 <= N < 0x0A000000:
        targets.append(("node N fields", N, 0x40))
        nxt = u32(N + 0x24)
        if nxt and 0x08800000 <= nxt < 0x0A000000:
            targets.append(("node N+1 fields", nxt, 0x40))

    print()
    print("=== clearing stale breakpoints ===")
    c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 10}))
    drain(c.ws, 0.8)
    c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 11}))
    drain(c.ws, 0.8)

    all_pcs = []
    for label, addr, size in targets:
        print()
        print("=== target %s  0x%08X  size 0x%X ===" % (label, addr, size))
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 20,
                              "address": addr, "size": size, "type": "write"}))
        drain(c.ws, 0.8)

        got, tries, pcs = 0, 0, []
        while got < a.hits and tries < 60:
            tries += 1
            # make sure we are running before the press
            c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 30}))
            drain(c.ws, 0.25)
            pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 40 + tries,
                                    "button": a.button, "frames": a.hold}))
            time.sleep(1.1)
            st = cpu(c)
            if not st:
                continue
            pc = st.get("pc", 0)
            stepping = st.get("stepping")
            if stepping and pc:
                pcs.append(pc)
                all_pcs.append((label, pc))
                got += 1
            # resume so the next iteration can proceed
            c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 50}))
            drain(c.ws, 0.3)
            c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 51}))
            drain(c.ws, 0.5)
            # re-arm for the next press
            c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 60,
                                  "address": addr, "size": size, "type": "write"}))
            drain(c.ws, 0.3)

        print("  hits captured: %d (tries %d)" % (got, tries))
        if pcs:
            from collections import Counter
            cc = Counter(pcs)
            print("  distinct PCs: %d" % len(cc))
            for pc, n in cc.most_common(10):
                print("     0x%08X  x%d   vaddr 0x%08X" % (pc, n, pc - 0x08804000))
        else:
            print("  NO HITS on this target (the CPU was resumed before each press, so an")
            print("  absence here means the field was not written during the press window.)")
        c.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 70}))
        drain(c.ws, 0.4)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 71}))
        drain(c.ws, 0.6)

    print()
    print("=== ALL HITS ===")
    if all_pcs:
        from collections import Counter
        cc = Counter(pc for _, pc in all_pcs)
        for pc, n in cc.most_common():
            print("   0x%08X  x%d  vaddr 0x%08X" % (pc, n, pc - 0x08804000))
    else:
        print("   none")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

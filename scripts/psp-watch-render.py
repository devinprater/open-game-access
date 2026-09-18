#!/usr/bin/env python3
"""psp-watch-render.py -- set a WRITE watchpoint on the render array and capture the writer's PC.

WHY THIS IS THE DECISIVE TEST (doc section 42)
    Everything reachable by reading memory has been cleared: the render array at 0x09DEE480 is produced
    by FUN_0025595c, the draw callback only reads it, and the logical selection index is not written by
    anything directly reachable from the mapped input path. Codex's answer plus my verification closed
    the READ side. The remaining question is which code WRITES the block triple, and a watchpoint
    answers it directly instead of by inference.

    PPSSPP's debugger DOES support this -- probing found:
        memory.breakpoint.add {address, size, type}   -> adds a memory breakpoint
        and the game immediately logged:
        "CHK Write128(CPU) at 09dee480 ((09dee480)), PC=08..."

    So a write to the render array reports the PC of the writing instruction. That PC, mapped back to a
    Ghidra function, names the writer -- and if the writer is a menu routine, the register/stack state
    around it identifies the selection field.

WHAT IT DOES
    1. clears any stale breakpoints;
    2. adds a write breakpoint on the render array's first block (and optionally a second address);
    3. resumes the CPU (the debugger pauses when a breakpoint fires);
    4. collects breakpoint/log events, extracting PC values, and reports them together with the
       Ghidra-side function each PC falls in;
    5. leaves the CPU running and removes the breakpoint at the end.

USAGE
    python scripts/psp-watch-render.py --addr 0x09DEE480 --size 4
"""
import argparse
import importlib.util
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = 0x08800000 - 0x00000000      # vaddr 0 == RAM 0x08800000 for Ghidra->RAM mapping


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def drain(ws, seconds):
    """Collect every message for `seconds`, never swallowing the error event."""
    out = []
    end = time.time() + seconds
    while time.time() < end:
        try:
            ws.settimeout(max(0.05, end - time.time()))
            out.append(json.loads(ws.recv()))
        except Exception:
            pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x09DEE480)
    ap.add_argument("--size", type=int, default=4)
    ap.add_argument("--hold", type=float, default=20.0, help="seconds to watch")
    a = ap.parse_args()

    pp = load_client()
    r = pp.Debugger()   # reads / control
    p = pp.Debugger()   # input, separate connection (doc Rule 127)
    print("game:", r.status().get("game", {}).get("title"))
    print("watchpoint: WRITE to 0x%08X size %d" % (a.addr, a.size))
    print()

    # 1. clear stale breakpoints
    r.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 1}))
    time.sleep(0.4)
    drain(r.ws, 0.4)

    # 2. add the write watchpoint (size is REQUIRED -- omitting it errors)
    r.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 2,
                          "address": a.addr, "size": a.size, "type": "write"}))
    msgs = drain(r.ws, 1.0)
    for m in msgs:
        if m.get("event") in ("error", "memory.breakpoint.add", "log"):
            print("  add -> %s" % json.dumps(m)[:200])

    # 3. breakpoints pause the CPU; resume so the game can run into it
    r.ws.send(json.dumps({"event": "cpu.resume", "requestId": 3}))
    time.sleep(0.4)

    # 4. press a button so the render array is rewritten (the highlight moves)
    print()
    print("  pressing down to force a render-array write ...")
    p.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 4,
                          "button": "down", "frames": 10}))
    time.sleep(1.5)
    r.ws.send(json.dumps({"event": "cpu.resume", "requestId": 5}))

    # 5. collect everything, extracting PCs
    msgs = drain(r.ws, a.hold)
    pcs = []
    print()
    print("=== breakpoint / log events ===")
    seen = 0
    for m in msgs:
        ev = m.get("event", "?")
        if ev in ("log", "memory.breakpoint", "cpu.stepping", "breakpoint",
                  "memory.breakpoint.hit"):
            seen += 1
            txt = json.dumps(m)
            print("  %s" % txt[:220])
            low = txt.lower()
            if "pc=" in low:
                for tok in txt.replace("(", " ").replace(")", " ").replace(",", " ").split():
                    if tok.upper().startswith("PC=") or (tok.upper().startswith("PC")
                                                        and "=" in tok):
                        pcs.append(tok)
        elif ev == "error":
            print("  ERROR: %s" % txt[:200])
    if seen == 0:
        print("  (no breakpoint events in %.0fs -- the address may not be written while idle)" % a.hold)

    print()
    print("=== PC values seen ===")
    for pc in pcs:
        print("   %s" % pc)
    if pcs:
        print()
        print("  map a PC to Ghidra with:  ghidra_vaddr = RAM_pc - 0x08800000")

    # 6. clean up: remove the breakpoint and leave the CPU running
    r.ws.send(json.dumps({"event": "memory.breakpoint.remove", "requestId": 6, "address": a.addr}))
    r.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 7}))
    r.ws.send(json.dumps({"event": "cpu.resume", "requestId": 8}))
    time.sleep(0.5)
    drain(r.ws, 0.5)
    print()
    print("breakpoint removed; CPU resumed.")
    r.close(); p.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""psp-watch-render2.py -- write-watchpoint on the render array, with correct CPU sequencing.

WHAT WENT WRONG IN THE FIRST ATTEMPT
    The first version added the breakpoint, sent cpu.resume once, then relied on a button press to
    trigger a write. It saw only `cpu.stepping` events and no breakpoint hit. But an earlier PROBE had
    proved the mechanism works -- the game logged:

        CHK Write128(CPU) at 09dee480 ((09dee480)), PC=08...

    so the debugger *can* catch a write to this address. The difference is CPU state: adding a
    breakpoint can leave the CPU stepping, and a stepping CPU does not service it unless resumed
    repeatedly. The earlier success happened while the game was running normally.

    This version therefore:
      1. resumes the CPU FIRST and confirms it is running (ticks advancing, stepping false);
      2. adds the breakpoint and resumes AGAIN, in a loop that keeps the CPU running while waiting;
      3. drives input on a separate connection (reads suppress presses -- doc Rule 127);
      4. reports every `log` line (that is where PPSSPP writes "CHK Write... at ADDR, PC=..."),
         extracting PCs, and also reports the state of the CPU at each step so a silent failure is
         distinguishable from "the address genuinely was not written".

USAGE
    python scripts/psp-watch-render2.py --addr 0x09DEE480 --size 4 --hold 25
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def collect(ws, seconds):
    out = []
    end = time.time() + seconds
    while time.time() < end:
        try:
            ws.settimeout(max(0.02, end - time.time()))
            out.append(json.loads(ws.recv()))
        except Exception:
            pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x09DEE480)
    ap.add_argument("--size", type=int, default=4)
    ap.add_argument("--hold", type=float, default=25.0)
    ap.add_argument("--button", default="down")
    a = ap.parse_args()

    pp = load_client()
    ctl = pp.Debugger()     # control + reads
    pad = pp.Debugger()     # input only

    print("game:", ctl.status().get("game", {}).get("title"))
    print("WRITE watchpoint on 0x%08X size %d" % (a.addr, a.size))
    print()

    # --- confirm the CPU is free-running BEFORE adding the breakpoint ---
    ctl.ws.send(json.dumps({"event": "cpu.resume", "requestId": 1}))
    time.sleep(0.6)
    collect(ctl.ws, 0.5)
    ctl.ws.send(json.dumps({"event": "cpu.status", "requestId": 2}))
    st = None
    for m in collect(ctl.ws, 2.0):
        if m.get("event") == "cpu.status":
            st = m
    print("  cpu before: %s" % json.dumps(st)[:160])

    ctl.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 3}))
    time.sleep(0.3); collect(ctl.ws, 0.3)
    ctl.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 4,
                            "address": a.addr, "size": a.size, "type": "write"}))
    for m in collect(ctl.ws, 1.0):
        print("  add -> %s" % json.dumps(m)[:160])

    # --- keep the CPU running while we drive input ---
    print()
    print("  driving input and keeping the CPU running ...")
    events = []
    t_end = time.time() + a.hold
    n = 0
    while time.time() < t_end:
        n += 1
        ctl.ws.send(json.dumps({"event": "cpu.resume", "requestId": 100 + n}))
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 200 + n,
                                "button": a.button, "frames": 8}))
        events.extend(collect(ctl.ws, 0.9))

    # --- report: breakpoint log lines first, then any CPU stop ---
    hits, stops = [], []
    for m in events:
        ev = m.get("event")
        if ev == "log":
            hits.append(m)
        elif ev in ("cpu.stepping", "memory.breakpoint.hit", "breakpoint"):
            stops.append(m)
        elif ev == "error":
            hits.append(m)

    print()
    print("=== LOG events (PPSSPP reports watchpoint hits here) : %d ===" % len(hits))
    pcs = []
    for m in hits:
        line = m.get("message", "") or json.dumps(m)
        print("   %s" % line[:230])
        for tok in re.findall(r"PC=([0-9a-fA-F]{6,8})", line):
            pcs.append(int(tok, 16))

    print()
    print("=== distinct PCs that hit the watchpoint: %d ===" % len(set(pcs)))
    for pc in sorted(set(pcs)):
        print("   RAM 0x%08X   -> Ghidra vaddr 0x%08X" % (pc, pc - 0x08800000))

    if not pcs:
        print()
        print("  NO WATCHPOINT HIT. Distinguish the causes:")
        print("   * cpu stops seen: %d (a stepping CPU does not service the breakpoint)" % len(stops))
        print("   * if 0 stops and 0 hits, this address was not written during the window --")
        print("     try the neighbour addresses (+0x04/+0x08) or a longer hold.")

    ctl.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 999}))
    ctl.ws.send(json.dumps({"event": "cpu.resume", "requestId": 1000}))
    time.sleep(0.4); collect(ctl.ws, 0.4)
    print()
    print("cleared and resumed.")
    ctl.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

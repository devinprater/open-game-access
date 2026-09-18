#!/usr/bin/env python3
"""psp-live-check.py -- assert the emulator is EXECUTING before trusting any negative result.

WHY THIS EXISTS (learned the hard way, twice)
    Debugger probing -- repeated cpu.resume calls, breakpoint add/clear cycles, many short-lived
    connections -- can leave PPSSPP HALTED with `stepping: true` and `ticks` frozen. A halted emulator
    produces exactly the readings a real negative produces:

      * a button press changes 0 pixels
      * no watchpoint ever fires
      * a polled field never moves

    Sections 43 and 44 both wasted a run this way. The distinguishing measurement is the **ticks
    delta**: if `ticks` does not advance, nothing is executing and NO conclusion is valid.

    This script is meant to be called BEFORE and AFTER any observation run. It also attempts a gentle
    recovery (clear breakpoints + cpu.resume) and reports whether that worked, so a frozen emulator is
    detected rather than silently believed.

USAGE
    python scripts/psp-live-check.py            # check once
    python scripts/psp-live-check.py --recover  # try to unfreeze, then re-check
"""
import argparse
import importlib.util
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


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


def status(d):
    d.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(d.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--recover", action="store_true", help="clear breakpoints and resume, then re-check")
    ap.add_argument("--seconds", type=float, default=2.5, help="ticks sampling interval")
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()

    def check(tag):
        s1 = status(d)
        time.sleep(a.seconds)
        s2 = status(d)
        if not s1 or not s2:
            print("  %-8s no cpu.status response -- is the debugger attached?" % tag)
            return None
        delta = s2.get("ticks", 0) - s1.get("ticks", 0)
        live = delta > 0
        print("  %-8s ticks delta over %.1fs: %-14d stepping=%-5s paused=%-5s -> %s"
              % (tag, a.seconds, delta, s2.get("stepping"), s2.get("paused"),
                 "EXECUTING" if live else "FROZEN (no conclusion is valid)"))
        return live

    print("game:", d.status().get("game", {}).get("title"))
    live = check("before")
    if live is False and a.recover:
        print()
        print("  attempting recovery: clear breakpoints + cpu.resume")
        d.ws.send(json.dumps({"event": "memory.breakpoint.clear.all", "requestId": 10}))
        drain(d.ws, 0.4)
        d.ws.send(json.dumps({"event": "cpu.resume", "requestId": 11}))
        drain(d.ws, 0.6)
        print()
        live = check("after")
        if live is False:
            print()
            print("  RECOVERY FAILED. The reliable fix is to kill and relaunch the emulator:")
            print("    powershell -Command \"Get-Process PPSSPPWindows64 | Stop-Process -Force\"")
            print("    then relaunch with --debugger=12345 and verify the process count is 1.")

    d.close()
    if live is False:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())

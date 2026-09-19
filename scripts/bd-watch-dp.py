#!/usr/bin/env python3
"""bd-watch-dp.py -- catch the WRITER PCs of authoritative DP (R+6) and DP cache (U+0xD78).

DESIGN (ANSWER4 tests 3+6)
    Board is freshly entered (DP==1 both chains). Arm a WRITE breakpoint on the target,
    send a direction press that CONFIRMS a piece move (as in the first observed move),
    and record the halt PC. Expect: spend PC within/calls 0x001CA7EC (authoritative),
    refresh PCs 0x001D42E8 / 0x001CA124 (cache). Display-verify the move acted.
    One address per run (clean attribution); resume+clear between rounds.

USAGE
    python scripts/bd-watch-dp.py --addr 0x08C3B7A6 --size 2 --rounds 6 --button right
"""
import argparse
import importlib.util
import json
import os
import subprocess
import sys
import time
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", required=True)
    ap.add_argument("--size", type=int, default=4)
    ap.add_argument("--rounds", type=int, default=6)
    ap.add_argument("--button", default="right")
    a = ap.parse_args()
    addr = int(a.addr, 16) if isinstance(a.addr, str) else a.addr

    from PIL import Image
    import numpy as np

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    def drain(ws, secs=0.08):
        end = time.time() + secs
        ws.settimeout(0.05)
        while time.time() < end:
            try:
                ws.recv()
            except Exception:
                pass

    def cpu(timeout=3.0):
        c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
        end = time.time() + timeout
        while time.time() < end:
            try:
                c.ws.settimeout(0.3)
                m = json.loads(c.ws.recv())
            except Exception:
                continue
            if m.get("event") == "cpu.status":
                return m
        return None

    def shot(t):
        q = os.path.join(TMP, "wp-%s.png" % t)
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def diff(p, q):
        x = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        y = np.asarray(Image.open(q).convert("RGB")).astype("int16")
        return int((np.abs(x - y).max(axis=2) > 16).sum())

    print("game:", c.status().get("game", {}).get("title"), flush=True)
    print("watching WRITES to 0x%08X size %d" % (addr, a.size), flush=True)
    # NOTE: memory.breakpoint.clear.all does NOT clear (verified live) -- remove
    # each armed breakpoint explicitly with address+size, or the game stays halted
    # on HUD-tick reads. PPSSPP arms read+write regardless of the type requested.
    armed = []
    pcs = []
    prev = shot("wp0")
    for i in range(a.rounds):
        c.ws.send(json.dumps({"event": "memory.breakpoint.remove", "requestId": 5,
                              "address": addr, "size": a.size}))
        drain(c.ws)
        c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
        drain(c.ws)
        c.ws.send(json.dumps({"event": "memory.breakpoint.add", "requestId": 7,
                              "address": addr, "size": a.size, "type": "write"}))
        drain(c.ws)
        armed = [(addr, a.size)]
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": a.button, "frames": 120}))
        time.sleep(2.5)
        st = cpu()
        if st and st.get("stepping") and st.get("pc"):
            pcs.append(st["pc"])
            print("  round %d: HALT pc=0x%08X vaddr=0x%08X" % (i + 1, st["pc"], st["pc"] - 0x08804000),
                  flush=True)
        else:
            print("  round %d: no halt" % (i + 1), flush=True)
        cur = shot("wp%d" % (i + 1))
        print("    display %d px" % diff(prev, cur), flush=True)
        prev = cur
    for ad, sz in armed:
        c.ws.send(json.dumps({"event": "memory.breakpoint.remove", "requestId": 5,
                              "address": ad, "size": sz}))
        drain(c.ws)
    c.ws.send(json.dumps({"event": "cpu.resume", "requestId": 6}))
    drain(c.ws)
    print("=== writer PCs ===", flush=True)
    for pc, n in Counter(pcs).most_common():
        print("  0x%08X (vaddr 0x%08X) x%d" % (pc, pc - 0x08804000, n), flush=True)
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

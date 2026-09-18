#!/usr/bin/env python3
"""psp-probe-struct.py -- run the VERIFIED harness on an arbitrary struct: liveness + delivery + wrap.

WHY
    Doc section 45 produced the first trustworthy negative in the cursor hunt, and it narrowed the
    search to the objects the manager POINTS AT. The three candidates are:
        manager +0x00 -> 0x09DEE380   (a struct with small ordinals 4/4/28/4 and pointers)
        manager +0x04 -> 0x09DF18F0   (all zeros -- likely a buffer, deprioritised)
        manager +0x0C -> 0              (not a pointer on this screen)
    This script applies the same verified-precondition harness to a given struct: assert the CPU is
    EXECUTING before and after, use TWO debugger connections, require each press to move the display,
    and then apply the WRAP test to any small-ordinal field that moves.

    The point of reusing the harness is that a null from THIS script is load-bearing, whereas earlier
    nulls (sections 40/41/43/44) had to be withdrawn because liveness or delivery was unproven.

USAGE
    python scripts/psp-probe-struct.py --base 0x09DEE380 --len 0x60 --press down --rounds 12
"""
import argparse
import importlib.util
import json
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")


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


def cpu_status(c):
    c.ws.send(json.dumps({"event": "cpu.status", "requestId": 1}))
    for m in drain(c.ws, 1.5):
        if m.get("event") == "cpu.status":
            return m
    return None


def live(c, tag):
    a = cpu_status(c); time.sleep(1.5); b = cpu_status(c)
    if not a or not b:
        print("  %-7s no cpu.status" % tag)
        return False
    d = b.get("ticks", 0) - a.get("ticks", 0)
    print("  %-7s ticks delta: %-13d stepping=%-5s -> %s"
          % (tag, d, b.get("stepping"), "EXECUTING" if d > 0 else "FROZEN"))
    return d > 0


def grab(tag):
    q = os.path.join(TMP, "ps-%s.png" % tag)
    try:
        if os.path.exists(q):
            os.remove(q)
    except OSError:
        pass
    subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), q], capture_output=True)
    return q if os.path.exists(q) and os.path.getsize(q) > 1000 else None


def pixdiff(a, b):
    from PIL import Image
    import numpy as np
    if not a or not b:
        return None
    x = np.asarray(Image.open(a).convert("RGB")).astype(np.int16)
    y = np.asarray(Image.open(b).convert("RGB")).astype(np.int16)
    return int((np.abs(x - y).max(axis=2) > 16).sum())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--len", type=lambda x: int(x, 0), default=0x60)
    ap.add_argument("--press", default="down")
    ap.add_argument("--rounds", type=int, default=12)
    ap.add_argument("--wait", type=float, default=1.3)
    ap.add_argument("--maxperiod", type=int, default=12)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()      # reads + control
    pad = pp.Debugger()    # input only

    print("game:", c.status().get("game", {}).get("title"))
    print("struct 0x%08X len 0x%X" % (a.base, a.len))
    print()
    print("=== liveness BEFORE ===")
    if not live(c, "before"):
        print("REFUSING: emulator not executing -- kill and relaunch, then re-run.")
        c.close(); pad.close()
        return 2

    nwords = a.len // 4

    def snap():
        b = c.read(a.base, a.len)
        if not b or len(b) < a.len:
            return None
        return list(struct.unpack_from("<%di" % nwords, b, 0))

    s0 = snap()
    if s0 is None:
        print("struct unreadable")
        c.close(); pad.close()
        return 3

    series = [s0]
    p_prev = grab("s0")
    moved = 0
    for i in range(1, a.rounds + 1):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 100 + i,
                                "button": a.press, "frames": 10}))
        time.sleep(a.wait)
        series.append(snap())
        cur = grab("s%d" % i)
        n = pixdiff(p_prev, cur)
        if n:
            moved += 1
        p_prev = cur

    print()
    print("=== presses that moved the display: %d/%d ===" % (moved, a.rounds))
    print()
    print("=== words that moved ===")
    found = []
    for i in range(nwords):
        vals = [s[i] if s else None for s in series]
        if len(set(vals)) > 1:
            found.append((i * 4, vals))
            print("   +0x%02X  %s" % (i * 4, vals))
    if not found:
        print("   (none)")

    print()
    print("=== wrap test (a cursor must return to a value it already held) ===")
    hit = False
    for off, vals in found:
        if any(v is None for v in vals):
            continue
        if max(vals) > 64 or min(vals) < 0:
            print("   +0x%02X  non-small (min %d max %d) -> allocator/pointer scale"
                  % (off, min(vals), max(vals)))
            continue
        for per in range(1, a.maxperiod + 1):
            if len(vals) > per + 1 and all(vals[k] == vals[k + per] for k in range(len(vals) - per)):
                print("   +0x%02X  WRAPS period %d  <== CURSOR CANDIDATE" % (off, per))
                hit = True
                break
        else:
            print("   +0x%02X  small but no wrap within %d" % (off, a.maxperiod))

    print()
    print("=== liveness AFTER ===")
    ok = live(c, "after")
    print()
    if moved == 0:
        print("VERDICT: VOID -- no press moved the display; the field readings are not evidence.")
    elif not ok:
        print("VERDICT: SUSPECT -- the emulator froze during the run.")
    elif hit:
        print("VERDICT: a wrapping small ordinal was found; verify on a menu with more rows.")
    elif found:
        print("VERDICT: fields move but none wraps -- these are not the selection index.")
    else:
        print("VERDICT: TRUSTWORTHY NEGATIVE -- nothing in this struct tracks the highlight.")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

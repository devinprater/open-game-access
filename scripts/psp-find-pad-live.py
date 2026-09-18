#!/usr/bin/env python3
"""psp-find-pad-live.py -- find the pad's LIVE (transient) button word by sampling DURING a hold.

THE FLAW THIS FIXES (doc section 58)
    Section 58 scanned the pad object (0x09EDA3A0) and found 95 responding words, but every series was
    LATCH-shaped -- values hold then change once. None showed the per-press alternation a live button
    word would produce. The cause was a timing blind spot:

        press sent with frames=20 (a hold)  ->  sleep(0.6)  ->  sample
                                                               ^ after release: transient state gone

    The proof it was a flaw and not a negative: `FUN_000f790c` writes `pad + 0x264` from the polled
    buttons, so that word MUST change while a button is held -- and it never appeared.

THE FIX: sample WHILE the button is down.
    Send the press with a LONG hold (frames=110 ~1.8s at 60fps) and take samples IMMEDIATELY with NO
    settle delay, several times during the hold. A transient button word then reads as a pulse -- set
    for the duration of the hold, clear after.

    A positive control is built in: `pad + 0x264` is known from the decompile to be driven by the
    buttons. If this method cannot make `+0x264` move, the method is wrong; if it does, the method is
    validated and any other pulse-shaped field found is a real button-state word.

USAGE
    python scripts/psp-find-pad-live.py --button cross --span 0x400 --samples 6
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
KNOWN_FLAGS_OFF = 0x264


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
    ap.add_argument("--button", default="cross")
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x400)
    ap.add_argument("--samples", type=int, default=6, help="samples taken DURING the hold")
    ap.add_argument("--hold", type=int, default=110, help="press duration in frames")
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

    raw = c.read(PAD_SLOT, 4)
    PAD = struct.unpack("<I", raw)[0] if raw and len(raw) == 4 else 0
    print()
    print("pad object 0x%08X" % PAD)
    print("positive control: pad + 0x%03X = 0x%08X  (FUN_000f790c writes this from the buttons)"
          % (KNOWN_FLAGS_OFF, PAD + KNOWN_FLAGS_OFF))
    if not (0x08800000 <= PAD < 0x0A000000):
        print("  pad pointer invalid")
        c.close(); pad.close()
        return 3

    nwords = a.span // 4

    def snap():
        b = c.read(PAD, a.span)
        if not b or len(b) < a.span:
            return None
        return list(struct.unpack_from("<%di" % nwords, b, 0))

    # --- IDLE baseline: sample with no press, same immediate cadence ---
    print()
    print("=== IDLE baseline (immediate samples, no press) ===")
    idle = []
    for i in range(a.samples):
        s = snap(); idle.append(s)
        print("  idle %d" % (i + 1), end="\r", flush=True)
    print()

    # --- HELD: send a long hold, then sample IMMEDIATELY, several times ---
    print("=== HELD phase: '%s' for %d frames, sampling DURING the hold ===" % (a.button, a.hold))
    held_runs = []
    for r in range(2):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 400 + r,
                                "button": a.button, "frames": a.hold}))
        seq = []
        for i in range(a.samples):
            seq.append(snap())
        held_runs.append(seq)
        print("  run %d captured" % (r + 1), end="\r", flush=True)
        time.sleep(1.2)   # let the hold finish and the button release
    print()

    # --- analysis: pulse-shaped = present in HELD, absent/idle in IDLE ---
    ki = KNOWN_FLAGS_OFF // 4
    idle_vals = [s[ki] for s in idle if s]
    held_vals = [s[ki] for run in held_runs for s in run if s]
    print()
    print("=== POSITIVE CONTROL: pad + 0x264 ===")
    print("   idle  : %s" % idle_vals)
    print("   held  : %s" % held_vals)
    if len(set(held_vals)) > 1:
        print("   -> the control MOVED. The method can see transient button state. VALID METHOD.")
    else:
        print("   -> the control did NOT move. The method is still blind -- result void.")

    pulse, latch = [], []
    for w in range(nwords):
        iv = [s[w] for s in idle if s]
        hv = [s[w] for run in held_runs for s in run if s]
        if len(set(hv)) < 2:
            continue
        # pulse: differs from the idle value while held
        if len(set(hv)) >= 2 and len(set(iv)) == 1:
            if iv[0] not in set(hv) or len(set(hv)) > 1:
                pulse.append((w, iv[0], hv))
        if len(set(iv)) > 1:
            latch.append(w)

    print()
    print("=== fields that CHANGE DURING THE HOLD ===")
    print("   total moving: %d   (churny in idle: %d)" % (len(pulse) + len(latch), len(latch)))
    show = [p for p in pulse if p[0] != ki]
    if show:
        for w, i0, hv in show[:20]:
            print("   pad + 0x%03X   0x%08X   idle %-12s  held %s" % (w * 4, PAD + w * 4, i0, hv))
    else:
        print("   (none besides the control)")

    print()
    print("=== liveness AFTER ===")
    live(c, "after")
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

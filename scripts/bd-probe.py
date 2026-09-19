#!/usr/bin/env python3
"""bd-probe.py -- direction probe with RAM-poll truth (no display-magnitude guessing).

LESSON (s106 correction): display-diff magnitude does NOT classify moves. Real moves
showed 0 px; non-moves showed ~90K px (camera settle). Texas... TRUTH IS THE RAM
(D+0x194/+0x195). Protocol per press: snapshot (x,y), send press, poll RAM at 2 Hz up
to 6 s for ANY change, then one screenshot for the record. A direction is BLOCKED only
after 3 consecutive no-change probes with settles.

USAGE
    python scripts/bd-probe.py --seq right,right,up,left --tag t1
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
BASE = 0x08804000


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seq", default="up,down,left,right")
    ap.add_argument("--tag", default="p")
    ap.add_argument("--tries", type=int, default=3)
    a = ap.parse_args()

    pp = load_client()
    c = pp.Debugger()
    pad = pp.Debugger()

    def u32(addr):
        try:
            b = c.read(addr, 4)
            return struct.unpack("<I", b)[0] if b and len(b) == 4 else None
        except Exception:
            return None

    def xy():
        try:
            M = u32(BASE + 0x00394940)
            P = u32(M + 0x118)
            B = u32(P + 0x04)
            D = u32(B + 0x10)
            return c.read(D + 0x194, 1)[0], c.read(D + 0x195, 1)[0]
        except Exception:
            return None

    def shot(t):
        q = os.path.join(TMP, "probe-%s-%s.png" % (a.tag, t))
        subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), q],
                       capture_output=True)
        return q

    def press(btn, frames=120):
        pad.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                                "button": btn, "frames": frames}))

    print("start:", xy(), flush=True)
    for d in a.seq.split(","):
        d = d.strip()
        if not d:
            continue
        moved = False
        for t in range(a.tries):
            before = xy()
            press(d)
            # poll RAM for change (2 Hz, 6 s); settle covers camera/animation locks
            for _ in range(12):
                time.sleep(0.5)
                after = xy()
                if after is not None and before is not None and after != before:
                    moved = True
                    break
            if moved:
                break
            time.sleep(1.0)
        print("%s: %s -> %s" % (d, "MOVED" if moved else "BLOCKEDx%d" % a.tries, xy()),
              flush=True)
        shot("probe-%s" % d)
    c.close(); pad.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

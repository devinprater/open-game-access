#!/usr/bin/env python3
"""psp-menu-cursor.py -- find the selection index by polling RAM while stepping a menu.

WHY THIS IS NOW POSSIBLE
    Earlier cursor hunts failed because the game was on a scene where only l/r responded (a sweep
    proved that), so no cursor could be exercised. The game has now been driven to a STATIC screen with
    saturated UI colour -- a menu -- so stepping with up/down is testable.

WHAT IT DOES
    On a static screen, reads a set of candidate words, presses down, reads again, and reports which
    words changed. Then presses up and reports again, so a value that goes
    n -> n+1 -> n (i.e. follows the highlight both ways) is distinguishable from one that only
    increments per press.

USAGE
    python scripts/psp-menu-cursor.py [--addr 0x08B96CF8] [--span 0x40]
"""
import argparse, importlib.util, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def words(buf):
    return [struct.unpack_from("<I", buf, i)[0] for i in range(0, len(buf) - 3, 4)]


def press(d, button, frames=10, wait=0.9):
    d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":%d}'
              % (button, frames))
    time.sleep(wait)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x08B96CD8)
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x40)
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    base = words(d.read(a.addr, a.span))
    print("baseline at 0x%08X (%d words):" % (a.addr, len(base)))
    for i, v in enumerate(base):
        print("   +0x%02X = %-10d 0x%08X" % (i * 4, v, v))

    hist = [base]
    for r in range(a.rounds):
        press(d, "down")
        b = words(d.read(a.addr, a.span))
        hist.append(b)
        press(d, "up")
        u = words(d.read(a.addr, a.span))
        hist.append(u)
        print("  round %d: down -> %s ; up -> %s"
              % (r + 1, "changed" if b != hist[-3] else "same", "changed" if u != b else "same"))

    print()
    print("=== fields that moved when DOWN was pressed (candidate selection indices) ===")
    for i in range(len(base)):
        vals = [h[i] for h in hist]
        if len(set(vals)) > 1:
            off = i * 4
            print("   +0x%02X  sequence over (base,down,up,down,up,...): %s" % (off, vals))
    d.close()


if __name__ == "__main__":
    main()

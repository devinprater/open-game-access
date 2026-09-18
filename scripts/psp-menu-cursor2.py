#!/usr/bin/env python3
"""psp-menu-cursor2.py -- poll RAM while pressing the controls that ACTUALLY drive the screen.

CORRECTION THIS IMPLEMENTS (doc section 24)
    The first cursor test pressed `down`/`up`, but on this screen `down` produced a screen change only
    1 of 3 rounds -- it does not scroll. The controls that demonstrably change the screen are `l` and
    `r` (the sweep found them) and `circle`/`triangle`/`select` (which opened the UI). So poll while
    pressing THOSE, and treat a value that follows the highlight both ways as the cursor.

WHAT IT DOES
    Reads a RAM window, presses a button, reads again, presses the opposite, reads again, and reports
    every word whose value moved -- with the full sequence so a cursor (n -> n+1 -> n) is separable
    from a counter (n -> n+1 -> n+2) and from churn.

USAGE
    python scripts/psp-menu-cursor2.py --addr 0x08B96CD8 --span 0x40 --pair l,r --rounds 3
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


def press(d, button, frames=10, wait=1.0):
    d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":%d}'
              % (button, frames))
    time.sleep(wait)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", type=lambda x: int(x, 0), default=0x08B96CD8)
    ap.add_argument("--span", type=lambda x: int(x, 0), default=0x40)
    ap.add_argument("--pair", default="l,r", help="two buttons, opposite directions")
    ap.add_argument("--rounds", type=int, default=3)
    a = ap.parse_args()

    b1, b2 = [x.strip() for x in a.pair.split(",")]
    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))
    print("pair: %s / %s   addr 0x%08X span 0x%X" % (b1, b2, a.addr, a.span))

    hist = [words(d.read(a.addr, a.span))]
    marks = ["base"]
    for _ in range(a.rounds):
        press(d, b1); hist.append(words(d.read(a.addr, a.span))); marks.append(b1)
        press(d, b2); hist.append(words(d.read(a.addr, a.span))); marks.append(b2)

    print("\n=== fields that moved ===")
    moved = 0
    for i in range(len(hist[0])):
        vals = [h[i] for h in hist]
        if len(set(vals)) > 1:
            moved += 1
            print("   +0x%02X  %s" % (i * 4, " -> ".join("%d" % v for v in vals)))
            print("         order: %s" % ", ".join(marks))
    if moved == 0:
        print("   (nothing in this window moved)")
    print("\nmoved field(s): %d" % moved)
    d.close()


if __name__ == "__main__":
    main()

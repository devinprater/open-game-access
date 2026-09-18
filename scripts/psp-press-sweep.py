#!/usr/bin/env python3
"""psp-press-sweep.py -- press a sequence of buttons, then let the CALLER capture and diff.

WHY IT DOES NOT CAPTURE ITSELF
    A screenshot helper invoked in-process via subprocess.run() silently produced no file (different
    cwd/env from the shell), so a previous sweep reported None for every button and concluded "no
    change" from an instrument that never ran. Rather than fight that, this script only PRESSES and
    prints timing markers; screenshots are taken from the shell between invocations, where the capture
    is known to work.

USAGE
    python scripts/psp-press-sweep.py --buttons cross,start --wait 1.5 --out markers.txt
"""
import argparse, importlib.util, json, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--buttons", default="cross,circle,triangle,square,start,select,l,r,down,up")
    ap.add_argument("--frames", type=int, default=12)
    ap.add_argument("--wait", type=float, default=1.5)
    ap.add_argument("--out", default=None, help="write 'before <button>' markers here")
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    buttons = [b.strip() for b in a.buttons.split(",") if b.strip()]
    fh = open(a.out, "w", encoding="utf-8") if a.out else None

    for b in buttons:
        if fh:
            fh.write("PRESS %s\n" % b)
            fh.flush()
        d.ws.send(json.dumps({"event": "input.buttons.press", "requestId": 1,
                              "button": b, "frames": a.frames}))
        print("  pressed %-9s" % b, flush=True)
        time.sleep(a.wait)

    if fh:
        fh.close()
    d.close()


if __name__ == "__main__":
    main()

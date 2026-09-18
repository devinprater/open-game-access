#!/usr/bin/env python3
"""psp-watch-words.py -- watch candidate words across repeated presses; report which track the highlight.

WHY
    A single-press RAM diff over a region is dominated by unrelated churn: one run returned 4109 changed
    words, of which 4096 were a single 16 KB buffer (frame/audio scratch). Buried among them were ~19
    SMALL clusters of 1-3 words -- the size a selection index would be.

    This script takes those small clusters as candidates and reads them across several presses of the
    SAME direction. Three outcomes are then distinguishable:
      * increments by a constant   -> a cursor/index following the highlight
      * toggles 0/1 or n/(n|0xFF)  -> a per-item FLAG (focused/active bit)
      * changes arbitrarily        -> churn (timing, counters, pool reuse)

USAGE
    python scripts/psp-watch-words.py --press down --steps 5 --addr 0x08BAC75C,0x08BB2E98,...
"""
import argparse, importlib.util, os, struct, time

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--steps", type=int, default=5)
    ap.add_argument("--addr", required=True, help="comma-separated addresses of the candidate words")
    ap.add_argument("--wait", type=float, default=1.1)
    a = ap.parse_args()

    addrs = [int(x, 0) for x in a.addr.split(",") if x.strip()]
    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))
    print("watching %d words, pressing %s %d times" % (len(addrs), a.press, a.steps))
    print()

    series = {ad: [] for ad in addrs}

    def sample():
        for ad in addrs:
            b = d.read(ad, 4)
            series[ad].append(struct.unpack("<i", b)[0] if b and len(b) == 4 else None)

    sample()
    for i in range(a.steps):
        d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % a.press)
        time.sleep(a.wait)
        sample()

    print("=== series (base, then after each %s) ===" % a.press)
    interesting = []
    for ad in addrs:
        v = series[ad]
        uniq = [x for x in v if x is not None]
        changed = len(set(uniq)) > 1
        print("  0x%08X  %s%s" % (ad, " ".join(str(x) for x in v),
                                  "   <== CHANGED" if changed else ""))
        if changed:
            # classify
            diffs = [b - a_ for a_, b in zip(v, v[1:]) if a_ is not None and b is not None]
            sd = set(diffs)
            if sd == {0}:  kind = "single-change (not tracking)"
            elif len(sd) == 1: kind = "monotonic step %d -> CURSOR CANDIDATE" % diffs[0]
            elif set(uniq) <= {0, 1}: kind = "0/1 toggle -> per-item FLAG"
            elif len(set(uniq)) == 2: kind = "two-valued"
            else: kind = "irregular"
            interesting.append((ad, kind, v))

    print()
    print("=== classification ===")
    for ad, kind, v in interesting:
        print("  0x%08X  %s" % (ad, kind))
        print("        %s" % v)
    if not interesting:
        print("   (nothing changed)")
    d.close()


if __name__ == "__main__":
    main()

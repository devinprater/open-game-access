#!/usr/bin/env python3
"""psp-dissidia-sweep.py -- find which input advances Dissidia, by sweeping buttons and watching.

WHY
    A screen that never changes plus input that demonstrably reaches the game (the debugger
    broadcasts `cross: true` then `false`) has several explanations, and guessing between them is
    useless. The reliable move is a SWEEP: send each button in turn, and after each one check both the
    SCREEN and a few RAM regions for any change. Whichever button produces a change is the one that
    advances the screen -- or none do, which is itself a finding (the game is waiting on something
    else, e.g. a file, or the screen really is static).

    Note on `cpu.status.pc`: it returned the SAME value (0x08909800) before and after a full emulator
    restart, so it is a stale/placeholder field in this PPSSPP build and must NOT be used as evidence
    that the CPU is hung. Judge by screen + memory change instead.

USAGE
    python scripts/psp-dissidia-sweep.py [--buttons cross,circle,start,...] [--wait 2.0]
"""
import argparse, hashlib, importlib.util, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")

WATCH = [0x09CED300, 0x09D16A68, 0x08B8C000, 0x09EF7200, 0x08800000]


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def shot(tag):
    p = os.path.join(TMP, "sweep-%s.png" % tag)
    subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), p],
                   capture_output=True)
    return p if os.path.exists(p) else None


def img_sig(path):
    if not path:
        return None
    from PIL import Image
    import numpy as np
    a = np.asarray(Image.open(path).convert("RGB"))
    # coarse signature: 32x32 block means
    h, w, _ = a.shape
    small = a[:h // 32 * 32, :w // 32 * 32].reshape(32, h // 32, 32, w // 32, 3).mean(axis=(1, 3))
    return hashlib.md5(small.astype("uint8").tobytes()).hexdigest()[:10]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--buttons",
                    default="cross,circle,triangle,square,start,select,up,down,left,right,l,r")
    ap.add_argument("--wait", type=float, default=2.0)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    def ram_sig():
        h = hashlib.md5()
        for addr in WATCH:
            h.update(d.read(addr, 0x40))
        return h.hexdigest()[:10]

    base_ram = ram_sig()
    base_img = img_sig(shot("base"))
    print("baseline  ram=%s  img=%s" % (base_ram, base_img))
    print()

    changed = []
    for b in [x.strip() for x in a.buttons.split(",") if x.strip()]:
        d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % b)
        time.sleep(a.wait)
        r = ram_sig()
        i = img_sig(shot(b))
        ram_ch = (r != base_ram)
        img_ch = (i != base_img)
        tag = []
        if ram_ch:
            tag.append("RAM")
        if img_ch:
            tag.append("SCREEN")
        print("  %-9s ram=%s img=%s  %s" % (b, r, i, ("CHANGED: " + "+".join(tag)) if tag else "no change"))
        if tag:
            changed.append(b)
            # reset the baseline so each button is judged on its own delta
            base_ram, base_img = r, i

    print()
    print("buttons that produced a change: %s" % (changed or "NONE"))
    d.close()


if __name__ == "__main__":
    main()

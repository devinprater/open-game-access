#!/usr/bin/env python3
"""psp-read-menu.py -- step through a menu with a button and OCR each position.

WHY
    OCR reads Dissidia's plain UI font reliably ("PAUSED", "Return to Game", tutorial text) but not
    the stylized game font. A vertical menu list is best read by stepping the highlight and OCRing
    the SAME crop each time, so the per-item text can be assembled from the sequence.

USAGE
    python scripts/psp-read-menu.py --press down --steps 6
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
S = os.path.join(TMP, "menu-read")
TESSDATA = r"C:\Users\Devin Prater\scoop\apps\tesseract-languages\current"


def capture(tag):
    p = os.path.join(S, "%s.png" % tag)
    subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), p], capture_output=True)
    return p if os.path.exists(p) and os.path.getsize(p) > 1000 else None


def ocr(path, crop=None, scale=2.5, psm="6"):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    if crop:
        im = im.crop(crop)
    if scale != 1.0:
        im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    work = os.path.join(S, "w.png")
    im.save(work)
    env = dict(os.environ); env["TESSDATA_PREFIX"] = TESSDATA
    out = os.path.join(S, "o")
    subprocess.run(["tesseract", work, out, "--psm", psm], capture_output=True, env=env)
    tl = out + ".txt"
    if not os.path.exists(tl):
        return []
    return [l.strip() for l in open(tl, encoding="utf-8", errors="replace") if l.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--steps", type=int, default=6)
    ap.add_argument("--crop", default="0,250,1280,1000")
    ap.add_argument("--scale", type=float, default=2.5)
    ap.add_argument("--psm", default="6")
    a = ap.parse_args()

    os.makedirs(S, exist_ok=True)
    crop = tuple(int(v) for v in a.crop.split(","))

    p = capture("step0")
    if not p:
        print("CAPTURE FAILED")
        return 1
    print("=== step 0 (no press) ===")
    for l in ocr(p, crop, a.scale, a.psm):
        print("   ", l)

    for i in range(1, a.steps + 1):
        # press via the sweep helper, one button, from the shell-equivalent subprocess
        subprocess.run(["python.exe", os.path.join(HERE, "psp-press-sweep.py"),
                        "--buttons", a.press, "--frames", "10", "--wait", "1.2"],
                       capture_output=True)
        time.sleep(1.0)
        p = capture("step%d" % i)
        print("=== step %d (%s) ===" % (i, a.press))
        if not p:
            print("    CAPTURE FAILED")
            continue
        for l in ocr(p, crop, a.scale, a.psm):
            print("   ", l)
    return 0


if __name__ == "__main__":
    sys.exit(main())

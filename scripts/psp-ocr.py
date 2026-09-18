#!/usr/bin/env python3
"""psp-ocr.py -- read the CURRENT PPSSPP screen as TEXT via OCR.

WHY THIS EXISTS
    Reading a screen by pixel statistics (mean luminance, saturated %) tells you *whether* UI is
    present but never *which* UI. Guessing from a luminance grid produced a wrong call once already
    ("story mode is loaded") because static EBOOT strings had been mistaken for live state.
    OCR answers "what does the screen say" directly, which is what matters for an accessibility
    reader, so it is the right instrument rather than a fallback.

SETUP (one-time)
    scoop install tesseract ; scoop install tesseract-languages
    TESSDATA_PREFIX must point at the tesseract-languages pack, NOT tesseract's own dir (which
    ships no eng.traineddata in this packaging).

USAGE
    python scripts/psp-ocr.py                 # capture + OCR the current screen
    python scripts/psp-ocr.py --image path.png  # OCR an existing capture
    python scripts/psp-ocr.py --crop 0,400,1706,1066   # limit to a region (x1,y1,x2,y2)
"""
import argparse, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")
TESSDATA = r"C:\Users\Devin Prater\scoop\apps\tesseract-languages\current"


def capture(path):
    subprocess.run(["python.exe", os.path.join(HERE, "psp-shot.py"), path], capture_output=True)
    return path if os.path.exists(path) and os.path.getsize(path) > 1000 else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", default=None)
    ap.add_argument("--crop", default=None, help="x1,y1,x2,y2")
    ap.add_argument("--scale", type=float, default=2.0, help="upscale factor (OCR likes ~2x)")
    ap.add_argument("--psm", default="6", help="tesseract page-segmentation mode")
    a = ap.parse_args()

    src = a.image or capture(os.path.join(TMP, "psp-ocr-shot.png"))
    if not src:
        print("CAPTURE FAILED (no readable PNG produced)")
        return 1

    work = src
    try:
        from PIL import Image
        im = Image.open(src).convert("RGB")
        if a.crop:
            x1, y1, x2, y2 = [int(v) for v in a.crop.split(",")]
            im = im.crop((x1, y1, x2, y2))
        if a.scale != 1.0:
            im = im.resize((int(im.width * a.scale), int(im.height * a.scale)), Image.LANCZOS)
        work = os.path.join(TMP, "psp-ocr-work.png")
        im.save(work)
    except Exception as e:
        print("image prep failed (%s) -- OCR on the raw file" % e)

    env = dict(os.environ)
    env["TESSDATA_PREFIX"] = TESSDATA
    out = os.path.join(TMP, "psp-ocr-out")
    r = subprocess.run(["tesseract", work, out, "--psm", a.psm],
                       capture_output=True, env=env, text=True)
    txt_path = out + ".txt"
    if not os.path.exists(txt_path):
        print("tesseract produced no output. stderr:")
        print((r.stderr or "")[:400])
        return 1

    lines = [l.rstrip() for l in open(txt_path, encoding="utf-8", errors="replace")]
    kept = [l for l in lines if l.strip()]
    print("=== OCR of %s : %d non-blank lines ===" % (os.path.basename(src), len(kept)))
    for l in kept:
        print("   ", l)
    return 0


if __name__ == "__main__":
    sys.exit(main())

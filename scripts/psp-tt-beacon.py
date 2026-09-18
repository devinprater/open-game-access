#!/usr/bin/env python3
"""psp-tt-beacon.py -- LIVE AUDIO BEACON on the objective chevron.

WHAT IT DOES
    Finds the objective chevron the game draws on the field, and plays a short panned tone that
    tells a blind player which way to steer:
        * PAN   -> which side the objective is on (left / right)
        * PITCH -> how far off-axis (straight ahead = low and calm, off to the side = higher)
        * a distinct "centred" tone when the objective is dead ahead

WHY A BEACON ON A *GLYPH* (and not on a direction vector)
    The objective direction was NOT found in RAM after several attempts (position and objective
    vector both unresolved). The chevron, however, is rendered on screen and encodes exactly the
    bearing the player needs. The project rule is "model the game, not the screen"; this is the
    sanctioned fallback, and it is justified because the bearing exists ONLY as that glyph.

THE GLYPH, MEASURED (not guessed)
    Double chevron, both heads along the bearing.
        FILL    RGB(73,112,254) periwinkle blue, flat-shaded (exact match works)
        OUTLINE white glow
        SEA     RGB(251,125,6) orange -- well separated from the fill
    In the reference frame it sat at centre (1282,152) of 1706x1066, extent 100x82, pointing UP.

    Two failed detectors preceded this one, both recorded so they are not repeated:
      1. a CYAN test (b>150 & b-r>55 & g>110) matched nothing -- the glyph is not cyan;
      2. a loose +-46 tolerance on the fill matched 2,220 px spanning 1704x978 -- terrain and the
         HUD portrait share that blue. Hence EXACT colour + a COMPACTNESS gate.

BEARING
    A chevron is symmetric about its pointing axis with its mass in the wings, so:
      * the MAJOR principal axis runs through the two wings,
      * the apex lies on the PERPENDICULAR of that axis,
      * the apex is the extreme that has FEWER points near it (wings are dense).
    Validated on the reference frame: bearing = -0.4 deg for a chevron pointing UP.

USAGE
    python scripts/psp-tt-beacon.py                 # live loop with audio
    python scripts/psp-tt-beacon.py --shot FILE     # analyse one PNG, print only
    python scripts/psp-tt-beacon.py --dump          # save the crop it selected
    python scripts/psp-tt-beacon.py --silent        # measure, no sound
    python scripts/psp-tt-beacon.py --interval 0.6 --min-gap 0.35
"""
import argparse, math, os, struct, subprocess, sys, tempfile, time, wave

try:
    from PIL import Image
    import numpy as np
except Exception as e:                                       # pragma: no cover
    sys.exit("need Pillow and numpy: %s" % e)

HERE = os.path.dirname(os.path.abspath(__file__))
SHOT = os.path.join(HERE, "psp-shot.py")

FILL = (73, 112, 254)
TOL = 8                 # exact: the fill is flat-shaded
MIN_PX = 80             # sampled every 2 px, so ~320 real pixels
MAX_EXTENT = 220        # a glyph, not a region of the world
SAMPLE_STEP = 2

TMP = os.path.join(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()), "Temp", "psp-beacon")


def capture(path: str) -> bool:
    """Grab the PPSSPP window into `path` (socket-free PrintWindow).

    ⚠️ The capture writes the PNG in place, so reading it too early raises
    `SyntaxError: broken PNG file`. That killed a live beacon run silently, which is worse than no
    beacon. Write to a temp name and only publish it once capture has returned AND the file parses.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    if os.path.exists(tmp):
        try: os.remove(tmp)
        except OSError: pass
    try:
        r = subprocess.run([sys.executable, SHOT, tmp], capture_output=True, text=True, timeout=60)
    except Exception as e:
        print("# capture failed: %s" % e)
        return False
    if not os.path.exists(tmp):
        print("# capture produced no file: %s" % (r.stderr or r.stdout or "")[-200:])
        return False

    # Verify it is a COMPLETE, parseable image before handing it to the eye.
    for attempt in range(6):
        try:
            with Image.open(tmp) as probe:
                probe.verify()
            os.replace(tmp, path)
            return True
        except Exception:
            time.sleep(0.25)
    print("# capture produced an unreadable PNG after retries")
    return False


def locate(path: str):
    """Return a dict with the chevron's centre + bearing, or {'found': False, ...}."""
    im = Image.open(path).convert("RGB")
    a = np.asarray(im)
    H, W = a.shape[0], a.shape[1]

    r, g, b = a[:, :, 0].astype(np.int16), a[:, :, 1].astype(np.int16), a[:, :, 2].astype(np.int16)
    mask = (np.abs(r - FILL[0]) <= TOL) & (np.abs(g - FILL[1]) <= TOL) & (np.abs(b - FILL[2]) <= TOL)
    mask[::SAMPLE_STEP, :] = mask[::SAMPLE_STEP, :]      # keep as-is; subsample below instead
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return {"found": False, "n": 0, "size": (W, H)}

    xs = xs[::SAMPLE_STEP]; ys = ys[::SAMPLE_STEP]
    n = len(xs)
    x0, x1 = int(xs.min()), int(xs.max())
    y0, y1 = int(ys.min()), int(ys.max())
    ext_w, ext_h = x1 - x0, y1 - y0

    if n < MIN_PX:
        return {"found": False, "n": n, "size": (W, H), "why": "only %d px (need %d)" % (n, MIN_PX)}
    if ext_w > MAX_EXTENT or ext_h > MAX_EXTENT:
        return {"found": False, "n": n, "size": (W, H),
                "why": "extent %dx%d too large to be the glyph" % (ext_w, ext_h)}

    cx, cy = float(xs.mean()), float(ys.mean())
    dx = xs - cx; dy = ys - cy
    sxx = float((dx * dx).mean()); syy = float((dy * dy).mean()); sxy = float((dx * dy).mean())
    theta = 0.5 * math.atan2(2 * sxy, sxx - syy)          # major axis: through the wings
    pvx, pvy = -math.sin(theta), math.cos(theta)          # perpendicular: the pointing axis

    proj = dx * pvx + dy * pvy
    lo, hi = float(proj.min()), float(proj.max())

    def near(v, tol=12.0):
        return int((np.abs(proj - v) < tol).sum())

    # apex = the sparser extreme (the wings hold the mass)
    apex_v = lo if near(lo) < near(hi) else hi
    k = int(np.argmin(np.abs(proj - apex_v)))
    bearing = math.degrees(math.atan2(xs[k] - cx, -(ys[k] - cy)))   # 0 = up, + = clockwise

    return {"found": True, "n": n, "cx": cx, "cy": cy, "bearing": bearing,
            "bbox": (x0, y0, x1, y1), "extent": (ext_w, ext_h), "size": (W, H)}


# ---------------------------------------------------------------- audio
def make_tone(path, freq, pan, ms=130, rate=44100):
    """Write a short stereo WAV: a soft tone panned by `pan` in [-1,1]."""
    n = int(rate * ms / 1000.0)
    left = math.sqrt(max(0.0, (1.0 - pan) / 2.0))
    right = math.sqrt(max(0.0, (1.0 + pan) / 2.0))
    fade = int(rate * 0.012)
    data = bytearray()
    for i in range(n):
        env = min(1.0, i / fade, (n - i) / fade) if fade else 1.0
        s = math.sin(2 * math.pi * freq * i / rate) * env * 0.42
        data += struct.pack("<hh", int(s * left * 32767), int(s * right * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(bytes(data))


def play(path):
    """Play asynchronously (Windows). Falls back to silence elsewhere."""
    try:
        import winsound
        winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_ASYNC)
        return True
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", help="analyse this PNG instead of capturing")
    ap.add_argument("--dump", action="store_true", help="save the crop it selected")
    ap.add_argument("--silent", action="store_true", help="measure only, no sound")
    ap.add_argument("--interval", type=float, default=0.7, help="seconds between samples")
    ap.add_argument("--min-gap", type=float, default=0.30, help="min seconds between tones")
    ap.add_argument("--once", action="store_true", help="single sample then exit")
    a = ap.parse_args()

    live = a.shot is None
    frame = a.shot or os.path.join(TMP, "beacon_shot.png")
    tone = os.path.join(TMP, "tone.wav")
    os.makedirs(TMP, exist_ok=True)

    print("# objective-chevron audio beacon")
    print("# fill RGB%s exact +- %d, compactness <= %dpx" % (FILL, TOL, MAX_EXTENT))
    print("# bearing 0 = ahead; pan LEFT/RIGHT; higher pitch = further off-axis")

    last_play = 0.0
    while True:
        if live and not capture(frame):
            time.sleep(a.interval); continue

        try:
            res = locate(frame)
        except Exception as e:
            # Never let a bad frame kill the beacon: report and keep going.
            print("# skipped a frame: %s" % e)
            if a.once or not live:
                break
            time.sleep(a.interval); continue
        if not res["found"]:
            why = res.get("why", "not present")
            print("# no chevron (%s)" % why)
        else:
            br = res["bearing"]
            pan = max(-1.0, min(1.0, br / 60.0))
            pitch = 440.0 * (2 ** (abs(br) / 90.0))
            side = "RIGHT" if br > 8 else "LEFT" if br < -8 else "AHEAD"
            print("bearing=%+6.1f deg  pan=%+.2f  pitch=%4.0f Hz  %-5s  px=%d  centre=(%.0f,%.0f)"
                  % (br, pan, pitch, side, res["n"], res["cx"], res["cy"]))

            if a.dump:
                im = Image.open(frame).convert("RGB")
                cx, cy = int(res["cx"]), int(res["cy"])
                im.crop((max(0, cx - 160), max(0, cy - 160), cx + 160, cy + 160)) \
                  .save(os.path.join(TMP, "beacon_crop.png"))
                print("#   crop -> %s" % os.path.join(TMP, "beacon_crop.png"))

            if not a.silent and (time.time() - last_play) >= a.min_gap:
                make_tone(tone, pitch, pan)
                play(tone)
                last_play = time.time()

        if a.once or not live:
            break
        time.sleep(a.interval)


if __name__ == "__main__":
    main()

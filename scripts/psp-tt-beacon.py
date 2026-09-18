#!/usr/bin/env python3
"""psp-tt-beacon.py -- LIVE STEERING BEACON: "turn left/right until the arrow points up".

THE BEHAVIOUR (this is the user's specification, not a guess)
    "point right until the arrow points up, and then go back to pointing forward"

    So the cue is a SIDE CUE, not a proportional pan:
        arrow off to the RIGHT  ->  a blip in the RIGHT ear, repeating until you turn
        arrow off to the LEFT   ->  a blip in the LEFT ear
        arrow pointing UP       ->  a low CENTRED tone = "on target, go forward"

    The blip RATE scales with how far off you are, so urgency conveys "how much to turn" without the
    player having to judge a continuous pan.

WHY NOT A PROPORTIONAL PAN
    The earlier version panned by the glyph's bearing angle. The user reported panning worked for
    plain left/right but fell apart when the arrow pointed down-right and similar: the glyph's angle
    has a large component that does not correspond to a left/right decision. A side cue is what is
    actionable, so that is what this emits.

THE GLYPH, MEASURED (not assumed)
    Double chevron -- a large `^` with a smaller filled triangle beneath it -- which ROTATES to point
    along the objective bearing. The user sees it as ">>" when the objective is right and as an
    up-arrow when the objective is ahead.
        FILL  RGB(73,112,254) periwinkle, flat-shaded  (exact match works; a +-46 tolerance matched
              the whole screen because terrain and the HUD portrait share that blue)
        size  ~106x85 px
    ⚠️ DRAWN ONLY WHILE THE PLAYER IS MOVING: holding the stick gave it in 9/10 frames, standing
    still 0/8. Silence while stationary is the game, not a fault.

BEARING METHOD (validated 8/8 against known rotations of the real glyph)
    centroid -> principal axis (runs through the two wings) -> pointing axis is its perpendicular ->
    the apex is the extreme end with FEWER pixels nearby (narrow at the tip, dense at the base) ->
    bearing = atan2(dx, -dy).  0 = up, + = clockwise (right).
    A `narrow-end` alternative scored 0/8 and was discarded. Harness:
    scripts/psp-tt-glyph-validate.py.

USAGE
    python scripts/psp-tt-beacon.py                  # live, with sound
    python scripts/psp-tt-beacon.py --debug          # print every sample
    python scripts/psp-tt-beacon.py --silent         # measure only, no audio
    python scripts/psp-tt-beacon.py --shot FILE      # one frame
    python scripts/psp-tt-beacon.py --interval 0.30 --tol 12
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
MIN_PX = 80
MAX_EXTENT = 280        # a glyph, not a region of the world
SAMPLE_STEP = 2

TMP = os.path.join(os.environ.get("LOCALAPPDATA", tempfile.gettempdir()), "Temp", "psp-beacon")


def capture(path: str) -> bool:
    """Grab the PPSSPP window into `path` (socket-free PrintWindow), atomically.

    ⚠️ The capture writes the PNG in place, so reading it too early raises
    `SyntaxError: broken PNG file`. That killed a live beacon run silently, which is worse than no
    beacon. Write to a temp name and only publish it once the file parses.
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # ⚠️ The temp name MUST keep a .png extension: psp-shot.py chooses the encoder from the file
    # extension, so a `.part` suffix made it raise KeyError('.part') and write NOTHING -- which
    # silently disabled the whole beacon. Use `<name>.tmp.png` instead.
    tmp = path + ".tmp.png"
    for _ in range(4):
        if os.path.exists(tmp):
            try: os.remove(tmp)
            except OSError: pass
        try:
            subprocess.run([sys.executable, SHOT, tmp], capture_output=True, text=True, timeout=60)
        except Exception:
            continue
        if not os.path.exists(tmp):
            continue
        try:
            with Image.open(tmp) as probe:
                probe.verify()
            os.replace(tmp, path)
            return True
        except Exception:
            time.sleep(0.2)
    return False


def locate(path: str):
    """Return (bearing_deg, centre_x, centre_y, npix) or None. bearing 0 = up, + = clockwise."""
    im = Image.open(path).convert("RGB")
    a = np.asarray(im)
    r, g, b = a[:, :, 0].astype(np.int16), a[:, :, 1].astype(np.int16), a[:, :, 2].astype(np.int16)
    mask = (np.abs(r - FILL[0]) <= TOL) & (np.abs(g - FILL[1]) <= TOL) & (np.abs(b - FILL[2]) <= TOL)
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None

    xs = xs[::SAMPLE_STEP]; ys = ys[::SAMPLE_STEP]
    n = len(xs)
    if n < MIN_PX:
        return None
    if xs.max() - xs.min() > MAX_EXTENT or ys.max() - ys.min() > MAX_EXTENT:
        return None

    xs = xs.astype(float); ys = ys.astype(float)
    cx, cy = float(xs.mean()), float(ys.mean())
    dx = xs - cx; dy = ys - cy
    sxx = float((dx * dx).mean()); syy = float((dy * dy).mean()); sxy = float((dx * dy).mean())
    theta = 0.5 * math.atan2(2 * sxy, sxx - syy)          # major axis: through the wings
    pvx, pvy = -math.sin(theta), math.cos(theta)          # perpendicular: the pointing axis
    proj = dx * pvx + dy * pvy
    lo, hi = float(proj.min()), float(proj.max())
    if hi - lo < 1:
        return None

    # ⚠️ SCALE-INVARIANT apex tolerance. A fixed 12-pixel tolerance made the SAME glyph read
    # differently at different rendered sizes: three by-eye-identical up-pointing glyphs read
    # -101.4, +0.0 and +1.4 deg. Use a fraction of the projection range instead.
    _span = max(1e-6, hi - lo)
    def near(v, tol=0.12 * _span):
        return int((np.abs(proj - v) < tol).sum())

    apex_v = lo if near(lo) < near(hi) else hi            # sparse end = the tip
    k = int(np.argmin(np.abs(proj - apex_v)))
    bearing = math.degrees(math.atan2(xs[k] - cx, -(ys[k] - cy)))
    return bearing, cx, cy, n


# ---------------------------------------------------------------- audio
def make_tone(path, freq, pan, ms=100, rate=44100):
    """Short stereo blip. pan -1 = left ear, 0 = centre, +1 = right ear."""
    n = int(rate * ms / 1000.0)
    left = math.sqrt(max(0.0, (1.0 - pan) / 2.0))
    right = math.sqrt(max(0.0, (1.0 + pan) / 2.0))
    fade = max(1, int(rate * 0.010))
    data = bytearray()
    for i in range(n):
        env = min(1.0, i / fade, (n - i) / fade)
        s = math.sin(2 * math.pi * freq * i / rate) * env * 0.45
        data += struct.pack("<hh", int(s * left * 32767), int(s * right * 32767))
    with wave.open(path, "wb") as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(bytes(data))


def play(path):
    try:
        import winsound
        winsound.PlaySound(path, winsound.SND_FILENAME | winsound.SND_ASYNC)
        return True
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", help="analyse this PNG instead of capturing")
    ap.add_argument("--silent", action="store_true", help="measure only, no audio")
    ap.add_argument("--debug", action="store_true", help="print every sample")
    ap.add_argument("--interval", type=float, default=0.30, help="seconds between samples")
    ap.add_argument("--tol", type=float, default=12.0,
                    help="degrees within which the arrow counts as 'up' (on target)")
    ap.add_argument("--once", action="store_true", help="single sample then exit")
    ap.add_argument("--hold", type=float, default=1.2,
                    help="seconds to keep repeating the last cue after the glyph disappears")
    a = ap.parse_args()

    live = a.shot is None
    frame = a.shot or os.path.join(TMP, "beacon_shot.png")
    os.makedirs(TMP, exist_ok=True)

    # Pre-render the three cues so the loop never waits on synthesis.
    cue = {
        "right": os.path.join(TMP, "cue_right.wav"),
        "left":  os.path.join(TMP, "cue_left.wav"),
        "ahead": os.path.join(TMP, "cue_ahead.wav"),
    }
    make_tone(cue["right"], 720, +1.0, 95)     # sharp blip, hard right
    make_tone(cue["left"],  720, -1.0, 95)     # sharp blip, hard left
    make_tone(cue["ahead"], 330,  0.0, 180)    # low, centred, longer = "go forward"
    cue["move"] = os.path.join(TMP, "cue_move.wav")
    make_tone(cue["move"], 240, 0.0, 60)       # very soft tick = "no arrow; keep moving"

    print("# steering beacon -- RIGHT blip: turn right | LEFT blip: turn left | low centre tone: go")
    print("# glyph RGB%s exact +- %d | on-target window +-%.0f deg" % (FILL, TOL, a.tol))
    print("# the glyph is drawn only while MOVING -- silence when stationary is the game")

    last = 0.0
    last_play = 0.0
    last_cue = None
    last_seen = time.time()   # "now" on startup, so the first tick waits a full --hold
    prev_b = None             # previous bearing, for jump rejection
    while True:
        if live and not capture(frame):
            if a.once:
                break
            time.sleep(a.interval); continue

        try:
            r = locate(frame)
        except Exception as e:
            print("# skipped a frame: %s" % e)
            if a.once or not live:
                break
            time.sleep(a.interval); continue

        cue_name = None
        if r is None:
            if a.debug:
                print("#   no glyph (stationary, or objective off-screen)")
            # ⚠️ The glyph is only drawn while MOVING, so a stationary player gets total silence --
            # reported by the user as "it worked for a moment then stopped". Hold the last known cue
            # through brief dropouts, and after a longer gap emit an occasional soft "keep moving"
            # tick so the beacon never goes completely dead without explanation.
            since = time.time() - last_seen
            if last_cue and since < a.hold:
                cue_name = last_cue
            elif since >= a.hold and time.time() - last_play >= 2.5:
                if not a.silent:
                    play(cue["move"])
                    last_play = time.time()
                if a.debug:
                    print("#   no glyph for %.1fs -> 'keep moving' tick" % since)
        else:
            b, cx, cy, n = r
            b = ((b + 180) % 360) - 180
            off = abs(b)
            # ⚠️ Reject single-frame jumps. The bearing occasionally flips by ~180 degrees for one
            # frame (apex ambiguity): an audit of 24 live glyph frames found only two discrete
            # readings, ~0 (22 frames, correct) and ~-101 (2 frames, flipped). The flip would produce
            # exactly the "points the wrong way sometimes" symptom the user reported. A jump of
            # >60 deg between consecutive frames, with no turn commanded, is noise -- ignore it.
            if prev_b is not None and abs(((b - prev_b + 180) % 360) - 180) > 60.0:
                if a.debug:
                    print("#   bearing jump %+.0f -> %+.0f ignored (apex ambiguity)" % (prev_b, b))
                b = prev_b
            prev_b = b

            if abs(b) <= a.tol:
                cue_name = "ahead"
            else:
                cue_name = "right" if b > 0 else "left"
            last_cue = cue_name
            last_seen = time.time()
            if a.debug or a.silent:
                print("#   bearing=%+6.1f deg -> %-5s  centre=(%.0f,%.0f) px=%d"
                      % (b, cue_name, cx, cy, n))

        if cue_name and not a.silent:
            # Faster blips the further off-target you are; a slow calm tone when on target.
            if cue_name == "ahead":
                gap = 0.85
            elif cue_name == "move":
                gap = 2.5
            else:
                gap = max(0.22, 0.70 - (abs(off) / 90.0) * 0.45)
            if time.time() - last_play >= gap:
                play(cue[cue_name])
                last_play = time.time()

        if a.once or not live:
            break
        time.sleep(a.interval)


if __name__ == "__main__":
    main()

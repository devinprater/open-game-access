#!/usr/bin/env python3
"""psp-diff-press.py -- find menu state by diffing a RAM REGION across one navigation press.

WHY THIS IS DIFFERENT FROM THE FAILED FULL-RAM SCANS
    Earlier attempts to find the objective/cursor by differencing RAM produced 174,900 and 75,223
    "fixed triples" -- dominated by static code, because the whole readable span was scanned with a
    float-shaped filter. Section 30 established why that could never work: the PSP EBOOT is ONE big
    RX segment (vaddr 0x00000000-0x003A6860) plus a small RW segment (0x003A6860+), so most of the
    span is immutable text.

    This script instead diffs a BOUNDED region that can plausibly hold state -- the ELF's data/BSS and
    the heap just above it -- across a SINGLE press, and reports every 4-byte offset that changed.
    A menu highlight writes one or two words; that shows up immediately.

    The press is verified: the script confirms the screen actually changed, so a null result cannot be
    mistaken for "the press did not land".

USAGE
    python scripts/psp-diff-press.py --press down --lo 0x08BA6860 --hi 0x08D00000
"""
import argparse, importlib.util, os, struct, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.expandvars(r"%LOCALAPPDATA%\Temp")


def load_client():
    spec = importlib.util.spec_from_file_location("pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def snap_screen(tag):
    """Capture screen stats, proving the file exists (in-process capture used to fail silently)."""
    p = os.path.join(TMP, "dp-%s.png" % tag)
    try:
        if os.path.exists(p):
            os.remove(p)
    except OSError:
        pass
    r = subprocess.run([sys.executable, os.path.join(HERE, "psp-shot.py"), p],
                       capture_output=True, text=True)
    if not os.path.exists(p) or os.path.getsize(p) < 1000:
        return None
    try:
        from PIL import Image
        import numpy as np
        a = np.asarray(Image.open(p).convert("RGB")).astype("int16")
        lum = a.mean(axis=2)
        h, w = lum.shape
        g = lum[:h // 8 * 8, :w // 8 * 8].reshape(8, h // 8, 8, w // 8).mean(axis=(1, 3))
        return {"lum": round(float(lum.mean()), 1), "grid": g.astype("uint8").tobytes()}
    except Exception:
        return None


def read_region(d, lo, hi, chunk=1 << 20):
    out = bytearray()
    pos = lo
    while pos < hi:
        b = d.read(pos, min(chunk, hi - pos))
        if b:
            out.extend(b)
        else:
            out.extend(b"\x00" * min(chunk, hi - pos))
        pos += chunk
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--press", default="down")
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08BA6860)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x08D00000)
    ap.add_argument("--wait", type=float, default=1.4)
    a = ap.parse_args()

    # the region must be 4-byte aligned and sane
    lo, hi = a.lo, a.hi
    print("region 0x%08X-0x%08X (%.2f MB)  press=%s" % (lo, hi, (hi - lo) / 1048576.0, a.press))

    pp = load_client()
    d = pp.Debugger()
    print("game:", d.status().get("game", {}).get("title"))

    s0 = snap_screen("before")
    before = read_region(d, lo, hi)
    print("baseline read: %d bytes   screen: %s" % (len(before), s0["lum"] if s0 else "CAPTURE FAILED"))

    d.ws.send('{"event":"input.buttons.press","requestId":1,"button":"%s","frames":10}' % a.press)
    time.sleep(a.wait)

    s1 = snap_screen("after")
    after = read_region(d, lo, hi)
    print("after  press:  %d bytes   screen: %s" % (len(after), s1["lum"] if s1 else "CAPTURE FAILED"))

    if s0 and s1:
        print("screen %s" % ("CHANGED (press landed)" if s0["grid"] != s1["grid"]
                             else "IDENTICAL -- press may not have moved the highlight"))

    n = min(len(before), len(after))
    diffs = []
    for off in range(0, n - 3, 4):
        x = before[off:off + 4]
        y = after[off:off + 4]
        if x != y:
            diffs.append((lo + off, struct.unpack("<I", x)[0], struct.unpack("<I", y)[0]))

    print()
    print("=== 4-byte words that changed: %d ===" % len(diffs))
    for addr, v0, v1 in diffs[:60]:
        print("   0x%08X  %10d (0x%08X)  ->  %10d (0x%08X)" % (addr, v0, v0, v1, v1))
    if len(diffs) > 60:
        print("   ... %d more" % (len(diffs) - 60))

    # cluster them so a single structure is visible as one block
    if diffs:
        print()
        print("=== clusters (gaps < 0x40) ===")
        runs = [[diffs[0]]]
        for cur in diffs[1:]:
            if cur[0] - runs[-1][-1][0] < 0x40:
                runs[-1].append(cur)
            else:
                runs.append([cur])
        for r in runs[:20]:
            print("   0x%08X..0x%08X  %d word(s)" % (r[0][0], r[-1][0], len(r)))
    d.close()


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""psp-dissidia-textdump.py -- dump Dissidia's DECODED text from live RAM to a file.

STATUS OF THE PROBLEM
    The on-disk resources (`*_help.bin`, `friend_card.bin`, `name.bin`) are encoded/compressed and
    are NOT plaintext in any form (verified: ten codecs falsified, index points at assets not text).
    But the game DECODES them at load, and the decoded text sits in RAM in plain form:

        ASCII    "quicksave. data and return you to the start menu"
                 "REPLAY DATA" / "Replay data is corrupt" / "Load failed"
                 "Insufficient space on Memory Stick" / "This game has an autosave function."
        UTF-16LE "Sword Thrust" "Rising Buckler" "Shield Strike" "Shield of Light"
                 "Shining Wave" "Radiant Sword" "Rune Saber" "Bitter End" "Swordslash"

    So the reader does not need the archive codec at all -- it needs the RAM text region.

WHAT THIS DOES
    Scans the MAPPED user-RAM span (Dissidia: 0x08800000..0x0A000000, but it is enumerated, never
    assumed) collecting both encodings, and writes them with their addresses to a report.

USAGE
    python scripts/psp-dissidia-textdump.py [--out FILE] [--min-len 8]
"""
import argparse, importlib.util, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load_client():
    spec = importlib.util.spec_from_file_location(
        "pp", os.path.join(HERE, "psp-ppsspp-client.py"))
    pp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pp)
    return pp


def utf16_runs(buf, minlen):
    out = []
    i = 0
    n = len(buf)
    while i < n - 1:
        j = i
        s = []
        while j < n - 1 and buf[j + 1] == 0 and 32 <= buf[j] < 127:
            s.append(chr(buf[j])); j += 2
        if len(s) >= minlen:
            # capture a possible 1-byte format prefix immediately before the run
            prefix = buf[i - 1] if i > 0 else 0
            out.append((i, "".join(s), prefix))
            i = j
        else:
            i += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.expanduser("~"),
                                                 "dissidia-ram-text.txt"))
    ap.add_argument("--min-len", type=int, default=8)
    ap.add_argument("--lo", type=lambda x: int(x, 0), default=0x08800000)
    ap.add_argument("--hi", type=lambda x: int(x, 0), default=0x0C000000)
    ap.add_argument("--chunk-kib", type=int, default=256)
    a = ap.parse_args()

    pp = load_client()
    d = pp.Debugger()

    st = d.status()
    game = st.get("game", {}) if isinstance(st, dict) else {}
    print("game: %s (%s)" % (game.get("title"), game.get("id")))

    spans = d.map_readable(a.lo, a.hi, step=0x100000)
    print("readable spans: %s" % [("0x%08X-0x%08X" % s) for s in spans])
    if not spans:
        print("no readable memory -- is the game running?")
        d.close()
        return

    asc, utf = [], []
    CH = a.chunk_kib << 10
    for lo, hi in spans:
        pos = lo
        while pos < hi:
            ln = min(CH, hi - pos)
            b = d.read(pos, ln)
            if b:
                for m in re.finditer(rb"[\x20-\x7e]{%d,}" % a.min_len, b):
                    asc.append((pos + m.start(), m.group().decode("latin1")))
                for off, s, pre in utf16_runs(b, a.min_len):
                    utf.append((pos + off, s, pre))
            pos += ln
            print("   ...0x%08X" % pos, end="\r", flush=True)
    print()

    # Filter the ASCII noise: keep text with at least one space and 2+ words of letters.
    def prose(s):
        return (" " in s) and len(re.findall(r"[A-Za-z]{2,}", s)) >= 2

    asc_keep = [(ad, s) for ad, s in asc if prose(s)]
    utf_keep = [(ad, s, p) for ad, s, p in utf if s.strip() and len(s) >= a.min_len]

    with open(a.out, "w", encoding="utf-8") as f:
        f.write("Dissidia Final Fantasy (ULUS10437) -- decoded text read from live RAM\n")
        f.write("readable spans: %s\n\n" % [("0x%08X-0x%08X" % s) for s in spans])
        f.write("=== ASCII strings (%d) ===\n" % len(asc_keep))
        for ad, s in asc_keep:
            f.write("0x%08X  %s\n" % (ad, s))
        f.write("\n=== UTF-16LE strings (%d) === prefix is the 1-byte code before the run\n"
                % len(utf_keep))
        for ad, s, p in utf_keep:
            f.write("0x%08X  pre=0x%02X  %s\n" % (ad, p, s))
    d.close()

    print("ASCII prose strings : %d" % len(asc_keep))
    print("UTF-16LE strings    : %d" % len(utf_keep))
    print("written -> %s" % a.out)

    print("\n=== sample UTF-16LE ===")
    for ad, s, p in utf_keep[:15]:
        print("  0x%08X  pre=0x%02X  %s" % (ad, p, s[:70]))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""psp-find-text.py -- find sentence-like text anywhere in a large binary.

WHY THIS SHAPE
    Dissidia's menu resources turn out NOT to hold plain strings: the label graphics are baked
    .tm2 textures ("title_09_BattleLobby.tm2") and the *_help.bin tables appear encoded or
    compressed. Rather than guess at an encoding, scan for the SHAPE of real prose:
        * UTF-16LE runs that contain a space and are long enough to be a sentence
        * single-byte ASCII runs that contain a space and look like prose
    Group hits into windows and rank by density, then print samples. This is the same approach that
    located the Tag Team story-text blob, generalised to both encodings.

USAGE
    python scripts/psp-find-text.py BIGFILE [--chunk-mib 16] [--min-len 24] [--top 20]
    python scripts/psp-find-text.py BIGFILE --at 12345 --show 400
"""
import argparse, os, re, sys

SPACE = ord(" ")


def sentence_utf16(buf, minlen):
    """UTF-16LE runs that contain at least one space and are >= minlen chars."""
    out = []
    i = 0
    n = len(buf)
    while i < n - 1:
        j = i
        s = []
        while j < n - 1:
            c, hi = buf[j], buf[j + 1]
            if hi == 0 and (c == 0x20 or 0x21 <= c <= 0x7E):
                s.append(chr(c)); j += 2
            else:
                break
        if len(s) >= minlen and (" " in s):
            out.append((i, "".join(s)))
            i = j
        else:
            i += 1
    return out


def sentence_ascii(buf, minlen):
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, buf):
        s = m.group()
        if b" " in s and not re.search(rb"[A-Za-z]{3}", s) is None:
            # require at least two words of letters
            if len(re.findall(rb"[A-Za-z]{2,}", s)) >= 2:
                out.append((m.start(), s.decode("latin1")))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--chunk-mib", type=int, default=16)
    ap.add_argument("--min-len", type=int, default=24)
    ap.add_argument("--top", type=int, default=20)
    ap.add_argument("--at", type=int)
    ap.add_argument("--show", type=int, default=400)
    a = ap.parse_args()

    size = os.path.getsize(a.path)
    if a.at is not None:
        with open(a.path, "rb") as f:
            f.seek(a.at)
            d = f.read(a.show)
        print("=== %s @%d (%d bytes) ===" % (os.path.basename(a.path), a.at, len(d)))
        print("hex : %s" % d[:64].hex())
        print("repr: %r" % d[:200])
        return

    CH = a.chunk_mib << 20
    print("scanning %s (%d bytes) in %d MiB chunks, min %d chars with a space"
          % (os.path.basename(a.path), size, a.chunk_mib, a.min_len))

    windows = {}
    pos = 0
    with open(a.path, "rb") as f:
        while pos < size:
            f.seek(pos)
            buf = f.read(CH + 1024)
            if not buf:
                break
            for i, s in sentence_utf16(buf, a.min_len):
                key = (pos + i) >> 20          # 1 MiB window
                windows.setdefault(key, []).append(("u16", pos + i, s))
            for i, s in sentence_ascii(buf, a.min_len):
                key = (pos + i) >> 20
                windows.setdefault(key, []).append(("ascii", pos + i, s))
            pos += CH
            print("   ...%d%%" % (100 * pos // size), end="\r", flush=True)

    print()
    ranked = sorted(windows.items(), key=lambda kv: -len(kv[1]))
    print("\n=== %d windows with hits; top %d by count ===" % (len(windows), a.top))
    for key, hits in ranked[:a.top]:
        kinds = {}
        for k, _, _ in hits:
            kinds[k] = kinds.get(k, 0) + 1
        print("   @%d MiB   %d hit(s)  %s" % (key, len(hits), kinds))
        for k, off, s in hits[:4]:
            print("        %-5s @%-11d %s" % (k, off, s[:88]))


if __name__ == "__main__":
    main()

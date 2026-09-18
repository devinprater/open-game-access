#!/usr/bin/env python3
"""psp-dissidia-textsolve.py -- validate the offset table and test the remaining text encodings.

WHAT IS ESTABLISHED (measured)
    accessory_help.bin: 64682 bytes. Leading u16 values ascend: 6091, 6154, 6229, 6302, 6359 ...
    If entry i starts at table[i] and the next entry starts at table[i+1], the length of entry i is
    the difference -- which lands at 57-75 bytes, exactly the size of a one-sentence help string.

    Body entropy 6.730 b/byte. That number is decisive in two directions:
        XOR with a RANDOM keystream would give ~8.0 (uniform output),
        a substitution cipher would keep ~4.0 (English letter frequency),
        and 6.73 sits between them -- which is what an 8/16-bit GLYPH INDEX stream looks like.

    So this script tests, in order:
      1. table validity -- do the offsets tile the file (is the table reading even right?)
      2. is it a substitution cipher?  index of coincidence + chi-square vs English
      3. is it bit-packed?  6/7/8-bit unpacking scored on printable rate and letter rate
      4. is it glyph indices?  the top values should be FEW and the distribution long-tailed
      5. do the two copies of the file differ (language variants)?
"""
import collections, math, os, struct, sys


def entropy(b):
    c = collections.Counter(b); n = max(1, len(b))
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def ioc(b):
    """Index of coincidence: ~0.066 for English, ~0.038 for uniform random (256 symbols)."""
    c = collections.Counter(b); n = len(b)
    return sum(v * (v - 1) for v in c.values()) / (n * (n - 1)) if n > 1 else 0.0


def load_table(d):
    vals = []
    for i in range(0, min(len(d) - 1, 100000), 2):
        vals.append(struct.unpack_from("<H", d, i)[0])
    return vals


def main():
    path = sys.argv[1]
    other = sys.argv[2] if len(sys.argv) > 2 else None
    d = open(path, "rb").read()
    print("=== %s : %d bytes ===" % (os.path.basename(path), len(d)))
    print()

    # ---- 1. table validity
    print("1) TABLE VALIDITY")
    vals = load_table(d)
    # ascending run from the first non-zero
    start = next((i for i, v in enumerate(vals) if v > 0), None)
    run = []
    prev = -1
    for i in range(start or 0, len(vals)):
        v = vals[i]
        if v == 0 and run:
            break
        if v >= prev:
            run.append(v); prev = v
        else:
            break
    print("   table begins at u16[%s], %d ascending entries" % (start, len(run)))
    if run:
        print("   first 8: %s" % run[:8])
        print("   last 4 : %s   (file is %d bytes)" % (run[-4:], len(d)))
        diffs = [run[i + 1] - run[i] for i in range(len(run) - 1)]
        print("   entry lengths: min %d  max %d  mean %.1f" % (min(diffs), max(diffs), sum(diffs) / len(diffs)))
        print("   table size %d bytes; strings would begin at %d" % (len(run) * 2, run[0]))
        tail_span = run[-1] - run[0]
        print("   offset span %d; bytes available after first string %d" % (tail_span, len(d) - run[0]))
    print()

    body = d[run[0]:] if run else d
    print("   BODY: %d bytes, entropy %.3f, ioc %.5f" % (len(body), entropy(body), ioc(body)))
    print("   (English text ioc ~0.066; uniform random 256-symbol ~0.0039)")
    print()

    # ---- 2. substitution?
    print("2) SUBSTITUTION CIPHER?")
    print("   ioc of body = %.5f  -> %s" % (ioc(body),
          "LOW: not a simple substitution of English" if ioc(body) < 0.02 else "high: could be"))
    print()

    # ---- 3. bit-packed?
    print("3) BIT PACKING")
    sample = body[:120000]

    def unpack(w, order):
        out = bytearray()
        acc = nb = 0
        if order == "lsb":
            for byte in sample:
                acc |= byte << nb
                nb += 8
                while nb >= w:
                    out.append(acc & ((1 << w) - 1))
                    acc >>= w
                    nb -= w
        else:
            for byte in sample:
                acc = (acc << 8) | byte
                nb += 8
                while nb >= w:
                    out.append((acc >> (nb - w)) & ((1 << w) - 1))
                    nb -= w
        return bytes(out)

    for w in (6, 7, 8):
        for order in ("lsb", "msb"):
            v = unpack(w, order)
            pr = sum(1 for x in v if 32 <= x < 127) / max(1, len(v))
            lt = sum(1 for x in v if 65 <= x <= 90 or 97 <= x <= 122) / max(1, len(v))
            print("   %d-bit %-3s  n=%-7d printable %.1f%%  letters %.1f%%  ioc %.5f"
                  % (w, order, len(v), pr * 100, lt * 100, ioc(v)))
    print()

    # ---- 4. glyph indices?
    print("4) GLYPH INDEX STREAM?")
    c = collections.Counter(body)
    print("   distinct byte values: %d of 256" % len(c))
    print("   most common 16: %s" % [(hex(v), k) for v, k in c.most_common(16)])
    # if glyph indices, the count of distinct symbols used should be well under 256
    used_above_0_1pct = sum(1 for v, k in c.items() if k / len(body) > 0.001)
    print("   values above 0.1%% frequency: %d" % used_above_0_1pct)
    print()

    # ---- 5. language copies
    if other and os.path.exists(other):
        o = open(other, "rb").read()
        ov = load_table(o)
        ostart = next((i for i, v in enumerate(ov) if v > 0), None)
        orun = []
        prev = -1
        for i in range(ostart or 0, len(ov)):
            v = ov[i]
            if v == 0 and orun:
                break
            if v >= prev:
                orun.append(v); prev = v
            else:
                break
        print("5) OTHER COPY %s : %d bytes, %d table entries, first string at %s"
              % (os.path.basename(other), len(o), len(orun), orun[0] if orun else "?"))


if __name__ == "__main__":
    main()

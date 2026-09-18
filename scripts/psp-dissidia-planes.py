#!/usr/bin/env python3
"""psp-dissidia-planes.py -- split an encoded body into its two interleaved byte planes and inspect.

THE CLUE THAT MOTIVATED THIS
    In accessory_help.bin, entry 0 and entry 1 (bytes 6091 and 6154) compared position by position
    share MOST EVEN-index bytes and NO ODD-index bytes:

        e0: 32 fa 2d 1f a6 ef c8 13 00 f3 57 54 f2 46 4b c6 ...
        e1: 32 bf 2d 77 a6 9f c8 23 00 9b 56 a6 0d 7d 4b 89 ...
             ^     ^     ^     ^     ^           ^     ^

    14 of 16 even positions match. Independently, the two planes measured very differently:
        even plane: 29,296 bytes, entropy 4.254, printable 42.5%, zeros 8.08%
        odd  plane: 29,295 bytes, entropy 7.667, printable 35.2%, zeros 0.57%
    A low-entropy plane next to a high-entropy plane is the signature of a split stream, so this
    script separates them and looks for text, NUL terminators, and cross-plane relationships.
"""
import collections, math, os, re, struct, sys


def entropy(b):
    c = collections.Counter(b); n = max(1, len(b))
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def show(label, b, n=160):
    print("--- %s (%d bytes, entropy %.3f) ---" % (label, len(b), entropy(b)))
    print("   repr : %r" % b[:n])
    txt = "".join(chr(c) if 32 <= c < 127 else "." for c in b[:n])
    print("   ascii: %s" % txt)
    runs = re.findall(rb"[\x20-\x7e]{4,}", b[:20000])
    print("   runs>=4 in first 20k: %d  %s" % (len(runs), [r.decode('latin1')[:26] for r in runs[:8]]))
    print()


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()

    # locate the string body via the leading u16 offset table
    first = None
    for i in range(0, min(len(d) - 1, 40000), 2):
        v = struct.unpack_from("<H", d, i)[0]
        if v > 0:
            first = v; break
    body = d[first:]
    print("file %s: %d bytes; body from %d (%d bytes)\n" % (os.path.basename(path), len(d), first, len(body)))

    even = body[0::2]
    odd = body[1::2]
    show("EVEN plane", even)
    show("ODD plane", odd)

    # ---- is the ODD plane a key and the EVEN plane text?  Test even XOR previous-odd, etc.
    print("=== cross-plane combinations, scored on printable rate ===")
    n = min(len(even), len(odd))
    cands = {
        "even ^ odd":            bytes(even[i] ^ odd[i] for i in range(n)),
        "even + odd":            bytes((even[i] + odd[i]) & 0xFF for i in range(n)),
        "even - odd":            bytes((even[i] - odd[i]) & 0xFF for i in range(n)),
        "even ^ odd_shift1":     bytes(even[i] ^ odd[i - 1] for i in range(1, n)),
        "odd only":              odd,
        "even only":             even,
    }
    for label, b in cands.items():
        pr = sum(1 for x in b if 32 <= x < 127) / max(1, len(b))
        z = b.count(0) / max(1, len(b))
        print("   %-20s printable %5.1f%%  zeros %5.2f%%  entropy %.3f" % (label, pr * 100, z * 100, entropy(b)))
    print()

    # ---- if the EVEN plane is text, NUL should terminate strings.  Look for it.
    print("=== EVEN plane structure ===")
    print("   zeros in even plane: %d (%.2f%%)" % (even.count(0), 100.0 * even.count(0) / len(even)))
    # positions of zeros -> gap distribution, which reveals record boundaries
    zpos = [i for i, x in enumerate(even) if x == 0][:400]
    if len(zpos) > 2:
        gaps = [zpos[i + 1] - zpos[i] for i in range(len(zpos) - 1)]
        c = collections.Counter(gaps)
        print("   gap between zeros, top 10: %s" % c.most_common(10))
    print()

    # ---- maybe the even plane is text XORed with a constant per entry.  Try: does the most common
    #      even byte minus 32 give a letter?
    print("=== EVEN plane byte histogram (top 20) and 'is this text shifted/XORed' check ===")
    c = collections.Counter(even)
    print("   top 20: %s" % [(hex(v), k) for v, k in c.most_common(20)])
    for k in range(256):
        x = bytes(b ^ k for b in even[:30000])
        pr = sum(1 for v in x if 32 <= v < 127)
        if pr / len(x) > 0.85:
            print("   XOR key 0x%02X lifts even plane to %.1f%% printable!" % (k, 100 * pr / len(x)))
    best = max(range(256), key=lambda k: sum(1 for v in bytes(b ^ k for b in even[:30000]) if 32 <= v < 127))
    x = bytes(b ^ best for b in even[:30000])
    print("   best single-byte XOR on even plane: key 0x%02X -> %.1f%% printable"
          % (best, 100 * sum(1 for v in x if 32 <= v < 127) / len(x)))
    print("   sample: %r" % x[:100])


if __name__ == "__main__":
    main()

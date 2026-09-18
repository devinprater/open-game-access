#!/usr/bin/env python3
"""psp-dissidia-cipher.py -- test the specific encodings that fit a 32-byte periodic, structured
high-entropy body.

MOTIVATION (measured on accessory_help.bin)
    body = bytes from the first offset-table target to EOF (58,591 bytes, 90.6% of the file)
    entropy 6.730 b/byte, all 256 byte values present, all 8 bit planes near 0.5
    ... BUT the byte histogram is NOT uniform: 0x00 appears 4.33% (11x the 0.39% a cipher would give)
    and 0x84 appears 5.80%. So there IS structure -- it is not a clean stream cipher.
    Autocorrelation shows period 32 (0.342) and 64 (0.236), nothing else.

    So the working hypotheses are:
      H1  position-mod-32 keyed transform (repeating 32-byte key)
      H2  two interleaved byte planes (a u16 stream split into even/odd bytes, so one plane is the
          high byte of each character and would be near-constant for ASCII text)
      H3  a NUL-terminated u16 string table where the body is really strings, just with one byte
          transformed
      H4  delta/cumulative encoding

    Each test prints a single decisive number so the answer is not a judgement call.
"""
import collections, math, os, struct, sys


def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b); n = len(b)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def printable_rate(b):
    return sum(1 for x in b if 32 <= x < 127) / max(1, len(b))


def zeros_rate(b):
    return b.count(0) / max(1, len(b))


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    # locate body
    first = None
    for i in range(0, min(len(d) - 1, 40000), 2):
        v = struct.unpack_from("<H", d, i)[0]
        if v > 0:
            first = v; break
    body = d[first:]
    print("file %s: %d bytes; body from %d, %d bytes" % (os.path.basename(path), len(d), first, len(body)))
    print("body entropy %.3f  printable %.3f  zeros %.4f" % (entropy(body), printable_rate(body), zeros_rate(body)))
    print()

    # ---- H2: interleaved planes.  Split even/odd bytes and measure each.
    even = body[0::2]
    odd = body[1::2]
    print("H2 interleaved byte planes")
    print("   even-index plane: %6d bytes  entropy %.3f  printable %.3f  zeros %.4f"
          % (len(even), entropy(even), printable_rate(even), zeros_rate(even)))
    print("   odd -index plane: %6d bytes  entropy %.3f  printable %.3f  zeros %.4f"
          % (len(odd), entropy(odd), printable_rate(odd), zeros_rate(odd)))
    if zeros_rate(odd) > 0.8 or zeros_rate(even) > 0.8:
        print("   *** one plane is almost all zeros -> a split u16 stream! DECODABLE ***")
    print()

    # ---- H1: position-mod-32 distribution.  If a repeating key is XORed, the byte distribution at
    #          each residue should be a SHIFTED copy of the plaintext distribution -- i.e. still
    #          low-entropy.  Measure the mean per-residue entropy.
    print("H1 repeating 32-byte key")
    ents = []
    for r in range(32):
        chunk = body[r::32]
        ents.append(entropy(chunk))
    print("   mean per-residue entropy (32 residues): %.3f  min %.3f  max %.3f"
          % (sum(ents) / len(ents), min(ents), max(ents)))
    print("   (for a repeating-key XOR every residue keeps the plaintext's low entropy ~3-4 for text)")
    ents8 = []
    for r in range(8):
        ents8.append(entropy(body[r::8]))
    print("   mean per-residue entropy (8 residues):  %.3f" % (sum(ents8) / len(ents8)))
    print()

    # ---- H4: delta decoding.  Reconstruct by cumulative sum mod 256 and re-measure.
    print("H4 delta / cumulative decode")
    acc = 0
    rec = bytearray()
    for x in body[:20000]:
        acc = (acc + x) & 0xFF
        rec.append(acc)
    print("   cumulative-sum: entropy %.3f  printable %.3f  zeros %.4f"
          % (entropy(bytes(rec)), printable_rate(bytes(rec)), zeros_rate(bytes(rec))))
    drec = bytes(((body[i] - body[i - 1]) & 0xFF) for i in range(1, 20001))
    print("   successive-diff: entropy %.3f  printable %.3f"
          % (entropy(drec), printable_rate(drec)))
    print()

    # ---- H3: does the body contain NUL-separated u16 strings at all?
    #          Count 2-byte units whose HIGH byte is 0 (ASCII UTF-16) -- if it is a real string
    #          table there should be many; if it is encrypted, ~1/256.
    print("H3 u16 string table?")
    units = [struct.unpack_from("<H", body, i)[0] for i in range(0, len(body) - 1, 2)]
    hi_zero = sum(1 for v in units if (v >> 8) == 0)
    print("   %d u16 units, %d have a zero HIGH byte (%.2f%%)"
          % (len(units), hi_zero, 100.0 * hi_zero / len(units)))
    print("   random would give ~0.39%%, a real ASCII u16 table gives ~90%%+")
    # same for big-endian
    hib = sum(1 for v in units if (v & 0xFF) == 0)
    print("   %d have a zero LOW byte (%.2f%%)  (i.e. big-endian ASCII)" % (hib, 100.0 * hib / len(units)))


if __name__ == "__main__":
    main()

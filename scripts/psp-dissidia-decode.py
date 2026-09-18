#!/usr/bin/env python3
"""psp-dissidia-decode.py -- work out how Dissidia encodes its menu/help string tables.

THE PROBLEM
    accessory_help.bin / item_help.bin / pause_help.bin / name.bin all begin with what looks like a
    u16 OFFSET TABLE (ascending values pointing into the file), but the string data after the table
    is NOT plaintext: entropy ~6.7 bits/byte, no single-byte XOR key, no ECB repetition, and a 32-byte
    periodic component (autocorrelation 0.342 at period 32).

    An earlier entropy figure was measured over the WHOLE file including the offset table, which
    inflates it. This script measures the string BODY only, and then tests the encodings that fit a
    high-entropy-but-not-random profile in order:

      1. body entropy + byte histogram
      2. bit-plane analysis (a packed fixed-width code usually leaves a constant or low-entropy plane)
      3. 5-, 6- and 7-bit unpacking, scored by how much printable ASCII and letter content results
      4. whether the two copies of accessory_help.bin in the payload agree (a shared keystream would
         make equal plaintext at equal offsets produce equal ciphertext)

USAGE
    python scripts/psp-dissidia-decode.py FILE [--table-entries N]
"""
import argparse, collections, math, os, struct, sys


def entropy(b):
    if not b:
        return 0.0
    c = collections.Counter(b)
    n = len(b)
    return -sum((v / n) * math.log2(v / n) for v in c.values())


def offset_table(d, max_entries=20000):
    """Read the leading u16 offset table. Entry 0's value is where the strings begin."""
    vals = []
    for i in range(0, min(len(d) - 1, max_entries * 2), 2):
        vals.append(struct.unpack_from("<H", d, i)[0])
    first = next((v for v in vals if v > 0), None)
    return vals, first


def bitplanes(b):
    out = []
    for bit in range(8):
        ones = sum((x >> bit) & 1 for x in b)
        out.append(ones / len(b))
    return out


def score_text(t):
    if not t:
        return 0.0, 0.0
    pr = sum(1 for c in t if 32 <= ord(c) < 127) / len(t)
    lt = sum(1 for c in t if c.isalpha()) / len(t)
    sp = sum(1 for c in t if c == " ") / len(t)
    return pr, lt + sp


def unpack_width(b, w):
    """Unpack a bitstream into w-bit codes (LSB-first and MSB-first)."""
    outs = {}
    for name, order in (("lsb", "lsb"), ("msb", "msb")):
        vals = []
        acc = 0
        nb = 0
        if order == "lsb":
            for byte in b:
                acc |= byte << nb
                nb += 8
                while nb >= w:
                    vals.append(acc & ((1 << w) - 1))
                    acc >>= w
                    nb -= w
        else:
            for byte in b:
                acc = (acc << 8) | byte
                nb += 8
                while nb >= w:
                    vals.append((acc >> (nb - w)) & ((1 << w) - 1))
                    nb -= w
        base = 32 if w <= 6 else 0
        s = "".join(chr(base + v) if base <= v < base + 96 else "." for v in vals)
        outs[name] = (len(vals), s)
    return outs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--other", help="a second file to compare (same-named copy)")
    a = ap.parse_args()

    d = open(a.path, "rb").read()
    print("=== %s : %d bytes ===" % (os.path.basename(a.path), len(d)))
    print("whole-file entropy: %.3f" % entropy(d))

    vals, first = offset_table(d)
    print("leading u16 offset table: first non-zero value = %s" % first)
    if not first or first >= len(d):
        sys.exit("no usable offset table")

    body = d[first:]
    print("string body: bytes %d..%d (%d bytes, %.1f%% of file)"
          % (first, len(d), len(body), 100.0 * len(body) / len(d)))
    print("BODY entropy: %.3f bits/byte" % entropy(body))
    print()

    print("=== byte histogram (top 12) ===")
    c = collections.Counter(body)
    n = len(body)
    for v, k in c.most_common(12):
        print("   0x%02X  %6d  %5.2f%%" % (v, k, 100.0 * k / n))
    print("distinct byte values in body: %d of 256" % len(c))
    print()

    print("=== bit planes (fraction of 1s); a packed code often leaves a skewed plane ===")
    for i, f in enumerate(bitplanes(body)):
        bar = "#" * int(f * 40)
        print("   bit %d  %.3f  %s" % (i, f, bar))
    print()

    print("=== fixed-width unpacking (scored on printable + letters/space) ===")
    # use a slice so unpacking is fast, but keep enough for a fair rate
    sample = body[:60000]
    for w in (5, 6, 7):
        for name, (cnt, s) in unpack_width(sample, w).items():
            pr, lt = score_text(s)
            base = 32 if w <= 6 else 0
            print("   %d-bit %-3s  codes=%-7d printable=%.1f%%  letters+space=%.1f%%   %r"
                  % (w, name, cnt, pr * 100, lt * 100, s[:40]))
    print()

    print("=== keystream-restart hypothesis ===")
    keystream_test(d, first, os.path.basename(a.path))
    print()

    if a.other and os.path.exists(a.other):
        o = open(a.other, "rb").read()
        ov, ofirst = offset_table(o)
        print("=== comparing with %s (%d bytes, body from %s) ===" %
              (os.path.basename(a.other), len(o), ofirst))
        if ofirst:
            m = min(len(d) - first, len(o) - ofirst)
            same = sum(1 for i in range(m) if d[first + i] == o[ofirst + i])
            print("   identical bytes over %d compared: %d (%.1f%%)"
                  % (m, same, 100.0 * same / max(1, m)))
            print("   -> a shared keystream from offset 0 would give ~100%%;")
            print("      different content at the same offsets gives the observed low rate.")


def keystream_test(d, first, label=""):
    """Test whether the cipher's keystream RESTARTS at each entry.

    LOGIC. The leading u16 table gives entry boundaries. If each entry is XORed with a keystream that
    restarts at offset 0 of that entry, then for any two entries i,j:
        enc_i[k] XOR enc_j[k] == plain_i[k] XOR plain_j[k]
    and because both plaintexts are 7-bit ASCII, every such XOR must have its high bit CLEAR.
    A single 8-bit value >= 0x80 proves the keystream does NOT restart per entry. If none appear,
    the hypothesis holds and the text can be recovered entry-by-entry.
    """
    vals = []
    for i in range(0, min(len(d) - 1, 20000 * 2), 2):
        vals.append(struct.unpack_from("<H", d, i)[0])
    # entry starts = the ascending non-zero run, then the strings begin at vals[0]
    starts = []
    prev = None
    for i, v in enumerate(vals):
        if v and (prev is None or v > prev) and v >= first:
            if v not in starts and len(starts) < 4000:
                starts.append(v)
            prev = v
        elif v and prev is not None and v < prev:
            break
    if len(starts) < 3:
        print("   (%s: only %d entry starts -- cannot test)" % (label, len(starts)))
        return None
    entries = [d[s2:s2 + (starts[i + 1] - s2 if i + 1 < len(starts) else len(d) - s2)]
               for i, s2 in enumerate(starts)]

    print("   %s: %d entries, first lens %s" % (label, len(entries), [len(e) for e in entries[:8]]))
    bad = total = 0
    for i in range(min(len(entries), 40)):
        for j in range(i + 1, min(len(entries), 40)):
            ei, ej = entries[i], entries[j]
            m = min(len(ei), len(ej), 16)
            for k in range(m):
                x = ei[k] ^ ej[k]
                total += 1
                if x >= 0x80:
                    bad += 1
    if total == 0:
        return None
    print("   position-wise XOR of entry pairs: %d of %d have the HIGH BIT SET (%.1f%%)"
          % (bad, total, 100.0 * bad / total))
    print("   -> %s" % ("keystream does NOT restart per entry" if bad else
                        "keystream RESTARTS per entry -- decodable entry-by-entry"))
    return bad == 0


if __name__ == "__main__":
    main()

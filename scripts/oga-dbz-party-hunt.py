#!/usr/bin/env python3
"""Hunt the ACTIVE PARTY in a DBZ: Attack of the Saiyans RAM dump.

WHY THIS IS A SEPARATE SCRIPT
-----------------------------
The character array is confirmed (base 0x020CD754, stride 0x24C, 8 records, names
at +0x20, a next-index chain at +0x180). But that table is the CHARACTER
DEFINITIONS, not the party — it includes Bubbles and Gregory, who are never
playable, and it is populated from frame 0.

The party is a different, smaller structure: at most ~4 entries, holding only
currently-fieldable characters, changing as the player recruits. This script
looks for it with signals that are checkable rather than plausible, because
"find a plausible integer" already produced 348 useless hits once.

Signals used (in order of strength):
  1. POINTERS into the character records. A party is very often an array of
     pointers to member records. A random u32 equalling a specific RAM address is
     vanishingly unlikely, so any hit is real evidence.
  2. A SHORT ID ARRAY. A compact run of small values that are all valid character
     indices (1..8), embedded in something party-sized, with NULs around it.
  3. The KNOWN-CHAIN value. The +0x180 chain is [2,3,4,5,6,7,8,0]; a party list
     may hold the same kind of small IDs but shorter.

Every candidate is printed with its surrounding bytes so the reasoning can be
checked by eye. Nothing here calls a result "the party".
"""
import struct
import sys

DUMP = sys.argv[1] if len(sys.argv) > 1 else \
    r"C:\Users\Devin Prater\AppData\Local\Temp\dbz\dbz-ram.bin"

raw = open(DUMP, "rb").read()
nl = raw.find(b"\n")
header = raw[:nl].decode("ascii", "replace")
base = 0
for tok in header.split():
    if tok.startswith("base="):
        base = int(tok.split("=", 1)[1], 0)
d = raw[nl + 1:]

REC_BASE = 0x020CD754
STRIDE = 0x24C
NCHARS = 8
NAME_BASE = REC_BASE + 0x20          # 0x020CD774
REC_ADDRS = [REC_BASE + i * STRIDE for i in range(NCHARS)]
NAME_ADDRS = [NAME_BASE + i * STRIDE for i in range(NCHARS)]

print(f"dump base 0x{base:08X}, {len(d):,} bytes")
print(f"character records: 0x{REC_BASE:08X} .. 0x{REC_ADDRS[-1] + STRIDE:08X}")
print(f"name addresses   : 0x{NAME_ADDRS[0]:08X} .. 0x{NAME_ADDRS[-1]:08X}")
print()


def u32(a):
    return struct.unpack_from("<I", d, a)[0]


def show(a, n=48):
    a = max(0, a - 8)
    seg = d[a:a + n]
    hexs = " ".join(f"{x:02X}" for x in seg)
    asc = "".join(chr(x) if 32 <= x < 127 else "." for x in seg)
    return f"0x{base+a:08X}  {hexs}  |{asc}|"


# ---- 1. pointers into the character records --------------------------------
print("=== 1. u32 words pointing INTO a character record ===")
hits = 0
for a in range(0, len(d) - 4, 4):
    v = u32(a)
    if REC_BASE <= v < REC_BASE + NCHARS * STRIDE:
        rec = (v - REC_BASE) // STRIDE
        off = (v - REC_BASE) % STRIDE
        print(f"  {show(a)}")
        print(f"      -> 0x{v:08X} = rec{rec} +0x{off:X}"
              f"{'  (name field!)' if off == 0x20 else ''}")
        hits += 1
        if hits >= 25:
            print("  ... (truncated)")
            break
print(f"  -- {hits} pointer(s) into the character array")

# ---- 2. short arrays of valid character IDs --------------------------------
print("\n=== 2. short runs of valid character IDs (1..8) ===")
found = 0
a = 0
while a < len(d) - 8:
    b = d[a]
    if 1 <= b <= 8:
        run = []
        j = a
        step = 1
        while j < len(d) and len(run) < 8 and 1 <= d[j] <= 8:
            run.append(d[j])
            j += step
        if 2 <= len(run) <= 5:
            # party-sized; require zero bytes bracketing it so it is a field
            pre = d[a - 1] if a > 0 else 0
            post = d[j] if j < len(d) else 0
            if pre == 0 and post == 0:
                found += 1
                if found <= 20:
                    print(f"  {show(a)}")
                    print(f"      ids={run}")
            a = j
            continue
    a += 1
print(f"  -- {found} candidate id-run(s)")

# ---- 3. the names in RAM are the anchor; print where each lives -------------
print("\n=== 3. where each character name lives ===")
for i, na in enumerate(NAME_ADDRS):
    # ⛔ OFF = RAM - BASE, ONCE. Subtracting the base a second time (`na - base`
    # then `- base` again inside the slice) makes every lookup land far past the
    # string and return '' — which reads as "the names vanished" rather than an
    # arithmetic bug. The dump is indexed by (address - base).
    off = na - base
    e = d.find(b"\x00", off)
    nm = d[off:e].decode("ascii", "replace")
    print(f"  rec{i}  0x{na:08X}  {nm!r}")

print("\n=== reading this ===")
print("  Any pointer hit in (1) is strong evidence of an index/party list, because")
print("  a random word matching a specific RAM address is very unlikely. A run in")
print("  (2) is only a candidate: confirm it by changing party members in-game.")

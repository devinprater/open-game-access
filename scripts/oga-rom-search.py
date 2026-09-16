#!/usr/bin/env python3
"""
Go to the ROM instead of scanning RAM.

WHY THIS EXISTS
---------------
Three RAM-scanning attempts failed to find the ACTIVE PARTY in DBZ: Attack of the
Saiyans (BRPE):

  1. "plausible integer that differs across stride-spaced records" -> 348 hits,
     all graphics noise;
  2. "the party is a pointer list to the roster"                   -> 0 pointers;
  3. "the duplicate 290 is a live copy"                            -> it was ARM
     overlay CODE, not data.

The roster itself WAS found by scanning for strings (8 records, stride 0x24C,
real base 0x020CD754 vs the code list's 0x020CD300). But the party is elsewhere.

So stop guessing at RAM and read the CODE that indexes these structures. On ARM,
an address reaches a function as a LITERAL POOL constant loaded with LDR. If any
code touches the roster, the address appears as a little-endian 32-bit literal in
arm9.bin or in one of the overlays.

That is a precise, falsifiable search: a literal matching a specific RAM address
is not something you get by accident. Finding it gives a CODE ADDRESS, and the
code around it says what the structure is and how big it is.
"""
import struct, sys, os

ROM = r"C:\Users\Devin Prater\Dropbox\Games\NDS\Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds"

# The addresses we have measured evidence for.
ROSTER_BASE = 0x020CD754   # Goku's record (name addr 0x020CD774 - 0x20)
NAME0       = 0x020CD774   # "Goku"
AR_BASE     = 0x020CD300   # what the published code list claims

def le32(b, off):
    return struct.unpack_from("<I", b, off)[0]

def main():
    data = open(ROM, "rb").read()
    print(f"ROM: {len(data):,} bytes")

    arm9_off = le32(data, 0x020)
    arm9_ram = le32(data, 0x028)
    arm9_sz  = le32(data, 0x02C)
    fat_off  = le32(data, 0x048)
    fat_sz   = le32(data, 0x04C)
    # ⛔ THE OVERLAY TABLE IS AT 0x50/0x54, NOT 0x58/0x5A. Reading the wrong
    # offset yields size 0 and "no overlays", which reads as "the ROM has none"
    # rather than "the header field was wrong" — a silent, plausible-looking zero.
    ovl_off  = le32(data, 0x050)
    ovl_sz   = le32(data, 0x054)

    print(f"  arm9   : rom 0x{arm9_off:08X}  ram 0x{arm9_ram:08X}  size {arm9_sz:,}")
    print(f"  FAT    : rom 0x{fat_off:08X}  size {fat_sz:,}")
    print(f"  overlay: rom 0x{ovl_off:08X}  size {ovl_sz:,}")

    fat = data[fat_off:fat_off + fat_sz]
    nfiles = len(fat) // 8
    print(f"  FAT entries: {nfiles}")

    def file_bytes(fid):
        if fid >= nfiles:
            return None
        o = le32(fat, fid * 8)
        s = le32(fat, fid * 8 + 4)
        return data[o:o + s] if s else b""

    targets = {
        "roster base 0x020CD754": ROSTER_BASE,
        "Goku name   0x020CD774": NAME0,
        "AR base     0x020CD300": AR_BASE,
    }

    # ---- search arm9.bin -------------------------------------------------
    arm9 = data[arm9_off:arm9_off + arm9_sz]
    print(f"\n=== arm9.bin ({len(arm9):,} bytes) ===")
    for label, val in targets.items():
        pat = struct.pack("<I", val)
        hits = []
        i = arm9.find(pat)
        while i != -1 and len(hits) < 12:
            hits.append(i)
            i = arm9.find(pat, i + 1)
        if hits:
            for h in hits:
                print(f"  {label}: arm9+0x{h:X}  (ram 0x{arm9_ram + h:08X})")
        else:
            print(f"  {label}: not present")

    # ---- search for ANY literal pointing into the character region --------
    #
    # ⛔ LOOK FOR A POINTER, NOT ONLY AN EXACT ADDRESS. A compiled index into an
    # array of records is usually `base + n * stride`, so the literal in the code
    # may be the ARRAY BASE rather than any single name. Scanning for every exact
    # known address can therefore miss the one literal that matters.
    #
    # Any word in 0x020C0000..0x020D0000 is a pointer into the character region.
    # Random 4-byte words in that narrow a range are rare, so hits are meaningful —
    # and each one tells us a code site that references character data.
    print("\n=== any literal pointing into the character region 0x020C0000..0x020D0000 ===")
    lo, hi = 0x020C0000, 0x020D0000
    for name, buf, base_ram in (("arm9", arm9, arm9_ram),):
        hits = []
        for off in range(0, len(buf) - 4, 4):
            v = le32(buf, off)
            if lo <= v < hi:
                hits.append((off, v))
        print(f"  {name}: {len(hits)} word(s)")
        for off, v in hits[:20]:
            print(f"    {name}+0x{off:X} (ram 0x{base_ram + off:08X}) -> 0x{v:08X}")

    # ---- search every overlay -------------------------------------------
    n_ovl = ovl_sz // 32
    print(f"\n=== {n_ovl} overlays ===")
    found_any = False
    for i in range(n_ovl):
        e = ovl_off + i * 32
        oid   = le32(data, e + 0x00)
        ram   = le32(data, e + 0x04)
        rsz   = le32(data, e + 0x08)
        fid   = le32(data, e + 0x18)
        flags = le32(data, e + 0x1C)
        comp  = flags & 1
        fb = file_bytes(fid)
        if fb is None:
            continue
        note = ""
        if comp:
            # compressed overlays: mark and skip decoding for now
            note = " [COMPRESSED - skipped]"
        for label, val in targets.items():
            pat = struct.pack("<I", val)
            j = fb.find(pat)
            while j != -1:
                if not comp:
                    print(f"  ovl {oid:3d} ram 0x{ram:08X} {label}: "
                          f"+0x{j:X} -> ram 0x{ram + j:08X}")
                found_any = True
                j = fb.find(pat, j + 1)
                if comp:
                    break
        if comp and fb.find(struct.pack("<I", ROSTER_BASE)) != -1:
            print(f"  ovl {oid:3d} ram 0x{ram:08X} CONTAINS ROSTER BASE{note}")

    if not found_any:
        print("  no uncompressed overlay references the roster addresses")

    print("\n=== reading this ===")
    print("  A hit is a CODE SITE that loads this address, i.e. code that")
    print("  indexes the structure. Disassemble around it (Ghidra) to learn")
    print("  the real party layout instead of guessing at RAM.")

if __name__ == "__main__":
    main()

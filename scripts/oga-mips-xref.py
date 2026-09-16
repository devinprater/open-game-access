#!/usr/bin/env python3
"""
oga-mips-xref.py — find code and data references to an address in a MIPS ELF.

WHY: the question is "which code walks the script?" A decrypted PSP EBOOT is a MIPS
ELF whose strings and tables live at known virtual addresses, so an address reference
is a way in. Searching the file for the 4-byte address finds DATA tables; searching for
the MIPS load idioms finds the CODE that uses them.

⛔ THE OFFSET MATTERS. In this EBOOT the first LOAD segment maps file offset 0xa0 to
virtual address 0. So `vaddr = file_offset - 0xa0`, NOT `vaddr = file_offset`. Getting
this wrong points you at an unrelated string that merely looks plausible.

Two MIPS idioms build a 32-bit constant:
  lui  rt, HI                 0x3C00_0000 | rt<<16 | HI
  addiu rt, rt, LO            0x2400_0000 | rs<<16 | rt<<8 | LO     (signed LO)
  ori  rt, rt, LO             0x3400_0000 | rs<<16 | rt<<8 | LO
and loads use them as a base:
  lw   rt, OFF(rs)            0x8C00_0000 | rs<<16 | rt<<8 | OFF

So: find every `lui` whose HI matches, then look at the next few instructions for a
matching LO. That is the standard way to recover a constant address in MIPS.

Usage:
  oga-mips-xref.py EBOOT.dec --addr 0x121DBC [--text-off 0xa0] [--window 6]
"""
import argparse
import struct
import sys

TEXT_OFF_DEFAULT = 0xA0          # file offset of the first LOAD segment
TEXT_VA_DEFAULT = 0x0


def sign16(v):
    return v - 0x10000 if v & 0x8000 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--addr", required=True, help="target virtual address, e.g. 0x121DBC")
    ap.add_argument("--text-off", default=hex(TEXT_OFF_DEFAULT))
    ap.add_argument("--text-va", default=hex(TEXT_VA_DEFAULT))
    ap.add_argument("--window", type=int, default=6)
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    target = int(args.addr, 0)
    off0 = int(args.text_off, 0)
    va0 = int(args.text_va, 0)
    hi = (target >> 16) & 0xFFFF
    lo = target & 0xFFFF
    lo_signed = sign16(lo)

    d = open(args.elf, "rb").read()
    print(f"file      : {args.elf}  ({len(d)} bytes)")
    print(f"target    : 0x{target:08X}   hi=0x{hi:04X} lo=0x{lo:04X} (signed {lo_signed})")
    print(f"text map  : file 0x{off0:X} -> vaddr 0x{va0:X}")
    print()

    # ---- 1. literal 4-byte pointers in DATA ---------------------------------
    needle = struct.pack("<I", target)
    data_hits = []
    i = d.find(needle)
    while i >= 0 and len(data_hits) < args.limit:
        data_hits.append(i)
        i = d.find(needle, i + 1)
    print(f"=== 4-byte literals equal to 0x{target:08X}: {len(data_hits)} ===")
    for off in data_hits:
        print(f"  file 0x{off:08X}  -> vaddr 0x{va0 + (off - off0):08X}")
    if not data_hits:
        print("  none — the address is built in CODE, not stored as a literal.")
    print()

    # ---- 2. code that BUILDS the address ------------------------------------
    hits = []
    for off in range(off0, len(d) - 4 * (args.window + 1), 4):
        w = struct.unpack_from("<I", d, off)[0]
        if (w & 0xFFFF0000) != (0x3C000000 | (hi << 16)):
            continue                       # not a lui with our HI
        rt = (w >> 16) & 0x1F
        # look ahead for addiu/ori/lw on the same register
        for k in range(1, args.window + 1):
            w2 = struct.unpack_from("<I", d, off + 4 * k)[0]
            op = w2 >> 26
            rs = (w2 >> 21) & 0x1F
            rt2 = (w2 >> 16) & 0x1F
            imm = w2 & 0xFFFF
            if rs != rt:
                continue
            if op == 0x09 and sign16(imm) == lo_signed:      # addiu
                hits.append((off, k, rt, "addiu", imm, w2))
                break
            if op == 0x0D and imm == lo:                     # ori
                hits.append((off, k, rt, "ori", imm, w2))
                break
            # a load using the register as base with our LO as displacement
            if op in (0x23, 0x21, 0x25, 0x29) and imm == lo:  # lw/lh/lhu/lb
                hits.append((off, k, rt, "load", imm, w2))
                break

    print(f"=== lui/addiu|ori|load pairs building 0x{target:08X}: {len(hits)} ===")
    for off, k, rt, kind, imm, w2 in hits[: args.limit]:
        va = va0 + (off - off0)
        print(f"  vaddr 0x{va:08X}  (file 0x{off:08X})  $r{rt} + {kind} imm=0x{imm:04X}"
              f"  [+{k} instr, 0x{w2:08X}]")
    if not hits:
        print("  none — maybe the code computes it from a different base, or the")
        print("  address is reached through a pointer rather than built directly.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

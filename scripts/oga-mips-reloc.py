#!/usr/bin/env python3
"""
oga-mips-reloc.py — read a PSP EBOOT's relocation tables and invert them.

⛔ THE PROBLEM THIS SOLVES. A decrypted PSP EBOOT is position-independent. Nothing in
the file contains a literal address like `0x00121DBC`, so searching for the 4 bytes of
an address finds NOTHING — every address is built as `lui`/`addiu` against a 16-bit
HIGH/LOW relocation entry, patched at load time.

Verified on Steins;Gate: My Darling's Embrace — a search for the SYSTEM.CFG string
address found 0 literals and 0 lui/addiu pairs, while the same file has 8 relocation
sections (type `LOPROC+0xa0`, entsize 8).

Each entry is 8 bytes:
    u32  r_offset  (where to patch)
    u32  r_info    (symbol << 8 | type)

PSP relocation types:
    0  R_MIPS_NONE
    2  R_MIPS_32      full 32-bit address written at the site
    4  R_MIPS_26      jump target  (26-bit field, << 2)
    5  R_MIPS_HI16    the high half
    6  R_MIPS_LO16    the low half

⭐ THE KEY TRICK: to find code that references an address, do NOT search for the
address. Search for the site that was relocated — a HI16/LO16 pair, or an R_MIPS_32
word — and then read what is there. `--at` reports the relocations covering a given
offset, and `--list` dumps the table.

Usage:
  oga-mips-reloc.py EBOOT.dec --list [--limit 40] [--type 2]
  oga-mips-reloc.py EBOOT.dec --at 0x121DBC
  oga-mips-reloc.py EBOOT.dec --stats
"""
import argparse
import struct
import sys

TYPES = {0: "NONE", 2: "R_MIPS_32", 4: "R_MIPS_26", 5: "R_MIPS_HI16", 6: "R_MIPS_LO16"}


def parse_elf_sections(d):
    """Minimal ELF32 section reader — we only need offsets/sizes/types."""
    if d[:4] != b"\x7fELF":
        raise ValueError("not an ELF")
    e_shoff = struct.unpack_from("<I", d, 0x20)[0]
    e_shentsize = struct.unpack_from("<H", d, 0x2E)[0]
    e_shnum = struct.unpack_from("<H", d, 0x30)[0]
    e_shstrndx = struct.unpack_from("<H", d, 0x32)[0]
    secs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = \
            struct.unpack_from("<IIIIIIIIII", d, off)
        secs.append(dict(i=i, name_off=name, type=typ, addr=addr, offset=offset,
                         size=size, link=link, entsize=entsize))
    # resolve names
    strtab = b""
    if e_shstrndx < e_shnum:
        s = secs[e_shstrndx]
        strtab = d[s["offset"]:s["offset"] + s["size"]]
    for s in secs:
        end = strtab.find(b"\0", s["name_off"])
        s["name"] = strtab[s["name_off"]:end if end >= 0 else None].decode("latin1", "replace")
    return secs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--at", help="show relocations covering this offset (hex or dec)")
    ap.add_argument("--type", type=int, help="filter to one relocation type")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    d = open(args.elf, "rb").read()
    secs = parse_elf_sections(d)

    # PSP marks relocation sections with type 0x700000A0 (LOPROC+0xa0), entsize 8.
    # ⛔ It is 0x70, NOT 0x60 — guessed 0x60 first and the tool reported "no relocation
    # sections found", which reads as "this binary is not a relocatable PRX" rather
    # than "the constant is wrong". Print the actual type before trusting a null result.
    relsecs = [s for s in secs if s["entsize"] == 8 and s["type"] == 0x700000A0]
    if not relsecs:
        print("no relocation sections found (looking for type 0x700000A0, entsize 8).")
        print("section types actually present:")
        for s in secs:
            if s["entsize"]:
                print(f"  [{s['i']:2d}] type=0x{s['type']:08X} entsize={s['entsize']}")
        return 1

    entries = []
    for s in relsecs:
        n = s["size"] // 8
        for k in range(n):
            off = s["offset"] + k * 8
            r_offset, r_info = struct.unpack_from("<II", d, off)
            entries.append(dict(sec=s["name"] or f"sec{s['i']}", r_offset=r_offset,
                                sym=r_info >> 8, type=r_info & 0xFF))
    print(f"{args.elf}: {len(relsecs)} relocation sections, {len(entries)} entries")
    print()

    if args.stats or (not args.list and not args.at and not args.type):
        from collections import Counter
        c = Counter(e["type"] for e in entries)
        print("=== entries by type ===")
        for t, n in sorted(c.items()):
            print(f"  {t:3d} {TYPES.get(t, '?'):<12} {n}")
        print()

    if args.at:
        target = int(args.at, 0)
        hits = [e for e in entries if e["r_offset"] == target]
        print(f"=== relocations AT 0x{target:X}: {len(hits)} ===")
        for e in hits:
            print(f"  {e['sec']}  off=0x{e['r_offset']:08X}  sym={e['sym']}  "
                  f"{TYPES.get(e['type'], '?')}")
        if not hits:
            # a HI16/LO16 pair straddles; show anything within 8 bytes
            near = [e for e in entries if abs(e["r_offset"] - target) <= 8]
            print(f"  (±8 bytes: {len(near)})")
            for e in near[:12]:
                print(f"    off=0x{e['r_offset']:08X}  {TYPES.get(e['type'], '?')}")
        return 0

    if args.list or args.type is not None:
        sel = entries if args.type is None else [e for e in entries if e["type"] == args.type]
        detail = ""
        if args.type is not None:
            detail = " of type %d (%s)" % (args.type, TYPES.get(args.type, "?"))
        print(f"=== {len(sel)} entries{detail} (showing {min(len(sel), args.limit)}) ===")
        for e in sel[: args.limit]:
            print(f"  0x{e['r_offset']:08X}  sym={e['sym']:<6} {TYPES.get(e['type'], '?')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

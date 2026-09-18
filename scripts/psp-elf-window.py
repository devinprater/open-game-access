#!/usr/bin/env python
"""psp-elf-window.py — dump data at PSP RAM addresses straight out of the decrypted ELF.

WHY: Codex's p-code analysis showed character/UI names are resolved through TABLES at fixed
addresses (0x08A75538, 0x08A7553C, 0x08A72100, 0x08A80028, 0x08A80090, 0x08A80118, 0x08B41AF8).
Those addresses are STATIC DATA, so they live in EBOOT.dec itself -- no running game needed.

The Tag Team ELF is PRE-LINKED (vaddr 0x08804040), so a RAM address maps to a file offset through
the program headers. This script resolves that mapping and dumps the bytes.

Usage:
    python scripts/psp-elf-window.py <EBOOT.dec> 0x08A75538 --words 32
    python scripts/psp-elf-window.py <EBOOT.dec> --phdrs
    python scripts/psp-elf-window.py <EBOOT.dec> 0x08C85C82 --utf16 48
"""
import argparse
import struct
import sys


def parse_phdrs(data):
    """Return list of (type, offset, vaddr, filesz, memsz, flags)."""
    if data[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    ei_class = data[4]          # 1 = 32-bit
    if ei_class != 1:
        raise SystemExit(f"expected 32-bit ELF, got class {ei_class}")
    e_phoff = struct.unpack_from("<I", data, 0x1C)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x2A)[0]
    e_phnum = struct.unpack_from("<H", data, 0x2C)[0]
    out = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
            struct.unpack_from("<8I", data, off)
        out.append((p_type, p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
    return out


def addr_to_offset(phdrs, addr):
    """Return (file_offset, segment) for a RAM address, or (None, None)."""
    for (p_type, p_offset, p_vaddr, p_filesz, p_memsz, p_flags) in phdrs:
        if p_type != 1:      # PT_LOAD
            continue
        if p_vaddr <= addr < p_vaddr + p_memsz:
            delta = addr - p_vaddr
            if delta < p_filesz:
                return p_offset + delta, (p_offset, p_vaddr, p_filesz, p_memsz)
            return None, (p_offset, p_vaddr, p_filesz, p_memsz)   # in .bss, not in file
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("addr", nargs="?", help="RAM address, e.g. 0x08A75538")
    ap.add_argument("--phdrs", action="store_true", help="list program headers and exit")
    ap.add_argument("--words", type=int, default=24, help="number of u32 words to dump")
    ap.add_argument("--utf16", type=int, default=0, help="dump N UTF-16 chars instead of words")
    args = ap.parse_args()

    data = open(args.elf, "rb").read()
    phdrs = parse_phdrs(data)

    if args.phdrs:
        print(f"{'type':>5} {'offset':>10} {'vaddr':>12} {'filesz':>10} {'memsz':>10}  flags")
        for (p_type, p_offset, p_vaddr, p_filesz, p_memsz, p_flags) in phdrs:
            print(f"{p_type:>5} 0x{p_offset:08X} 0x{p_vaddr:08X} 0x{p_filesz:08X} 0x{p_memsz:08X}  0x{p_flags:X}")
        return 0

    if not args.addr:
        raise SystemExit("give an address (or use --phdrs)")

    addr = int(args.addr, 0)
    off, seg = addr_to_offset(phdrs, addr)
    if off is None:
        if seg:
            print(f"0x{addr:08X} lies in a segment's .bss region (offset 0x{seg[0]:08X}, "
                  f"vaddr 0x{seg[1]:08X}, filesz 0x{seg[2]:08X}) -> NOT present in the file.")
        else:
            print(f"0x{addr:08X} is not inside any PT_LOAD segment.")
        return 1

    print(f"0x{addr:08X} -> file offset 0x{off:08X}  (segment vaddr 0x{seg[1]:08X}, filesz 0x{seg[2]:08X})")

    if args.utf16:
        raw = data[off:off + args.utf16 * 2]
        txt = raw.decode("utf-16-le", "replace")
        print(f"utf16[{args.utf16}]: {txt!r}")
        return 0

    for i in range(args.words):
        o = off + i * 4
        if o + 4 > len(data):
            break
        w = struct.unpack_from("<I", data, o)[0]
        f = struct.unpack_from("<f", data, o)[0]
        note = ""
        if 0x08800000 <= w < 0x0A000000:
            note = f"  -> ptr 0x{w:08X}"
        print(f"  0x{addr + i*4:08X}   0x{w:08X}  {w:>12}  f={f:<14.4f}{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

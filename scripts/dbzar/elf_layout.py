#!/usr/bin/env python3
"""Parse EBOOT.dec's ELF headers correctly (the first attempt mis-unpacked the header).

Purpose: decide whether vaddr 0x99DAE is CODE or DATA -- the decompile says the chapter
records live at DAT_001b11d5 * 0x4a + 0x99dae, but a live RAM read there returned
MIPS-looking bytes. The section table settles which claim is wrong.
"""
import struct

ELF = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-ar-extract\EBOOT.dec"
d = open(ELF, "rb").read()
print(f"file size {len(d):,}")

e_type, e_machine, e_version = struct.unpack_from("<HH I", d, 0x10)
e_entry, e_phoff, e_shoff = struct.unpack_from("<III", d, 0x18)
e_flags, e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = \
    struct.unpack_from("<IHHHHHH", d, 0x24)
print(f"type={e_type} machine={e_machine} (8=MIPS) version={e_version}")
print(f"entry=0x{e_entry:08X} phoff=0x{e_phoff:X} shoff=0x{e_shoff:X}")
print(f"ehsize={e_ehsize} phentsize={e_phentsize} phnum={e_phnum}  "
      f"shentsize={e_shentsize} shnum={e_shnum} shstrndx={e_shstrndx}")

print("\n=== PROGRAM HEADERS ===")
print(f"{'type':>10} {'offset':>10} {'vaddr':>10} {'filesz':>10} {'memsz':>10} flags  align")
segs = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    if o + 32 > len(d):
        break
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = \
        struct.unpack_from("<IIIIIIII", d, o)
    segs.append((p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
    fl = ("R" if p_flags & 4 else "-") + ("W" if p_flags & 2 else "-") + ("X" if p_flags & 1 else "-")
    print(f"{p_type:>10} 0x{p_offset:08X} 0x{p_vaddr:08X} 0x{p_filesz:08X} 0x{p_memsz:08X} {fl:>5} 0x{p_align:X}")

print("\n=== SECTIONS ===")
secs = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    if o + 40 > len(d):
        break
    name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIIIIIIIII", d, o)
    secs.append((name, typ, flags, addr, off, size))
if secs and e_shstrndx < len(secs):
    strtab_off = secs[e_shstrndx][4]
    def nm(x):
        end = d.index(b"\x00", strtab_off + x)
        return d[strtab_off + x:end].decode("latin1", "replace")
    print(f"{'name':>16} {'type':>6} {'flags':>8} {'addr':>10} {'offset':>10} {'size':>10}")
    for (name, typ, flags, addr, off, size) in secs:
        print(f"{nm(name):>16} {typ:>6} 0x{flags:06X} 0x{addr:08X} 0x{off:08X} 0x{size:08X}")
else:
    print("  (section name table unavailable)")

def which(va):
    for (name, typ, flags, addr, off, size) in secs:
        if addr and addr <= va < addr + size:
            return (nm(name), addr, off, size, typ)
    for (p_off, p_va, p_filesz, p_memsz, p_flags) in segs:
        if p_va <= va < p_va + p_memsz:
            return ("<segment>", p_va, p_off, p_filesz, p_flags)
    return None

print("\n=== KEY ADDRESSES ===")
for va in (0x99DAE, 0x99DAC, 0x1B11D8, 0x1B11D4, 0x6B90, 0xC139C, 0x34660, 0x220530, 0x19FB80):
    r = which(va)
    if r:
        n, addr, off, size, typ = r
        fo = off + (va - addr)
        print(f"  vaddr 0x{va:06X} -> {n:<12} addr0x{addr:06X} off0x{off:06X} size0x{size:06X} "
              f"type={typ} fileoff 0x{fo:06X} skew 0x{fo - va:06X}")
    else:
        print(f"  vaddr 0x{va:06X} -> MAPPED NOWHERE")

print("\n=== what is stored at vaddr 0x99DAE (the chapter record base)? ===")
r = which(0x99DAE)
if r:
    n, addr, off, size, typ = r
    fo = off + (0x99DAE - addr)
    print(f"  section {n}, file offset 0x{fo:06X}")
    print("  first 96 bytes:", d[fo:fo + 96].hex(" "))

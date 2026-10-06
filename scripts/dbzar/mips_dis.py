#!/usr/bin/env python3
"""Disassemble FUN_000327b4 to see the REAL constant the chapter-record code uses.

Ghidra rendered it as `DAT_001b11d5 * 0x4a + 0x99dae`, which cannot be right: vaddr 0x99DAE
is inside .text (which runs 0..0x19F170), and the code WRITES there. So the constant must be
something else -- most likely the low bits of a base that Ghidra folded badly.

File offset = vaddr + 0x74 for this ELF.
"""
import struct

ELF = r"C:\Users\Devin Prater\AppData\Local\Temp\dbz-ar-extract\EBOOT.dec"
d = open(ELF, "rb").read()
SKEW = 0x74

REGS = ["zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
        "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"]

def s16(x):
    return x - 0x10000 if x & 0x8000 else x

def dis(w, a):
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    rd = (w >> 11) & 31
    sa = (w >> 6) & 31
    fn = w & 63
    im = w & 0xFFFF
    tgt = w & 0x3FFFFFF
    if op == 0x0F:  # lui
        return f"lui   {REGS[rt]}, 0x{im:04X}          # 0x{im<<16:08X}"
    if op == 0x09:  # addiu
        return f"addiu {REGS[rt]}, {REGS[rs]}, {s16(im)}   # 0x{im:04X}"
    if op == 0x0D:  # ori
        return f"ori   {REGS[rt]}, {REGS[rs]}, 0x{im:04X}"
    if op == 0x23:  # lw
        return f"lw    {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x24:  # lbu
        return f"lbu   {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x20:  # lb
        return f"lb    {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x28:  # sb
        return f"sb    {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x2B:  # sw
        return f"sw    {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x21:  # lh
        return f"lh    {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x25:  # lhu
        return f"lhu   {REGS[rt]}, {s16(im)}({REGS[rs]})"
    if op == 0x0C:  # andi
        return f"andi  {REGS[rt]}, {REGS[rs]}, 0x{im:04X}"
    if op == 0x04:  # beq
        return f"beq   {REGS[rs]}, {REGS[rt]}, 0x{a + 4 + s16(im)*4:06X}"
    if op == 0x05:  # bne
        return f"bne   {REGS[rs]}, {REGS[rt]}, 0x{a + 4 + s16(im)*4:06X}"
    if op == 0x02:  # j
        return f"j     0x{((a+4) & 0xF0000000) | (tgt<<2):06X}"
    if op == 0x03:  # jal
        return f"jal   0x{((a+4) & 0xF0000000) | (tgt<<2):06X}"
    if op == 0x00:
        names = {0x08:"jr",0x09:"jalr",0x21:"addu",0x23:"subu",0x24:"and",0x25:"or",0x2A:"slt",
                 0x2B:"sltu",0x00:"sll",0x02:"srl",0x03:"sra",0x18:"mult",0x1A:"div",0x10:"mfhi",0x12:"mflo"}
        nm = names.get(fn, f"special_{fn}")
        if fn == 0x08: return f"jr    {REGS[rs]}"
        if fn == 0x21: return f"addu  {REGS[rd]}, {REGS[rs]}, {REGS[rt]}"
        if fn == 0x00: return f"sll   {REGS[rd]}, {REGS[rt]}, {sa}"
        if fn == 0x18: return f"mult  {REGS[rs]}, {REGS[rt]}"
        if fn == 0x1A: return f"div   {REGS[rs]}, {REGS[rt]}"
        return f"{nm:<6} {REGS[rd]}, {REGS[rs]}, {REGS[rt]}"
    if op == 0x1C:  # special2 (mult/madd etc)
        return f"special2 fn=0x{fn:X}"
    return f"op=0x{op:02X} 0x{w:08X}"

def dump(vaddr, nwords, label):
    fo = vaddr + SKEW
    print(f"\n===== {label}  vaddr 0x{vaddr:06X} (file 0x{fo:06X}) =====")
    for i in range(nwords):
        w = struct.unpack_from("<I", d, fo + i * 4)[0]
        print(f"  0x{vaddr + i*4:06X}  {w:08X}  {dis(w, vaddr + i*4)}")

# FUN_000327b4 (chapter select entry) and FUN_00032a0c / FUN_00032c10
dump(0x327B4, 46, "FUN_000327b4  chapter-select entry")
dump(0x32A0C, 34, "FUN_00032a0c  leave chapter select")

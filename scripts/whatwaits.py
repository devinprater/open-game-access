#!/usr/bin/env python3
# whatwaits.py — disassemble the ARM9 code at the address the game spins on.
#
# The ROM is mapped at 0x02000000 in the DS address space, so a PC of
# 0x020882DC is file offset 0x000882DC. Seeing the actual instructions around
# that loop says what hardware condition is never satisfied.
import struct, sys

ROM = sys.argv[1]
ADDR = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x020882DC
SPAN = int(sys.argv[3]) if len(sys.argv) > 3 else 0x80

data = open(ROM, "rb").read()
off = ADDR - 0x02000000
print(f"PC=0x{ADDR:08X}  file offset=0x{off:X}")

try:
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
    md.detail = True
    code = data[off:off+SPAN]
    print(f"\n--- disassembly from 0x{ADDR:08X} ---")
    for ins in md.disasm(code, ADDR):
        print(f"  0x{ins.address:08X}:  {ins.mnemonic:<8} {ins.op_str}")
except ImportError:
    print("capstone not installed; falling back to a word dump")
    for i in range(0, SPAN, 4):
        w = struct.unpack_from("<I", data, off+i)[0]
        print(f"  0x{ADDR+i:08X}:  {w:08X}")

# Also show what is at the other hot PCs seen in the run.
print("\n--- words at the other observed hot PCs ---")
for pc in (0x02076ED8, 0x02019A80, 0x02004E64, 0x01FF878C, 0x02082A84):
    o = pc - 0x02000000
    if 0 <= o < len(data) - 16:
        words = " ".join(f"{struct.unpack_from('<I', data, o+i)[0]:08X}" for i in range(0, 16, 4))
        print(f"  0x{pc:08X}: {words}")

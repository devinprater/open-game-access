#!/usr/bin/env python3
"""Build a minimal, valid NES ROM as a KNOWN-GOOD FIXTURE for the core probe.

There is no commercial NES ROM on this machine, and a probe that cannot run at all proves nothing.
So build a cartridge whose behaviour is fully known in advance:

    reset:  INC $00        (increment zero-page 0 every pass)
            JMP reset      (forever)

That is one property, checkable exactly: after N frames, RAM[0x00] must be NON-ZERO AND INCREASING.
A core that is not stepping leaves it at 0; a core stepping the wrong code leaves it constant. So
the fixture distinguishes "running" from "loaded but dead", which is the question a link probe
actually has to answer.

iNES header: 16 KB PRG (1 x 16 KB), 8 KB CHR (1 x 8 KB), mapper 0, horizontal mirroring.
Vectors live at the TOP of the 16 KB PRG bank: NMI $FFFA, RESET $FFFC, IRQ $FFFE.
"""
import pathlib, struct

PRG = bytearray([0xEA] * 0x4000)      # 16 KB, NOP-filled
CHR = bytearray([0x00] * 0x2000)      # 8 KB, all zero tiles

# --- the program, at PRG offset 0x0000 (mapped to CPU $C000 in a 16 KB NROM)
prog = bytes([
    0xE6, 0x00,     # INC $00
    0x4C, 0x00, 0xC0  # JMP $C000
])
PRG[0:len(prog)] = prog

# --- vectors, at the end of the PRG bank ($FFFA..$FFFF -> PRG offset 0x3FFA..0x3FFF)
def vec(off, addr):
    PRG[off] = addr & 0xFF
    PRG[off + 1] = (addr >> 8) & 0xFF

vec(0x3FFA, 0xC000)   # NMI
vec(0x3FFC, 0xC000)   # RESET
vec(0x3FFE, 0xC000)   # IRQ/BRK

header = bytearray(b"NES\x1A")
header += bytes([
    1,      # PRG banks of 16 KB
    1,      # CHR banks of 8 KB
    0x00,   # flags 6: mapper 0 low, horizontal mirroring, no trainer, no battery
    0x00,   # flags 7: mapper 0 high, NES (not VS), no PC10
    0, 0, 0, 0, 0, 0, 0, 0,
])
assert len(header) == 16

out = pathlib.Path("/home/devin/nes-rom/fixture.nes")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(bytes(header) + bytes(PRG) + bytes(CHR))
print(f"wrote {out}  ({out.stat().st_size} bytes)")
print("  PRG=16KB CHR=8KB mapper=0 reset=$C000 program: INC $00 ; JMP $C000")

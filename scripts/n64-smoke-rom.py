#!/usr/bin/env python3
"""n64-smoke-rom.py -- write a tiny synthetic Nintendo 64 cartridge for the host proof.

No commercial ROM is ever needed (or allowed) in this repository, so the N64
host proof boots this instead: a 1 MiB z64 image whose boot code (the part the
console's IPL copies to SP DMEM and runs at 0xA4000040) does just enough to
exercise every path the app uses:

  1. Programs the Video Interface for a 320x240 NTSC frame. Without VI_V_SYNC
     and VI_V_INTR the core never raises a VI interrupt, so retro_run never
     returns: the first version of this ROM hung the proof exactly that way.
  2. Fills the 320x240 framebuffer at RDRAM 0x100000 with one colour
     (0xFF8000, orange), so the proof can check pixels, not just "a frame".
  3. Writes the marker word 0x4F474136 ("OGA6") to RDRAM 0x400, and the
     halfword-distinct word 0x12345678 to 0x408, for the byte-order reads.
  3b. Copies those markers by PI DMA into the cartridge's SRAM, so the save
     path has something to write.
  4. Fills an audio buffer at RDRAM 0x200000 with a square wave and starts the
     Audio Interface DMA, re-queueing it whenever the AI FIFO has room.
  5. Loops forever incrementing the word at RDRAM 0x404, so "the CPU is
     running" and "a loaded state rewinds it" are both observable.

The cartridge ID at header 0x3B is "NOGE" so n64_load_rom's code is n64:NOGE.
Usage: n64-smoke-rom.py <out.z64> [--byteorder z64|v64|n64]
"""
import struct
import sys

T0, T1, T2, T3, T4, T5, T6, T7, T8, T9 = 8, 9, 10, 11, 12, 13, 14, 15, 24, 25
ZERO = 0


def lui(rt, imm): return (0x0F << 26) | (rt << 16) | (imm & 0xFFFF)
def ori(rt, rs, imm): return (0x0D << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def sw(rt, off, base): return (0x2B << 26) | (base << 21) | (rt << 16) | (off & 0xFFFF)
def lw(rt, off, base): return (0x23 << 26) | (base << 21) | (rt << 16) | (off & 0xFFFF)
def addiu(rt, rs, imm): return (0x09 << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)
def and_(rd, rs, rt): return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def li(r, v): return [lui(r, v >> 16), ori(r, r, v & 0xFFFF)]
NOP = 0
BOOT = 0xA4000040       # where the IPL runs the cartridge's boot code


def bne(code, rs, rt, target_index):
    """bne rs,rt,code[target_index]; offset is relative to the delay slot."""
    off = target_index - (len(code) + 1)
    return (0x05 << 26) | (rs << 21) | (rt << 16) | (off & 0xFFFF)


def j(target_addr): return (0x02 << 26) | ((target_addr & 0x0FFFFFFF) >> 2)


code = []
# 1. Video Interface (NTSC 320x240, 32-bit pixels at RDRAM 0x100000)
code += li(T4, 0xA4400000)
vi = [(0x18, 0x20D), (0x1C, 0xC15), (0x20, 0x0C150C15), (0x24, 0x006C02EC),
      (0x28, 0x002501FF), (0x2C, 0x000E0204), (0x30, 0x200), (0x34, 0x400),
      (0x14, 0x03E52239), (0x08, 320), (0x04, 0x00100000), (0x00, 0x3003), (0x0C, 2)]
for off, val in vi:
    code += li(T5, val) + [sw(T5, off, T4)]
# 2. framebuffer fill
code += li(T0, 0xA0100000) + li(T1, 0xFF8000FF) + li(T3, 320 * 240)
fill = len(code)
code += [sw(T1, 0, T0), addiu(T0, T0, 4), addiu(T3, T3, -1)]
code += [bne(code, T3, ZERO, fill), NOP]
# 3. markers
code += li(T0, 0xA0000400) + li(T1, 0x4F474136) + [sw(T1, 0, T0)]
code += li(T1, 0x12345678) + [sw(T1, 8, T0)]
# 3b. battery save: PI DMA 16 bytes from RDRAM 0x400 (the markers) to SRAM at
#     cartridge address 0x08000000, so n64_stop has a save to write.
code += li(T4, 0xA4600000)
code += li(T5, 0x00000400) + [sw(T5, 0x00, T4)]        # PI_DRAM_ADDR
code += li(T5, 0x08000000) + [sw(T5, 0x04, T4)]        # PI_CART_ADDR (SRAM)
code += li(T5, 15) + [sw(T5, 0x08, T4)]                # PI_RD_LEN: RDRAM -> cart
# 4. audio: 8 KiB square wave (2048 stereo frames), 64 frames per half-cycle
code += li(T0, 0xA0200000) + li(T3, 2048)
code += li(T5, 0x40004000) + li(T6, 0xC000C000)
wave = len(code)
code += [sw(T5, 0, T0), addiu(T0, T0, 4), addiu(T3, T3, -1)]
# swap the two levels every 64 frames: when (t3 & 63) == 0, exchange t5/t6
code += [addiu(T8, ZERO, 63), and_(T8, T3, T8)]
skip_swap_branch = len(code)
code += [0, NOP]   # patched below: bne t8,zero,after_swap
code += [addiu(T9, T5, 0), addiu(T5, T6, 0), addiu(T6, T9, 0)]
after_swap = len(code)
code[skip_swap_branch] = (0x05 << 26) | (T8 << 21) | (ZERO << 16) | ((after_swap - (skip_swap_branch + 1)) & 0xFFFF)
code += [bne(code, T3, ZERO, wave), NOP]
code += li(T7, 0xA4500000)
code += li(T5, 0x00200000) + [sw(T5, 0x00, T7)]        # AI_DRAM_ADDR
code += li(T5, 1520) + [sw(T5, 0x10, T7)]              # AI_DACRATE (~32 kHz NTSC)
code += li(T5, 15) + [sw(T5, 0x14, T7)]                # AI_BITRATE
code += li(T5, 1) + [sw(T5, 0x08, T7)]                 # AI_CONTROL: DMA on
# AI_LEN 8000, not 8192: a DMA that ENDS on an 8 KiB boundary sets the AI's
# "delayed carry" (real hardware, emulated by the core), and the next buffer
# then plays from address + 0x2000 -- silence, which the first proof heard.
code += li(T9, 8000) + [sw(T9, 0x04, T7)]              # AI_LEN: start
# 5. the run loop
code += li(T2, 0xA0000404) + li(T8, 0x80000000) + li(T1, 0x00200000)
loop = len(code)
code += [lw(T3, 0, T2), addiu(T3, T3, 1), sw(T3, 0, T2)]
code += [lw(T6, 0x0C, T7), and_(T6, T6, T8)]          # AI_STATUS & FULL
requeue = len(code)
code += [0, NOP]   # patched: bne t6,zero,back
# The AI advances its DRAM address as it plays (games set it before every
# buffer), so re-queue the SAME buffer: address first, then length.
code += [sw(T1, 0x00, T7), sw(T9, 0x04, T7)]         # re-queue AI_DRAM_ADDR + AI_LEN
back = len(code)
code[requeue] = (0x05 << 26) | (T6 << 21) | (ZERO << 16) | ((back - (requeue + 1)) & 0xFFFF)
code += [j(BOOT + 4 * loop), NOP]

rom = bytearray(0x100000)
struct.pack_into('>I', rom, 0, 0x80371240)
struct.pack_into('>I', rom, 8, 0x80000400)
rom[0x20:0x34] = b'OGA N64 SMOKE TEST  '
rom[0x3B:0x3F] = b'NOGE'
assert 0x40 + 4 * len(code) < 0x1000, "boot code must fit in the IPL3 area"
for i, w in enumerate(code):
    struct.pack_into('>I', rom, 0x40 + 4 * i, w)

order = 'z64'
if '--byteorder' in sys.argv:
    order = sys.argv[sys.argv.index('--byteorder') + 1]
if order == 'v64':
    for i in range(0, len(rom), 2):
        rom[i], rom[i + 1] = rom[i + 1], rom[i]
elif order == 'n64':
    for i in range(0, len(rom), 4):
        rom[i:i + 4] = rom[i:i + 4][::-1]
elif order != 'z64':
    sys.exit('unknown byte order ' + order)
with open(sys.argv[1], 'wb') as f:
    f.write(rom)

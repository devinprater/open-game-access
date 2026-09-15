#!/usr/bin/env bash
# dis3.sh — CORRECTED disassembly.
#
# The earlier attempt read the ROM at (PC - 0x02000000), which is wrong: PCs in
# the 0x02000000..0x023FFFFF range are MAIN RAM, where the ARM9 binary has been
# copied. The real file offset is:
#     ARM9ROMOffset + (PC - ARM9RAMAddress)
# ARM9ROMOffset/RAMAddress/Size come from the ROM header.
set -uo pipefail

# objdump may not be installed (earlier runs suppressed the error with 2>/dev/null).
if ! command -v objdump >/dev/null 2>&1; then
  echo "objdump missing; installing binutils..."
  sudo apt-get update -qq >/dev/null 2>&1
  sudo apt-get install -y -qq binutils >/dev/null 2>&1
fi
command -v objdump && objdump --version | head -1

python3 - <<'PY'
import struct, subprocess, os

def hdr(path):
    d = open(path,'rb').read()
    return {
        'rom': d,
        'arm9_off':  struct.unpack_from('<I', d, 0x20)[0],
        'arm9_entry':struct.unpack_from('<I', d, 0x24)[0],
        'arm9_ram':  struct.unpack_from('<I', d, 0x28)[0],
        'arm9_size': struct.unpack_from('<I', d, 0x2C)[0],
        'arm7_off':  struct.unpack_from('<I', d, 0x30)[0],
        'arm7_entry':struct.unpack_from('<I', d, 0x34)[0],
        'arm7_ram':  struct.unpack_from('<I', d, 0x38)[0],
        'gamecode':  d[0x0C:0x10].decode('ascii','replace'),
    }

def dis(rom, pc, label, thumb=False):
    off = rom['arm9_off'] + (pc - rom['arm9_ram'])
    data = rom['rom']
    if off < 0 or off+96 > len(data):
        print(f"  !! {label}: offset 0x{off:X} out of range"); return
    chunk = data[off:off+96]
    p = '/tmp/c.bin'
    open(p,'wb').write(chunk)
    mode = 'thumb' if thumb else 'arm'
    r = subprocess.run(['objdump','-D','-b','binary','-m','arm',
                        f'-M{mode}','--adjust-vma=0x%X'%pc, p],
                       capture_output=True, text=True)
    lines = [l for l in r.stdout.splitlines() if l.strip() and 'file format' not in l and l.strip().endswith(':') is False]
    # drop the header lines objdump emits
    body = [l for l in lines if '\t' in l][:12]
    print(f"  --- {label}: PC=0x{pc:08X} (rom off 0x{off:X}) [{mode}] ---")
    for l in body: print('   ', l.strip())
    if not body: print("    (no decode)", r.stderr.strip()[:200])

for name, path, pcs in [
    ("BLACK",   os.path.expanduser('~/hosttest-data/black.nds'),
     [0x020882DC, 0x02076ED8, 0x02019A80, 0x02004E64, 0x02082A84]),
    ("DIAMOND", os.path.expanduser('~/roms/diamond.nds'),
     [0x020CD850, 0x020CC13C, 0x020D7564]),
]:
    r = hdr(path)
    print(f"\n===== {name}  gamecode={r['gamecode']} =====")
    print(f"  ARM9: off=0x{r['arm9_off']:X} ram=0x{r['arm9_ram']:X} entry=0x{r['arm9_entry']:X} size=0x{r['arm9_size']:X}")
    print(f"  ARM7: off=0x{r['arm7_off']:X} ram=0x{r['arm7_ram']:X} entry=0x{r['arm7_entry']:X}")
    for pc in pcs:
        dis(r, pc, f"{name} ARM9", thumb=False)
PY

#!/usr/bin/env bash
# ramdump-run.sh — dump emulated Main RAM, then disassemble the hot PCs in it.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"
cp "/mnt/c/Users/Devin Prater/pokemon-access-ios/Core/ramdump.cpp" Core/
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/ramdump Core/ramdump.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
echo "linked: $([ -x Vendor/ramdump ] && echo yes || echo NO)"

echo
echo "########## BLACK ##########"
timeout 200 ./Vendor/ramdump "$HOME/hosttest-data/black.nds" 3000 "$HOME/mainram_black.bin" 2>&1 | tail -10

echo
python3 - <<'PY'
import os, struct
from capstone import *

path = os.path.expanduser('~/mainram_black.bin')
if not os.path.exists(path):
    print("no dump"); raise SystemExit
ram = open(path,'rb').read()
base = 0x02000000
md = Cs(CS_ARCH_ARM, CS_MODE_ARM | CS_MODE_LITTLE_ENDIAN)

for pc in (0x020882DC, 0x02076ED8, 0x02019A80, 0x02004E64, 0x02082A84):
    off = pc - base
    if off < 0 or off+80 > len(ram):
        print(f"\n=== PC 0x{pc:08X}: out of range ==="); continue
    print(f"\n=== BLACK ARM9 @ 0x{pc:08X} (main RAM off 0x{off:X}) ===")
    for ins in md.disasm(ram[off:off+80], pc):
        print(f"  {ins.address:08X}  {ins.mnemonic:<9} {ins.op_str}")
PY

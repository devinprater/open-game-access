#!/usr/bin/env bash
# ramdump.sh — set up a proper ARM disassembly of EMULATED memory.
#
# Retail DS ROMs store the ARM9 code ENCRYPTED; melonDS decrypts it into Main
# RAM at boot. So the only meaningful thing to disassemble is emulated memory,
# not the ROM file. This (1) finds the core's memory-read path, (2) installs an
# ARM-capable disassembler.
set -uo pipefail

echo "########## 1. how does the Lua memory API read emulated memory? ##########"
grep -n 'ReadMemory\|read_u8\|read_u16\|read_u32\|MainRAM\|ReadByte' \
  "$HOME/pokemon-access-ios/Core/pokecore.cpp" | head -30

echo
echo "########## 2. the shim-facing memory function body ##########"
grep -n 'static int l_memory_read\|l_read_u8\|memory_read' -A 25 \
  "$HOME/pokemon-access-ios/Core/pokecore.cpp" | head -45

echo
echo "########## 3. install an ARM disassembler ##########"
sudo apt-get install -y -qq python3-capstone >/dev/null 2>&1
python3 -c "import capstone; print('capstone OK', capstone.__version__ if hasattr(capstone,'__version__') else '')" 2>&1 | head -2
sudo apt-get install -y -qq binutils-arm-none-eabi >/dev/null 2>&1
command -v arm-none-eabi-objdump || echo "(no arm-none-eabi-objdump)"

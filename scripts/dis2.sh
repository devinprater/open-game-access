#!/usr/bin/env bash
# dis2.sh — disassemble the ARM9 loop the game spins in. objdump, fixed.
# DS ROMs map at 0x02000000 => file offset = addr - 0x02000000.
set -uo pipefail
D="$HOME/disasm2"; mkdir -p "$D"

dis() { # rom addr
  local rom="$1" addr="$2" off
  off=$(( addr - 0x02000000 ))
  printf -v hexoff '0x%X' "$off"
  dd if="$rom" of="$D/c.bin" bs=1 skip="$off" count=96 status=none
  printf -v vma '0x%X' "$addr"
  echo "===== $rom  PC=0x$(printf '%08X' $addr) (off $hexoff) ====="
  objdump -D -b binary -m arm --adjust-vma="$vma" "$D/c.bin" 2>/dev/null | sed -n '7,40p'
  echo
}

echo "############ POKEMON BLACK (ARM9, hot PCs) ############"
for pc in 0x020882DC 0x02076ED8 0x02019A80 0x02004E64; do
  dis "$HOME/hosttest-data/black.nds" "$pc"
done

echo "############ POKEMON DIAMOND (ARM9, hot PCs) ############"
for pc in 0x01FF8350 0x020CD850 0x020CC13C; do
  dis "$HOME/roms/diamond.nds" "$pc"
done

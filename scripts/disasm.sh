#!/usr/bin/env bash
# disasm.sh — disassemble what the game is actually doing at the hot PCs.
# objdump comes with binutils and needs no pip.
set -uo pipefail
ROM="$HOME/hosttest-data/black.nds"
D="$HOME/disasm"
mkdir -p "$D"

# DS ROMs map at 0x02000000, so PC 0x020882DC is file offset 0x882DC.
dis() {
  local addr="$1" span="${2:-0x100}"
  local off=$(( addr - 0x02000000 ))
  local a=$(printf "0x%X" "$off")
  dd if="$ROM" of="$D/chunk.bin" bs=1 skip=$((off)) count=$((span)) status=none
  echo "=== PC 0x$(printf '%08X' "$addr")  (file offset 0x$(printf '%X' "$off")) ==="
  objdump -D -b binary -m arm -M force-thumb=0 \
    --adjust-vma=$(printf '0x%X' "$addr") "$D/chunk.bin" 2>/dev/null \
    | sed -n '/<\.data>:/,$p' | tail -n +2 | head -40
  echo
}

# Black's hot PCs
for pc in 0x020882DC 0x02076ED8 0x02004E64 0x01FF878C 0x02019A80; do
  dis "$pc" 0x80
done

# Diamond's hot PCs (different ROM)
ROM="$HOME/roms/diamond.nds"
for pc in 0x020CD850 0x01FF8350 0x020D7564; do
  dis "$pc" 0x80
done

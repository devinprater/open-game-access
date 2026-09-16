#!/usr/bin/env bash
# bootstate.sh — what does SetupDirectBoot set, and why are both CPUs parked in
# the BIOS at 0xFC / 0x107C?
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"

echo "=== NDS::SetupDirectBoot(romname) ==="
sed -n '390,470p' "$SRC/NDS.cpp"

echo
echo "=== where do the CPUs start? (Reset / ARMv4::Reset) ==="
grep -n 'void ARMv4::Reset' -A 20 "$SRC/ARM.cpp" | head -26

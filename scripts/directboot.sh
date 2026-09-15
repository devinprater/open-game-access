#!/usr/bin/env bash
# directboot.sh — the FULL NDS::SetupDirectBoot, and the IPC/halt machinery.
#
# Direct boot has to do a lot: copy the ARM7 program into ARM7 WRAM, set up the
# IPC FIFOs, seed the RTC, etc. The ARM9 waits on the ARM7 over IPC during boot,
# so a missing piece here means the ARM9 spins forever with no graphics init.
set -uo pipefail
SRC="$HOME/src/melonds-lua/src"
OUT="$HOME/directboot.txt"
: > "$OUT"

echo "########## NDS::SetupDirectBoot() no-arg, full (280-400) ##########" >> "$OUT"
sed -n '280,400p' "$SRC/NDS.cpp" >> "$OUT"

echo >> "$OUT"
echo "########## NDSCartSlot::SetupDirectBoot ##########" >> "$OUT"
grep -n 'void NDSCartSlot::SetupDirectBoot' -A 30 "$SRC/NDSCart.cpp" >> "$OUT"

cat "$OUT"

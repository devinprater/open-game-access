#!/usr/bin/env bash
# region-scan.sh — decide whether a claimed cheat address region is populated.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1
bash "$ROOT/scripts/build-host.sh" || exit 1
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/ramscan Core/ramscan.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/ramscan ] || { echo "!! ramscan did not link"; exit 1; }
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
FRAMES="${FRAMES:-5000}"
G="/mnt/c/Users/Devin Prater/Dropbox/Games/NDS"

local_rom() { local r="$HOME/roms/$(basename "$1")"; [ -f "$r" ] || cp "$1" "$r"; echo "$r"; }

echo "################ Dragon Ball Z: Attack of the Saiyans (US) ################"
echo "-- claimed: 020CC370 money / 020CD4A4 AP / 020CD3xx battle struct --"
timeout 600 ./Vendor/ramscan "$(local_rom "$G/Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds")" "$FRAMES" 020CC000 2000 12

echo
echo "################ Dragon Ball: Origins 2 (US) ################"
echo "-- claimed: 0210A200 zenny / 0210A31C episodes --"
timeout 600 ./Vendor/ramscan "$(local_rom "$G/Dragon Ball - Origins 2 (USA) (En,Fr,Es).nds")" "$FRAMES" 0210A000 1000 12

echo
echo "################ Bleach 3rd Phantom (US) — the VERIFIED one, as a control ################"
echo "-- claimed: 021DB432..021DB45C party stat block, stride 0x13C --"
timeout 600 ./Vendor/ramscan "$(local_rom "$G/Bleach - The 3rd Phantom (USA).nds")" "$FRAMES" 021DB400 300 14

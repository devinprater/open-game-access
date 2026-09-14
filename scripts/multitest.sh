#!/usr/bin/env bash
# multitest.sh — control experiment: does the SAME core render ANY other ROM?
#
# Pokémon Black is DSi-enhanced and huge. Pokémon Diamond is a plain DS ROM.
#   Diamond renders  → my wiring is fine; Black specifically is stuck.
#   Diamond blank    → my wiring is broken (renderer/boot sequence), and Black was
#                      never the problem.
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT"

mkdir -p "$HOME/roms"
cp "/mnt/c/Users/Devin Prater/Dropbox/Games/NDS/Pokemon - Diamond Version (USA) (Rev 5).nds" "$HOME/roms/diamond.nds"
ls -la "$HOME/roms/"

echo
echo "########## DIAMOND, direct boot, 6000 frames ##########"
PA_SCRIPT="$HOME/hosttest-data/noop.lua" \
timeout 200 ./Vendor/fwtest "$HOME/roms/diamond.nds" "" "" "" 6000 2>&1 \
  | grep -vE '^\s*$' | tail -20

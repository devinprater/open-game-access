#!/usr/bin/env bash
# verify-cheats.sh — read published cheat addresses in a LIVE game.
#
# The point is not the cheats. An Action Replay code names an absolute RAM
# address, which is precisely what an accessibility reader needs — and internet
# lists are unverified, version-specific and frequently mistyped. Reading them in
# a running console separates "claimed address" from "real address".
set -uo pipefail
ROOT="$HOME/pokemon-access-ios"
SRC="$HOME/src/melonds-lua/src"
cd "$ROOT" || exit 1

bash "$ROOT/scripts/build-host.sh" || exit 1
g++ -O2 -g -ICore -ISources/CPokeCore/include -I"$SRC" -std=c++17 \
  -o Vendor/ramwatch Core/ramwatch.cpp Vendor/hostobj/*.o -lpthread -lm -ldl 2>&1 \
  | grep -E '\berror\b|undefined reference' | head -8
[ -x Vendor/ramwatch ] || { echo "!! ramwatch did not link"; exit 1; }
export PA_SHIM="$ROOT/Sources/PokemonAccess/Resources/bizhawk_compat.lua"
mkdir -p "$ROOT/ramwatch"
FRAMES="${FRAMES:-5000}"

run() { # run <rom-file> <label> <addr:width:label>...
  local rom="$1"; shift
  local label="$1"; shift
  echo
  echo "################ $label ################"
  echo "rom: $(basename "$rom")"
  local local_rom="$HOME/roms/$(basename "$rom")"
  [ -f "$local_rom" ] || cp "$rom" "$local_rom"
  timeout 900 ./Vendor/ramwatch "$local_rom" "$FRAMES" "$ROOT/ramwatch/$label.csv" "$@" 2>&1 | tail -30
}

G="/mnt/c/Users/Devin Prater/Dropbox/Games/NDS"

# ---- Dragon Ball Z: Attack of the Saiyans (US, BRPE) ----
# 020CC370 money(32) | 020CC85C bonus pts | 020CD4A4 AP(32) | 020CD308/020CD300 HP pair
run "$G/Dragon Ball Z - Attack of the Saiyans (USA) (En,Fr).nds" aots \
  020cc370:4:money32 020cc84c:1:bonusPts8 020cd4a4:4:ap32 \
  020cd308:2:hpA16 020cd300:2:hpB16 020cc3fc:2:consumables16

# ---- Bleach: The 3rd Phantom (US, YBTE) ----
# A character struct ARRAY with stride 0x13C: HP@+0x16, MaxHP@+0x18, Level@+0x02, SP@+0x1C
run "$G/Bleach - The 3rd Phantom (USA).nds" bleach3rd \
  021db446:2:char0_hp 021db448:2:char0_maxhp 021db432:1:char0_level \
  021db44c:2:char0_sp 021db45c:2:char0_stat
run "$G/Bleach - The 3rd Phantom (USA).nds" bleach3rd_stride \
  021db446:2:char0_hp 021db582:2:char1_hp 021db6be:2:char2_hp 021db7fa:2:char3_hp

# ---- Dragon Ball: Origins 2 (US, BDBE) ----
run "$G/Dragon Ball - Origins 2 (USA) (En,Fr,Es).nds" dborigins2 \
  0210a200:4:zenny32 0210a204:4:skillpts32 0210a31c:1:episodes

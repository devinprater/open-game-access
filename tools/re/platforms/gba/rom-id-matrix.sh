#!/usr/bin/env bash
# rom-id-matrix.sh — run the reader against EVERY supported Pokémon ROM and report which it
# identifies. One shim, nine games, no per-game special-casing.
#
# ⛔ WHY A MATRIX AND NOT ONE GAME. A shim that only works for FireRed would still be wrong —
# the plan's exit criterion is explicitly "the same shim, unmodified per game, serves all
# generations". Running all nine is how that gets checked rather than assumed.
#
# This test needs no emulator: the readers identify a game from the CARTRIDGE HEADER, which
# is a file. Live RAM reads still return zero, so anything past identification is not
# exercised here.
set -uo pipefail

READER_DIR="/c/Users/Devin Prater/Dropbox/programs/pokemon-access/lua"
ROM_DIR_WIN="C:/Users/Devin Prater/Dropbox/Games/GBA"

cd "$READER_DIR" || exit 1

# ROM filename -> a short label.
#
# ⛔ ONLY THE GAMES v3.1.0 ACTUALLY SUPPORTS. From readme.txt, verbatim:
#
#     - Pokémon Red, Blue and Yellow.
#     - Pokémon Gold, Silver and Crystal.
#     - Pokémon Fire Red and Leaf Green.
#     - Pokémon Emerald.
#
# Ruby and Sapphire are NOT in that list, so the reader answering `game_not_supported`
# for them is CORRECT BEHAVIOUR, not a failure. An earlier version of this script counted
# them as failures, which would have reported a working shim as broken.
ROMS=(
  "Pokemon - Red Version (USA, Europe) (SGB Enhanced).gb"
  "Pokemon - Blue Version (USA, Europe) (SGB Enhanced).gb"
  "Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced).gb"
  "Pokemon - Gold Version (USA, Europe) (SGB Enhanced) (GB Compatible).gbc"
  "Pokemon - Silver Version (USA, Europe) (SGB Enhanced) (GB Compatible).gbc"
  "Pokemon - Crystal Version (USA).gbc"
  "Pokemon - Emerald Version (USA, Europe).gba"
  "Pokemon - FireRed Version (USA).gba"
  "Pokemon - LeafGreen Version (USA).gba"
)

# Games the reader is KNOWN to reject, checked as negative tests.
UNSUPPORTED=(
  "Pokemon - Ruby Version (USA).gba"
  "Pokemon - Sapphire Version (USA).gba"
)

pass=0; fail=0; missing=0
printf '%-22s %-10s %-8s %s\n' "GAME" "PLATFORM" "RESULT" "READER SAID"
printf '%-22s %-10s %-8s %s\n' "----" "--------" "------" "-----------"

for rom in "${ROMS[@]}"; do
  # Short label: the game name between "Pokemon - " and " Version".
  label=$(printf '%s' "$rom" | sed -E 's/^Pokemon - ([A-Za-z]+) Version.*/\1/')
  ext="${rom##*.}"
  case "$ext" in gba) plat="GBA" ;; gb) plat="GB" ;; gbc) plat="GBC" ;; *) plat="?" ;; esac

  if [ ! -f "$ROM_DIR_WIN/$rom" ]; then
    printf '%-22s %-10s %-8s %s\n' "$label" "$plat" "MISSING" "(file not found)"
    missing=$((missing + 1))
    continue
  fi

  out=$(timeout 150 lua host-sim-rom.lua "$ROM_DIR_WIN/$rom" . 2>&1)

  if printf '%s' "$out" | grep -q "ROM-ID PASS"; then
    result="PASS"; pass=$((pass + 1))
  else
    result="FAIL"; fail=$((fail + 1))
  fi

  # The reader's own words: the [oga-shim] line that is not a boot message.
  said=$(printf '%s' "$out" | grep "oga-shim" \
         | grep -vE "installed|host platform" | head -1 | sed 's/.*oga-shim\] //' | tr -d '\r')
  # A real failure reason, if there was one.
  err=$(printf '%s' "$out" | grep -oE "!! reader raised: .*" | head -1 | sed 's/!! reader raised: //' | cut -c1-60 | tr -d '\r')
  [ -n "$err" ] && said="$said  [$err]"

  printf '%-22s %-10s %-8s %s\n' "$label" "$plat" "$result" "$said"
done

echo
echo "pass=$pass  fail=$fail  missing=$missing  of ${#ROMS[@]} supported games"
echo
echo "=== negative tests: games v3.1.0 does NOT support (must be rejected) ==="
for rom in "${UNSUPPORTED[@]}"; do
  label=$(printf '%s' "$rom" | sed -E 's/^Pokemon - ([A-Za-z]+) Version.*/\1/')
  if [ ! -f "$ROM_DIR_WIN/$rom" ]; then
    printf '  %-10s (file not found)\n' "$label"; continue
  fi
  out=$(timeout 100 lua host-sim-rom.lua "$ROM_DIR_WIN/$rom" . 2>&1)
  if printf '%s' "$out" | grep -q "game_not_supported"; then
    printf '  %-10s correctly rejected\n' "$label"
  else
    printf '  %-10s UNEXPECTEDLY ACCEPTED — the reader thinks it supports this\n' "$label"
  fi
done

echo
if [ "$fail" -eq 0 ] && [ "$missing" -eq 0 ]; then
  echo "ALL ${#ROMS[@]} SUPPORTED GAMES IDENTIFIED"
else
  echo "SOME SUPPORTED GAMES DID NOT IDENTIFY ($fail)"
fi

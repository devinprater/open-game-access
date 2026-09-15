#!/usr/bin/env bash
# gb-completion-check.sh — finish the one open identification question: do the GB games
# (Red/Blue/Yellow) reach "Ready"?
#
# ⛔ WHY THIS NEEDS ITS OWN SCRIPT. The GB reader calls get_screen() EVERY FRAME, which reads
# 360 bytes and processes them — so identification takes ~60,001 frames and ~1.2M host calls.
# In the Lua-only harness that is minutes of wall-clock time per game, and the generic matrix
# timed out before finishing. That produced a "FAIL" that was really "I did not wait long
# enough" — the exact kind of confident-wrong-result this project keeps tripping over.
#
# Measured: Red logs `READY at frames=60001`. So the budget must exceed 60,001 and the
# wall-clock timeout must be generous.
set -uo pipefail

P="/c/Users/Devin Prater/Dropbox/programs/pokemon-access/lua"
R="C:/Users/Devin Prater/Dropbox/Games/GBA"
cd "$P" || exit 1

GB_ROMS=(
  "Pokemon - Red Version (USA, Europe) (SGB Enhanced).gb"
  "Pokemon - Blue Version (USA, Europe) (SGB Enhanced).gb"
  "Pokemon - Yellow Version - Special Pikachu Edition (USA, Europe) (CGB+SGB Enhanced).gb"
)

FRAMES="${OGA_GB_FRAMES:-70000}"
echo "frame budget per game: $FRAMES"
echo

for rom in "${GB_ROMS[@]}"; do
  label=$(printf '%s' "$rom" | sed -E 's/^Pokemon - ([A-Za-z]+) Version.*/\1/')
  printf '%-10s ' "$label"

  start=$(date +%s)
  out=$(OGA_ROM_FRAMES="$FRAMES" timeout 900 lua host-sim-rom.lua "$R/$rom" . 2>&1)
  rc=$?
  elapsed=$(( $(date +%s) - start ))

  if printf '%s' "$out" | grep -q "ROM-ID PASS"; then
    printf 'PASS  (%ss)  Ready\n' "$elapsed"
  elif printf '%s' "$out" | grep -q "frame budget"; then
    printf 'TIMEOUT of harness (%ss) — frames exhausted, not a reader failure\n' "$elapsed"
  else
    printf 'FAIL  (%ss)  ' "$elapsed"
    printf '%s' "$out" | grep -oE "!! reader raised: .*" | head -1 | cut -c1-70
    echo
  fi
done

echo
echo "Note: a PASS here means the reader identified the cartridge from its header and"
echo "reached its main loop. It says nothing about live RAM, which the harness returns as 0."

#!/usr/bin/env bash
# compare-roms.sh — is the stall specific to Pokémon Black, or every ROM?
# A different, simple DS game booting at speed would exonerate the interpreter
# and point at this ROM's boot path (or its save/secure-area handling).
set -uo pipefail
cd "$HOME/pokemon-access-ios"

NDS_DIR="/mnt/c/Users/Devin Prater/Dropbox/Games/NDS"
mkdir -p "$HOME/roms2"

# Pick a couple of small, simple commercial games (not Pokémon) if present.
for NAME in "BlayzBloo (USA) (En).nds" "Bleach - Dark Souls (USA).nds"; do
  SRC="$NDS_DIR/$NAME"
  [ -f "$SRC" ] || continue
  DST="$HOME/roms2/$(echo "$NAME" | tr ' ' '_')"
  [ -f "$DST" ] || cp "$SRC" "$DST"
  echo "=== $NAME ==="
  timeout 90 ./Vendor/speedtest2 "$DST" "$HOME/hosttest-data/noop.lua" 2 > "$HOME/cmp-$(basename "$DST").log" 2>&1
  echo "exit=$? (124=timeout)"
  grep -E '== |RESULT' "$HOME/cmp-$(basename "$DST").log" | tail -3
  echo
done
